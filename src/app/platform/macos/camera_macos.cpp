#include "interfaces/camera.hpp"
#include "platform/detail/opencv_capture_loop.hpp"

namespace cube::platform::camera {

namespace {

detail::OpenCvCaptureLoop& loop() {
    static detail::OpenCvCaptureLoop instance(cv::CAP_AVFOUNDATION);
    return instance;
}

}  // namespace

// OpenCvCaptureLoop leaves DeviceInfo::facing at its unknown default, which
// is correct here: desktop webcams face the user, and unknown already gets
// the selfie-mirror treatment (see Facing in interfaces/camera.hpp).
Result<std::vector<DeviceInfo>> enumerate_devices() {
    return loop().enumerate_devices();
}

Result<void> start(const DeviceInfo& device) {
    return loop().start(device);
}

void stop() {
    loop().stop();
}

std::optional<TimedFrame> try_pop_latest_frame() {
    return loop().try_pop_latest_frame();
}

bool is_capturing() noexcept {
    return loop().is_capturing();
}

void open_permission_settings() {}

// Forwarded to the shared capture loop, which owns the device and the
// capture thread - see interfaces/camera.hpp for what a lock is for and
// what this backend was measured to support.
bool photometry_control_supported() noexcept {
    return loop().photometry_supported();
}

Result<void> request_photometry_lock(const PhotometryLockRequest& request) {
    return loop().request_photometry_lock(request);
}

Result<void> request_photometry_restore() {
    return loop().request_photometry_restore();
}

std::optional<PhotometryLockReport> take_photometry_report() {
    return loop().take_photometry_report();
}

}  // namespace cube::platform::camera
