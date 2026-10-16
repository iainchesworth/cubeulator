#pragma once

// Recorder v2 (docs/recording-format.md): the corpus-grade recording path.
// Three concerns live here, all shared between the app, the tests and
// `cube_scan`, so no side can drift:
//
//  - the ANALYSIS DOWNSCALE: the one function that turns a camera frame
//    into the stream the reader (and therefore replay) consumes. The
//    recorder thread needs the exact same rounding as the live path, so
//    it lives here.
//  - the MANIFEST: per-clip metadata with the cube's true state, VALIDATED
//    through the real solver oracle at record time (a transcribed state
//    that cannot build a solvable cube refuses to record - a silent slip
//    here would poison every exact-state gate downstream).
//  - the ASYNC RECORDER: a bounded-queue writer thread that persists, per
//    pushed camera frame, the analysis-scale JPEG (frame_recording.hpp
//    format, the replay input) plus, when asked, a full-resolution JPEG,
//    and appends the index row. The producer never blocks
//    and never writes: a full queue drops the frame and COUNTS it - the
//    zero-dropped-frames exit gate is checked, not assumed
//    (check_recording below). A frame the writer cannot write is counted
//    too, and ends the recording rather than being retried.

#include <cube/error.hpp>
#include <cube/nxn/cube.hpp>
#include <cube/solver/cube.hpp>
#include <cube/vision/frame_recording.hpp>

#include <opencv2/core.hpp>

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <iosfwd>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

namespace cube::vision::recording {

// The reader is resolution-independent above this width; the live path
// downscales to it and all recorded analysis frames are at most this wide.
inline constexpr int kAnalysisWidth = 640;

// Full-resolution stream: JPEG quality and subdirectory. q95 keeps chroma
// detail; the analysis stream stays q90 for replay-format compatibility.
inline constexpr int kFullResJpegQuality = 95;
inline constexpr std::string_view kFullResSubdir = "fullres";
inline constexpr std::string_view kManifestName = "manifest.json";

struct AnalysisFrame {
    cv::Mat frame;            // <= kAnalysisWidth wide; may share input data when no downscale
    float scale_back = 1.0F;  // full-frame px per analysis px (derived from actual resize)
};

// scale_back comes from the actual resized width because cv::resize rounds
// the output size.
[[nodiscard]] AnalysisFrame analysis_downscale(const cv::Mat& full_bgr);

// Per-clip manifest (docs/recording-format.md). Absent optional fields
// serialise as JSON null.
struct Manifest {
    // 6 * size^2 characters of URFDLB, side-major, each side row-major: 54 for a
    // 3x3.
    std::optional<std::string> state;
    // The cube's side length, 2 to 9. Written only when it is not 3, so that a
    // 3x3's manifest is the file it always was; a manifest without it is a 3x3.
    int size = 3;
    std::string lighting;
    std::string motion_script;
    std::string camera;
    std::optional<std::string> intrinsics;  // path relative to the recording dir
    std::string cube;
};

// The state-string oracle: 6 * size^2 chars of URFDLB, the middle sticker of
// every side of an odd size canonical by position, and the resulting cube must
// pass Cube::from_facelets AND is_solvable() for a 3x3 and cube::nxn::validate
// for any other size. Returns a specific human-readable reason on failure so
// the recorder can refuse loudly. `size` outside 2 to 9 is a problem too.
[[nodiscard]] std::optional<std::string> state_string_problem(const std::string& state,
                                                              int size = 3);

// The state string of a cube: what state_string_problem accepts, for the cube
// the solver holds. The developer diagnostics channel writes it so that a
// cube on screen can be pasted into a manifest.
[[nodiscard]] std::string state_string(const cube::solver::Cube& cube);
[[nodiscard]] std::string state_string(const cube::nxn::NCube& cube);

// Serialise / parse. read_manifest tolerates unknown keys (the spec says
// consumers must) and returns nullopt only for unreadable/unparseable
// files. write_manifest REFUSES (returns the problem) a present state the
// oracle rejects - validation at record time is the whole point.
[[nodiscard]] std::optional<std::string> write_manifest(const std::string& dir,
                                                        const Manifest& manifest);
[[nodiscard]] std::optional<Manifest> read_manifest(const std::string& dir);

// Post-hoc recording integrity: index parses, passes are contiguous from
// the first row, t_ms is non-decreasing, every non-overwritten row's
// analysis frame exists (and fullres/ file too when the stream was
// recorded), the recorder dropped no frames and did not fail to write one.
// Returns nullopt when sound, else the first problem; a writer failure is
// reported first, because it is the cause of an empty index or a missing frame.
[[nodiscard]] std::optional<std::string> check_recording(const std::string& dir);

// How a frame becomes a file: whether it was written. cv::imwrite is the
// default, which returns false for a path it cannot write and THROWS for an
// extension no encoder is built for (the browser's OpenCV has no JPEG codec).
// A recorder takes another so that a test can make either happen on any build.
using FrameEncoder =
    std::function<bool(const std::string& path, const cv::Mat& frame, int jpeg_quality)>;

// Single-producer bounded-queue recorder. push() clones the frame and
// returns immediately; the writer thread does the downscale, both encodes
// and the index append. stop() drains what was accepted, then joins.
//
// A frame that cannot be written (the disk is full, the directory is gone, no
// encoder for the format) stops the writer at once: it is not retried, no index
// row is appended for it, and it and every frame after it are counted as
// `failed`. After stop(), pushed == written + dropped + failed.
class AsyncRecorder {
public:
    struct Stats {
        int pushed = 0;
        int written = 0;
        int dropped = 0;     // producer-side: queue full
        int failed = 0;      // accepted but not written: the writer hit an error
        int queue_peak = 0;  // high-water mark, for the throughput bench
        std::string error;   // the first error, empty when there was none
    };

    explicit AsyncRecorder(FrameEncoder encoder = {});
    ~AsyncRecorder();
    AsyncRecorder(const AsyncRecorder&) = delete;
    AsyncRecorder& operator=(const AsyncRecorder&) = delete;
    AsyncRecorder(AsyncRecorder&&) = delete;
    AsyncRecorder& operator=(AsyncRecorder&&) = delete;

    // Creates the directory (and fullres/ when enabled), truncates the
    // index, writes the manifest. Returns the problem on refusal (bad
    // state string, unwritable directory).
    [[nodiscard]] std::optional<std::string> start(const std::string& dir, const Manifest& manifest,
                                                   bool record_full_res = true);

    // GUI/capture-thread side: never blocks, never touches disk.
    void push(const cv::Mat& full_bgr, std::int64_t t_ms);

    [[nodiscard]] Stats stop();
    [[nodiscard]] bool running() const noexcept { return running_; }

private:
    struct Job {
        cv::Mat full;
        std::int64_t t_ms = 0;
    };

    void writer_loop();
    // Writes one job's files and index row, or says why it could not. Never
    // throws: the writer is a thread, and an exception leaving it terminates.
    [[nodiscard]] std::optional<std::string> write_job(const Job& job, std::ostream& index) const;
    // The writer gives up: records `problem`, counts the job in hand and every
    // queued one as failed, and makes push() count rather than queue.
    void fail_writer(const std::string& problem);

    static constexpr std::size_t kQueueCapacity = 256;  // ~8.5 s at 30 Hz

    FrameEncoder encoder_;
    std::string dir_;
    bool record_full_res_ = true;
    bool running_ = false;
    int pass_ = 0;
    Stats stats_;

    std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<Job> queue_;
    bool stopping_ = false;
    bool writer_failed_ = false;
    std::thread writer_;
};

}  // namespace cube::vision::recording
