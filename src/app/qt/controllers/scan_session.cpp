#include "controllers/scan_session.hpp"

#include "controllers/bundled_models.hpp"
#include "interfaces/camera.hpp"
#include "interfaces/inference_env.hpp"

#include <cube/model/color_scheme.hpp>
#include <cube/model/face.hpp>
#include <cube/solver/scan_assembly.hpp>
#include <cube/vision/color_resolution.hpp>
#include <cube/vision/frame_recording.hpp>
#include <cube/vision/grid_detection.hpp>
#include <cube/vision/pose_estimation.hpp>

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
constexpr int kAnalysisWidth = 640;

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
// Primary gate: commits require a LOCKED track whose identity has this
// many consecutive accepted resolutions behind it - a fresh re-lock must
// re-prove itself before its routing is trusted with a write.
constexpr int kMinCaptureAccepts = 3;
// Per-identity staleness: convergence means "consistent across
// consecutive-ish frames" (the lesson the old global 700 ms reset
// encoded), but identity-keyed windows legitimately pause while the user
// shows OTHER faces - only a gap long enough for lighting to have drifted
// resets a face's window now, because the identity key already guarantees
// the samples belong to the same physical face.
constexpr std::int64_t kIdentityAggStaleMs = 5000;
// Frontal-ness: beyond ~35 degrees a face view foreshortens too much to
// trust colours; a corner view reads its flank faces obliquely by
// construction, so faces whose window took corner samples get the looser
// corner bound instead.
constexpr float kMaxFaceCaptureDeg = 35.0F;
constexpr float kMaxCornerFlankDeg = 70.0F;
// Motion: a commit mid-turn samples blur. The resolved per-pass rotation
// bounds actual cube motion; the stability score (threshold well above
// its 0.02 steady floor) bounds scene motion the pose can't see.
constexpr float kMaxCaptureRotDeg = 8.0F;
constexpr float kMaxCaptureMotionScore = 0.05F;

// AR-overlay persistence (display-only; see handleAnalysisFinished): held
// geometry outlives a detection blip by this long, fading via
// overlayFreshness, so the overlay doesn't strobe at the per-frame
// detection rate. Fresh geometry is blended toward the held geometry by
// (1 - kOverlaySmoothing) to damp frame-to-frame jitter.
constexpr qint64 kOverlayHoldMs = 450;
constexpr float kOverlaySmoothing = 0.6F;

// Pose-quaternion smoothing (see cubeOrientation): each fresh valid
// orientation pulls the held quaternion this fraction of the way toward
// itself (nlerp, then renormalise). 0.35 tracks a deliberately turned cube
// with only a few frames of lag at the poll rate while flattening the
// frame-to-frame wobble of the pose estimate - noticeably heavier damping
// than the 2D overlay's because a 3D cube rendering amplifies small
// angular noise into very visible rocking.
constexpr float kOrientationSmoothing = 0.35F;

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
    std::string edge_status = "unrun";  // edge fallback: "ok conf=..." | "pose-failed" | "none" | "unrun"
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
    log << "pass=" << pass << " slot=" << slot << " size=" << analysis_frame.cols << 'x' << analysis_frame.rows
        << " bright_candidates=" << stats.bright_candidates << " face=" << stats.face_status
        << " corner=" << stats.corner_status << " edge=" << stats.edge_status << " steady=" << stats.is_steady
        << " hand=" << stats.hand_present << '\n';
    slot = (slot + 1) % 6;
}

