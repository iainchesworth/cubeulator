#include "controllers/direct_scan.hpp"

#include "interfaces/camera.hpp"

#include <QColor>
#include <QPointF>
#include <QVariantMap>
#include <QtConcurrent/QtConcurrentRun>

#include <string>
#include <utility>

namespace cube::app {

namespace {

// ~30 Hz: every camera frame is looked at, and a read is a few milliseconds.
constexpr int kPollIntervalMs = 33;

// How long to wait for the camera to report its lock before scanning anyway.
// The lock normally reports within about 40 frames of the camera starting;
// a search for the white balance takes up to about 130 more.
constexpr int kLockWaitFrames = 240;

[[nodiscard]] QString colour_string(const cv::Vec3f& lab) {
    const cv::Vec3b bgr = cube::vision::lab_to_bgr(lab);
    return QColor(bgr[2], bgr[1], bgr[0]).name();
}

[[nodiscard]] bool same_sides(const std::vector<cube::vision::SideScan::Side>& a,
                              const std::vector<cube::vision::SideScan::Side>& b) {
    if (a.size() != b.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (a[i].in_view != b[i].in_view || a[i].lab != b[i].lab) {
            return false;
        }
    }
    return true;
}

}  // namespace

DirectScan::DirectScan(QObject* parent) : QObject(parent) {
    poll_timer_.setInterval(kPollIntervalMs);
    connect(&poll_timer_, &QTimer::timeout, this, &DirectScan::poll);
    connect(&start_watcher_, &QFutureWatcher<cube::Result<void>>::finished, this,
            &DirectScan::handleStartFinished);
}

DirectScan::~DirectScan() {
    start_watcher_.waitForFinished();
    if (running_) {
        cube::platform::camera::stop();
    }
}

QVariantList DirectScan::stickerPoints() const {
    QVariantList points;
    if (read_.has_value()) {
        for (const cv::Point2f& centre : read_->centres) {
            points.append(QPointF(centre.x, centre.y));
        }
    }
    return points;
}

QVariantList DirectScan::sides() const {
    QVariantList out;
    for (const auto& side : sides_) {
        QVariantList colors;
        for (const cv::Vec3f& lab : side.lab) {
            colors.append(colour_string(lab));
        }
        out.append(QVariantMap{{"colors", colors}, {"inView", side.in_view}});
    }
    return out;
}

cv::Mat DirectScan::latestFrame() const {
    const std::scoped_lock lock(frame_mutex_);
    return latest_frame_;
}

void DirectScan::start(const QString& deviceId) {
    if (start_watcher_.isRunning()) {
        // Back on the scan screen while the camera is still opening: cancel
        // the stop queued in between, or the camera would open and close.
        stop_requested_while_starting_ = false;
        return;
    }
    if (running_) {
        return;
    }
    restart();
    camera_error_.clear();

    // Opening a camera takes seconds on some drivers; keep the GUI alive.
    const cube::platform::camera::DeviceInfo device{.identifier = deviceId.toStdString(),
                                                    .display_name = ""};
    start_watcher_.setFuture(
        QtConcurrent::run([device]() { return cube::platform::camera::start(device); }));
    emit runningChanged();
}

void DirectScan::handleStartFinished() {
    const auto result = start_watcher_.result();
    const bool stop_requested = std::exchange(stop_requested_while_starting_, false);
    if (!result.has_value()) {
        camera_error_ = QString::fromStdString(std::string(cube::to_string(result.error())));
        emit runningChanged();
        return;
    }
    if (stop_requested) {
        cube::platform::camera::stop();
        emit runningChanged();
        return;
    }

    // Freeze exposure and white balance for the scan: the same sticker has
    // to measure the same on every side, and automatics re-meter for
    // whichever colour fills the frame. A camera that cannot be locked still
    // scans; sides are matched within the session either way.
    awaiting_lock_ = cube::platform::camera::request_photometry_lock(
                         cube::platform::camera::PhotometryLockRequest{})
                         .has_value();
    frames_awaiting_lock_ = 0;

    running_ = true;
    poll_timer_.start();
    emit runningChanged();
}

void DirectScan::stop() {
    if (start_watcher_.isRunning()) {
        stop_requested_while_starting_ = true;
        return;
    }
    if (!running_) {
        return;
    }
    poll_timer_.stop();
    // Also puts the camera's exposure and white balance back as found.
    cube::platform::camera::stop();
    running_ = false;
    awaiting_lock_ = false;
    read_.reset();
    reading_ = false;
    {
        // The next scan must not open on this one's last frame.
        const std::scoped_lock lock(frame_mutex_);
        latest_frame_ = cv::Mat();
    }
    frame_seq_ = 0;
    emit runningChanged();
    emit frameUpdated();
}

void DirectScan::restart() {
    scan_.reset();
    read_.reset();
    reading_ = false;
    sides_.clear();
    conflicted_ = false;
    cube_.reset();
    emit frameUpdated();
    emit sidesChanged();
}

void DirectScan::poll() {
    auto frame = cube::platform::camera::try_pop_latest_frame();
    if (!frame.has_value()) {
        return;
    }
    // The lock reports once, when it has been applied or refused. Either
    // way the camera is as steady as it is going to get.
    if (cube::platform::camera::take_photometry_report().has_value() ||
        ++frames_awaiting_lock_ > kLockWaitFrames) {
        awaiting_lock_ = false;
    }
    processFrame(frame->frame);
}

void DirectScan::processFrame(const cv::Mat& bgr) {
    {
        const std::scoped_lock lock(frame_mutex_);
        latest_frame_ = bgr;
    }
    frame_width_ = bgr.cols;
    frame_height_ = bgr.rows;
    ++frame_seq_;

    if (cube_.has_value()) {
        // Scan finished; keep the preview live until the screen changes.
        emit frameUpdated();
        return;
    }

    read_ = cube::vision::read_side(bgr);
    if (awaiting_lock_) {
        // Show where the cube is, but count nothing yet.
        emit frameUpdated();
        return;
    }
    scan_.add(read_);

    std::vector<cube::vision::SideScan::Side> sides = scan_.sides();
    reading_ = scan_.reading();
    emit frameUpdated();

    if (same_sides(sides, sides_)) {
        return;
    }
    sides_ = std::move(sides);

    auto cube = scan_.cube();
    conflicted_ = !cube.has_value() && cube.error() != cube::CubeError::face_capture_incomplete;
    emit sidesChanged();
    if (cube.has_value()) {
        cube_ = *cube;
        emit scanCompleted();
    }
}

}  // namespace cube::app
