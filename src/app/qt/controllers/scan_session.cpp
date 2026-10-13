#include "controllers/scan_session.hpp"

#include "controllers/bundled_models.hpp"
#include "controllers/live_track.hpp"
#include "interfaces/camera.hpp"
#include "interfaces/inference_env.hpp"

#include <cube/model/color_scheme.hpp>
#include <cube/model/face.hpp>
#include <cube/solver/scan_assembly.hpp>
#include <cube/vision/camera_intrinsics.hpp>
#include <cube/vision/capture_gates.hpp>
#include <cube/vision/color_resolution.hpp>
#include <cube/vision/frame_recording.hpp>
#include <cube/vision/grid_detection.hpp>
#include <cube/vision/pose_estimation.hpp>
#include <cube/vision/recording_v2.hpp>

#include <QVariantMap>
#include <QtConcurrent/QtConcurrentRun>

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <QDir>
#include <QElapsedTimer>
#include <QtLogging>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <fstream>
#include <iomanip>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace cube::app {

namespace {

// Fast enough that temporal fusion converges in well under a second
// (FaceAggregator::kMinObservations frames), while the single-in-flight
// analysis gate still protects the Qt event loop: a pass that overruns the
// interval just means the next poll skips queuing another.
constexpr int kPollIntervalMs = 33;

// Watchdog threshold for the strict-alternation analysis gate (see
// analysis_inflight_ in the header): a healthy pass completes within a few
// poll intervals even with ONNX inference in it, so one still unfinished
// after this long is stuck, not slow.
constexpr qint64 kAnalysisOverrunWarnMs = 2000;

// Vision analysis cost scales with pixel count and the detectors are
// resolution-independent above this width, so frames are downscaled before
// observation and the resulting geometry scaled back up for the overlay.
// Owned by the shared recorder/downscale header now (recording_v2.hpp):
// the recorder thread and analyze() must agree byte-for-byte.
using cube::vision::recording::kAnalysisWidth;

// Hand-presence ONNX inference runs on the FULL frame and dominates the
// analysis pass's budget, yet its output is a purely advisory UI hint -
// a hand arriving or leaving is a many-hundred-millisecond event, so
// sampling it every fourth pass (~7-8 Hz at the poll rate) loses nothing
// a user can perceive while reclaiming the worker time that pose seeding
// and (later) tracking spend. The GUI holds the last verdict across
// skipped passes (see AnalysisResult::hand_checked) so the hint doesn't
// strobe at the decimation duty cycle.
constexpr int kHandOnnxDecimation = 4;
// While the tracker is LOST the decimation stretches further still: hand
// presence gates nothing in that state (it is a UI hint only, and there
// is no capture to protect), yet the ONNX pass dominates worker cost at
// exactly the moment the pipeline most needs cadence to reacquire - the
// live1 lesson that a lost tracker spent its budget on a hint nobody was
// consuming. ~2 Hz sampling keeps the hint from freezing entirely.
constexpr int kHandOnnxDecimationLost = 16;

// --- capture gates (see tryCommitConverged) --------------------------------
// The gate values and their rationale live in cube/vision/capture_gates.hpp
// so the offline replay harnesses measure the shipping policy by
// construction — they used to hand-transcribe these constants.
using cube::vision::gates::kIdentityAggStaleMs;
using cube::vision::gates::kMaxCaptureMotionScore;
using cube::vision::gates::kMaxCaptureRotDeg;
using cube::vision::gates::kMaxCornerFlankDeg;
using cube::vision::gates::kMaxFaceCaptureDeg;
using cube::vision::gates::kMinCaptureAccepts;

// AR-overlay persistence (display-only; see handleAnalysisFinished): held
// geometry outlives a detection blip for the shared hold window (also in
// capture_gates.hpp — the replay harnesses hold seeds/hints for the same
// window), fading via overlayFreshness, so the overlay doesn't strobe at
// the per-frame detection rate. Fresh geometry is blended toward the held
// geometry by (1 - kOverlaySmoothing) to damp frame-to-frame jitter.
using cube::vision::gates::kOverlayHoldMs;
constexpr float kOverlaySmoothing = 0.6F;

// Pose-quaternion smoothing (see cubeOrientation): each fresh valid
// orientation pulls the held quaternion this fraction of the way toward
// itself (nlerp, then renormalise). 0.35 tracks a deliberately turned cube
// with only a few frames of lag at the poll rate while flattening the
// frame-to-frame wobble of the pose estimate - noticeably heavier damping
// than the 2D overlay's because a 3D cube rendering amplifies small
// angular noise into very visible rocking.
constexpr float kOrientationSmoothing = 0.35F;

// Tracked-translation display mapping (cubePosition, P2.6). The tracked
// tvec is Singmaster half-edge-1 units in the OpenCV camera frame (x
// right, y down, z away); the 3D view wants scene units (y up) around
// its origin. Lateral displacement maps from the normalised image
// offset (tx/tz, ty/tz) - the fraction of the screen the user sees the
// cube sit off-centre - and depth from tz against an arm's-length
// reference, so bringing the cube closer pushes the model toward the
// viewer. Gains are sized to Cube3DView's fixed camera (z=430, 26deg
// vertical FOV -> ~±99 scene units visible at the cube's depth, cube
// body ~106 wide): the clamps keep the model inside the viewport at
// full excursion. Smoothed by the same EMA fraction as orientation.
constexpr double kPositionLateralGain = 130.0;
constexpr double kPositionLateralLimit = 62.0;
constexpr double kPositionDepthGain = 180.0;
constexpr double kPositionDepthReference = 12.0;  // typical tz, half-edge units (~34 cm)
constexpr double kPositionDepthMin = -130.0;
constexpr double kPositionDepthMax = 70.0;
constexpr double kPositionSmoothing = 0.35;

// CUBE_VISION_RAW_POSE=1: the display adopts each tracked pose sample
// outright - no orientation nlerp, no position EMA, and the QML view
// disables its own Behavior animations (rawPoseMode) - so the tracker's
// raw output can be judged against its smoothed presentation (P2.6).
[[nodiscard]] bool raw_pose_display() {
    static const bool raw = qEnvironmentVariable("CUBE_VISION_RAW_POSE") == QLatin1String("1");
    return raw;
}

// CUBE_VISION_GATE_LOG=path: one CSV row per handled analysis pass plus
// a summary on stop() - the measurement channel for the Phase 2 live
// gates (docs/gates/phase2.md). Latency is the frame-dequeue ->
// result-handled interval (analysis_inflight_timer_), a proxy that
// excludes camera-driver and render-present time - stated with the gate
// numbers, not hidden.
[[nodiscard]] const QString& gate_log_path() {
    static const QString path = qEnvironmentVariable("CUBE_VISION_GATE_LOG");
    return path;
}
// How long the 3D view keeps showing the last known pose after the tracker
// stops supplying one. Sized to outlast an ordinary detection gap (a regrip
// or a mid-turn blur runs 5-8 passes at this corpus's ~100 ms cadence)
// without outlasting a genuine put-down, where a frozen cube would be a lie.
constexpr std::int64_t kOrientationHoldMs = 900;

// --- live-pipeline observability -------------------------------------------
// Set CUBE_VISION_DEBUG_DIR to a writable directory and the analysis worker
// dumps, roughly twice a second, the exact downscaled frame the detectors
// ran on (frame_analysis_<slot>.png, six rotating slots) plus an appended
// vision_debug.log line of per-stage stats. A dumped frame can then be
// replayed offline, stage by stage, through the hidden [.frame-debug]
// harness in tests/unit/frame_debug_test.cpp - the point is diagnosing a
// real webcam scene the synthetic tests can't reproduce. Runs entirely on
// the (single-in-flight) analysis worker, so the statics need no locking.
struct VisionDebugStats {
    std::size_t bright_candidates = 0;
    std::string face_status;    // "ok conf=..." | "pose-only conf=..." | "gated-offframe" | "none"
    std::string corner_status;  // likewise, or "unarmed"
    std::string edge_status =
        "unrun";  // edge fallback: "ok conf=..." | "pose-failed" | "none" | "unrun"
    bool is_steady = false;
    bool hand_present = false;
};

void dump_vision_debug(const cv::Mat& analysis_frame, const VisionDebugStats& stats) {
    static const QString dir = qEnvironmentVariable("CUBE_VISION_DEBUG_DIR");
    if (dir.isEmpty()) {
        return;
    }
    static QElapsedTimer throttle;
    static int pass = 0;
    static int slot = 0;
    ++pass;
    if (throttle.isValid() && throttle.elapsed() < 500) {
        return;
    }
    if (!throttle.isValid()) {
        QDir().mkpath(dir);
    }
    throttle.restart();

    const std::string dir_path = QDir(dir).absolutePath().toStdString();
    cv::imwrite(dir_path + "/frame_analysis_" + std::to_string(slot) + ".png", analysis_frame);

    std::ofstream log(dir_path + "/vision_debug.log", std::ios::app);
    log << "pass=" << pass << " slot=" << slot << " size=" << analysis_frame.cols << 'x'
        << analysis_frame.rows << " bright_candidates=" << stats.bright_candidates
        << " face=" << stats.face_status << " corner=" << stats.corner_status
        << " edge=" << stats.edge_status << " steady=" << stats.is_steady
        << " hand=" << stats.hand_present << '\n';
    slot = (slot + 1) % 6;
}

// Recorder v2 (docs/pipeline-plan.md Phase 0 item 2). Set
// CUBE_VISION_RECORD_DIR to a writable directory and EVERY frame poll()
// pops - including ones the busy analysis gate skips - is recorded via the
// async bounded-queue writer in cube/vision/recording_v2.hpp: the
// analysis-scale stream (the replay input, format unchanged) plus a
// full-resolution JPEG under fullres/ for labelling, plus a manifest with
// the cube's true state VALIDATED through the solver oracle - a state
// string that cannot build a solvable cube refuses to record, loudly,
// because a silent slip would poison every exact-state gate downstream.
// Manifest fields come from CUBE_VISION_RECORD_{STATE,LIGHTING,MOTION,
// CAMERA,CUBE}. Called from the GUI thread only (poll), so the statics
// need no locking; the writer thread owns all disk I/O and the producer
// never blocks (drops are counted and fail the clip's integrity check).
//
// Fidelity note vs the v1 recorder this replaces: v1 wrote per analysis
// PASS from the worker; v2 writes per POPPED FRAME, so a recording may
// carry more frames than the original run analysed. The offline runner -
// the acceptance surface - analyses every frame deterministically either
// way; the in-app replay path was never frame-deterministic (busy-skip).
void record_camera_frame_v2(const cv::Mat& full_frame, std::int64_t frame_t_ms) {
    namespace recording = cube::vision::recording;
    static const QString dir = qEnvironmentVariable("CUBE_VISION_RECORD_DIR");
    if (dir.isEmpty()) {
        return;
    }
    static recording::AsyncRecorder recorder;
    static bool start_attempted = false;
    if (!start_attempted) {
        start_attempted = true;
        recording::Manifest manifest;
        const QString state = qEnvironmentVariable("CUBE_VISION_RECORD_STATE");
        if (!state.isEmpty()) {
            manifest.state = state.toStdString();
        } else {
            qWarning(
                "ScanSession: recording without CUBE_VISION_RECORD_STATE - clip will carry no "
                "true-state manifest and cannot feed exact-state gates");
        }
        manifest.lighting = qEnvironmentVariable("CUBE_VISION_RECORD_LIGHTING").toStdString();
        manifest.motion_script = qEnvironmentVariable("CUBE_VISION_RECORD_MOTION").toStdString();
        manifest.camera = qEnvironmentVariable("CUBE_VISION_RECORD_CAMERA").toStdString();
        manifest.cube = qEnvironmentVariable("CUBE_VISION_RECORD_CUBE").toStdString();
        QDir().mkpath(dir);
        // P6.12: stamp the ACTIVE intrinsics into the clip. The tracking
        // loop runs under CUBE_VISION_INTRINSICS (see live_track::step); a
        // clip recorded without them replays under the f=width
        // approximation and scores a DIFFERENT pipeline than the one the
        // session actually ran - the manifest.intrinsics field existed for
        // exactly this and was never written. The calibration is copied
        // into the clip (self-contained, the runner's load_clip_intrinsics
        // reads <dir>/intrinsics.txt) and referenced from the manifest.
        if (const QString intrinsics_path = qEnvironmentVariable("CUBE_VISION_INTRINSICS");
            !intrinsics_path.isEmpty()) {
            if (const auto intrinsics =
                    cube::vision::read_intrinsics(intrinsics_path.toStdString());
                intrinsics.has_value()) {
                const std::string copy_path =
                    QDir(dir).absolutePath().toStdString() + "/intrinsics.txt";
                if (const auto problem = cube::vision::write_intrinsics(copy_path, *intrinsics);
                    !problem) {
                    manifest.intrinsics = "intrinsics.txt";
                } else {
                    qWarning("ScanSession: recording could not stamp intrinsics: %s",
                             problem->c_str());
                }
            } else {
                qWarning(
                    "ScanSession: CUBE_VISION_INTRINSICS set but unreadable at record time: %s",
                    qUtf8Printable(intrinsics_path));
            }
        }
        if (const auto problem = recorder.start(QDir(dir).absolutePath().toStdString(), manifest)) {
            qWarning("ScanSession: recording REFUSED: %s", problem->c_str());
        }
    }
    recorder.push(full_frame, frame_t_ms);
}

// Set CUBE_VISION_REPLAY_DIR to a directory previously written by
// CUBE_VISION_RECORD_DIR and poll() draws its frames from that clip instead
// of the camera, at the clip's own recorded inter-frame timestamps, so the
// entire live pipeline - detectors, tracker, slot router, and every
// Q_PROPERTY the QML binds to - runs against a known scene with no webcam
// and no cube in hand. This is a debug-only observation hook, and it exists
// because the unit tests can all pass while the SCREEN stays dead: the only
// thing that proves the presentation layer actually reflects backend state
// is driving the real app and watching it.
//
// Fidelity: frames are replayed at their recorded (analysis) scale, the
// same note the offline [.track-replay] harness carries. For a clip
// recorded from a camera at or below kAnalysisWidth the replayed frame is
// byte-for-byte what the detectors originally saw, since analyze()'s
// downscale is a no-op at that width; for a wider original, geometry lands
// in analysis-scale rather than full-frame pixels, which is a uniform
// scale and so leaves the overlay's shape correct but its coordinates
// smaller than a live run's.
//
// Unset - the normal case, including every shipped build - each function
// here early-outs on an empty directory and the camera path is untouched.
[[nodiscard]] const QString& replay_dir() {
    static const QString dir = qEnvironmentVariable("CUBE_VISION_REPLAY_DIR");
    return dir;
}

[[nodiscard]] bool replay_active() {
    return !replay_dir().isEmpty();
}

// The clip's index rows, loaded once on first use and consumed in order.
// Rows whose JPEG a later ring lap has already overwritten are dropped up
// front (frame_overwritten): replaying one would pair this row's timestamp
// with a later frame's pixels and quietly corrupt the tracker's dt.
struct ReplayClip {
    std::vector<cube::vision::recording::RecordedFrameInfo> rows;
    std::size_t next = 0;
    bool exhausted_logged = false;
};

[[nodiscard]] ReplayClip& replay_clip() {
    static ReplayClip clip = []() {
        namespace recording = cube::vision::recording;
        ReplayClip loaded;
        if (!replay_active()) {
            return loaded;
        }
        const QDir dir(replay_dir());
        const std::string index_path =
            dir.absoluteFilePath(
                   QString::fromUtf8(recording::kIndexCsvName.data(),
                                     static_cast<qsizetype>(recording::kIndexCsvName.size())))
                .toStdString();
        std::ifstream index(index_path);
        if (!index) {
            qWarning("ScanSession: CUBE_VISION_REPLAY_DIR set but no readable %s in it",
                     index_path.c_str());
            return loaded;
        }
        auto rows = recording::read_index(index);
        int max_pass = 0;
        for (const auto& row : rows) {
            max_pass = std::max(max_pass, row.pass);
        }
        for (const auto& row : rows) {
            if (!recording::frame_overwritten(row.pass, max_pass)) {
                loaded.rows.push_back(row);
            }
        }
        qInfo("ScanSession: replaying %lld recorded frames from %s (camera bypassed)",
              static_cast<long long>(loaded.rows.size()), qUtf8Printable(replay_dir()));
        return loaded;
    }();
    return clip;
}

// One recorded frame per poll, in pass order, carrying the recorded t_ms so
// the tracker sees the clip's true inter-frame gaps rather than the wall
// clock the replay happens to run at. Returns nullopt once the clip runs
// out, which simply stops feeding poll() - the last frame's UI state stays
// on screen, which is exactly what you want to photograph.
[[nodiscard]] std::optional<cube::platform::camera::TimedFrame> try_pop_replay_frame() {
    namespace recording = cube::vision::recording;
    auto& clip = replay_clip();
    if (clip.next >= clip.rows.size()) {
        if (!clip.exhausted_logged) {
            clip.exhausted_logged = true;
            qInfo("ScanSession: replay clip exhausted after %lld frames; UI holds its final state",
                  static_cast<long long>(clip.rows.size()));
        }
        return std::nullopt;
    }
    const auto& row = clip.rows[clip.next++];
    const std::string path =
        QDir(replay_dir())
            .absoluteFilePath(QString::fromStdString(recording::frame_filename(row.pass)))
            .toStdString();
    cv::Mat frame = cv::imread(path, cv::IMREAD_COLOR);
    if (frame.empty()) {
        qWarning("ScanSession: replay frame %s missing or unreadable; skipping", path.c_str());
        return std::nullopt;
    }
    return cube::platform::camera::TimedFrame{.frame = std::move(frame), .t_ms = row.t_ms};
}

// GUI-thread third leg of the shared observability log (worker per-pass
// lines from dump_vision_debug, SLOT routing lines from log_slot_decision):
// one TRACK line per analysis pass with the tracker's full state and gate
// verdict. This is what the corpus-replay calibration sweeps and any "why
// did it lose the cube there" post-mortem grep for - the tracker's whole
// behaviour is these numbers, and shadow mode exists to collect them
// against live scans before routing depends on the answers.
// Shared by the TRACK log line and the trackingState Q_PROPERTY - the
// on-disk and the QML identifiers must never drift apart, or a corpus
// post-mortem stops matching what the UI showed.
[[nodiscard]] const char* track_state_name(cube::vision::TrackState state) {
    switch (state) {
        case cube::vision::TrackState::lost:
            return "lost";
        case cube::vision::TrackState::acquiring:
            return "acquiring";
        case cube::vision::TrackState::locked:
            return "locked";
        case cube::vision::TrackState::coasting:
            return "coasting";
    }
    return "?";
}

void log_track_pass(const cube::vision::TrackerPassOutput& track) {
    static const QString dir = qEnvironmentVariable("CUBE_VISION_DEBUG_DIR");
    if (dir.isEmpty()) {
        return;
    }
    const auto gate_name = [](cube::vision::TrackGate gate) {
        switch (gate) {
            case cube::vision::TrackGate::accepted:
                return "accepted";
            case cube::vision::TrackGate::anchored:
                return "anchored";
            case cube::vision::TrackGate::no_observation:
                return "no-obs";
            case cube::vision::TrackGate::basin:
                return "basin";
            case cube::vision::TrackGate::margin:
                return "margin";
            case cube::vision::TrackGate::omega_outlier:
                return "omega-outlier";
            case cube::vision::TrackGate::implausible:
                return "implausible";
            case cube::vision::TrackGate::slip_evidence:
                return "slip-evidence";
        }
        return "?";
    };
    // model::Face order U,R,F,D,L,B.
    const char front =
        track.front_face ? "URFDLB"[static_cast<std::size_t>(*track.front_face)] : '-';
    std::ofstream log(QDir(dir).absolutePath().toStdString() + "/vision_debug.log", std::ios::app);
    log << "TRACK state=" << track_state_name(track.state) << " gate=" << gate_name(track.gate)
        << " q=(" << track.q_track[0] << ',' << track.q_track[1] << ',' << track.q_track[2] << ','
        << track.q_track[3] << ") sigma=" << track.sigma_deg << " theta=" << track.theta_deg
        << " S=" << track.lattice_index << " margin=" << track.margin_deg << " front=" << front
        << " k=" << track.quarter_turn_k << " belief=" << track.belief_margin
        << " certain=" << track.identity_certain << " certainF=" <<
        [&track] {
            // Face-scoped certainty as a URFDLB bit string ("010000" =
            // R commit-grade) - the capture layer's actual bar now, so
            // a post-mortem can see it without re-running the belief.
            std::string bits(track.identity_certain_face.size(), '0');
            for (std::size_t f = 0; f < track.identity_certain_face.size(); ++f) {
                bits[f] = track.identity_certain_face[f] ? '1' : '0';
            }
            return bits;
        }()
        << " epoch=" << track.epoch << " dt=" << track.dt_ms << '\n';
}

[[nodiscard]] QString facet_color_css(cube::model::FacetColor color) {
    using cube::model::FacetColor;
    switch (color) {
        case FacetColor::white:
            return QStringLiteral("#F2F0EC");
        case FacetColor::yellow:
            return QStringLiteral("#F0C94A");
        case FacetColor::red:
            return QStringLiteral("#D1372A");
        case FacetColor::orange:
            return QStringLiteral("#DF7A2B");
        case FacetColor::green:
            return QStringLiteral("#3A9D5D");
        case FacetColor::blue:
            return QStringLiteral("#2F6FAE");
        case FacetColor::unknown:
        default:
            return {};
    }
}

// English colour name for user-facing text (the scanNeedsRetry re-show
// hint). Empty for unknown - callers fall back to a colourless phrasing
// rather than telling the user to find an "unknown-centred" face.
[[nodiscard]] QString facet_color_display_name(cube::model::FacetColor color) {
    using cube::model::FacetColor;
    switch (color) {
        case FacetColor::white:
            return QStringLiteral("white");
        case FacetColor::yellow:
            return QStringLiteral("yellow");
        case FacetColor::red:
            return QStringLiteral("red");
        case FacetColor::orange:
            return QStringLiteral("orange");
        case FacetColor::green:
            return QStringLiteral("green");
        case FacetColor::blue:
            return QStringLiteral("blue");
        case FacetColor::unknown:
        default:
            return {};
    }
}

// The same centre-colour -> cube-position anchor the assembler stands on
// (scan_assembly.hpp: centres never move, so under Cube::solved()'s scheme
// White -> D, Yellow -> U, Green -> F, Blue -> B, Red -> L, Orange -> R).
// Here it is only a display GUESS for slotSummaries - global colour
// resolution can still reclassify a centre at assembly time.
[[nodiscard]] QString face_letter_for_center(cube::model::FacetColor color) {
    using cube::model::FacetColor;
    switch (color) {
        case FacetColor::white:
            return QStringLiteral("D");
        case FacetColor::yellow:
            return QStringLiteral("U");
        case FacetColor::green:
            return QStringLiteral("F");
        case FacetColor::blue:
            return QStringLiteral("B");
        case FacetColor::red:
            return QStringLiteral("L");
        case FacetColor::orange:
            return QStringLiteral("R");
        case FacetColor::unknown:
        default:
            return QStringLiteral("?");
    }
}

[[nodiscard]] std::array<cube::model::Facelet, cube::model::facelets_per_face> provisional_facelets(
    const std::array<cv::Vec3f, 9>& lab) {
    std::array<cube::model::Facelet, cube::model::facelets_per_face> facelets{};
    for (std::size_t i = 0; i < facelets.size(); ++i) {
        facelets[i] = cube::vision::classify_lab(lab[i]);
    }
    return facelets;
}

// cube::solver::Face (U,D,L,R,F,B) and cube::model::Face (U,R,F,D,L,B)
// name the same six physical positions in different enum orders - map by
// name, never by numeric value.
[[nodiscard]] cube::model::Face model_face(cube::solver::Face face) {
    using cube::model::Face;
    switch (face) {
        case cube::solver::Face::U:
            return Face::U;
        case cube::solver::Face::D:
            return Face::D;
        case cube::solver::Face::L:
            return Face::L;
        case cube::solver::Face::R:
            return Face::R;
        case cube::solver::Face::F:
            return Face::F;
        case cube::solver::Face::B:
            return Face::B;
    }
    return Face::U;
}

[[nodiscard]] float mean_confidence(const std::array<cube::model::Facelet, 9>& facelets) {
    float sum = 0.0F;
    for (const auto& facelet : facelets) {
        sum += facelet.confidence;
    }
    return sum / static_cast<float>(facelets.size());
}

void scale_quad(std::array<cv::Point2f, 4>& corners, float factor) {
    for (auto& corner : corners) {
        corner *= factor;
    }
}

// Maps geometry produced on the downscaled analysis frame back to
// full-frame pixel coordinates for the AR overlay.
void scale_observation(cube::vision::FaceObservation& observation, float factor) {
    scale_quad(observation.face_quad, factor);
    for (auto& cell : observation.cells) {
        scale_quad(cell.corners, factor);
    }
}

void scale_corner_view(cube::vision::CornerFaceView& view, float factor) {
    scale_quad(view.quad, factor);
    for (auto& cell : view.cells) {
        scale_quad(cell.corners, factor);
    }
}

void smooth_quad(std::array<cv::Point2f, 4>& next, const std::array<cv::Point2f, 4>& previous) {
    for (std::size_t i = 0; i < next.size(); ++i) {
        next[i] = kOverlaySmoothing * next[i] + (1.0F - kOverlaySmoothing) * previous[i];
    }
}

// Damps overlay jitter by blending fresh geometry toward the held frame's.
// Colour samples are NOT smoothed - only geometry; lab feeds the temporal
// aggregators, which do their own (windowed median) smoothing.
void smooth_observation(cube::vision::FaceObservation& next,
                        const cube::vision::FaceObservation& previous) {
    smooth_quad(next.face_quad, previous.face_quad);
    for (std::size_t i = 0; i < next.cells.size(); ++i) {
        smooth_quad(next.cells[i].corners, previous.cells[i].corners);
    }
}

// FaceAggregator only consumes the lab samples, but building a full
// FaceObservation keeps one add() path for both flows.
[[nodiscard]] cube::vision::FaceObservation slot_observation(
    const cube::vision::CornerFaceView& view, float geometry_confidence) {
    cube::vision::FaceObservation observation;
    observation.cells = view.cells;
    observation.lab = view.lab;
    observation.face_quad = view.quad;
    observation.geometry_confidence = geometry_confidence;
    return observation;
}

void append_quad(QVariantList& list, const std::array<cv::Point2f, 4>& corners) {
    for (const auto& corner : corners) {
        list.append(static_cast<qreal>(corner.x));
        list.append(static_cast<qreal>(corner.y));
    }
}

}  // namespace

