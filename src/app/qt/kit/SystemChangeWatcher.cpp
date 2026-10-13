#include "SystemChangeWatcher.hpp"

#include <QCoreApplication>
#include <QGuiApplication>
#include <QPointer>

#if defined(Q_OS_WIN)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

SystemChangeWatcher::SystemChangeWatcher(QObject* parent) : QObject(parent) {
    if (qobject_cast<QGuiApplication*>(QCoreApplication::instance()) != nullptr) {
        connect(qGuiApp, &QGuiApplication::applicationStateChanged, this, [this](Qt::ApplicationState state) {
            if (state == Qt::ApplicationActive) {
                emit changed();
            }
        });
    }
#if defined(Q_OS_WIN)
    QCoreApplication::instance()->installNativeEventFilter(this);
#endif
}

bool SystemChangeWatcher::nativeEventFilter(const QByteArray& eventType, void* message, qintptr* /*result*/) {
#if defined(Q_OS_WIN)
    // Sees every message Qt's windows receive. WM_SETTINGCHANGE is broadcast
    // for any system parameter, so this only prompts a re-read; the helpers
    // compare values and emit nothing when they are unchanged.
    if (eventType == "windows_generic_MSG" || eventType == "windows_dispatcher_MSG") {
        const auto* msg = static_cast<const MSG*>(message);
        if (msg->message == WM_SETTINGCHANGE) {
            // Queued: this runs inside the window procedure, and a re-read
            // calls into the registry and then QML bindings.
            QMetaObject::invokeMethod(this, &SystemChangeWatcher::changed, Qt::QueuedConnection);
        }
    }
#else
    Q_UNUSED(eventType)
    Q_UNUSED(message)
#endif
    return false;
}

SystemChangeWatcher* SystemChangeWatcher::instance() {
    QCoreApplication* app = QCoreApplication::instance();
    if (app == nullptr) {
        return nullptr;
    }
    static QPointer<SystemChangeWatcher> shared;
    if (shared.isNull()) {
        shared = new SystemChangeWatcher(app);
    }
    return shared.data();
}
