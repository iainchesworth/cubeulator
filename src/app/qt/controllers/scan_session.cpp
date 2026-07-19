#include "controllers/scan_session.hpp"

#include "controllers/bundled_models.hpp"
#include "interfaces/camera.hpp"
#include "interfaces/inference_env.hpp"

#include <cube/model/color_scheme.hpp>
#include <cube/model/face.hpp>
#include <cube/vision/auto_advance.hpp>
#include <cube/vision/vision.hpp>

#include <QVariantMap>
#include <QtConcurrent/QtConcurrentRun>

#include <utility>

namespace cube::app {

namespace {

// Throttled independent of the capture thread's own rate (camera-limited,
// typically ~30fps): confidence/coverage numbers can update responsively
// without saturating the Qt event loop with full-frame image-provider
// round-trips every single capture frame.
constexpr int kPollIntervalMs = 66;  // ~15fps

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

bool ScanSession::running() const noexcept {
    return running_;
}

bool ScanSession::starting() const noexcept {
    return start_watcher_.isRunning();
}

int ScanSession::frameSeq() const noexcept {
    return frame_seq_;
}

int ScanSession::facesScanned() const {
    return progress_.faces_confirmed_count();
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
    return cube_detected_;
}

bool ScanSession::readyToConfirm() const noexcept {
    if (!cube_detected_ || progress_.is_complete()) {
        return false;
    }
    return cube::vision::should_auto_advance(cube::vision::FaceConfirmSignal{
        .color_confidence = overall_confidence_, .is_steady = is_steady_, .hand_absent = !hand_present_});
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

cv::Mat ScanSession::latestFrame() const {
    std::scoped_lock lock(frame_mutex_);
    return latest_frame_;
}

void ScanSession::start(const QString& deviceId) {
    if (start_watcher_.isRunning() || running_) {
        return;
    }

    progress_ = cube::model::ScanProgress();
    resetReading();

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
    if (!cube_detected_ || progress_.is_complete()) {
        return;
    }

    const auto face = static_cast<cube::model::Face>(progress_.faces_confirmed_count());
    const cube::vision::FaceReading reading{.facelets = facelets_, .overall_confidence = overall_confidence_};
    cube::vision::confirm_face(face, reading, progress_);

    emit faceConfirmed(static_cast<int>(face));
    emit facesScannedChanged();

    if (progress_.is_complete()) {
        emit scanCompleted();
    }
}

void ScanSession::retakeFace() {
    resetReading();
    emit readingChanged();
}

void ScanSession::poll() {
    auto frame = cube::platform::camera::try_pop_latest_frame();
    if (!frame) {
        return;
    }

    {
        std::scoped_lock lock(frame_mutex_);
        latest_frame_ = *frame;
    }
    ++frame_seq_;
    emit frameUpdated();

    if (analysis_watcher_.isRunning()) {
        // Still analyzing a previous frame; the live image above still
        // updated, just skip queuing another analysis pass.
        return;
    }

    analysis_watcher_.setFuture(
        QtConcurrent::run([this, captured_frame = *frame]() { return analyze(captured_frame); }));
}

void ScanSession::handleAnalysisFinished() {
    const auto result = analysis_watcher_.result();
    cube_detected_ = result.cube_detected;
    facelets_ = result.facelets;
    overall_confidence_ = result.overall_confidence;
    is_steady_ = result.is_steady;
    hand_present_ = result.hand_present;
    emit readingChanged();

    if (auto_advance_ && readyToConfirm()) {
        confirmFace();
    }
}

ScanSession::AnalysisResult ScanSession::analyze(const cv::Mat& frame) {
    AnalysisResult result;

    const auto stability = stability_.update(frame);
    result.is_steady = stability.is_steady;

    if (hand_detector_ && hand_detection_enabled_.load()) {
        const auto hand_result = hand_detector_->hand_present(frame);
        result.hand_present = hand_result.has_value() && *hand_result;
    }

    const auto reading = cube::vision::analyze_frame(frame);
    result.cube_detected = reading.has_value();
    if (reading.has_value()) {
        result.facelets = reading->facelets;
        result.overall_confidence = reading->overall_confidence;
    }

    return result;
}

void ScanSession::resetReading() {
    cube_detected_ = false;
    facelets_.fill(cube::model::Facelet{});
    overall_confidence_ = 0.0F;
    is_steady_ = false;
    hand_present_ = false;
}

}  // namespace cube::app
