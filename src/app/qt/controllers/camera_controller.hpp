#pragma once

#include "interfaces/camera.hpp"

#include <QFutureWatcher>
#include <QObject>
#include <QString>
#include <QVariantList>

#include <vector>

namespace cube::app {

// Owns cube::platform::camera device enumeration/selection state for the
// QML shell. Deliberately does not itself start/stop a capture session -
// that's ScanSession's job - nor does it track OS camera-permission status,
// which CameraPermission (a thin QCameraPermission wrapper) owns instead;
// this only tracks which devices exist and which one is selected.
//
// enumerate_devices() is a real blocking hardware probe (observed 15-20s on
// this machine, opening/closing several device indices) - it must never run
// synchronously on the calling (GUI) thread, or the whole window freezes for
// that long. It runs via QtConcurrent on Qt's thread pool instead; callers
// react to devicesChanged/errorOccurred rather than reading the result
// immediately after calling refreshDevices().
class CameraController : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList devices READ devices NOTIFY devicesChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString selectedDeviceId READ selectedDeviceId NOTIFY selectedDeviceIdChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY errorOccurred)

public:
    explicit CameraController(QObject* parent = nullptr);

    [[nodiscard]] QVariantList devices() const;
    [[nodiscard]] bool busy() const;
    [[nodiscard]] QString selectedDeviceId() const;
    [[nodiscard]] QString lastError() const;

public slots:
    void refreshDevices();
    void selectDevice(const QString& deviceId);

signals:
    void devicesChanged();
    void busyChanged();
    void selectedDeviceIdChanged();
    void errorOccurred(const QString& message);

private slots:
    void handleEnumerationFinished();

private:
    QVariantList devices_;
    QString selectedDeviceId_;
    QString lastError_;
    QFutureWatcher<Result<std::vector<platform::camera::DeviceInfo>>> watcher_;
};

}  // namespace cube::app
