#pragma once

#include <QAbstractNativeEventFilter>
#include <QObject>

// Tells the system-setting helpers (SystemTextScale, SystemMotion) when to
// look again. Qt has no change signal for the person's text size or
// animation preference, so this is a prompt to re-read rather than a report
// of what changed:
//
//   * Windows: a WM_SETTINGCHANGE message reaches the app's windows when the
//     person moves a slider in Settings, which is a prompt as soon as it
//     happens;
//   * everywhere: the app becoming active, which covers every other platform
//     and a person who changed a setting while the app was behind.
//
// One shared instance, parented to the QCoreApplication. It is its own
// native event filter; the filter base class removes itself from the
// application when the watcher goes.
class SystemChangeWatcher final : public QObject, public QAbstractNativeEventFilter {
    Q_OBJECT

public:
    // Null before a QCoreApplication exists.
    [[nodiscard]] static SystemChangeWatcher* instance();

    bool nativeEventFilter(const QByteArray& eventType, void* message, qintptr* result) override;

signals:
    void changed();

private:
    explicit SystemChangeWatcher(QObject* parent);
};
