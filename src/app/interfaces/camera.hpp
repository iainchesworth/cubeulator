#pragma once

#include <cube/error.hpp>

#include <opencv2/core.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace cube::platform::camera {

// Which way the lens points relative to the user. This drives the UI's
// preview-mirror convention: front-facing previews are mirrored selfie-style
// (users expect a mirror when the lens looks at them), and 'unknown' gets
// the same treatment because it is what desktop webcam APIs report and
// those cameras face the user. Rear cameras must NOT be mirrored - their
// preview shows the scene, not the user, and mirroring would flip the
// cube's sticker layout left-to-right. Mobile platforms carry both kinds
// on one device, so their enumerate_devices() implementations need special
// attention to distinguish front vs rear per device (user requirement)
// rather than assuming a single value for the whole platform.
enum class Facing : std::uint8_t { front, rear, unknown };

// A capture device as enumerated by the host platform's camera API.
struct DeviceInfo {
    std::string identifier;
    std::string display_name;
    Facing facing = Facing::unknown;
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

// A captured frame plus the moment it entered the mailbox. t_ms is
// std::chrono::steady_clock milliseconds (monotonic, arbitrary epoch -
// only differences between frames are meaningful). The stamp exists for
// motion prediction downstream: the mailbox is latest-wins, so consumers
// polling slower than capture see irregular inter-frame gaps and must not
// assume a constant dt - the poll cadence says nothing about how old the
// frame actually is.
struct TimedFrame {
    cv::Mat frame;
    std::int64_t t_ms = 0;
};

// The most recently captured frame (BGR) not yet returned by a previous
// call, or nullopt if nothing new has arrived since the last call or no
// capture is running. Desktop platforms back this with a single-slot
// mailbox fed by a capture thread and stamp t_ms at mailbox push (the
// closest observable point to exposure without per-driver timestamp
// APIs); mobile backends, whose native capture callbacks hand frames over
// directly, stamp at delivery into whatever mailbox they maintain - the
// contract is "as close to capture as the platform allows", never "when
// the consumer happened to poll". Mobile stubs currently return nullopt.
[[nodiscard]] std::optional<TimedFrame> try_pop_latest_frame();
[[nodiscard]] bool is_capturing() noexcept;

}  // namespace cube::platform::camera