ScanSession::ScanSession(QObject* parent) : QObject(parent) {
    poll_timer_.setInterval(kPollIntervalMs);
    connect(&poll_timer_, &QTimer::timeout, this, &ScanSession::poll);
    connect(&analysis_watcher_, &QFutureWatcher<AnalysisResult>::finished, this,
            &ScanSession::handleAnalysisFinished);
    connect(&start_watcher_, &QFutureWatcher<Result<void>>::finished, this,
            &ScanSession::handleStartFinished);

    // Debug-only companion to CUBE_VISION_REPLAY_DIR: without it a replay
    // run sits on the home screen and the analysis path never executes,
    // because scanning only begins when the user navigates to the scan
    // screen and start() fires. Setting CUBE_VISION_AUTOSTART_SCAN drives
    // the whole capture pipeline unattended, which is what makes a crash
    // that only appears mid-scan reproducible without a human and a cube in
    // front of the camera. Queued, not called inline: start() expects a
    // running event loop (it dispatches the camera open to a watcher).
    if (!qEnvironmentVariable("CUBE_VISION_AUTOSTART_SCAN").isEmpty()) {
        QMetaObject::invokeMethod(this, [this]() { start(QString()); }, Qt::QueuedConnection);
    }

    // Hand-detection AI is on by default; SettingsScreen's toggle flips
    // handDetectionEnabled_. make_hand_presence_detector_from_memory()
    // already falls back to the heuristic detector gracefully when the
    // model/execution provider isn't actually available. The model is a Qt
    // resource embedded in the executable (see bundled_models.hpp), not a
    // deployed file, so there's no filesystem path to resolve at all.
    const auto env = cube::platform::inference_env::current();
    const QByteArray model_bytes = hand_detection_model_bytes();
    const auto capability = cube::vision::check_hand_detection_capability(
        !model_bytes.isEmpty(), env.execution_providers, /*user_enabled=*/true);
    hand_detector_ = cube::vision::make_hand_presence_detector_from_memory(
        capability, model_bytes.constData(), static_cast<std::size_t>(model_bytes.size()),
        env.execution_providers);
}

