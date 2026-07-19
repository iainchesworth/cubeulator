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
#include <limits>
#include <optional>
#include <string>
#include <utility>

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

// Re-arm guard bounds (see the class comment): the cube leaving detection
// for this many consecutive analysis passes, or this much wall time,
// re-enables aggregation after a capture.
constexpr int kRearmQuietPolls = 10;
constexpr qint64 kRearmTimeoutMs = 1500;

// Convergence means "consistent across CONSECUTIVE frames". Sporadic
// single-frame detections minutes apart must not accumulate in the ring
// buffer until they masquerade as a stable reading (observed in live
// testing: a face captured itself from stray detections spread over a
// minute of searching) - a gap longer than this between observations
// resets the aggregator before the new one is added.
constexpr qint64 kAggregatorStaleMs = 700;

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

// Slot-store centre matching (see ScanSession::slotCapture): a converged
// capture whose centre Lab reading is within the tight radius of an
// existing slot's centre (and classifies as the same provisional colour)
// is a re-show of that face and updates the slot in place. Centres are
// rotation-invariant (index 4 stays put however the face is held), so this
// survives a face being re-presented turned. The looser radius applies
// only when all six slots are filled and nothing matched tightly: the
// lighting drifted since the original capture, but the reading still has
// to belong to one of the six faces, so the nearest slot takes it - unless
// even that is implausibly far, in which case the capture is dropped.
constexpr float kSameCenterLabRadius = 25.0F;
constexpr float kLooseCenterLabRadius = 35.0F;

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
    std::string face_status;    // "ok conf=..." | "gated-offframe" | "none"
    std::string corner_status;  // likewise, or "unarmed"
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
        << " corner=" << stats.corner_status << " steady=" << stats.is_steady << " hand=" << stats.hand_present
        << '\n';
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
    // completed scan assembles (slots have no face identity before that).
    return static_cast<int>(slots_.size());
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
    return faceCaptureReady() || cornerCaptureReady();
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
    // Empty (QML idles) unless the current/held pose carries a robust
    // orientation. A held corner pose whose orientation_valid is false
    // still clears this even while an older smoothed quaternion exists -
    // "not tracking" must win over "showing a stale rotation".
    if (cube_pose_ && cube_pose_->orientation_valid && has_smoothed_orientation_) {
        for (const float component : smoothed_orientation_) {
            list.append(static_cast<qreal>(component));
        }
    }
    return list;
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
    if (corner_observation_) {
        qreal sum = 0.0;
        for (const auto& aggregator : corner_aggregators_) {
            sum += static_cast<qreal>(aggregator.convergence());
        }
        return sum / static_cast<qreal>(corner_aggregators_.size());
    }
    return static_cast<qreal>(face_aggregator_.convergence());
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
    for (const auto& slot : slots_) {
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
    // delivered at all (its handler would have cleared it); dropping it
    // matches stop()'s reset and can't double-queue - poll() doesn't run
    // again until handleStartFinished() restarts the timer, long after any
    // such pass has drained.
    analysis_inflight_ = false;

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
    // and no new pass can race it because the poll timer is stopped until
    // the next start() completes.
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
    // Corner first: when both flows happen to be ready at once, the corner
    // capture supersedes the single-face one - it includes that face and
    // two more.
    if (cornerCaptureReady()) {
        captureCornerFaces();
        return;
    }
    if (faceCaptureReady()) {
        captureGuidedFace();
    }
}

void ScanSession::retakeFace() {
    resetAggregators();
    // Un-arm the guard immediately: a retake means the user is already
    // re-presenting the same face, so waiting out the quiet period would
    // just add dead time.
    rearm_guard_active_ = false;
    rearm_quiet_polls_ = 0;
    resetReading();
    emit readingChanged();
}

void ScanSession::restartScan() {
    progress_ = cube::model::ScanProgress();
    slots_.clear();
    assembled_cube_.reset();
    resetAggregators();
    resetReading();
    emit facesScannedChanged();
    emit readingChanged();
}

