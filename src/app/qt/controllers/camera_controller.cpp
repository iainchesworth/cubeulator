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

bool CameraController::granted() const noexcept {
    return granted_;
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

void CameraController::refreshDevices() {
    startEnumeration(/*grantOnSuccess=*/false);
}

void CameraController::requestAccess() {
    // There's no separate OS "request permission" call across our target
    // platforms - where the OS has a camera-privacy prompt, it fires
    // implicitly the first time a device is actually opened. Enumerating
    // successfully is the closest cross-platform proxy available today for
    // "camera access isn't blocked".
    startEnumeration(/*grantOnSuccess=*/true);
}

void CameraController::startEnumeration(bool grantOnSuccess) {
    if (watcher_.isRunning()) {
        return;
    }

    grant_on_success_ = grantOnSuccess;
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

    devices_.clear();
    for (const auto& device : *result) {
        QVariantMap entry;
        entry["id"] = QString::fromStdString(device.identifier);
        entry["label"] = QString::fromStdString(device.display_name);
        devices_.append(entry);
    }
    emit devicesChanged();

    if (selectedDeviceId_.isEmpty() && !devices_.isEmpty()) {
        selectDevice(devices_.first().toMap()["id"].toString());
    }

    if (grant_on_success_) {
        granted_ = true;
        emit grantedChanged();
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
