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
}

void CameraController::selectDevice(const QString& deviceId) {
    if (selectedDeviceId_ == deviceId) {
        return;
    }
    selectedDeviceId_ = deviceId;
    emit selectedDeviceIdChanged();
}

}  // namespace cube::app
