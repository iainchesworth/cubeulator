#pragma once

#include <qqmlintegration.h>
#include <QObject>

// QML-facing camera permission status/request. Deliberately does not
// hand-roll per-platform permission code: Qt's own cross-platform
// QCameraPermission (QCoreApplication::checkPermission/requestPermission,
// since Qt 6.5) already covers checking and requesting on every platform
// with a runtime permission model, and returns Granted immediately on
// platforms without one (desktop). The one thing that API doesn't cover -
// opening the OS's app-settings page once a permission is durably denied -
// goes through cube::platform::camera::open_permission_settings(), which
// *is* real per-platform code (see interfaces/camera.hpp).
class CameraPermission : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    // Mirrors Qt::PermissionStatus (Undetermined=0, Denied=1, Granted=2) so
    // QML can compare against plain ints without importing anything extra.
    Q_PROPERTY(int status READ status NOTIFY statusChanged)

public:
    explicit CameraPermission(QObject* parent = nullptr);

    [[nodiscard]] int status() const;

    // Triggers the OS permission prompt if the status is undetermined (or
    // re-prompts if still allowed to); status updates + statusChanged fires
    // once the user responds. Safe to call when already granted/denied.
    Q_INVOKABLE void request();

    // Escape hatch for "denied and the system won't prompt again".
    Q_INVOKABLE void openSettings();

signals:
    void statusChanged();

private:
    void refreshStatus();

    int status_ = 0;
};
