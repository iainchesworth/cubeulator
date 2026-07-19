#include "interfaces/camera.hpp"
#include "platform/detail/opencv_capture_loop.hpp"

namespace cube::platform::camera {

namespace {

detail::OpenCvCaptureLoop& loop() {
    static detail::OpenCvCaptureLoop instance(cv::CAP_V4L2);
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

}  // namespace cube::platform::camera