// P2.9 gate-log summary: the session's headline numbers, appended so
// the CSV is self-contained evidence for the gate write-up. Called from
// stop() and the destructor - closing the app mid-scan must not lose
// the summary.
void ScanSession::finishGateLog() {
    if (!gate_log_.is_open()) {
        return;
    }
    std::sort(gate_latencies_.begin(), gate_latencies_.end());
    const auto latency_at = [this](double q) {
        return gate_latencies_.empty() ? -1.0
                                       : gate_latencies_[std::min(
                                             gate_latencies_.size() - 1,
                                             static_cast<std::size_t>(
                                                 q * static_cast<double>(gate_latencies_.size())))];
    };
    gate_log_ << "# summary passes=" << gate_passes_ << " tracked=" << gate_tracked_
              << " tracked_frac="
              << (gate_passes_ > 0 ? static_cast<double>(gate_tracked_) / gate_passes_ : 0.0)
              << " latency_median_ms=" << latency_at(0.5) << " latency_p95_ms=" << latency_at(0.95)
              << "\n";
    gate_log_.close();
    gate_latencies_.clear();
    gate_passes_ = 0;
    gate_tracked_ = 0;
}

ScanSession::~ScanSession() {
    finishGateLog();
    // See the header: block until in-flight workers finish so they can't
    // touch members after destruction begins. Both waits return
    // immediately when no future was ever set or it already finished.
    start_watcher_.waitForFinished();
    analysis_watcher_.waitForFinished();
}

bool ScanSession::running() const noexcept {
    return running_;
}

bool ScanSession::starting() const noexcept {
    return start_watcher_.isRunning();
}

int ScanSession::frameSeq() const noexcept {
    return frame_seq_;
}

int ScanSession::frameWidth() const noexcept {
    return frame_width_;
}

int ScanSession::frameHeight() const noexcept {
    return frame_height_;
}

int ScanSession::facesScanned() const {
    // Track mode (P6.12): the fusion store is the capture surface, so
    // progress is the best registration-merged sub-epoch's DISTINCT
    // converged face count (P6.15, cached by maybeFinalizeTrackFusion) -
    // the same quantity the finalize bar counts to six. Distinct, not
    // merely converged: measured on live-demo-p6-retry, two of the six
    // converged slots held one physical face, so the ring read 6/6 for the
    // rest of the session while completion was unreachable - a progress
    // indicator that has stopped describing progress. Fusion faces are
    // anonymous body ids exactly as router slots were, so the UI contract
    // is unchanged.
    if (track_fusion_active_) {
        return track_fusion_faces_;
    }
    // Legacy pipeline: filled slot count, NOT progress_ - progress_ only
    // gains faces once a completed scan assembles (slot keys are tracked
    // body faces, which stay anonymous to the UI until assembly names
    // their colours).
    return router_.filled_count();
}

int ScanSession::trackFusionObservations() const {
    int total = 0;
    for (const auto& [sub_epoch, faces] : track_fusion_) {
        for (const auto& [face_id, aggregator] : faces) {
            total += static_cast<int>(aggregator.observation_count());
        }
    }
    return total;
}

QVariantList ScanSession::cellColors() const {
    QVariantList list;
    for (const auto& facelet : facelets_) {
        list.append(facet_color_css(facelet.color));
    }
    return list;
}

QVariantList ScanSession::cellConfidence() const {
    QVariantList list;
    for (const auto& facelet : facelets_) {
        list.append(static_cast<qreal>(facelet.confidence));
    }
    return list;
}

qreal ScanSession::overallConfidence() const noexcept {
    return static_cast<double>(overall_confidence_);
}

bool ScanSession::isSteady() const noexcept {
    return is_steady_;
}

bool ScanSession::handPresent() const noexcept {
    return hand_present_;
}

bool ScanSession::cubeDetected() const noexcept {
    return face_observation_.has_value() || corner_observation_.has_value();
}

bool ScanSession::readyToConfirm() const noexcept {
    return anyCaptureReady();
}

QVariantList ScanSession::faceQuadPoints() const {
    QVariantList list;
    if (face_observation_) {
        append_quad(list, face_observation_->face_quad);
    }
    return list;
}

QVariantList ScanSession::cellQuadPoints() const {
    QVariantList list;
    if (face_observation_) {
        for (const auto& cell : face_observation_->cells) {
            append_quad(list, cell.corners);
        }
    }
    return list;
}

bool ScanSession::cornerDetected() const noexcept {
    return corner_observation_.has_value();
}

QVariantList ScanSession::cornerQuadPoints() const {
    QVariantList list;
    if (corner_observation_) {
        append_quad(list, corner_observation_->top.quad);
        append_quad(list, corner_observation_->lower_left.quad);
        append_quad(list, corner_observation_->lower_right.quad);
    }
    return list;
}

QVariantList ScanSession::cubeVertexPoints() const {
    QVariantList list;
    if (cube_pose_) {
        for (const auto& vertex : cube_pose_->vertices) {
            list.append(static_cast<qreal>(vertex.x));
            list.append(static_cast<qreal>(vertex.y));
        }
    }
    return list;
}

QVariantList ScanSession::cubeOrientation() const {
    QVariantList list;
    // has_smoothed_orientation_ is maintained to be true EXACTLY while the
    // tracker is locked or coasting (set with each pass's smoothing update,
    // dropped on lost/acquiring and by resetReading), so it alone decides.
    // Deliberately NOT gated on cube_pose_: the pose overlay's 450 ms hold
    // expires well inside the tracker's coast window, and a fresh corner
    // pose can carry orientation_valid == false while the tracker is still
    // confidently coasting - either condition would blank the 3D cube in
    // precisely the detection gaps the tracker exists to carry it through.
    if (has_smoothed_orientation_) {
        // Mirror compensation, display-only: the preview mirrors the video
        // about the screen-vertical axis (x -> -x in the display frame),
        // and conjugating a rotation by that reflection maps the axis to
        // its mirror image and flips the turn direction - in quaternion
        // components, (w, x, y, z) -> (w, x, -y, -z). Without it the 3D
        // progress cube yaws AGAINST the mirrored video (map RC12); with
        // it, cube and video turn the same way the user's hands do. Applied
        // at read time so tracker state, routing and the TRACK log all stay
        // in the camera's unmirrored frame - flipping the flag mid-scan
        // costs nothing but this getter's output. The sign convention is
        // derived, not yet verified against a live mirrored webcam (map
        // open question 7): if empirical checking shows the opposite
        // handedness, the fix is local to these two negations.
        const float mirror = preview_mirrored_ ? -1.0F : 1.0F;
        list.append(static_cast<qreal>(smoothed_orientation_[0]));
        list.append(static_cast<qreal>(smoothed_orientation_[1]));
        list.append(static_cast<qreal>(mirror * smoothed_orientation_[2]));
        list.append(static_cast<qreal>(mirror * smoothed_orientation_[3]));
    }
    return list;
}

QVariantList ScanSession::cubePosition() const {
    QVariantList list;
    // Same lifetime rule as cubeOrientation: empty exactly while no
    // continuous track (or its hold window) backs a translation, which
    // eases the model back to the viewport centre in QML.
    if (has_position_) {
        // Read-time mirror, the cubeOrientation story: the preview
        // mirrors the video about the screen-vertical axis (x -> -x),
        // so the model's lateral travel flips with it or the render
        // slides against the video the user is watching.
        const double mirror = preview_mirrored_ ? -1.0 : 1.0;
        list.append(mirror * smoothed_position_[0]);
        list.append(smoothed_position_[1]);
        list.append(smoothed_position_[2]);
    }
    return list;
}

bool ScanSession::debugHudEnabled() const {
    static const bool enabled = qEnvironmentVariable("CUBE_VISION_HUD") == QLatin1String("1");
    return enabled;
}

bool ScanSession::rawPoseMode() const {
    return raw_pose_display();
}

bool ScanSession::previewMirrored() const noexcept {
    return preview_mirrored_;
}

void ScanSession::setPreviewMirrored(bool mirrored) {
    if (preview_mirrored_ == mirrored) {
        return;
    }
    preview_mirrored_ = mirrored;
    emit previewMirroredChanged();
    // cubeOrientation's NOTIFY is readingChanged; without this a mirror
    // flip (camera switch mid-scan) would leave the 3D cube in the stale
    // handedness until the next analysis pass happened to re-notify.
    emit readingChanged();
}

QString ScanSession::trackingState() const {
    // A live continuous-track pose outranks the legacy tracker's verdict
    // for the UI: measured on a live diagnosis session, the M3T lock was
    // live for 75% of passes while the legacy tracker - whose
    // plausibility gates refuse the track's frame convention - reported
    // "lost" 85% of the time, so the chip told the user the opposite of
    // what the wireframe was doing.
    if (last_pose_from_track_) {
        return QStringLiteral("locked");
    }
    return QString::fromLatin1(track_state_name(tracker_.state()));
}

qreal ScanSession::overlayFreshness() const {
    if (!face_observation_ && !corner_observation_ && !cube_pose_) {
        return 0.0;
    }
    if (!overlay_hold_timer_.isValid()) {
        return 0.0;
    }
    const auto elapsed = overlay_hold_timer_.elapsed();
    if (elapsed <= 0) {
        return 1.0;
    }
    return std::clamp(1.0 - static_cast<double>(elapsed) / static_cast<double>(kOverlayHoldMs), 0.0,
                      1.0);
}

QVariantList ScanSession::cornerCellColors() const {
    QVariantList list;
    if (corner_observation_) {
        for (const auto* view : {&corner_observation_->top, &corner_observation_->lower_left,
                                 &corner_observation_->lower_right}) {
            for (const auto& lab : view->lab) {
                list.append(facet_color_css(cube::vision::classify_lab(lab).color));
            }
        }
    }
    return list;
}

qreal ScanSession::captureProgress() const {
    // The ring shows progress toward capturing what the user is LOOKING
    // AT: the tracked front face's window. While the tracker claims no
    // identity there is no window to show - the ring idles at zero rather
    // than flashing some other face's leftover convergence.
    const auto state = tracker_.state();
    if (state != cube::vision::TrackState::locked && state != cube::vision::TrackState::coasting) {
        return 0.0;
    }
    const auto front = cube::vision::nearest_face(tracker_.orientation());
    return static_cast<qreal>(aggregators_[static_cast<std::size_t>(front)].convergence());
}

bool ScanSession::autoAdvance() const noexcept {
    return auto_advance_;
}

void ScanSession::setAutoAdvance(bool enabled) {
    if (auto_advance_ == enabled) {
        return;
    }
    auto_advance_ = enabled;
    emit autoAdvanceChanged();
}

bool ScanSession::handDetectionEnabled() const noexcept {
    return hand_detection_enabled_.load();
}

void ScanSession::setHandDetectionEnabled(bool enabled) {
    if (hand_detection_enabled_.exchange(enabled) == enabled) {
        return;
    }
    emit handDetectionEnabledChanged();
}

QVariantList ScanSession::progressCenters() const {
    const auto observed = progress_.observed_centers();
    const auto scheme = cube::model::detect_color_scheme(observed);

    QVariantList list;
    for (std::size_t i = 0; i < cube::model::face_count; ++i) {
        if (observed[i]) {
            list.append(facet_color_css(*observed[i]));
        } else if (scheme.face_colors[i]) {
            list.append(facet_color_css(*scheme.face_colors[i]));
        } else {
            list.append(QString());
        }
    }
    return list;
}

QVariantList ScanSession::progressCentersConfirmed() const {
    QVariantList list;
    for (std::size_t i = 0; i < cube::model::face_count; ++i) {
        list.append(progress_.is_face_confirmed(static_cast<cube::model::Face>(i)));
    }
    return list;
}

QVariantList ScanSession::progressFacelets() const {
    QVariantList list;
    for (std::size_t i = 0; i < cube::model::face_count; ++i) {
        const auto face = static_cast<cube::model::Face>(i);
        const bool confirmed = progress_.is_face_confirmed(face);
        for (const auto& facelet : progress_.face_scan(face).facelets) {
            // Unconfirmed faces contribute empty strings for all nine cells
            // (never a stale or predicted colour) so the progress cube can
            // only ever show stickers that were genuinely read.
            list.append(confirmed ? facet_color_css(facelet.color) : QString());
        }
    }
    return list;
}

QVariantList ScanSession::slotSummaries() const {
    QVariantList list;
    for (int i = 0; i < router_.filled_count(); ++i) {
        const cube::vision::CapturedFace& slot = *router_.slot_at(i);
        QVariantMap entry;
        entry.insert(
            QStringLiteral("face"),
            face_letter_for_center(
                cube::vision::classify_lab(slot.lab[cube::model::center_facelet_index]).color));
        QVariantList colors;
        for (const auto& lab : slot.lab) {
            colors.append(facet_color_css(cube::vision::classify_lab(lab).color));
        }
        entry.insert(QStringLiteral("colors"), colors);
        list.append(entry);
    }
    return list;
}

QVariantList ScanSession::faceCaptureStates() const {
    QVariantList list;
    for (int i = 0; i < router_.filled_count(); ++i) {
        list.append(router_.strikes_at(i) > 0 ? QStringLiteral("conflict") : QStringLiteral("ok"));
    }
    return list;
}

bool ScanSession::reorienting() const {
    return router_.reorienting();
}

