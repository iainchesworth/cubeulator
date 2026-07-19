#include "CameraPermission.hpp"

#include "interfaces/camera.hpp"

#include <QCoreApplication>
#include <QPermission>

CameraPermission::CameraPermission(QObject* parent) : QObject(parent) {
    refreshStatus();
}

int CameraPermission::status() const {
    return status_;
}

void CameraPermission::refreshStatus() {
    const auto qt_status = qApp->checkPermission(QCameraPermission{});
    const auto new_status = static_cast<int>(qt_status);
    if (new_status != status_) {
        status_ = new_status;
        emit statusChanged();
    }
}

void CameraPermission::request() {
    qApp->requestPermission(QCameraPermission{}, this, [this](const QPermission& permission) {
        const auto new_status = static_cast<int>(permission.status());
        if (new_status != status_) {
            status_ = new_status;
            emit statusChanged();
        }
    });
}

void CameraPermission::openSettings() {
    cube::platform::camera::open_permission_settings();
}
