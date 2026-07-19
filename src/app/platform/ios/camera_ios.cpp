#include "interfaces/camera.hpp"

namespace cube::platform::camera {

Result<std::vector<DeviceInfo>> enumerate_devices() {
    // Real enumeration (AVFoundation, Objective-C++ bridge) lands with the
    // vision pipeline spec.
    return std::vector<DeviceInfo>{};
}

Result<void> start(const DeviceInfo& /*device*/) {
    return std::unexpected(CubeError::camera_not_found);
}

void stop() {}

std::optional<cv::Mat> try_pop_latest_frame() {
    // Native capture (AVFoundation) lands with the vision pipeline spec.
    return std::nullopt;
}

bool is_capturing() noexcept {
    return false;
}

}  // namespace cube::platform::camera