cv::Mat ScanSession::latestFrame() const {
    std::scoped_lock lock(frame_mutex_);
    return latest_frame_;
}

const cube::model::ScanProgress& ScanSession::progress() const noexcept {
    return progress_;
}

std::optional<cube::solver::Cube> ScanSession::assembledCube() const {
    return assembled_cube_;
}

void ScanSession::start(const QString& deviceId) {
    if (start_watcher_.isRunning()) {
        // Re-entering the scan screen while the multi-second camera open is
        // still in flight: cancel any stop() queued in between, or
        // handleStartFinished() would tear the just-opened camera down and
        // leave the scan screen permanently dead (nothing re-triggers
        // start() until the user navigates away and back).
        stop_requested_while_starting_ = false;
        return;
    }
    if (running_) {
        return;
    }

    restartScan();

    // A stale gate can only be held here if a pre-stop() pass never
    // delivered at all; dropping it matches stop()'s reset so a wedged
    // worker can't lock analysis out of the new session forever. Dropping
    // it does NOT assert the pass has drained - the watchdog's own premise
    // is that a pass can outlive a stop()/start() cycle - so poll() gates
    // on isRunning() as well: until the orphan actually finishes, no new
    // pass is queued beside it (two concurrent analyze() calls would race
    // the lock-free worker-side per-pass state).
    analysis_inflight_ = false;

    // Debug-only clip playback has no camera to open, so it skips the
    // multi-second async start entirely and goes straight to running -
    // the same terminal state handleStartFinished() reaches on success,
    // set here in the same order so the QML sees no difference.
    if (replay_active()) {
        running_ = true;
        emit runningChanged();
        emit facesScannedChanged();
        poll_timer_.start();
        return;
    }

    const cube::platform::camera::DeviceInfo device{.identifier = deviceId.toStdString(),
                                                    .display_name = ""};
    start_watcher_.setFuture(
        QtConcurrent::run([device]() { return cube::platform::camera::start(device); }));
    emit startingChanged();
}

void ScanSession::stop() {
    if (start_watcher_.isRunning()) {
        stop_requested_while_starting_ = true;
        return;
    }

    poll_timer_.stop();
    // Puts the driver's photometry back as it was found on the way out -
    // DirectShow property state outlives the process, so a scan's locked
    // exposure would otherwise follow the user into every other camera app
    // (see interfaces/camera.hpp: request_photometry_restore).
    cube::platform::camera::stop();

    finishGateLog();

    if (camera_locked_ || camera_lock_warning_ || !camera_lock_status_.isEmpty()) {
        camera_locked_ = false;
        camera_lock_warning_ = false;
        camera_lock_status_.clear();
        emit cameraLockChanged();
    }

    // The gate is deliberately released even if a pass is still in flight:
    // its late-delivered result is discarded by handleAnalysisFinished()'s
    // !running_ guard (clearing an already-clear flag there is harmless),
    // and no new pass can race it - the poll timer is stopped until the
    // next start() completes, and if the pass is STILL executing by then,
    // poll()'s isRunning() gate keeps skipping until it drains.
    analysis_inflight_ = false;

    running_ = false;
    emit runningChanged();
}

void ScanSession::handleStartFinished() {
    emit startingChanged();

    const auto result = start_watcher_.result();
    const bool stop_requested = std::exchange(stop_requested_while_starting_, false);
    if (!result.has_value()) {
        return;
    }
    if (stop_requested) {
        cube::platform::camera::stop();
        return;
    }

    running_ = true;
    emit runningChanged();
    emit facesScannedChanged();
    poll_timer_.start();

    // Lock the camera's photometry for the whole scan, the moment the
    // device is live. This is the point of the whole exercise: with the
    // automatics running, mean L drifted 14-18 points BETWEEN FACES of a
    // single scan and blue (chroma 18-24) overlapped warm-cast white
    // (15.7-21.0), which no classifier can separate. Requested, not
    // applied, here - the capture thread owns the device and applies it
    // between grabs once the automatics have settled (see
    // interfaces/camera.hpp). The outcome arrives via poll().
    //
    // Defaults are used deliberately: every value is frozen at whatever the
    // camera's own automatics chose for THIS room, rather than at numbers
    // measured on one developer's webcam.
    const auto requested = cube::platform::camera::request_photometry_lock(
        cube::platform::camera::PhotometryLockRequest{});
    if (!requested) {
        // Can only happen if the capture stopped between the future
        // completing and this call. Nothing is broken - the scan runs on an
        // unlocked camera - but the user must not be told otherwise.
        camera_locked_ = false;
        camera_lock_warning_ = true;
        camera_lock_status_ =
            QString::fromStdString(std::string(cube::to_string(requested.error())));
        emit cameraLockChanged();
    }
}

bool ScanSession::cameraLocked() const noexcept {
    return camera_locked_;
}

bool ScanSession::cameraLockWarning() const noexcept {
    return camera_lock_warning_;
}

QString ScanSession::cameraLockStatus() const {
    return camera_lock_status_;
}

void ScanSession::confirmFace() {
    // Manual confirmation runs the exact same gated commit path as
    // auto-advance - the button exists so auto_advance_ off still works,
    // not to bypass any safety gate.
    tryCommitConverged();
}

void ScanSession::retakeFace() {
    resetAggregators();
    resetReading();
    emit readingChanged();
}

void ScanSession::retakeSlot(int slotIndex) {
    if (!router_.clear_slot(slotIndex)) {
        return;
    }
    // Every window restarts: the cleared face's samples are stale by
    // definition, and the store<->epoch mapping behind the other windows
    // is router-internal - a few hundred milliseconds of re-convergence is
    // cheaper than exposing the gauge just to save the other windows.
    resetAggregators();
    emit facesScannedChanged();
    emit readingChanged();
}

void ScanSession::restartScan() {
    progress_ = cube::model::ScanProgress();
    router_.clear();
    assembled_cube_.reset();
    // P6.12 track-mode fusion: a restarted scan may be a different cube,
    // so every sub-epoch's evidence (and the generation counters keyed to
    // the dead track's epochs) goes with it.
    track_fusion_.clear();
    track_fusion_generation_.clear();
    track_fusion_active_ = false;
    track_fusion_faces_ = 0;
    track_fusion_attempt_sig_ = {-1, -1};
    resetAggregators();
    resetReading();
    emit facesScannedChanged();
    emit readingChanged();
}

void ScanSession::poll() {
    // Debug-only clip playback substitutes for the camera mailbox here and
    // nowhere else, so everything downstream - analysis queueing, the
    // tracker, the slot router, every Q_PROPERTY - runs the identical code
    // path it runs live (see replay_dir()). Normal runs take the camera
    // branch unconditionally.
    // Checked before the frame gate below, not after: a report is published
    // once per lock and would be dropped for a whole poll tick every time
    // the mailbox happened to be empty.
    if (auto report = cube::platform::camera::take_photometry_report()) {
        camera_locked_ = report->exposure_locked();
        camera_lock_warning_ = !report->fully_applied();
        camera_lock_status_ = QString::fromStdString(report->summary());
        emit cameraLockChanged();
    }

    // Replay is LOSSLESS (P6.12): while a pass is in flight the clip is
    // HELD, not popped-and-dropped - a recorded frame the busy gate
    // discards is gone forever, which made replay coverage depend on this
    // machine's poll-vs-analysis timing (measured on synth-freerot: 124
    // fusion observations and 5/6 converged where the deterministic
    // offline replay of the same clip reaches 6/6). The camera path is
    // untouched below: the mailbox pop must happen every tick so the
    // preview always shows the latest frame, and busy-skip remains the
    // live cadence policy by design.
    if (replay_active() && (analysis_inflight_ || analysis_watcher_.isRunning())) {
        return;
    }

    auto frame =
        replay_active() ? try_pop_replay_frame() : cube::platform::camera::try_pop_latest_frame();
    if (!frame) {
        return;
    }

    {
        std::scoped_lock lock(frame_mutex_);
        latest_frame_ = frame->frame;
    }
    frame_width_ = frame->frame.cols;
    frame_height_ = frame->frame.rows;
    ++frame_seq_;
    emit frameUpdated();

    // Recorded at pop time, GUI-side, so the corpus captures every frame
    // the pipeline COULD see - including ones the busy gate below skips.
    record_camera_frame_v2(frame->frame, frame->t_ms);

    if (analysis_inflight_ || analysis_watcher_.isRunning()) {
        // Still analyzing a previous frame (or its result is queued but not
        // yet handled - see analysis_inflight_ in the header). The two
        // halves cover each other's blind spot: the flag closes
        // isRunning()'s finished-but-unhandled window, while isRunning()
        // covers the one window where the flag is deliberately
        // force-cleared with a pass possibly still executing - a wedged or
        // long pass straddling a stop()/start() cycle (futures are
        // uncancelable, so it keeps running on the pool after the restart,
        // and queuing a second pass beside it would race the lock-free
        // worker-side per-pass state: the StabilityTracker history, the
        // recorder statics). The live image above still updated, just skip
        // queuing another pass. A pass
        // should take tens of milliseconds; one that has held the gate for
        // seconds means a wedged worker (driver stall, debugger break) and
        // deserves a trace, since the only visible symptom is the overlay
        // silently freezing while the video keeps playing.
        if (analysis_inflight_timer_.isValid() &&
            analysis_inflight_timer_.elapsed() > kAnalysisOverrunWarnMs &&
            !analysis_overrun_logged_) {
            analysis_overrun_logged_ = true;
            qWarning("ScanSession: analysis pass still unfinished after %lld ms",
                     static_cast<long long>(analysis_inflight_timer_.elapsed()));
        }
        return;
    }

    // Sampled here (GUI thread) rather than inside analyze() so the worker
    // never reads the slot store concurrently with a capture mutating it.
    const bool corner_armed = cornerScanArmed();
    // Pose continuity prior, sampled by the same GUI-thread-at-queue-time
    // rule from the held pose. cube_pose_'s own hold-and-clear lifecycle
    // (kOverlayHoldMs) bounds the seed's staleness for free - a pose the
    // overlay already dropped is too old to bias arbitration with. Gated
    // on orientation_valid because an invalid corner pose carries zeroed
    // extrinsics (a model AT the camera), which as a seed would be poison.
    std::optional<cube::vision::PoseSeed> pose_prior;
    if (cube_pose_ && cube_pose_->orientation_valid) {
        pose_prior = cube::vision::PoseSeed{cube_pose_->rvec, cube_pose_->tvec};
    }
    // Lost-mode cadence recovery, both verdicts sampled GUI-side at queue
    // time like every other per-pass prior: (1) ONNX decimation stretches
    // while lost (see kHandOnnxDecimationLost); (2) the idle gate may skip
    // the detector cascade outright on a cube-absent frame - its state was
    // updated from the PREVIOUS pass's cool-hue count in
    // handleAnalysisFinished, so the worker stays stateless and the
    // one-pass verdict lag is covered by the gate's entry hysteresis.
    const bool tracker_lost = tracker_.state() == cube::vision::TrackState::lost;
    const int hand_decimation = tracker_lost ? kHandOnnxDecimationLost : kHandOnnxDecimation;
    const bool run_hand_detector = (analysis_pass_seq_++ % hand_decimation) == 0;
    const bool skip_detectors = idle_gate_.skip_next();
    analysis_inflight_ = true;
    analysis_inflight_timer_.restart();
    analysis_overrun_logged_ = false;
    analysis_watcher_.setFuture(QtConcurrent::run([this, captured_frame = frame->frame,
                                                   t_ms = frame->t_ms, corner_armed, pose_prior,
                                                   run_hand_detector, skip_detectors]() {
        // The analysis body must NEVER let an exception escape. A
        // QtConcurrent future STORES a thrown exception and rethrows it
        // at analysis_watcher_.result() in handleAnalysisFinished - on
        // the GUI thread, where nothing catches it, so std::terminate
        // kills the app mid-scan (observed live: an MSVC C++ runtime
        // error, no Windows Error Reporting entry, process simply
        // gone). OpenCV is the likeliest thrower - cv::Exception comes
        // out of any ROI/resize/solve whose preconditions a degenerate
        // frame or transform violates - but this guard is deliberately
        // blanket: dropping one frame's analysis always beats losing
        // the whole scan, and the logged message NAMES the thrower so
        // the underlying precondition gets fixed rather than guessed.
        try {
            return analyze(captured_frame, t_ms, corner_armed, pose_prior, run_hand_detector,
                           skip_detectors);
        } catch (const cv::Exception& error) {
            qWarning("ScanSession: OpenCV threw during analysis (frame dropped): %s", error.what());
        } catch (const std::exception& error) {
            qWarning("ScanSession: analysis threw during analysis (frame dropped): %s",
                     error.what());
        } catch (...) {
            qWarning("ScanSession: analysis threw a non-std exception (frame dropped)");
        }
        // A default-constructed result reads as "this pass saw
        // nothing": no face, no corner, no pose. The tracker treats it
        // exactly like a detection miss and coasts, which is the
        // correct degradation - the cube did not move just because one
        // frame's maths failed.
        return AnalysisResult{};
    }));
}