// Set CUBE_VISION_RECORD_DIR to a writable directory and the analysis
// worker records EVERY pass's exact detector-input frame plus an index.csv
// row of per-pass metadata (format shared with the offline [.track-replay]
// harness via cube/vision/frame_recording.hpp). Unlike dump_vision_debug's
// throttled 6-frame ring - built for eyeballing single frames - this
// captures whole motion sequences: the corpus that free-rotation tracking
// is calibrated and regression-tested against, where inter-frame timing
// and completeness are the whole point. JPEG q=90 keeps a clip's disk cost
// ~30x below PNG while staying visually lossless for the Lab-space
// detectors; the file ring bounds a forgotten recorder's disk use. Same
// locking story as dump_vision_debug: single-in-flight worker, so the
// statics need no synchronization.
void record_vision_frame(const cv::Mat& analysis_frame, std::int64_t frame_t_ms, const cv::Size& full_size) {
    namespace recording = cube::vision::recording;
    static const QString dir = qEnvironmentVariable("CUBE_VISION_RECORD_DIR");
    if (dir.isEmpty()) {
        return;
    }
    static int pass = 0;
    static std::string dir_path;
    if (pass == 0) {
        QDir().mkpath(dir);
        dir_path = QDir(dir).absolutePath().toStdString();
        // Truncate, don't append: each app run is a fresh recording, and a
        // stale index describing last run's (partially overwritten) frames
        // would poison a replay far more subtly than losing the old clip.
        std::ofstream header(dir_path + "/" + std::string(recording::kIndexCsvName), std::ios::trunc);
        header << recording::kIndexCsvHeader << '\n';
    }
    cv::imwrite(dir_path + "/" + recording::frame_filename(pass), analysis_frame,
                {cv::IMWRITE_JPEG_QUALITY, 90});
    std::ofstream index(dir_path + "/" + std::string(recording::kIndexCsvName), std::ios::app);
    index << pass << ',' << frame_t_ms << ',' << analysis_frame.cols << ',' << analysis_frame.rows << ','
          << full_size.width << ',' << full_size.height << '\n';
    ++pass;
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
            dir.absoluteFilePath(QString::fromUtf8(recording::kIndexCsvName.data(),
                                                   static_cast<qsizetype>(recording::kIndexCsvName.size())))
                .toStdString();
        std::ifstream index(index_path);
        if (!index) {
            qWarning("ScanSession: CUBE_VISION_REPLAY_DIR set but no readable %s in it", index_path.c_str());
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
    const char front = track.front_face ? "URFDLB"[static_cast<std::size_t>(*track.front_face)] : '-';
    std::ofstream log(QDir(dir).absolutePath().toStdString() + "/vision_debug.log", std::ios::app);
    log << "TRACK state=" << track_state_name(track.state) << " gate=" << gate_name(track.gate) << " q=("
        << track.q_track[0]
        << ',' << track.q_track[1] << ',' << track.q_track[2] << ',' << track.q_track[3]
        << ") sigma=" << track.sigma_deg << " theta=" << track.theta_deg << " S=" << track.lattice_index
        << " margin=" << track.margin_deg << " front=" << front << " k=" << track.quarter_turn_k
        << " belief=" << track.belief_margin << " certain=" << track.identity_certain << " certainF="
        << [&track] {
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
// White -> D, Yellow -> U, Green -> F, Blue -> B, Orange -> L, Red -> R).
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
        case FacetColor::orange:
            return QStringLiteral("L");
        case FacetColor::red:
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
void smooth_observation(cube::vision::FaceObservation& next, const cube::vision::FaceObservation& previous) {
    smooth_quad(next.face_quad, previous.face_quad);
    for (std::size_t i = 0; i < next.cells.size(); ++i) {
        smooth_quad(next.cells[i].corners, previous.cells[i].corners);
    }
}

// FaceAggregator only consumes the lab samples, but building a full
// FaceObservation keeps one add() path for both flows.
[[nodiscard]] cube::vision::FaceObservation slot_observation(const cube::vision::CornerFaceView& view,
                                                             float geometry_confidence) {
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
    connect(&start_watcher_, &QFutureWatcher<Result<void>>::finished, this, &ScanSession::handleStartFinished);

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

ScanSession::~ScanSession() {
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
    // Filled slot count, NOT progress_ - progress_ only gains faces once a
    // completed scan assembles (slot keys are tracked body faces, which
    // stay anonymous to the UI until assembly names their colours).
    return router_.filled_count();
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
    return overall_confidence_;
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

QString ScanSession::trackingState() const { return QString::fromLatin1(track_state_name(tracker_.state())); }

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
    return std::clamp(1.0 - static_cast<double>(elapsed) / static_cast<double>(kOverlayHoldMs), 0.0, 1.0);
}

QVariantList ScanSession::cornerCellColors() const {
    QVariantList list;
    if (corner_observation_) {
        for (const auto* view :
             {&corner_observation_->top, &corner_observation_->lower_left, &corner_observation_->lower_right}) {
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
        entry.insert(QStringLiteral("face"),
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
    start_watcher_.setFuture(QtConcurrent::run([device]() { return cube::platform::camera::start(device); }));
    emit startingChanged();
}

void ScanSession::stop() {
    if (start_watcher_.isRunning()) {
        stop_requested_while_starting_ = true;
        return;
    }

    poll_timer_.stop();
    cube::platform::camera::stop();

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
    auto frame = replay_active() ? try_pop_replay_frame() : cube::platform::camera::try_pop_latest_frame();
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
        if (analysis_inflight_timer_.isValid() && analysis_inflight_timer_.elapsed() > kAnalysisOverrunWarnMs &&
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
    analysis_watcher_.setFuture(QtConcurrent::run(
        [this, captured_frame = frame->frame, t_ms = frame->t_ms, corner_armed, pose_prior, run_hand_detector,
         skip_detectors]() {
            return analyze(captured_frame, t_ms, corner_armed, pose_prior, run_hand_detector, skip_detectors);
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
    } else if (cube_pose_ && overlay_hold_timer_.isValid() && overlay_hold_timer_.elapsed() > kOverlayHoldMs) {
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
                track_in.centre_lab[0] = lab_entry(result.corner->top.lab[cube::model::center_facelet_index]);
                track_in.centre_lab[1] =
                    lab_entry(result.corner->lower_left.lab[cube::model::center_facelet_index]);
                track_in.centre_lab[2] =
                    lab_entry(result.corner->lower_right.lab[cube::model::center_facelet_index]);
            } else if (face_pose && result.face) {
                track_in.centre_lab_count = 1;
                track_in.centre_lab[0] = lab_entry(result.face->lab[cube::model::center_facelet_index]);
            }
            // An edge-sourced pose deliberately attaches nothing: the
            // observation carries no colour by design.
        }
        track = tracker_.on_pass(track_in);

        // Display smoothing - the same nlerp the raw pose stream used to
        // feed, now fed by the tracker (which coasts through detection
        // gaps, so the 3D cube keeps turning where it used to freeze).
        // Lost/acquiring drop the smoothed state entirely: cubeOrientation
        // must go empty (QML idles), and the next lock must adopt the new
        // orientation outright instead of visibly easing over from a dead
        // track's pose.
        if (track.state == cube::vision::TrackState::locked ||
            track.state == cube::vision::TrackState::coasting) {
            const std::array<float, 4>& fresh = track.q_track;
            if (!has_smoothed_orientation_) {
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
                    smoothed_orientation_[i] += kOrientationSmoothing * (sign * fresh[i] - smoothed_orientation_[i]);
                    norm_squared += smoothed_orientation_[i] * smoothed_orientation_[i];
                }
                const float norm = std::sqrt(norm_squared);
                for (auto& component : smoothed_orientation_) {
                    component /= norm;
                }
            }
        } else {
            has_smoothed_orientation_ = false;
        }
        log_track_pass(track);

        // Idle-gate update from the SAME pass's presence statistic and
        // the tracker's post-pass state (only a lost tracker may idle).
        // Mutated here - GUI thread, once per pass - so poll() can sample
        // the skip verdict at queue time with the worker kept stateless.
        idle_gate_.on_pass(result.cool_hue_pixels, track.state == cube::vision::TrackState::lost);
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
                       consec_accepts_ >= kMinCaptureAccepts && pass_accepted && track.lattice_index >= 0;
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
                                   const cube::vision::FaceObservation& observation, bool from_corner,
                                   bool full_quads) {
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

    emit readingChanged();

    if (auto_advance_) {
        tryCommitConverged();
    }
}

ScanSession::AnalysisResult ScanSession::analyze(const cv::Mat& frame, std::int64_t frame_t_ms, bool corner_armed,
                                                 std::optional<cube::vision::PoseSeed> pose_prior,
                                                 bool run_hand_detector, bool skip_detectors) {
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

    cv::Mat analysis_frame = frame;
    float scale_back = 1.0F;
    if (frame.cols > kAnalysisWidth) {
        const double scale = static_cast<double>(kAnalysisWidth) / frame.cols;
        cv::resize(frame, analysis_frame, cv::Size(), scale, scale, cv::INTER_AREA);
        // Derived from the actual resized width, not the requested scale -
        // cv::resize rounds the output size.
        scale_back = static_cast<float>(frame.cols) / static_cast<float>(analysis_frame.cols);
    }

    // Recorded before the detectors run so the corpus captures the exact
    // input of a pass even if a detector crashes on it - that frame is
    // precisely the one worth replaying.
    record_vision_frame(analysis_frame, frame_t_ms, cv::Size(frame.cols, frame.rows));

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
        VisionDebugStats idle_stats{.is_steady = result.is_steady, .hand_present = result.hand_present};
        idle_stats.face_status = "idle-skipped";
        idle_stats.corner_status = "idle-skipped";
        dump_vision_debug(analysis_frame, idle_stats);
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

    VisionDebugStats debug_stats{.is_steady = result.is_steady, .hand_present = result.hand_present};

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
        auto corner = cube::vision::observe_corner(analysis_frame, corner_hint ? &*corner_hint : nullptr);
        if (corner.has_value()) {
            scale_corner_view(corner->top, scale_back);
            scale_corner_view(corner->lower_left, scale_back);
            scale_corner_view(corner->lower_right, scale_back);
            const bool all_visible = std::ranges::all_of(corner->top.cells, cell_in_frame) &&
                                     std::ranges::all_of(corner->lower_left.cells, cell_in_frame) &&
                                     std::ranges::all_of(corner->lower_right.cells, cell_in_frame);
            if (all_visible) {
                debug_stats.corner_status = "ok conf=" + std::to_string(corner->geometry_confidence);
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
            return corner.x >= 0.0F && corner.y >= 0.0F && corner.x < static_cast<float>(frame.cols) &&
                   corner.y < static_cast<float>(frame.rows);
        });
    };
    if (result.face) {
        result.face_full_quads = std::ranges::all_of(result.face->cells, quad_fully_in_frame);
    }
    if (result.corner) {
        result.corner_full_quads = std::ranges::all_of(result.corner->top.cells, quad_fully_in_frame) &&
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

    debug_stats.bright_candidates = cube::vision::detail::find_square_candidates(analysis_frame).size();
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
    detail << " reg=" << status << " cand=" << registration.candidates << " tied=" << registration.tied
           << std::fixed << std::setprecision(2) << " score=" << registration.best_score << '/'
           << registration.runner_up_score << " roll=" << registration.best_oriented_lab << '/'
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
        if (pass_rotation_deg_ > kMaxCaptureRotDeg || capture_motion_score_ >= kMaxCaptureMotionScore) {
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
        const bool has_unknown_cell = std::ranges::any_of(captured.lab, [](const cv::Vec3f& lab) {
            return cube::vision::classify_lab(lab).color == cube::model::FacetColor::unknown;
        });
        if (has_unknown_cell) {
            continue;
        }

        const auto commit = router_.commit(body_face, captured, last_frame_ms_);
        log_slot_decision("commit face=" + std::string(1, "URFDLB"[f]) +
                          " outcome=" + commit_outcome_name(commit.outcome) +
                          " confirmed=" + std::to_string(commit.confirmed_slots.size()) +
                          (commit.registration ? registration_detail(*commit.registration) : std::string{}) +
                          (commit.duplicate_centre ? " DUP-CENTRE" : "") +
                          (commit.layer_turn ? " LAYER-TURN" : "") +
                          (commit.force_lost ? " FORCE-LOST" : ""));

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

QString ScanSession::dropMostSuspectSlot(const std::array<float, cube::model::face_count>& mean_confidence) {
    const int filled = router_.filled_count();
    if (filled == 0) {
        return {};
    }
    int worst = 0;
    for (int i = 1; i < filled; ++i) {
        if (mean_confidence[static_cast<std::size_t>(i)] < mean_confidence[static_cast<std::size_t>(worst)]) {
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
    for (std::size_t i = 0; i < captured.size(); ++i) {
        captured[i] = *router_.slot_at(static_cast<int>(i));
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

    const auto assembled = cube::solver::assemble_scanned_faces_with_mapping(*resolved);
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
    // Full tracker re-init (epoch counter included), for the same reason:
    // a scan restarted inside the coast window must not inherit the
    // previous scan's locked track - the user may well have put down a
    // different cube. Routing consumes tracker identity now, so this is
    // load-bearing: the epoch reset also invalidates the router's gauge on
    // the next pass, and the commit-gate context must not let a stale
    // "locked" verdict authorise a commit into the new scan.
    tracker_.reset();
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
