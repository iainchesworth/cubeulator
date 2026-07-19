#pragma once

#include <QObject>
#include <QVariantList>
#include <qqmlintegration.h>

// QML-facing wrapper around cube::platform::camera::enumerate_devices() (see
// interfaces/camera.hpp) - lets SettingsScreen.qml show real, live camera
// devices without any QML file needing to know that interface exists.
// Deliberately thin: this only lists devices, it doesn't start/stop capture,
// so it has no overlap with the vision pipeline's capture-loop work.
class CameraDeviceModel : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(QVariantList devices READ devices NOTIFY devicesChanged)

public:
    explicit CameraDeviceModel(QObject* parent = nullptr);

    [[nodiscard]] QVariantList devices() const { return devices_; }

    // Re-runs enumeration - lets Settings pick up a webcam that was plugged
    // in after the app started, without a restart.
    Q_INVOKABLE void refresh();

signals:
    void devicesChanged();

private:
    QVariantList devices_;
};