void ScanSession::handleAnalysisFinished() {
    // Cleared unconditionally before ANY early-out: every queued pass must
    // release the gate exactly once, or a result discarded below (post-stop
    // delivery) would leave the gate held forever and no analysis would
    // ever run again.
    analysis_inflight_ = false;
    if (!running_) {
        // An analysis pass in flight when stop() was called still delivers
        // its result afterward (QtConcurrent futures are uncancelable).
        // Without this guard it could aggregate, auto-capture the final
        // face, and complete the scan - yanking the user to the solve
        // screen from wherever they navigated to.
        return;
    }
    auto result = analysis_watcher_.result();
    // P2.9 gate latency, captured before any handling work inflates it:
    // elapsed since THIS pass's frame left the camera mailbox.
    const qint64 gate_latency_ms =
        analysis_inflight_timer_.isValid() ? analysis_inflight_timer_.elapsed() : -1;

    // Overlay persistence: per-frame detection blips (~1 in 4 frames drops
    // even on a good view) must not strobe the AR overlay in and out. A
    // fresh observation replaces the held one, gently smoothed toward the
    // previous geometry so the overlay reads as locked-on rather than
    // jittering; a missing observation KEEPS the held one for
    // kOverlayHoldMs (QML fades it via overlayFreshness) before it clears.
    // Aggregation below uses ONLY the fresh observation - persistence is
    // display-only and never feeds colour data.
    const bool fresh_face = result.face.has_value();
    const bool fresh_corner = result.corner.has_value();
    if (fresh_face) {
        if (face_observation_) {
            smooth_observation(*result.face, *face_observation_);
        }
        face_observation_ = result.face;
    } else if (face_observation_ && overlay_hold_timer_.isValid() &&
               overlay_hold_timer_.elapsed() > kOverlayHoldMs) {
        face_observation_.reset();
    }
    if (fresh_corner) {
        corner_observation_ = result.corner;
    } else if (corner_observation_ && overlay_hold_timer_.isValid() &&
               overlay_hold_timer_.elapsed() > kOverlayHoldMs) {
        corner_observation_.reset();
    }
    if (result.pose.has_value()) {
        if (cube_pose_) {
            for (std::size_t i = 0; i < result.pose->vertices.size(); ++i) {
                result.pose->vertices[i] = kOverlaySmoothing * result.pose->vertices[i] +
                                           (1.0F - kOverlaySmoothing) * cube_pose_->vertices[i];
            }
        }
        cube_pose_ = result.pose;
    } else if (cube_pose_ && overlay_hold_timer_.isValid() &&
               overlay_hold_timer_.elapsed() > kOverlayHoldMs) {
        cube_pose_.reset();
    }
    // A fresh pose restarts the hold too: pose-only face fits (and later
    // pose-only paths) legitimately produce a wireframe pose with NO
    // family observation, and without this the wireframe would go stale
    // mid-stream while being replaced every pass. Today a pose implies a
    // fresh family observation, so the extra clause is behaviour-identical
    // for the historical paths.
    if (fresh_face || fresh_corner || result.pose.has_value()) {
        overlay_hold_timer_.restart();
    }

    is_steady_ = result.is_steady;
    // Held across decimation-skipped passes: an unchecked pass says
    // nothing about the hand either way (see kHandOnnxDecimation).
    if (result.hand_checked) {
        hand_present_ = result.hand_present;
    }

    if (face_observation_) {
        facelets_ = provisional_facelets(face_observation_->lab);
        float sum = 0.0F;
        for (const auto& facelet : facelets_) {
            sum += facelet.confidence;
        }
        overall_confidence_ = sum / static_cast<float>(facelets_.size());
    } else {
        facelets_.fill(cube::model::Facelet{});
        overall_confidence_ = 0.0F;
    }

    // --- Orientation tracking ----------------------------------------------
    // Exactly one tracker pass per analysis pass, after the display block
    // and before routing: the tracker's output IS the routing key for
    // everything below - the identity-keyed aggregation and the commit
    // path read this pass's state, epoch and resolved lattice element.
    // Inputs are strictly THIS pass's fresh results, never the
    // persistence-held observations: the tracker does its own coasting
    // from real timestamps, and re-feeding held data would double-count
    // frames as fresh evidence.
    const bool corner_pose = result.pose_source == AnalysisResult::PoseSource::corner ||
                             result.pose_source == AnalysisResult::PoseSource::corner_seeded;
    // Face-family provenance must be POSITIVE, not "not corner": an edge
    // pose (or a pose-only face pass) can coexist with a fresh face
    // observation that failed ITS pose solve, and keying colour evidence
    // or aggregation off that observation under a foreign pose's gauge
    // would scramble stickers.
    const bool face_pose = result.pose_source == AnalysisResult::PoseSource::face ||
                           result.pose_source == AnalysisResult::PoseSource::face_seeded;
    cube::vision::TrackerPassOutput track;
    {
        cube::vision::TrackerPassInput track_in;
        track_in.t_ms = result.frame_ms;
        track_in.motion_score = result.motion_score;
        if (result.pose && result.pose->orientation_valid) {
            track_in.q_obs = result.pose->orientation;
            // The centre-Lab evidence must come from the SAME observation
            // family that produced the pose - pose_source, not mere
            // presence, decides (a pass can hold both a face and a corner
            // observation, whose model frames differ).
            track_in.corner_view = corner_pose;
            const auto lab_entry = [](const cv::Vec3f& lab) {
                return std::array<float, 3>{lab[0], lab[1], lab[2]};
            };
            if (corner_pose && result.corner) {
                track_in.centre_lab_count = 3;
                track_in.centre_lab[0] =
                    lab_entry(result.corner->top.lab[cube::model::center_facelet_index]);
                track_in.centre_lab[1] =
                    lab_entry(result.corner->lower_left.lab[cube::model::center_facelet_index]);
                track_in.centre_lab[2] =
                    lab_entry(result.corner->lower_right.lab[cube::model::center_facelet_index]);
            } else if (face_pose && result.face) {
                track_in.centre_lab_count = 1;
                track_in.centre_lab[0] =
                    lab_entry(result.face->lab[cube::model::center_facelet_index]);
            }
            // An edge-sourced pose deliberately attaches nothing: the
            // observation carries no colour by design.
        }
        last_pose_from_track_ = result.pose_from_track;
        // HUD facts ride each TRACK pass verbatim - the HUD's point is
        // the loop's real numbers, unsmoothed and unheld. Idle-skipped
        // passes never ran the loop, so they update nothing: reinits is
        // a cumulative loop counter and a skipped pass zeroing it would
        // be the HUD lying in the other direction.
        if (result.track_pass) {
            hud_health_ = result.track_health;
            hud_reinits_ = result.track_reinits;
            hud_pass_ms_ = result.pass_ms;
        }
        // P2.9 live-gate log: every handled pass, raw values only.
        if (!gate_log_path().isEmpty()) {
            if (!gate_log_.is_open()) {
                gate_log_.open(gate_log_path().toStdString(), std::ios::trunc);
                gate_log_ << "frame_ms,latency_ms,track_pass,tracked,health,reinits,pass_ms\n";
            }
            gate_log_ << result.frame_ms << ',' << gate_latency_ms << ','
                      << (result.track_pass ? 1 : 0) << ',' << (result.pose_from_track ? 1 : 0)
                      << ',' << result.track_health << ',' << result.track_reinits << ','
                      << result.pass_ms << '\n';
            ++gate_passes_;
            gate_tracked_ += result.pose_from_track ? 1 : 0;
            if (gate_latency_ms >= 0) {
                gate_latencies_.push_back(static_cast<double>(gate_latency_ms));
            }
        }
        // In track mode the legacy orientation tracker is NOT fed: its
        // gates speak a different frame convention than the continuous
        // track (measured refusing 85% of M3T orientations as
        // implausible), and every downstream legacy consumer - identity
        // aggregation, routing, capture - correctly no-ops off its
        // default lost state. Its real successor for identity is Phase 3.
        if (!result.pose_from_track) {
            track = tracker_.on_pass(track_in);
        }

        // Display smoothing - the same nlerp the raw pose stream used to
        // feed, now fed by the tracker (which coasts through detection
        // gaps, so the 3D cube keeps turning where it used to freeze).
        // Lost/acquiring drop the smoothed state entirely: cubeOrientation
        // must go empty (QML idles), and the next lock must adopt the new
        // orientation outright instead of visibly easing over from a dead
        // track's pose.
        if (result.pose_from_track || track.state == cube::vision::TrackState::locked ||
            track.state == cube::vision::TrackState::coasting) {
            // Stamped only for a REAL sample, never while holding one -
            // refreshing it during the hold would make the hold immortal.
            last_orientation_ms_ = result.frame_ms;
            // A pose from the continuous track feeds the display DIRECTLY:
            // the legacy tracker's plausibility gates speak a different
            // frame convention and would refuse/coast it (the P2.5 known
            // limitation this closes).
            const std::array<float, 4>& fresh =
                result.pose_from_track && result.pose ? result.pose->orientation : track.q_track;
            if (!has_smoothed_orientation_ || raw_pose_display()) {
                smoothed_orientation_ = fresh;
                has_smoothed_orientation_ = true;
            } else {
                // nlerp toward the fresh sample. Quaternions double-cover
                // rotations (q and -q are the SAME orientation), and the
                // tracker is free to hand back either sign frame to frame
                // - blending raw components across that sign flip would
                // drag the interpolant through zero (a wild spin on
                // screen, or a vanishing norm). dot < 0 means the fresh
                // sample sits on the far hemisphere, so negate it onto
                // ours first; nlerp between same-hemisphere unit
                // quaternions then never degenerates and normalising keeps
                // the result unit.
                float dot = 0.0F;
                for (std::size_t i = 0; i < 4; ++i) {
                    dot += smoothed_orientation_[i] * fresh[i];
                }
                const float sign = dot < 0.0F ? -1.0F : 1.0F;
                float norm_squared = 0.0F;
                for (std::size_t i = 0; i < 4; ++i) {
                    smoothed_orientation_[i] +=
                        kOrientationSmoothing * (sign * fresh[i] - smoothed_orientation_[i]);
                    norm_squared += smoothed_orientation_[i] * smoothed_orientation_[i];
                }
                const float norm = std::sqrt(norm_squared);
                for (auto& component : smoothed_orientation_) {
                    component /= norm;
                }
            }
        } else if (has_smoothed_orientation_ &&
                   result.frame_ms - last_orientation_ms_ <= kOrientationHoldMs) {
            // HOLD the last pose rather than dropping it. Dropping on ANY
            // lost/acquiring pass made the 3D cube snap back to its idle
            // presentation and then re-adopt from scratch: ScanScreen binds
            // autoRotate to (liveOrientation === null), so an empty
            // cubeOrientation re-imposes homeRotation on cubeRoot. live5
            // spends 42 of its 373 passes lost or acquiring, scattered
            // throughout, so the view was reset several times a second and
            // never sustained a pose long enough to read as following the
            // cube in hand - the user reported exactly this, as "3d cube
            // barely moved". A brief detection gap is not evidence that the
            // cube moved, so the last known pose remains the best available
            // estimate and freezing it is the honest display; it is only
            // abandoned once the gap outlasts any plausible coast, below.
        } else {
            has_smoothed_orientation_ = false;
        }

        // Tracked translation for the 3D model (P2.6). Only the
        // continuous track carries a translation the display can use
        // (the legacy tracker is orientation-only), so cubePosition is
        // set exactly on tracked passes, held through the same window
        // as the orientation, and cleared with it - the model eases
        // home rather than snapping on a detection blip.
        if (result.pose_from_track && result.pose) {
            const auto& t = result.track_tvec;
            std::array<double, 3> fresh_position{0.0, 0.0, 0.0};
            if (t[2] > 1.0e-6) {
                fresh_position[0] = std::clamp(kPositionLateralGain * (t[0] / t[2]),  //
                                               -kPositionLateralLimit, kPositionLateralLimit);
                fresh_position[1] = std::clamp(-kPositionLateralGain * (t[1] / t[2]),  //
                                               -kPositionLateralLimit, kPositionLateralLimit);
                fresh_position[2] =
                    std::clamp(kPositionDepthGain * (1.0 - t[2] / kPositionDepthReference),
                               kPositionDepthMin, kPositionDepthMax);
            }
            if (!has_position_ || raw_pose_display()) {
                smoothed_position_ = fresh_position;
                has_position_ = true;
            } else {
                for (std::size_t i = 0; i < 3; ++i) {
                    smoothed_position_[i] +=
                        kPositionSmoothing * (fresh_position[i] - smoothed_position_[i]);
                }
            }
        } else if (has_position_ && result.frame_ms - last_orientation_ms_ <= kOrientationHoldMs) {
            // Hold: a detection gap is not evidence the cube went home.
        } else {
            has_position_ = false;
        }
        log_track_pass(track);

        // Idle-gate update from the SAME pass's presence statistic and
        // the tracker's post-pass state (only a lost tracker may idle).
        // Mutated here - GUI thread, once per pass - so poll() can sample
        // the skip verdict at queue time with the worker kept stateless.
        // "Lost" must consult the CONTINUOUS track too: in track mode the
        // legacy tracker is never fed and reads lost forever, and the
        // cool-hue statistic reads near-zero on a low-saturation cube -
        // measured on the cardboard live-gate run, the gate idled 432 of
        // 2058 passes WHILE M3T was locked, costing 21 points of
        // on-screen locked fraction (79% raw vs 97.8% over passes the
        // loop actually ran).
        idle_gate_.on_pass(
            result.cool_hue_pixels,
            !result.pose_from_track && track.state == cube::vision::TrackState::lost);
    }

    // --- Identity-keyed aggregation & routing ------------------------------
    // Epoch bookkeeping first: aggregator keys are the tracker's
    // epoch-local body faces, so a new epoch (re-anchor after loss,
    // teleport re-anchor, restart) resets every window before anything
    // mis-keyed could accumulate. The router runs its own epoch response
    // (gauge invalidation, recovery-mode entry, strike reset).
    router_.on_epoch(track.epoch);
    if (track.epoch != agg_epoch_) {
        agg_epoch_ = track.epoch;
        resetAggregators();
        consec_accepts_ = 0;
        has_last_track_q_ = false;
    }
    const bool pass_accepted = track.gate == cube::vision::TrackGate::accepted ||
                               track.gate == cube::vision::TrackGate::anchored;
    // The resolved per-pass rotation - the commit-time motion gate. Only
    // meaningful between two accepted passes of one epoch; a coasted or
    // rejected pass zeroes the streak, which also (deliberately) makes a
    // fresh re-lock re-earn kMinCaptureAccepts before it may commit.
    pass_rotation_deg_ = pass_accepted && has_last_track_q_
                             ? cube::vision::quat_angle_deg(track.q_track, last_track_q_)
                             : 0.0F;
    if (pass_accepted) {
        last_track_q_ = track.q_track;
        has_last_track_q_ = true;
        ++consec_accepts_;
    } else {
        consec_accepts_ = 0;
    }

    // Gate 1 of the capture policy - the identity gate: locked track with
    // an accept streak, plus commit-grade identity PER BODY FACE (the
    // face-scoped belief margin, waived while fewer than two slots exist
    // for it to be wrong against). Face-scoped because the full-24 margin
    // is structurally capped at zero on single-centre evidence (live3: 0
    // certain passes in 480 - the observed face's in-plane stabilizers
    // score identically to identity forever), while quotienting out
    // exactly those provably-harmless hypotheses makes the same bar
    // reachable in one frontal burst. Failing the track-level half drops
    // the pass's observations entirely (routing them anywhere would be a
    // guess); the per-face half is applied where a face is actually
    // named - aggregation adds and commits.
    const bool identity_waived = router_.filled_count() < 2;
    for (std::size_t f = 0; f < capture_identity_ok_.size(); ++f) {
        capture_identity_ok_[f] = track.identity_certain_face[f] || identity_waived;
    }
    capture_gate_ok_ = track.state == cube::vision::TrackState::locked &&
                       consec_accepts_ >= kMinCaptureAccepts && pass_accepted &&
                       track.lattice_index >= 0;
    capture_q_ = track.q_track;
    capture_motion_score_ = result.motion_score;
    last_frame_ms_ = result.frame_ms;

    // Aggregation consumes result.face/result.corner (fresh only), never
    // the persistence-held observations - re-adding a held frame would
    // stuff duplicate samples into the ring buffer and fake convergence.
    // Only the observation family that produced THIS pass's pose routes:
    // the resolved lattice element describes that family's model frame,
    // and applying it to the other family's cells would scramble stickers.
    // The slot-count gate stops aggregation once six faces are in - and
    // reopens if a failed assembly drops a slot back to five.
    if (capture_gate_ok_ && router_.filled_count() < static_cast<int>(cube::model::face_count)) {
        const auto lattice = static_cast<std::size_t>(track.lattice_index);
        const auto add_keyed = [&](const cube::vision::FaceCellMap& map,
                                   const cube::vision::FaceObservation& observation,
                                   bool from_corner, bool full_quads) {
            // Gate 4 (fully-in-frame quads) is enforced HERE, per
            // observation, by refusing the add: a quad clipped by the
            // frame edge samples clamped border colour, so letting it into
            // the ring would pollute the fused capture, while tracking it
            // as window state would have to be un-set again somehow - a
            // sticky flag provably wedged a face for as long as it stayed
            // observed (adds kept refreshing the staleness clock that was
            // supposed to clear it). Skipping keeps the window's clean
            // samples intact, so one clipped pan costs nothing, and a face
            // that stays clipped simply goes stale and restarts.
            if (!full_quads) {
                return;
            }
            const auto face_index = static_cast<std::size_t>(map.body_face);
            // Gate 1's per-face half: no commit-grade identity for THIS
            // body face means the observation's routing key is a guess -
            // drop it rather than accumulate under it.
            if (!capture_identity_ok_[face_index]) {
                return;
            }
            auto& aggregator = aggregators_[face_index];
            auto& meta = metas_[face_index];
            // Per-identity staleness AND permutation consistency: a window
            // whose cell mapping just flipped (the resolution re-keyed at
            // a 45-degree boundary faster than the samples aged out) must
            // restart rather than mix two labellings of the same stickers.
            if (meta.has_window && (result.frame_ms - meta.last_add_ms > kIdentityAggStaleMs ||
                                    meta.window_map != map.cell)) {
                aggregator.reset();
                meta = AggregatorMeta{};
            }
            // Permute screen-order cells to solver order ON THE WAY IN, so
            // window cell k always means the same physical sticker no
            // matter how the face was presented - the chimera-window
            // defeat, and what makes the captured slot directly
            // assemblable.
            cube::vision::FaceObservation permuted = observation;
            for (std::size_t obs = 0; obs < permuted.lab.size(); ++obs) {
                const auto solver_index = static_cast<std::size_t>(map.cell[obs]);
                permuted.lab[solver_index] = observation.lab[obs];
                permuted.cells[solver_index] = observation.cells[obs];
            }
            aggregator.add(permuted);
            meta.has_window = true;
            meta.window_map = map.cell;
            meta.last_add_ms = result.frame_ms;
            meta.corner_fed = meta.corner_fed || from_corner;
        };
        // Each family carries its OWN full-quads verdict: only the routed
        // family's observation reaches the aggregators, so the other
        // family's incidental clipping must not withhold it (a face
        // observation grazing the border says nothing about the corner
        // views a corner-pose pass is actually feeding).
        if (corner_pose && fresh_corner) {
            const std::array<const cube::vision::CornerFaceView*, 3> views{
                &result.corner->top, &result.corner->lower_left, &result.corner->lower_right};
            for (std::size_t view = 0; view < views.size(); ++view) {
                add_keyed(cube::vision::kCornerObsToBody[lattice][view],
                          slot_observation(*views[view], result.corner->geometry_confidence),
                          /*from_corner=*/true, result.corner_full_quads);
            }
        } else if (face_pose && fresh_face) {
            add_keyed(cube::vision::kFaceObsToBody[lattice], *result.face, /*from_corner=*/false,
                      result.face_full_quads);
        }
        // Edge-sourced passes aggregate nothing: pose-only by contract.
    }

    // --- P6.12: track-mode fusion feed + completion -------------------------
    // The chain the offline runner replays, run live: capture-grade
    // tracked reads accumulate in the sub-epoch fusion store, and
    // completion goes registration -> best sub-epoch -> resolve ->
    // rotation hint -> assembly. The legacy aggregation/commit path above
    // is untouched and inert in track mode (its tracker is never fed);
    // both surfaces coexist until Phase 7 deletes the legacy one.
    if (result.track_locked) {
        feedTrackFusion(result);
    }
    maybeFinalizeTrackFusion();

    emit readingChanged();

    if (auto_advance_) {
        tryCommitConverged();
    }
}

