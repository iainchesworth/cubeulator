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

// Real implementation (UIApplication.openSettingsURLString) needs an
// Objective-C++ (.mm) translation unit and cube_platform isn't set up to
// compile Objective-C++ yet - CUBEULATOR_BUILD_APP is already OFF for the
// ios-clang preset (see CMakeLists.txt: "Qt-for-iOS packaging isn't wired
// up yet"), and there's no Mac available in this environment to build or
// verify Objective-C++ against. Left as a documented no-op rather than
// unverifiable/unbuildable code - see open_permission_settings() in
// interfaces/camera.hpp for what this needs to do once that lands.
void open_permission_settings() {}

}  // namespace cube::platform::camera
