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
//    (check_recording below).

#include <cube/error.hpp>
#include <cube/vision/frame_recording.hpp>

#include <opencv2/core.hpp>

#include <condition_variable>
#include <cstdint>
#include <deque>
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
    std::optional<std::string> state;  // 54-char URFDLB face-major, row-major faces
    std::string lighting;
    std::string motion_script;
    std::string camera;
    std::optional<std::string> intrinsics;  // path relative to the recording dir
    std::string cube;
};

// The state-string oracle: 54 chars of URFDLB, centres canonical by
// position, and the resulting cube must pass Cube::from_facelets AND
// is_solvable(). Returns a specific human-readable reason on failure so
// the recorder can refuse loudly.
[[nodiscard]] std::optional<std::string> state_string_problem(const std::string& state);

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
// recorded), no recorder-side drops were flagged. Returns nullopt when
// sound, else the first problem.
[[nodiscard]] std::optional<std::string> check_recording(const std::string& dir);

// Single-producer bounded-queue recorder. push() clones the frame and
// returns immediately; the writer thread does the downscale, both encodes
// and the index append. stop() drains what was accepted, then joins.
class AsyncRecorder {
public:
    struct Stats {
        int pushed = 0;
        int written = 0;
        int dropped = 0;     // producer-side: queue full
        int queue_peak = 0;  // high-water mark, for the throughput bench
    };

    AsyncRecorder() = default;
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

    static constexpr std::size_t kQueueCapacity = 256;  // ~8.5 s at 30 Hz

    std::string dir_;
    bool record_full_res_ = true;
    bool running_ = false;
    int pass_ = 0;
    Stats stats_;

    std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<Job> queue_;
    bool stopping_ = false;
    std::thread writer_;
};

}  // namespace cube::vision::recording