ScanSession::AnalysisResult ScanSession::analyze(const cv::Mat& frame, std::int64_t frame_t_ms,
                                                 bool corner_armed,
                                                 std::optional<cube::vision::PoseSeed> pose_prior,
                                                 bool run_hand_detector, bool skip_detectors) {
    QElapsedTimer pass_timer;
    pass_timer.start();
    AnalysisResult result;
    result.frame_ms = frame_t_ms;

    // Steadiness and hand presence stay on the full frame - both are
    // advisory hint signals now (cross-frame variance in the aggregators
    // gates capture), and their tuned thresholds predate the downscale.
    const auto stability = stability_.update(frame);
    result.is_steady = stability.is_steady;
    // The raw score rides along for the tracker (coasting uncertainty
    // inflates faster when the scene is visibly moving) - dropping it here
    // would silently pin the tracker's motion term at zero and poison the
    // calibration sweeps shadow mode exists to feed.
    result.motion_score = stability.motion_score;

    // Decimated (see kHandOnnxDecimation): the verdict was decided on the
    // GUI thread at queue time, so the worker stays counter-free. A pass
    // that was ELECTED to check reports hand_checked even when detection
    // is off or unavailable - "checked, no hand" - so toggling the setting
    // off clears the UI hint within a decimation period instead of
    // freezing a stale "hand present".
    if (run_hand_detector) {
        result.hand_checked = true;
        if (hand_detector_ && hand_detection_enabled_.load()) {
            const auto hand_result = hand_detector_->hand_present(frame);
            result.hand_present = hand_result.has_value() && *hand_result;
        }
    }

    // The shared downscale (recording_v2.hpp): the recorder thread applies
    // the identical function, so recorded analysis frames are exactly what
    // this pass consumed.
    const auto downscaled = cube::vision::recording::analysis_downscale(frame);
    const cv::Mat& analysis_frame = downscaled.frame;
    const float scale_back = downscaled.scale_back;

    // Cool-hue presence statistic, computed EVERY pass (idle-skipped ones
    // included - it is the signal that ENDS an idle span, so it cannot
    // itself be skipped). Stateless and cheap (fixed 160x120); the GUI
    // thread owns the IdleGate state it feeds.
    result.cool_hue_pixels = cube::vision::count_cool_hue_pixels(analysis_frame);

    // Idle skip (see IdleGate): the tracker is lost and the last passes
    // showed no cube-coloured pixels, so the detector cascade would burn
    // its full cost on an empty frame. Stability, hand election, the
    // recorder and the presence statistic above all still ran - only the
    // detectors and pose are elided, so the pass still ticks the tracker's
    // clock as an honest no-observation pass.
    if (skip_detectors) {
        VisionDebugStats idle_stats{};
        idle_stats.is_steady = result.is_steady;
        idle_stats.hand_present = result.hand_present;
        idle_stats.face_status = "idle-skipped";
        idle_stats.corner_status = "idle-skipped";
        dump_vision_debug(analysis_frame, idle_stats);
        return result;
    }

    // The DEFAULT pipeline (P2.10): a pass is a tracking pass - detector
    // (decimated while locked) + M3T step + health, nothing else. The
    // legacy cascade below never runs in track mode, locked or lost; it
    // is the quarantined CUBE_VISION_PIPELINE=legacy path (still the
    // scan-to-solve route until Phases 3-5 replace colour capture). The
    // wireframe and 3D model follow the track; lost passes return with
    // no pose and the detector keeps hunting for the next anchor.
    if (live_track::track_mode()) {
        auto sample = live_track::step(analysis_frame, scale_back);
        if (!live_track::track_mode()) {
            // Model load failed on this very pass: fall through to the
            // legacy cascade from the next pass on (this one returns
            // empty rather than running half of each).
            return result;
        }
        if (sample.pose.has_value()) {
            result.pose = *sample.pose;
            result.pose_from_track = true;
            result.track_tvec = {sample.tvec[0], sample.tvec[1], sample.tvec[2]};
        }
        result.track_pass = true;
        result.track_health = sample.health_frac;
        result.track_reinits = sample.reinits;
        // P6.12: the pass's capture surface rides the result to the GUI
        // thread, which owns the fusion store (same all-mutation-on-GUI
        // rule as the legacy aggregators).
        result.track_locked = sample.locked;
        result.track_gauge_epoch = sample.gauge_epoch;
        result.track_face_samples = std::move(sample.faces);
        result.pass_ms = static_cast<double>(pass_timer.nsecsElapsed()) / 1.0e6;
        VisionDebugStats track_stats{};
        track_stats.is_steady = result.is_steady;
        track_stats.hand_present = result.hand_present;
        track_stats.face_status = sample.pose.has_value() ? "track-locked" : "track-lost";
        track_stats.corner_status = track_stats.face_status;
        dump_vision_debug(analysis_frame, track_stats);
        return result;
    }

    // A cell projected outside the frame samples fabricated colour (a
    // clamped border strip, or corner sampling's all-zero fallback) that is
    // perfectly stable frame to frame - stable enough to pass temporal
    // convergence and get captured as a real sticker. Only fully-visible
    // observations may feed the aggregators.
    const auto cell_in_frame = [&frame](const cube::vision::StickerQuad& cell) {
        cv::Point2f center(0.0F, 0.0F);
        for (const auto& corner : cell.corners) {
            center += corner;
        }
        center *= 0.25F;
        return center.x >= 0.0F && center.y >= 0.0F && center.x < static_cast<float>(frame.cols) &&
               center.y < static_cast<float>(frame.rows);
    };

    VisionDebugStats debug_stats{};
    debug_stats.is_steady = result.is_steady;
    debug_stats.hand_present = result.hand_present;

    // Pose-only face observations (translation-ambiguous lattice fits, see
    // FaceObservation::pose_only) are held OUT of result.face on purpose:
    // result.face is what feeds facelet display, colour aggregation and
    // capture, and a pitch-shifted grid's lab samples must never reach any
    // of those. The observation's only legitimate consumer is the pose
    // projector below, so it lives in a local that goes out of scope with
    // this pass.
    std::optional<cube::vision::FaceObservation> pose_only_face;
    auto face = cube::vision::observe_face(analysis_frame);
    if (face.has_value()) {
        scale_observation(*face, scale_back);
        if (!std::ranges::all_of(face->cells, cell_in_frame)) {
            debug_stats.face_status = "gated-offframe";
        } else if (face->pose_only) {
            debug_stats.face_status = "pose-only conf=" + std::to_string(face->geometry_confidence);
            pose_only_face = std::move(*face);
        } else {
            debug_stats.face_status = "ok conf=" + std::to_string(face->geometry_confidence);
            result.face = std::move(*face);
        }
    } else {
        debug_stats.face_status = "none";
    }

    debug_stats.corner_status = "unarmed";
    if (corner_armed) {
        // The same GUI-sampled prior that seeds the pose solves also hints
        // the corner detector: at a predicted three-faces-visible pose the
        // fixed isometric sectors/bases are replaced with pose-derived ones
        // (compute_corner_hint), so corner detections keep firing well away
        // from the prompted pose - the detection-cadence closure the
        // tracker's coasting budget depends on. Computed HERE on the worker
        // from the by-value prior (the GUI thread ships only the pose, per
        // the sampling contract); a frontal/edge-on prediction yields no
        // hint and the detector runs exactly the historical fixed-pose way.
        std::optional<cube::vision::CornerHint> corner_hint;
        if (pose_prior) {
            corner_hint = cube::vision::compute_corner_hint(*pose_prior);
        }
        auto corner =
            cube::vision::observe_corner(analysis_frame, corner_hint ? &*corner_hint : nullptr);
        if (corner.has_value()) {
            scale_corner_view(corner->top, scale_back);
            scale_corner_view(corner->lower_left, scale_back);
            scale_corner_view(corner->lower_right, scale_back);
            const bool all_visible = std::ranges::all_of(corner->top.cells, cell_in_frame) &&
                                     std::ranges::all_of(corner->lower_left.cells, cell_in_frame) &&
                                     std::ranges::all_of(corner->lower_right.cells, cell_in_frame);
            if (all_visible) {
                debug_stats.corner_status =
                    "ok conf=" + std::to_string(corner->geometry_confidence);
                result.corner = std::move(*corner);
            } else {
                debug_stats.corner_status = "gated-offframe";
            }
        } else {
            debug_stats.corner_status = "none";
        }
    }

    // Strict full-visibility flags for capture-time policy (see the
    // fields' comment in the header): every cell quad of the family's
    // accepted observation must have ALL FOUR corners in-frame, not just
    // its centre. Computed here where the geometry is already in
    // full-frame pixels; false when the family produced nothing. Kept PER
    // FAMILY: only the pose-producing family feeds the aggregators, and
    // AND-ing both families together let the non-routed one's clipping
    // veto observations it had nothing to do with.
    const auto quad_fully_in_frame = [&frame](const cube::vision::StickerQuad& cell) {
        return std::ranges::all_of(cell.corners, [&frame](const cv::Point2f& corner) {
            return corner.x >= 0.0F && corner.y >= 0.0F &&
                   corner.x < static_cast<float>(frame.cols) &&
                   corner.y < static_cast<float>(frame.rows);
        });
    };
    if (result.face) {
        result.face_full_quads = std::ranges::all_of(result.face->cells, quad_fully_in_frame);
    }
    if (result.corner) {
        result.corner_full_quads =
            std::ranges::all_of(result.corner->top.cells, quad_fully_in_frame) &&
            std::ranges::all_of(result.corner->lower_left.cells, quad_fully_in_frame) &&
            std::ranges::all_of(result.corner->lower_right.cells, quad_fully_in_frame);
    }

    // 3D pose for the wireframe overlay, from whichever observation
    // survived its gates - corner first, since three pinned face quads
    // constrain the pose far better than one. Runs on the scaled-BACK
    // observation with the full frame size so the projected vertices land
    // in full-frame pixel coordinates like every other overlay quad (the
    // projection's approximate intrinsics derive from the frame size, so
    // geometry and size must describe the same coordinate space). A
    // degenerate pose estimate just means no wireframe this reading. The
    // GUI-sampled prior seeds both projectors toward temporal continuity;
    // the projectors gate every seeded acceptance themselves, so a stale
    // prior degrades to the unseeded answer rather than a wrong one.
    const cv::Size full_size(frame.cols, frame.rows);
    const cube::vision::PoseSeed* seed = pose_prior ? &*pose_prior : nullptr;
    if (result.corner) {
        const auto pose = cube::vision::project_cube_from_corner(*result.corner, full_size, seed);
        if (pose.has_value()) {
            result.pose_source = pose->from_seed ? AnalysisResult::PoseSource::corner_seeded
                                                 : AnalysisResult::PoseSource::corner;
            result.pose = *pose;
        }
    } else if (result.face) {
        const auto pose = cube::vision::project_cube_from_face(*result.face, full_size, seed);
        if (pose.has_value()) {
            result.pose_source = pose->from_seed ? AnalysisResult::PoseSource::face_seeded
                                                 : AnalysisResult::PoseSource::face;
            result.pose = *pose;
        }
    } else if (pose_only_face) {
        // The translation-ambiguous fit's orientation is as good as any
        // face pose (a pitch-shifted quad of the same planar grid solves
        // to the same rotation), so it keeps the tracker fed on frames the
        // capture pipeline rightly refuses. The observation itself is NOT
        // exported: handleAnalysisFinished sees a face-sourced pose with
        // result.face empty and therefore attaches no colour evidence and
        // aggregates nothing - the tracker's documented colour-free pass.
        const auto pose = cube::vision::project_cube_from_face(*pose_only_face, full_size, seed);
        if (pose.has_value()) {
            result.pose_source = pose->from_seed ? AnalysisResult::PoseSource::face_seeded
                                                 : AnalysisResult::PoseSource::face;
            result.pose = *pose;
        }
    }

    // Two-face edge-view fallback, ONLY when every richer estimator came
    // up empty - which bounds its cost to exactly the failure passes the
    // live1 census showed dominating (oblique two-face grip). Hinted from
    // the same GUI-sampled prior as the corner detector when the predicted
    // pose is a two-face presentation; unhinted it probes a small fixed
    // canonical basis set. Pose-only end to end: the observation never
    // reaches result.face/result.corner, so display, aggregation and
    // capture cannot see it - the tracker gets q_obs with no colour, and
    // the wireframe gets vertices, exactly like a pose-only face pass.
    if (!result.pose) {
        std::optional<cube::vision::EdgeHint> edge_hint;
        if (pose_prior) {
            edge_hint = cube::vision::compute_edge_hint(*pose_prior);
        }
        auto edge = cube::vision::observe_edge(analysis_frame, edge_hint ? &*edge_hint : nullptr);
        if (edge.has_value()) {
            scale_quad(edge->left_quad, scale_back);
            scale_quad(edge->right_quad, scale_back);
            const auto pose = cube::vision::project_cube_from_edge(*edge, full_size, seed);
            if (pose.has_value()) {
                debug_stats.edge_status = "ok conf=" + std::to_string(edge->geometry_confidence);
                result.pose_source = pose->from_seed ? AnalysisResult::PoseSource::edge_seeded
                                                     : AnalysisResult::PoseSource::edge;
                result.pose = *pose;
            } else {
                debug_stats.edge_status = "pose-failed";
            }
        } else {
            debug_stats.edge_status = "none";
        }
    }

    debug_stats.bright_candidates =
        cube::vision::detail::find_square_candidates(analysis_frame).size();
    dump_vision_debug(analysis_frame, debug_stats);

    return result;
}

