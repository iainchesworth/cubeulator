#pragma once

#include <cube/error.hpp>

#include <string>
#include <vector>

namespace cube::platform::camera {

// A capture device as enumerated by the host platform's camera API.
struct DeviceInfo {
    std::string identifier;
    std::string display_name;
};

// There is exactly one definition of each function below per platform,
// living in platform/<os>/camera_<os>.cpp. CMake compiles only the file
// matching the target OS, so the code contains no preprocessor branching.
[[nodiscard]] Result<std::vector<DeviceInfo>> enumerate_devices();
[[nodiscard]] Result<void> start(const DeviceInfo& device);
void stop();

}  // namespace cube::platform::camera
