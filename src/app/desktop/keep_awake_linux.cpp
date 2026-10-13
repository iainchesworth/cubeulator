// Linux: the session's inhibit interface, over D-Bus.
//
// Tried in this order, the first that answers wins:
//   1. org.freedesktop.portal.Inhibit on the desktop portal. It works inside a
//      Flatpak or Snap sandbox and on every desktop that ships
//      xdg-desktop-portal, which is nearly all of them. Flag 8 is "idle".
//   2. org.freedesktop.ScreenSaver.Inhibit, which KDE and the desktops that
//      implement that interface directly answer.
// Neither asks the person for anything. With no session bus (a CI container,
// a bare TTY) both fail and the helper reports that it is not holding: the
// app carries on, and the screen may dim - which is the honest outcome.
//
// The calls are asynchronous so that a slow or absent service can never
// stall the GUI thread.
#include "keep_awake.hpp"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QObject>
#include <QString>
#include <QVariantMap>

namespace cube::app {

namespace {

const QString kAppName = QStringLiteral("Cubeulator");

class LinuxKeepAwake final : public QObject, public KeepAwakeBackend {
public:
    LinuxKeepAwake() = default;

    void acquire() override {
        wanted_ = true;
        if (mode_ != Mode::None || pending_) {
            return;
        }
        if (!QDBusConnection::sessionBus().isConnected()) {
            report(false);
            return;
        }
        tryPortal();
    }

    void release() override {
        wanted_ = false;
        if (pending_) {
            return;  // the reply handler sees wanted_ is false and lets go
        }
        releaseNow();
    }

private:
    enum class Mode { None, Portal, ScreenSaver };

    void tryPortal() {
        pending_ = true;
        auto call = QDBusMessage::createMethodCall(
            QStringLiteral("org.freedesktop.portal.Desktop"),
            QStringLiteral("/org/freedesktop/portal/desktop"),
            QStringLiteral("org.freedesktop.portal.Inhibit"), QStringLiteral("Inhibit"));
        QVariantMap options;
        options.insert(QStringLiteral("reason"), kAppName);
        call << QString() << static_cast<quint32>(8) << options;

        auto* watcher =
            new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(call), this);
        connect(watcher, &QDBusPendingCallWatcher::finished, this,
                [this](QDBusPendingCallWatcher* w) {
                    w->deleteLater();
                    pending_ = false;
                    const QDBusPendingReply<QDBusObjectPath> reply = *w;
                    if (reply.isError()) {
                        if (wanted_) {
                            tryScreenSaver();
                        }
                        return;
                    }
                    portalHandle_ = reply.value().path();
                    mode_ = Mode::Portal;
                    settle();
                });
    }

    void tryScreenSaver() {
        pending_ = true;
        auto call = QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.ScreenSaver"),
                                                   QStringLiteral("/org/freedesktop/ScreenSaver"),
                                                   QStringLiteral("org.freedesktop.ScreenSaver"),
                                                   QStringLiteral("Inhibit"));
        call << kAppName << kAppName;

        auto* watcher =
            new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(call), this);
        connect(watcher, &QDBusPendingCallWatcher::finished, this,
                [this](QDBusPendingCallWatcher* w) {
                    w->deleteLater();
                    pending_ = false;
                    const QDBusPendingReply<quint32> reply = *w;
                    if (reply.isError()) {
                        report(false);
                        return;
                    }
                    cookie_ = reply.value();
                    mode_ = Mode::ScreenSaver;
                    settle();
                });
    }

    // An inhibit has just been granted. If the request was withdrawn while
    // the call was in flight, hand it straight back.
    void settle() {
        if (wanted_) {
            report(true);
        } else {
            releaseNow();
        }
    }

    void releaseNow() {
        switch (mode_) {
            case Mode::Portal: {
                auto close = QDBusMessage::createMethodCall(
                    QStringLiteral("org.freedesktop.portal.Desktop"), portalHandle_,
                    QStringLiteral("org.freedesktop.portal.Request"), QStringLiteral("Close"));
                QDBusConnection::sessionBus().asyncCall(close);
                portalHandle_.clear();
                break;
            }
            case Mode::ScreenSaver: {
                auto uninhibit = QDBusMessage::createMethodCall(
                    QStringLiteral("org.freedesktop.ScreenSaver"),
                    QStringLiteral("/org/freedesktop/ScreenSaver"),
                    QStringLiteral("org.freedesktop.ScreenSaver"), QStringLiteral("UnInhibit"));
                uninhibit << cookie_;
                QDBusConnection::sessionBus().asyncCall(uninhibit);
                cookie_ = 0;
                break;
            }
            case Mode::None:
                break;
        }
        mode_ = Mode::None;
    }

    Mode mode_ = Mode::None;
    QString portalHandle_;
    quint32 cookie_ = 0;
    bool wanted_ = false;
    bool pending_ = false;
};

}  // namespace

std::unique_ptr<KeepAwakeBackend> makePlatformKeepAwakeBackend() {
    return std::make_unique<LinuxKeepAwake>();
}

}  // namespace cube::app
