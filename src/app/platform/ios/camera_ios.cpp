#include "interfaces/camera.hpp"

namespace cube::platform::camera {

Result<std::vector<DeviceInfo>> enumerate_devices() {
    // Real enumeration (AVFoundation, Objective-C++ bridge) lands with the
    // vision pipeline spec. TODO: when it does, each DeviceInfo::facing
    // must be queried per device from AVCaptureDevice.position, falling
    // back to Facing::rear - not unknown - when the position is
    // unspecified: on a phone the rear camera is the one pointed at the
    // cube, and unknown's selfie-mirror default would flip its preview
    // (see Facing in interfaces/camera.hpp).
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

std::optional<TimedFrame> try_pop_latest_frame() {
    // Native capture (AVFoundation) lands with the vision pipeline spec.
    // TODO: when it does, TimedFrame::t_ms must be stamped (steady_clock
    // ms) at delivery into the mailbox - inside the
    // AVCaptureVideoDataOutput sample-buffer callback, NOT here in the
    // poll - per the timestamp contract in interfaces/camera.hpp. The
    // sample buffer's presentation timestamp is host-time based and could
    // be converted instead, but a delivery stamp keeps one clock domain
    // across platforms.
    return std::nullopt;
}

bool is_capturing() noexcept {
    return false;
}

// No native capture backend exists here yet (see try_pop_latest_frame()
// above), so there is nothing to lock. Reported honestly rather than
// stubbed as success: interfaces/camera.hpp's whole contract is that a user
// who cannot get an automatic lock is TOLD, so they fix their lighting by
// hand instead of scanning under drifting automatics.
//
// TODO: when Camera2/CameraX (Android) / AVFoundation (iOS) capture lands,
// this is where the equivalent of the DirectShow lock goes - Camera2's
// CONTROL_AE_MODE_OFF + SENSOR_EXPOSURE_TIME and CONTROL_AWB_MODE_OFF, or
// AVCaptureDevice's setExposureModeCustom/setWhiteBalanceModeLocked. Both
// give a stronger guarantee than DirectShow does, including the deliberate
// white-balance value this Windows hardware refuses to expose.
bool photometry_control_supported() noexcept {
    return false;
}

Result<void> request_photometry_lock(const PhotometryLockRequest& /*request*/) {
    return std::unexpected(CubeError::camera_control_unavailable);
}

Result<void> request_photometry_restore() {
    return std::unexpected(CubeError::camera_control_unavailable);
}

std::optional<PhotometryLockReport> take_photometry_report() {
    return std::nullopt;
}

}  // namespace cube::platform::camera
