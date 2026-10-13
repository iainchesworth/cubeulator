#include "fake_camera.hpp"

#include "drawn_cube.hpp"

#include <chrono>
#include <thread>
#include <utility>

namespace cube::systest {

FakeCamera& FakeCamera::instance() {
    static FakeCamera camera;
    return camera;
}

FakeCamera::FakeCamera() {
    reset();
}

void FakeCamera::reset() {
    const std::scoped_lock lock(mutex_);
    devices_ = {platform::camera::DeviceInfo{.identifier = "fake:0",
                                             .display_name = "Test camera (drawn frames)",
                                             .facing = platform::camera::Facing::unknown}};
    next_start_error_.reset();
    start_delay_ms_ = 150;
    script_.clear();
    current_ = cube::test::room({640, 360});
    capturing_ = false;
    starts_ = 0;
    stops_ = 0;
    frames_delivered_ = 0;
}

void FakeCamera::set_devices(std::vector<platform::camera::DeviceInfo> devices) {
    const std::scoped_lock lock(mutex_);
    devices_ = std::move(devices);
}

void FakeCamera::set_start_delay(int milliseconds) {
    const std::scoped_lock lock(mutex_);
    start_delay_ms_ = milliseconds;
}

void FakeCamera::fail_next_start(CubeError error) {
    const std::scoped_lock lock(mutex_);
    next_start_error_ = error;
}

void FakeCamera::play(const cv::Mat& frame, int repeat) {
    const std::scoped_lock lock(mutex_);
    script_.push_back({frame, repeat});
}

void FakeCamera::clear_script() {
    const std::scoped_lock lock(mutex_);
    script_.clear();
}

bool FakeCamera::script_drained() const {
    const std::scoped_lock lock(mutex_);
    return script_.empty();
}

int FakeCamera::starts() const {
    const std::scoped_lock lock(mutex_);
    return starts_;
}

int FakeCamera::stops() const {
    const std::scoped_lock lock(mutex_);
    return stops_;
}

bool FakeCamera::capturing() const {
    const std::scoped_lock lock(mutex_);
    return capturing_;
}

int FakeCamera::frames_delivered() const {
    const std::scoped_lock lock(mutex_);
    return frames_delivered_;
}

Result<std::vector<platform::camera::DeviceInfo>> FakeCamera::enumerate() const {
    const std::scoped_lock lock(mutex_);
    return devices_;
}

Result<void> FakeCamera::start() {
    int delay = 0;
    {
        const std::scoped_lock lock(mutex_);
        delay = start_delay_ms_;
    }
    // Called from a pool thread (DirectScan::start): sleeping here is the
    // camera opening, and the GUI thread carries on meanwhile.
    std::this_thread::sleep_for(std::chrono::milliseconds(delay));

    const std::scoped_lock lock(mutex_);
    if (next_start_error_.has_value()) {
        const CubeError error = *next_start_error_;
        next_start_error_.reset();
        return std::unexpected(error);
    }
    capturing_ = true;
    ++starts_;
    return {};
}

void FakeCamera::stop() {
    const std::scoped_lock lock(mutex_);
    if (capturing_) {
        ++stops_;
    }
    capturing_ = false;
}

std::optional<platform::camera::TimedFrame> FakeCamera::pop() {
    const std::scoped_lock lock(mutex_);
    if (!capturing_) {
        return std::nullopt;
    }
    if (!script_.empty()) {
        Scripted& next = script_.front();
        current_ = next.frame;
        if (--next.remaining <= 0) {
            script_.pop_front();
        }
    }
    ++frames_delivered_;
    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    return platform::camera::TimedFrame{
        .frame = current_,
        .t_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now).count()};
}

}  // namespace cube::systest

// ---------------------------------------------------------------------------
// cube::platform::camera, as the test executable defines it. Every function
// in interfaces/camera.hpp is here, so none of the platform library's
// camera_<os>.cpp is ever needed by the linker.
// ---------------------------------------------------------------------------
namespace cube::platform::camera {

Result<std::vector<DeviceInfo>> enumerate_devices() {
    return systest::FakeCamera::instance().enumerate();
}

Result<void> start(const DeviceInfo& /*device*/) {
    return systest::FakeCamera::instance().start();
}

void stop() {
    systest::FakeCamera::instance().stop();
}

std::optional<TimedFrame> try_pop_latest_frame() {
    return systest::FakeCamera::instance().pop();
}

bool is_capturing() noexcept {
    return systest::FakeCamera::instance().capturing();
}

void open_permission_settings() {}

bool photometry_control_supported() noexcept {
    return true;
}

// A camera that is already steady: the lock is granted at once and reports
// at once, so the scan screen's "setting the camera's exposure" stage passes
// on the first frame.
Result<void> request_photometry_lock(const PhotometryLockRequest& /*request*/) {
    return {};
}

Result<void> request_photometry_restore() {
    return {};
}

std::optional<PhotometryLockReport> take_photometry_report() {
    return PhotometryLockReport{};
}

}  // namespace cube::platform::camera
