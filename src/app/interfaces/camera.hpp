#pragma once

#include <cube/error.hpp>

#include <opencv2/core.hpp>

#include <optional>
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

// Opens the OS's app-specific settings page, for when a permission the app
// needs (camera) has been denied and there's no in-app "ask again" API left
// - Android's Settings.ACTION_APPLICATION_DETAILS_SETTINGS, iOS's
// UIApplication.openSettingsURLString. The permission check/request itself
// goes through Qt's own cross-platform QCameraPermission API (see
// CameraPermission.hpp) rather than duplicating that per platform here; this
// is only for the one piece Qt doesn't provide. No-op on desktop platforms,
// which have no per-app camera permission gate to escape.
void open_permission_settings();

// The most recently captured frame (BGR) not yet returned by a previous
// call, or nullopt if nothing new has arrived since the last call or no
// capture is running. Desktop platforms back this with a single-slot
// mailbox fed by a capture thread; mobile stubs always return nullopt.
[[nodiscard]] std::optional<cv::Mat> try_pop_latest_frame();
[[nodiscard]] bool is_capturing() noexcept;

}  // namespace cube::platform::camera
