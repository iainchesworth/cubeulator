#include "CameraDeviceModel.hpp"

#include "interfaces/camera.hpp"

#include <QString>
#include <QVariantMap>

CameraDeviceModel::CameraDeviceModel(QObject* parent) : QObject(parent) {
    refresh();
}

void CameraDeviceModel::refresh() {
    devices_.clear();

    auto result = cube::platform::camera::enumerate_devices();
    if (result.has_value()) {
        for (const auto& device : *result) {
            QVariantMap entry;
            entry["id"] = QString::fromStdString(device.identifier);
            entry["name"] = QString::fromStdString(device.display_name);
            devices_.append(entry);
        }
    }

    emit devicesChanged();
}
