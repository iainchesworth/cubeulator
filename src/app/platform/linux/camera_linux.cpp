#include "interfaces/camera.hpp"
#include "platform/detail/opencv_capture_loop.hpp"

namespace cube::platform::camera {

namespace {

detail::OpenCvCaptureLoop& loop() {
    static detail::OpenCvCaptureLoop instance(cv::CAP_V4L2);
    return instance;
}

}  // namespace

Result<std::vector<DeviceInfo>> enumerate_devices() {
    return loop().enumerate_devices();
}

Result<void> start(const DeviceInfo& device) {
    return loop().start(device);
}

void stop() {
    loop().stop();
}

std::optional<cv::Mat> try_pop_latest_frame() {
    return loop().try_pop_latest_frame();
}

bool is_capturing() noexcept {
    return loop().is_capturing();
}

}  // namespace cube::platform::camera