bool ScanSession::cornerScanArmed() const {
    // A corner view routes its three faces independently through the
    // identity-keyed store, so it's usable at ANY point in the scan. Armed
    // while any slot is unfilled OR the tracker is locked: a full store
    // can still be waiting on assembly (the 5<->6 retry loop reopens it),
    // and while locked the corner path is the tracker's best measurement -
    // three pinned faces constrain all axes where a face view leaves the
    // planar ambiguity - so pose continuity keeps its cadence rather than
    // going face-only the moment the sixth slot lands.
    //
    // Budget note, and the plan's "decimate corner passes if M7 profiling
    // demands" decision: NOT decimated. Profiled via the hidden
    // [.corner-profile] harness (corner_observation_test.cpp; desktop
    // MSVC fastdebug, 480x480 synthetic analysis frames):
    // compute_corner_hint ~0.7 us, and hinted observe_corner is no slower
    // than the historical detector (~11.5 ms hit / ~11.3 ms miss vs
    // ~15.1 / ~11.4 unhinted; a stale hint's failed fits stay inside the
    // kMaxCandidatesConsidered bound). The extra locked-with-6-slots
    // arming window therefore spends the same tens-of-ms the pre-M7
    // corner pass already spent whenever armed - no new per-pass cost to
    // shed. Phone-device numbers need hardware the repo cannot drive;
    // re-run the harness there when the recorded-corpus work happens, and
    // revisit only if that measurement disagrees.
    return router_.filled_count() < static_cast<int>(cube::model::face_count) ||
           tracker_.state() == cube::vision::TrackState::locked;
}

bool ScanSession::anyCaptureReady() const {
    if (!capture_gate_ok_) {
        return false;
    }
    for (std::size_t f = 0; f < aggregators_.size(); ++f) {
        // Same per-face identity conjunction the commit itself applies: a
        // window converged under a face whose identity is not (or no
        // longer) commit-grade is not "ready", it is paused.
        if (capture_identity_ok_[f] && aggregators_[f].converged()) {
            return true;
        }
    }
    return false;
}

namespace {

// GUI-thread twin of dump_vision_debug (which runs on the analysis
// worker): appends slot-store decisions to the same log so a stuck scan
// is diagnosable - the per-frame log can't show WHERE captures went or
// WHY one was refused, and that routing is exactly what identity-keyed
// scanning stands on.
void log_slot_decision(const std::string& decision) {
    static const QString dir = qEnvironmentVariable("CUBE_VISION_DEBUG_DIR");
    if (dir.isEmpty()) {
        return;
    }
    std::ofstream log(QDir(dir).absolutePath().toStdString() + "/vision_debug.log", std::ios::app);
    log << "SLOT " << decision << '\n';
}

[[nodiscard]] const char* commit_outcome_name(cube::vision::CommitOutcome outcome) {
    switch (outcome) {
        case cube::vision::CommitOutcome::filled:
            return "filled";
        case cube::vision::CommitOutcome::updated:
            return "updated";
        case cube::vision::CommitOutcome::quarantined:
            return "quarantined";
        case cube::vision::CommitOutcome::deferred:
            return "deferred";
        case cube::vision::CommitOutcome::holdoff:
            return "holdoff";
    }
    return "?";
}

// Registration detail for the slot log. A bare "deferred" says only that
// the gauge did not resolve, never why - and the two reasons demand
// opposite responses (keep showing faces, versus the roll is genuinely
// undecidable and the scan needs a different face shown). Emitting the
// candidate/tie counts and both score pairs is what makes a stuck
// re-orientation diagnosable from a recorded clip alone.
[[nodiscard]] std::string registration_detail(const cube::vision::EpochRegistration& registration) {
    const auto status = [&]() -> const char* {
        switch (registration.status) {
            case cube::vision::EpochRegistration::Status::resolved:
                return "resolved";
            case cube::vision::EpochRegistration::Status::ambiguous:
                return "ambiguous";
            case cube::vision::EpochRegistration::Status::insufficient:
                return "insufficient";
        }
        return "?";
    }();
    const auto basis = [&]() -> const char* {
        switch (registration.basis) {
            case cube::vision::EpochRegistration::Basis::unique:
                return "unique";
            case cube::vision::EpochRegistration::Basis::oriented:
                return "oriented";
            case cube::vision::EpochRegistration::Basis::content_equivalent:
                return "content-equiv";
        }
        return "?";
    }();
    std::ostringstream detail;
    detail << " reg=" << status << " cand=" << registration.candidates
           << " tied=" << registration.tied << std::fixed << std::setprecision(2)
           << " score=" << registration.best_score << '/' << registration.runner_up_score
           << " roll=" << registration.best_oriented_lab << '/'
           << registration.runner_up_oriented_lab;
    if (registration.status == cube::vision::EpochRegistration::Status::resolved) {
        detail << " by=" << basis << " gauge=" << registration.lattice_index;
    }
    return detail.str();
}

}  // namespace

void ScanSession::tryCommitConverged() {
    // Gate 1 (locked + identity-certain + accept streak) was evaluated on
    // the last analysis pass; without it no window may commit, however
    // converged - identity is the routing key, and an uncertain key is a
    // guess.
    if (!capture_gate_ok_) {
        return;
    }
    bool any_slot_change = false;
    for (std::size_t f = 0; f < aggregators_.size(); ++f) {
        auto& aggregator = aggregators_[f];
        auto& meta = metas_[f];
        if (!aggregator.converged()) {
            continue;
        }
        const auto body_face = static_cast<cube::model::Face>(f);
        // Gate 1's per-face half at commit time: the window may have
        // converged under an earlier certain pass, but the WRITE happens
        // now, under this pass's belief - an uncertain key must pause it.
        if (!capture_identity_ok_[f]) {
            continue;
        }
        // Commit-time gates 2-5. Failing any of them leaves the window
        // accumulating - the user is mid-motion or the view is marginal,
        // and the capture will be better in a few hundred milliseconds.
        const float frontal = cube::vision::face_frontal_deg(capture_q_, body_face);
        if (frontal > (meta.corner_fed ? kMaxCornerFlankDeg : kMaxFaceCaptureDeg)) {
            continue;
        }
        if (pass_rotation_deg_ > kMaxCaptureRotDeg ||
            capture_motion_score_ >= kMaxCaptureMotionScore) {
            continue;
        }
        // Gate 4 (fully-in-frame quads) needs no check here: clipped
        // observations were refused at aggregation time, so a converged
        // window is fully-visible by construction (see add_keyed).
        const auto captured = aggregator.capture();
        // Unknown-cell rule (the stationary-finger defeat): a cell whose
        // fused colour classifies as no sticker colour at all is something
        // that isn't a sticker - a fingertip, deep shadow, specular burn.
        // Its cross-frame stability is exactly why convergence can't catch
        // it, so it blocks the commit here instead.
        // Bounded, not absolute. This gate could never fire until
        // classify_lab gained its lightness floor ("H1"), because the
        // classifier had no unknown to return; the moment it could, an
        // all-or-nothing rule discarded a whole face - eight sound
        // stickers - for one contaminated cell, and runD's coverage fell
        // from six faces to four. A face is not made unusable by one bad
        // cell: the resolver assigns all 54 stickers under a global
        // nine-per-colour constraint, so a single doubtful cell is the
        // case it is best placed to settle by elimination, whereas a
        // missing face gives it nothing to work with at all.
        //
        // The bound is the same shape and size as FaceAggregator's
        // kMaxUnstableCells, and for the same reason: two cells is what a
        // glare highlight or a fingertip covers, and beyond that the
        // reading is not a face with a blemish, it is a view of something
        // else - which is the stationary-finger case this gate exists to
        // defeat, and which it still defeats.
        const auto unknown_cells =
            static_cast<std::size_t>(std::ranges::count_if(captured.lab, [](const cv::Vec3f& lab) {
                return cube::vision::classify_lab(lab).color == cube::model::FacetColor::unknown;
            }));
        if (unknown_cells > cube::vision::FaceAggregator::kMaxUnstableCells) {
            continue;
        }

        const auto commit = router_.commit(body_face, captured, last_frame_ms_);
        log_slot_decision(
            "commit face=" + std::string(1, "URFDLB"[f]) +
            " outcome=" + commit_outcome_name(commit.outcome) +
            " confirmed=" + std::to_string(commit.confirmed_slots.size()) +
            (commit.registration ? registration_detail(*commit.registration) : std::string{}) +
            (commit.duplicate_centre ? " DUP-CENTRE" : "") +
            (commit.layer_turn ? " LAYER-TURN" : "") + (commit.force_lost ? " FORCE-LOST" : ""));

        if (commit.outcome == cube::vision::CommitOutcome::holdoff) {
            // The refractory is the one refusal that keeps the window: the
            // capture is fine, just early - it commits on a later pass.
            continue;
        }
        // Every other outcome consumed (or invalidated) the window.
        aggregator.reset();
        meta = AggregatorMeta{};

        if (commit.layer_turn) {
            // The router already cleared every slot: the stickers changed
            // under the scan. All windows restart (their samples predate
            // the turn) and the tracker keeps running - the pose is fine,
            // the premise wasn't.
            resetAggregators();
            emit facesScannedChanged();
            emit readingChanged();
            emit scanInvalidated(QStringLiteral("cube changed mid-scan"));
            return;
        }
        if (commit.force_lost) {
            // Two verification strikes on one face inside a locked
            // segment: the TRACK is the suspect, not the slot (its content
            // was verified when written). An honest loss re-anchors a
            // fresh epoch whose identity must re-prove itself.
            tracker_.force_lost();
            capture_gate_ok_ = false;
            capture_identity_ok_.fill(false);
            resetAggregators();
            emit readingChanged();
            return;
        }
        for (const int slot_index : commit.confirmed_slots) {
            emit faceConfirmed(slot_index);
        }
        any_slot_change = any_slot_change || !commit.confirmed_slots.empty();
    }

    if (any_slot_change) {
        emit facesScannedChanged();
        emit readingChanged();
        finalizeIfComplete();
    }
}

