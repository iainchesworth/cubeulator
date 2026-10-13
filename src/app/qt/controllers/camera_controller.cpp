#include "controllers/camera_controller.hpp"

#include <QVariantMap>
#include <QtConcurrent/QtConcurrentRun>

namespace cube::app {

CameraController::CameraController(QObject* parent) : QObject(parent) {
    connect(&watcher_, &QFutureWatcher<Result<std::vector<platform::camera::DeviceInfo>>>::finished, this,
            &CameraController::handleEnumerationFinished);
}

QVariantList CameraController::devices() const {
    return devices_;
}

bool CameraController::busy() const {
    return watcher_.isRunning();
}

QString CameraController::selectedDeviceId() const {
    return selectedDeviceId_;
}

QString CameraController::lastError() const {
    return lastError_;
}

bool CameraController::previewMirroredFor(platform::camera::Facing facing) noexcept {
    // Selfie-mirror for front and unknown facings; rear previews show the
    // scene rather than the user and must stay un-mirrored (see Facing in
    // interfaces/camera.hpp).
    return facing != platform::camera::Facing::rear;
}

bool CameraController::previewMirrored() const {
    // A selection with no matching entry (nothing selected yet, or a stale id
    // after re-enumeration) is treated as unknown, which keeps the
    // desktop-webcam default of mirroring.
    const std::string id = selectedDeviceId_.toStdString();
    for (const auto& device : deviceInfos_) {
        if (device.identifier == id) {
            return previewMirroredFor(device.facing);
        }
    }
    return previewMirroredFor(platform::camera::Facing::unknown);
}

void CameraController::refreshDevices() {
    if (watcher_.isRunning()) {
        return;
    }

    lastError_.clear();
    watcher_.setFuture(QtConcurrent::run(&platform::camera::enumerate_devices));
    emit busyChanged();
}

void CameraController::handleEnumerationFinished() {
    const auto result = watcher_.result();
    emit busyChanged();

    if (!result.has_value()) {
        lastError_ = QString::fromStdString(std::string(to_string(result.error())));
        emit errorOccurred(lastError_);
        return;
    }

    const bool wasMirrored = previewMirrored();

    deviceInfos_ = *result;
    devices_.clear();
    for (const auto& device : deviceInfos_) {
        QVariantMap entry;
        entry["id"] = QString::fromStdString(device.identifier);
        entry["label"] = QString::fromStdString(device.display_name);
        devices_.append(entry);
    }
    emit devicesChanged();

    if (selectedDeviceId_.isEmpty() && !devices_.isEmpty()) {
        selectDevice(devices_.first().toMap()["id"].toString());
    } else if (previewMirrored() != wasMirrored) {
        // previewMirrored's NOTIFY is selectedDeviceIdChanged (see header):
        // a refresh can change the facing recorded for an unchanged
        // selection, and without this re-emit QML bindings on
        // previewMirrored would keep the stale value.
        emit selectedDeviceIdChanged();
    }
}

void CameraController::selectDevice(const QString& deviceId) {
    if (selectedDeviceId_ == deviceId) {
        return;
    }
    selectedDeviceId_ = deviceId;
    emit selectedDeviceIdChanged();
}

}  // namespace cube::app