void ScanSession::poll() {
    auto frame = cube::platform::camera::try_pop_latest_frame();
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

    if (analysis_inflight_) {
        // Still analyzing a previous frame (or its result is queued but not
        // yet handled - see analysis_inflight_ in the header); the live
        // image above still updated, just skip queuing another pass. A pass
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
    analysis_inflight_ = true;
    analysis_inflight_timer_.restart();
    analysis_overrun_logged_ = false;
    analysis_watcher_.setFuture(
        QtConcurrent::run([this, captured_frame = frame->frame, t_ms = frame->t_ms, corner_armed]() {
            return analyze(captured_frame, t_ms, corner_armed);
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
        if (cube_pose_->orientation_valid) {
            const std::array<float, 4>& fresh = cube_pose_->orientation;
            if (!has_smoothed_orientation_) {
                smoothed_orientation_ = fresh;
                has_smoothed_orientation_ = true;
            } else {
                // nlerp toward the fresh sample. Quaternions double-cover
                // rotations (q and -q are the SAME orientation), and the
                // estimator is free to hand back either sign frame to
                // frame - blending raw components across that sign flip
                // would drag the interpolant through zero (a wild spin on
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
        }
    } else if (cube_pose_ && overlay_hold_timer_.isValid() && overlay_hold_timer_.elapsed() > kOverlayHoldMs) {
        cube_pose_.reset();
    }
    if (fresh_face || fresh_corner) {
        overlay_hold_timer_.restart();
    }

    is_steady_ = result.is_steady;
    hand_present_ = result.hand_present;

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

    if (rearm_guard_active_) {
        // "Quiet" means genuinely nothing FRESHLY detected (the held
        // display overlay doesn't count); any real detection resets the
        // streak since it may still be the just-captured face.
        rearm_quiet_polls_ = (fresh_face || fresh_corner) ? 0 : rearm_quiet_polls_ + 1;
        if (rearm_quiet_polls_ >= kRearmQuietPolls || rearm_timer_.hasExpired(kRearmTimeoutMs)) {
            rearm_guard_active_ = false;
        }
    }

    // Aggregation consumes result.face/result.corner (fresh only), never
    // the persistence-held observations - re-adding a held frame would
    // stuff duplicate samples into the ring buffer and fake convergence.
    // The slot-count gate (not progress_, which stays empty until assembly)
    // stops aggregation once six distinct faces are in - and reopens it if
    // a failed assembly drops a slot back to five.
    if (!rearm_guard_active_ && slots_.size() < cube::model::face_count) {
        if (fresh_face) {
            if (face_add_gap_timer_.isValid() && face_add_gap_timer_.elapsed() > kAggregatorStaleMs) {
                face_aggregator_.reset();
            }
            face_add_gap_timer_.restart();
            face_aggregator_.add(*result.face);
        }
        if (fresh_corner) {
            if (corner_add_gap_timer_.isValid() && corner_add_gap_timer_.elapsed() > kAggregatorStaleMs) {
                for (auto& aggregator : corner_aggregators_) {
                    aggregator.reset();
                }
            }
            corner_add_gap_timer_.restart();
            const std::array<const cube::vision::CornerFaceView*, 3> views{
                &result.corner->top, &result.corner->lower_left, &result.corner->lower_right};
            for (std::size_t slot = 0; slot < views.size(); ++slot) {
                corner_aggregators_[slot].add(slot_observation(*views[slot], result.corner->geometry_confidence));
            }
        }
    }

    emit readingChanged();

    if (auto_advance_) {
        if (cornerCaptureReady()) {
            captureCornerFaces();
        } else if (faceCaptureReady()) {
            captureGuidedFace();
        }
    }
}

ScanSession::AnalysisResult ScanSession::analyze(const cv::Mat& frame, std::int64_t frame_t_ms, bool corner_armed) {
    AnalysisResult result;

    // Steadiness and hand presence stay on the full frame - both are
    // advisory hint signals now (cross-frame variance in the aggregators
    // gates capture), and their tuned thresholds predate the downscale.
    const auto stability = stability_.update(frame);
    result.is_steady = stability.is_steady;

    if (hand_detector_ && hand_detection_enabled_.load()) {
        const auto hand_result = hand_detector_->hand_present(frame);
        result.hand_present = hand_result.has_value() && *hand_result;
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

    auto face = cube::vision::observe_face(analysis_frame);
    if (face.has_value()) {
        scale_observation(*face, scale_back);
        if (std::ranges::all_of(face->cells, cell_in_frame)) {
            debug_stats.face_status = "ok conf=" + std::to_string(face->geometry_confidence);
            result.face = std::move(*face);
        } else {
            debug_stats.face_status = "gated-offframe";
        }
    } else {
        debug_stats.face_status = "none";
    }

    debug_stats.corner_status = "unarmed";
    if (corner_armed) {
        auto corner = cube::vision::observe_corner(analysis_frame);
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

    // 3D pose for the wireframe overlay, from whichever observation
    // survived its gates - corner first, since three pinned face quads
    // constrain the pose far better than one. Runs on the scaled-BACK
    // observation with the full frame size so the projected vertices land
    // in full-frame pixel coordinates like every other overlay quad (the
    // projection's approximate intrinsics derive from the frame size, so
    // geometry and size must describe the same coordinate space). A
    // degenerate pose estimate just means no wireframe this reading.
    const cv::Size full_size(frame.cols, frame.rows);
    if (result.corner) {
        const auto pose = cube::vision::project_cube_from_corner(*result.corner, full_size);
        if (pose.has_value()) {
            result.pose = *pose;
        }
    } else if (result.face) {
        const auto pose = cube::vision::project_cube_from_face(*result.face, full_size);
        if (pose.has_value()) {
            result.pose = *pose;
        }
    }

    debug_stats.bright_candidates = cube::vision::detail::find_square_candidates(analysis_frame).size();
    dump_vision_debug(analysis_frame, debug_stats);

    return result;
}

bool ScanSession::cornerScanArmed() const {
    // A corner view slots its three faces independently by centre colour,
    // so it's usable at ANY point in the scan - the only reason to stop
    // looking for corners (or faces) is that all six slots are filled.
    return slots_.size() < cube::model::face_count;
}

bool ScanSession::faceCaptureReady() const {
    return face_observation_.has_value() && slots_.size() < cube::model::face_count &&
           face_aggregator_.converged();
}

bool ScanSession::cornerCaptureReady() const {
    if (!corner_observation_.has_value() || !cornerScanArmed()) {
        return false;
    }
    for (const auto& aggregator : corner_aggregators_) {
        if (!aggregator.converged()) {
            return false;
        }
    }
    return true;
}

void ScanSession::captureGuidedFace() {
    slotCapture(face_aggregator_.capture());

    armRearmGuard();
    // The reset aggregators changed readyToConfirm/captureProgress; without
    // this, QML would show the stale pre-capture values until the next
    // analysis pass lands.
    emit readingChanged();
    finalizeIfComplete();
}

void ScanSession::captureCornerFaces() {
    // Each of the three converged readings routes through the slot store
    // independently by its own centre colour - no CornerViewId conventions,
    // no cell remapping. The reading keeps whatever row-major orientation
    // the camera saw, which is some rigid quarter-rotation of the face
    // (optics can't reflect); assemble_scanned_faces() searches those
    // rotations at the end, so no per-view permutation is needed here.
    for (auto& aggregator : corner_aggregators_) {
        slotCapture(aggregator.capture());
    }

    armRearmGuard();
    // Same post-capture re-notify as captureGuidedFace().
    emit readingChanged();
    finalizeIfComplete();
}

namespace {

// Rotation-invariant fingerprint of a captured face: how many of its nine
// stickers provisionally classify as each colour. A genuine re-show of the
// same face keeps (nearly) the same counts however it is rotated; a
// DIFFERENT face whose washed centre happens to alias an existing slot's
// centre almost never shares its sticker population. L1 distance between
// count vectors: 0 = identical population, each single sticker
// reclassification costs 2.
[[nodiscard]] std::array<int, 7> color_census(const cube::vision::CapturedFace& face) {
    std::array<int, 7> census{};
    for (const auto& lab : face.lab) {
        ++census[static_cast<std::size_t>(cube::vision::classify_lab(lab).color)];
    }
    return census;
}

[[nodiscard]] int census_l1_distance(const std::array<int, 7>& a, const std::array<int, 7>& b) {
    int total = 0;
    for (std::size_t i = 0; i < a.size(); ++i) {
        total += std::abs(a[i] - b[i]);
    }
    return total;
}

// Two sticker reclassifications of drift tolerance: enough for lighting
// changes between two showings of the same face, far under the difference
// between two distinct scrambled faces.
constexpr int kMaxCensusDrift = 4;

// GUI-thread twin of dump_vision_debug (which runs on the analysis
// worker): appends slot-store decisions to the same log so a stuck scan
// ("always 5 of 6") is diagnosable - the per-frame log can't show WHERE
// captures went, and that routing is exactly what free-form scanning
// stands on.
void log_slot_decision(const std::string& decision) {
    static const QString dir = qEnvironmentVariable("CUBE_VISION_DEBUG_DIR");
    if (dir.isEmpty()) {
        return;
    }
    std::ofstream log(QDir(dir).absolutePath().toStdString() + "/vision_debug.log", std::ios::app);
    log << "SLOT " << decision << '\n';
}

[[nodiscard]] std::string lab_string(const cv::Vec3f& lab) {
    return "(" + std::to_string(static_cast<int>(lab[0])) + "," + std::to_string(static_cast<int>(lab[1])) + "," +
           std::to_string(static_cast<int>(lab[2])) + ")";
}

}  // namespace

void ScanSession::slotCapture(const cube::vision::CapturedFace& captured) {
    const cv::Vec3f center = captured.lab[cube::model::center_facelet_index];
    const auto center_color = cube::vision::classify_lab(center).color;
    const auto census = color_census(captured);
    const auto distance_to = [&center](const cube::vision::CapturedFace& slot) {
        const cv::Vec3f diff = slot.lab[cube::model::center_facelet_index] - center;
        return std::sqrt(diff.dot(diff));
    };

    // Tight match = same provisional colour class AND close centre Lab AND
    // a matching rotation-invariant sticker census. The class check keeps
    // washed-out neighbours (an overexposed red centre drifts toward
    // orange) from aliasing at the boundary; the census check catches the
    // case the first two can't: a DIFFERENT face whose centre reads within
    // tolerance of a captured slot's - without it that face can never fill
    // its own slot, and the scan wedges forever at five of six (observed
    // live).
    std::optional<std::size_t> match;
    float best_distance = kSameCenterLabRadius;
    for (std::size_t i = 0; i < slots_.size(); ++i) {
        const float distance = distance_to(slots_[i]);
        if (distance < best_distance &&
            cube::vision::classify_lab(slots_[i].lab[cube::model::center_facelet_index]).color == center_color &&
            census_l1_distance(census, color_census(slots_[i])) <= kMaxCensusDrift) {
            match = i;
            best_distance = distance;
        }
    }

    if (match.has_value()) {
        // The user re-showed an already-captured face: refresh that slot in
        // place with the newer (usually better-lit, more deliberate) read.
        log_slot_decision("update slot=" + std::to_string(*match) + " center=" + lab_string(center) +
                          " dist=" + std::to_string(best_distance));
        slots_[*match] = captured;
        emit faceConfirmed(static_cast<int>(*match));
        emit facesScannedChanged();
        return;
    }

    if (slots_.size() < cube::model::face_count) {
        log_slot_decision("new slot=" + std::to_string(slots_.size()) + " center=" + lab_string(center) +
                          " total=" + std::to_string(slots_.size() + 1));
        slots_.push_back(captured);
        emit faceConfirmed(static_cast<int>(slots_.size()) - 1);
        emit facesScannedChanged();
        return;
    }

    // Six distinct slots and no tight match: the lighting drifted since the
    // original capture, but the reading still has to belong to one of the
    // six faces - the nearest slot takes it, unless even that is
    // implausibly far, in which case the capture is dropped (probably a
    // spurious read; the aggregators will converge again if it was real).
    std::size_t nearest = 0;
    float nearest_distance = std::numeric_limits<float>::max();
    for (std::size_t i = 0; i < slots_.size(); ++i) {
        const float distance = distance_to(slots_[i]);
        if (distance < nearest_distance) {
            nearest = i;
            nearest_distance = distance;
        }
    }
    if (nearest_distance < kLooseCenterLabRadius) {
        log_slot_decision("loose-update slot=" + std::to_string(nearest) + " center=" + lab_string(center) +
                          " dist=" + std::to_string(nearest_distance));
        slots_[nearest] = captured;
        emit faceConfirmed(static_cast<int>(nearest));
        emit facesScannedChanged();
    } else {
        log_slot_decision("dropped center=" + lab_string(center) +
                          " nearest_dist=" + std::to_string(nearest_distance));
    }
}

void ScanSession::dropMostSuspectSlot(const std::array<float, cube::model::face_count>& mean_confidence) {
    if (slots_.empty()) {
        return;
    }
    std::size_t worst = 0;
    for (std::size_t i = 1; i < slots_.size(); ++i) {
        if (mean_confidence[i] < mean_confidence[worst]) {
            worst = i;
        }
    }
    slots_.erase(slots_.begin() + static_cast<std::ptrdiff_t>(worst));
}

void ScanSession::finalizeIfComplete() {
    if (slots_.size() < cube::model::face_count) {
        return;
    }

    std::array<cube::vision::CapturedFace, cube::model::face_count> captured{};
    std::copy(slots_.begin(), slots_.end(), captured.begin());

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
        dropMostSuspectSlot(confidence);
        emit facesScannedChanged();
        emit scanNeedsRetry(QString::fromStdString(std::string(to_string(resolved.error()))));
        return;
    }
    for (std::size_t i = 0; i < resolved->size(); ++i) {
        confidence[i] = mean_confidence((*resolved)[i]);
    }

    const auto assembled = cube::solver::assemble_scanned_faces_with_mapping(*resolved);
    if (!assembled.has_value()) {
        dropMostSuspectSlot(confidence);
        emit facesScannedChanged();
        emit scanNeedsRetry(QString::fromStdString(std::string(to_string(assembled.error()))));
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

void ScanSession::armRearmGuard() {
    rearm_guard_active_ = true;
    rearm_quiet_polls_ = 0;
    rearm_timer_.restart();
    resetAggregators();
}

void ScanSession::resetAggregators() {
    face_aggregator_.reset();
    for (auto& aggregator : corner_aggregators_) {
        aggregator.reset();
    }
}

void ScanSession::resetReading() {
    face_observation_.reset();
    corner_observation_.reset();
    cube_pose_.reset();
    // Drop the smoothed quaternion too, or the next scan's first pose
    // would visibly ease in from wherever the previous cube last pointed.
    smoothed_orientation_ = {1.0F, 0.0F, 0.0F, 0.0F};
    has_smoothed_orientation_ = false;
    facelets_.fill(cube::model::Facelet{});
    overall_confidence_ = 0.0F;
    is_steady_ = false;
    hand_present_ = false;
}

}  // namespace cube::app