QString ScanSession::dropMostSuspectSlot(
    const std::array<float, cube::model::face_count>& mean_confidence) {
    const int filled = router_.filled_count();
    if (filled == 0) {
        return {};
    }
    int worst = 0;
    for (int i = 1; i < filled; ++i) {
        if (mean_confidence[static_cast<std::size_t>(i)] <
            mean_confidence[static_cast<std::size_t>(worst)]) {
            worst = i;
        }
    }
    // Cleared BY KEY through the router so both the routing log AND the
    // scanNeedsRetry banner can name the dropped capture - the whole point
    // of clearing by key is that "re-show a face" becomes "re-show the red
    // one". The user-facing name is the centre-colour GUESS (body-face
    // letters mean nothing to a user, and resolution just failed, so the
    // provisional classification is the best name available); it must be
    // read BEFORE clear_slot destroys the content.
    const auto face = router_.face_at(worst);
    const cube::vision::CapturedFace* content = router_.slot_at(worst);
    QString colour;
    if (content != nullptr) {
        colour = facet_color_display_name(
            cube::vision::classify_lab(content->lab[cube::model::center_facelet_index]).color);
    }
    log_slot_decision("drop-suspect slot=" + std::to_string(worst) + " face=" +
                      std::string(1, face ? "URFDLB"[static_cast<std::size_t>(*face)] : '?') +
                      " centre=" + (colour.isEmpty() ? "?" : colour.toStdString()));
    router_.clear_slot(worst);
    return colour.isEmpty() ? QStringLiteral("the least confident face")
                            : QStringLiteral("the %1-centred face").arg(colour);
}

void ScanSession::finalizeIfComplete() {
    if (router_.filled_count() < static_cast<int>(cube::model::face_count)) {
        return;
    }

    std::array<cube::vision::CapturedFace, cube::model::face_count> captured{};
    std::array<cube::model::Face, cube::model::face_count> body_faces{};
    bool body_faces_known = true;
    for (std::size_t i = 0; i < captured.size(); ++i) {
        captured[i] = *router_.slot_at(static_cast<int>(i));
        // Store convention (solver layout of the slot's BODY face) ->
        // assembly's outside-view contract; see slot_to_outside_view.
        if (const auto body = router_.face_at(static_cast<int>(i))) {
            cube::vision::slot_to_outside_view(*body, captured[i]);
            body_faces[i] = *body;
        } else {
            body_faces_known = false;  // no label, no hint - never guess
        }
    }

    // Per-slot suspicion score for the recovery path: provisional palette
    // confidence until global resolution succeeds, then the (better)
    // resolved confidence - whichever is the latest information available
    // at the point of failure.
    std::array<float, cube::model::face_count> confidence{};
    for (std::size_t i = 0; i < captured.size(); ++i) {
        confidence[i] = mean_confidence(provisional_facelets(captured[i].lab));
    }

    const auto resolved = cube::vision::resolve_colors(captured);
    if (!resolved.has_value()) {
        const QString dropped = dropMostSuspectSlot(confidence);
        emit facesScannedChanged();
        emit scanNeedsRetry(QString::fromStdString(std::string(to_string(resolved.error()))) +
                            QStringLiteral(" - re-show ") + dropped);
        return;
    }
    for (std::size_t i = 0; i < resolved->size(); ++i) {
        confidence[i] = mean_confidence((*resolved)[i]);
    }

    // P6.5: tracked identity contributes its one extra bit - the per-slot
    // quarter-turn digits, derived from body labels + resolved centres
    // (pure geometry, degrades to no-advice on any inconsistency). Tie-
    // break only: a unique survivor wins regardless of the hint.
    cube::solver::RotationHint hint;
    if (body_faces_known) {
        hint = cube::solver::rotation_hint_for_store_scan(*resolved, body_faces);
    }
    const auto assembled = cube::solver::assemble_scanned_faces_with_mapping(*resolved, &hint);
    if (!assembled.has_value()) {
        const QString dropped = dropMostSuspectSlot(confidence);
        emit facesScannedChanged();
        emit scanNeedsRetry(QString::fromStdString(std::string(to_string(assembled.error()))) +
                            QStringLiteral(" - re-show ") + dropped);
        return;
    }

    assembled_cube_ = assembled->cube;

    // Rewrite the legacy ScanProgress with each resolved face at its
    // ASSEMBLED cube position - display/back-compat only (SolveController
    // consumes assembledCube() directly). The facelets keep the camera's
    // row-major orientation; centres (all the progress UI reads) are
    // rotation-invariant, so the possibly-uncorrected rotation is harmless
    // here.
    progress_ = cube::model::ScanProgress();
    for (const auto solver_face : cube::solver::kAllFaces) {
        const auto input = assembled->input_index_of_face[static_cast<std::size_t>(solver_face)];
        progress_.set_face(model_face(solver_face), (*resolved)[input]);
    }
    // Centers gained identity; progressCenters/progressFacelets listen here.
    emit facesScannedChanged();

    emit scanCompleted();
}

void ScanSession::feedTrackFusion(const AnalysisResult& result) {
    for (const auto& read : result.track_face_samples) {
        // The runner's feed bar, verbatim: a sampled centre (purity > 0)
        // is the "this face was really read" proxy - purity 0 means "not
        // measured" and fails closed.
        if (read.purity[4] <= 0.0F) {
            continue;
        }
        cube::vision::FaceObservation observation;
        observation.lab = read.lab;
        observation.cell_purity = read.purity;
        const auto fed =
            cube::vision::fusion::add_observation(track_fusion_, track_fusion_generation_,
                                                  result.track_gauge_epoch, read.face, observation);
        track_fusion_active_ = true;
        if (fed.generation_split) {
            log_slot_decision(
                "fusion generation-split epoch=" + std::to_string(result.track_gauge_epoch) +
                " sub=" + std::to_string(fed.sub_epoch) + " face=" + std::to_string(read.face));
        }
    }
}

void ScanSession::maybeFinalizeTrackFusion() {
    // A completed scan stays completed: SolveController owns the flow from
    // scanCompleted on, and further passes must not re-fire it (the poll
    // loop keeps running until the user leaves the scan screen).
    if (!track_fusion_active_ || assembled_cube_.has_value()) {
        return;
    }
    // Attempt only when the store's convergence signature moved - see the
    // member comment. Counting converged() across every window each pass
    // is a few hundred bounded-window MAD evaluations, comfortably cheap;
    // registration is not, which is exactly what the signature gates.
    int face_slots = 0;
    int converged_total = 0;
    for (const auto& [sub_epoch, faces] : track_fusion_) {
        for (const auto& [face_id, aggregator] : faces) {
            ++face_slots;
            converged_total += aggregator.converged() ? 1 : 0;
        }
    }
    const std::pair<int, int> signature{face_slots, converged_total};
    if (signature == track_fusion_attempt_sig_) {
        return;
    }
    track_fusion_attempt_sig_ = signature;

    // Registration runs on a COPY: the live store keeps its raw sub-epoch
    // keys (so the feed-time takeover baselines and future generations
    // stay coherent) and every attempt re-registers from the full
    // evidence - the runner's once-at-end pass, applied to "the evidence
    // so far" rather than locking early merges in. The copy is a few
    // hundred KB at the measured worst case (33 sub-epochs), at
    // signature-change cadence.
    cube::vision::fusion::FusionStore registered = track_fusion_;
    const auto registration = cube::vision::fusion::register_epochs(registered);
    const auto best = cube::vision::fusion::best_sub_epoch(registered);
    if (best.distinct != track_fusion_faces_) {
        track_fusion_faces_ = best.distinct;
        emit facesScannedChanged();
    }
    log_slot_decision("fusion attempt sub=" + std::to_string(best.sub_epoch) +
                      " observed=" + std::to_string(best.observed) +
                      "/6 converged=" + std::to_string(best.converged) +
                      "/6 distinct=" + std::to_string(best.distinct) +
                      "/6 merges=" + std::to_string(registration.merges) +
                      " refusals=" + std::to_string(registration.refusals));
    // P6.15: DISTINCT is the completion bar. Six converged slots holding
    // five physical faces are five faces - resolve_colors refuses them
    // (correctly, "no valid cube"), and the session would otherwise sit at
    // a full ring emitting a colour-shaped retry banner for a scan that is
    // simply missing a face.
    if (best.sub_epoch < 0 || best.distinct < static_cast<int>(cube::model::face_count)) {
        return;
    }

    // Finalize exactly as the runner does: the best sub-epoch's converged
    // windows become the six captures, body ids feed the tracked-scan
    // rotation hint (P6.4 - tie-break advice only), and
    // assemble_scanned_faces_with_mapping stays the acceptance oracle.
    std::array<cube::vision::CapturedFace, cube::model::face_count> captured{};
    std::array<int, cube::model::face_count> body_face_ids{};
    std::size_t slot = 0;
    for (auto& [face_id, aggregator] : registered[best.sub_epoch]) {
        if (aggregator.converged() && slot < captured.size()) {
            body_face_ids[slot] = face_id;
            captured[slot++] = aggregator.capture();
        }
    }
    const auto resolved = cube::vision::resolve_colors(captured);
    if (!resolved.has_value()) {
        // Fail closed, keep scanning: fusion has no slot to drop (evidence
        // is windows, not commitments), so the recovery is more evidence -
        // the next converged read changes the signature and retries. The
        // banner keeps the user showing faces instead of waiting on a
        // progress ring that reads 6/6.
        log_slot_decision("fusion resolve-refused: " + std::string(to_string(resolved.error())));
        emit scanNeedsRetry(QString::fromStdString(std::string(to_string(resolved.error()))) +
                            QStringLiteral(" - keep showing faces"));
        return;
    }
    const cube::solver::RotationHint hint =
        cube::solver::rotation_hint_for_tracked_scan(*resolved, body_face_ids);
    const auto assembled = cube::solver::assemble_scanned_faces_with_mapping(*resolved, &hint);
    if (!assembled.has_value()) {
        log_slot_decision("fusion assembly-refused: " + std::string(to_string(assembled.error())));
        emit scanNeedsRetry(QString::fromStdString(std::string(to_string(assembled.error()))) +
                            QStringLiteral(" - keep showing faces"));
        return;
    }

    // The finalizeIfComplete() completion tail, verbatim contract: expose
    // the validated cube, rewrite the legacy ScanProgress at ASSEMBLED
    // positions (display/back-compat only - SolveController consumes
    // assembledCube() directly), and hand off.
    assembled_cube_ = assembled->cube;
    progress_ = cube::model::ScanProgress();
    for (const auto solver_face : cube::solver::kAllFaces) {
        const auto input = assembled->input_index_of_face[static_cast<std::size_t>(solver_face)];
        progress_.set_face(model_face(solver_face), (*resolved)[input]);
    }
    log_slot_decision("fusion assembled sub=" + std::to_string(best.sub_epoch));
    emit facesScannedChanged();
    emit scanCompleted();
}

void ScanSession::resetAggregators() {
    for (auto& aggregator : aggregators_) {
        aggregator.reset();
    }
    metas_ = {};
}

void ScanSession::resetReading() {
    face_observation_.reset();
    corner_observation_.reset();
    cube_pose_.reset();
    // Drop the smoothed quaternion too, or the next scan's first pose
    // would visibly ease in from wherever the previous cube last pointed.
    smoothed_orientation_ = {1.0F, 0.0F, 0.0F, 0.0F};
    has_smoothed_orientation_ = false;
    // The translation and HUD facts go with it - same fresh-scan rule.
    smoothed_position_ = {};
    has_position_ = false;
    hud_health_ = -1.0;
    hud_reinits_ = 0;
    hud_pass_ms_ = 0.0;
    // Zeroed with it: a restarted scan must not hold the previous cube's
    // pose through its first passes (see kOrientationHoldMs).
    last_orientation_ms_ = 0;
    // Full tracker re-init (epoch counter included), for the same reason:
    // a scan restarted inside the coast window must not inherit the
    // previous scan's locked track - the user may well have put down a
    // different cube. Routing consumes tracker identity now, so this is
    // load-bearing, and the router must be told EXPLICITLY: reset restarts
    // the epoch counter at zero, so the next anchor can hand back a number
    // the router has already seen, whereupon on_epoch() early-returns and
    // keeps a gauge describing the dead track's body frame. (retakeFace()
    // reaches here without clearing the store, so that stale gauge then
    // routes every later commit under a dead permutation - capture stalls
    // into duplicate-centre quarantines and repeated force-lost.)
    tracker_.reset();
    router_.on_tracker_reset();
    // The idle gate's streaks describe the previous scan's scene; a new
    // scan must start with the detectors running.
    idle_gate_.reset();
    capture_gate_ok_ = false;
    capture_identity_ok_.fill(false);
    consec_accepts_ = 0;
    has_last_track_q_ = false;
    pass_rotation_deg_ = 0.0F;
    facelets_.fill(cube::model::Facelet{});
    overall_confidence_ = 0.0F;
    is_steady_ = false;
    hand_present_ = false;
}

}  // namespace cube::app
