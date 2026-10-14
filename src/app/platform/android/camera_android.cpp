#include "interfaces/camera.hpp"

#include <QCoreApplication>
#include <QJniObject>

namespace cube::platform::camera {

Result<std::vector<DeviceInfo>> enumerate_devices() {
    // Real enumeration (Camera2/CameraX via JNI) lands with the vision
    // pipeline spec. TODO: when it does, each DeviceInfo::facing must be
    // queried per device from CameraCharacteristics.LENS_FACING, falling
    // back to Facing::rear - not unknown - when the query fails: on a phone
    // the rear camera is the one pointed at the cube, and unknown's
    // selfie-mirror default would flip its preview (see Facing in
    // interfaces/camera.hpp).
    return std::vector<DeviceInfo>{};
}

Result<void> start(const DeviceInfo& /*device*/) {
    return std::unexpected(CubeError::camera_not_found);
}

void stop() {}

// Launches Settings.ACTION_APPLICATION_DETAILS_SETTINGS for this app's
// package - the standard escape hatch once a permission has been denied
// with "don't ask again" and QCameraPermission's requestPermission() can no
// longer show the system prompt (see CameraPermission.hpp: the actual
// check/request goes through Qt's own cross-platform permissions API, not
// hand-rolled JNI - this is only for the one thing that API doesn't cover).
void open_permission_settings() {
    QJniObject context = QNativeInterface::QAndroidApplication::context();
    if (!context.isValid()) {
        return;
    }

    QJniObject package_name = context.callObjectMethod("getPackageName", "()Ljava/lang/String;");
    QJniObject uri = QJniObject::callStaticObjectMethod(
        "android/net/Uri", "fromParts",
        "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)Landroid/net/Uri;",
        QJniObject::fromString("package").object<jstring>(), package_name.object<jstring>(),
        nullptr);

    QJniObject action = QJniObject::getStaticObjectField<jstring>(
        "android/provider/Settings", "ACTION_APPLICATION_DETAILS_SETTINGS");

    QJniObject intent("android/content/Intent", "(Ljava/lang/String;)V", action.object<jstring>());
    intent.callObjectMethod("setData", "(Landroid/net/Uri;)Landroid/content/Intent;", uri.object());

    context.callMethod<void>("startActivity", "(Landroid/content/Intent;)V", intent.object());
}

std::optional<TimedFrame> try_pop_latest_frame() {
    // Native capture (Camera2/CameraX) lands with the vision pipeline spec.
    // TODO: when it does, TimedFrame::t_ms must be stamped (steady_clock
    // ms) at delivery into the mailbox - inside the ImageReader/analysis
    // callback, NOT here in the poll - per the timestamp contract in
    // interfaces/camera.hpp. Camera2's SENSOR_TIMESTAMP is a different
    // (sometimes boottime) clock domain, so don't pass it through raw.
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
