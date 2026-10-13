#include "desktop_integration.hpp"

#include <cube/version.hpp>

#include <QtQml/qqml.h>
#include <QGuiApplication>
#include <QIcon>
#include <QLoggingCategory>
#include <QQmlApplicationEngine>
#include <QQuickWindow>

#ifdef _WIN32
#include <objbase.h>
#include <shobjidl.h>
#endif

namespace cube::app {

namespace {

// Off by default. `QT_LOGGING_RULES=cubeulator.desktop.debug=true` shows what
// the identity step did, which is the only way to see the AppUserModelID.
Q_LOGGING_CATEGORY(lcDesktop, "cubeulator.desktop", QtInfoMsg)

void applyIdentity() {
#ifdef _WIN32
    // Before the first window exists. If it fails the taskbar just groups by
    // executable, as it would without this, so the app carries on.
    const HRESULT set = ::SetCurrentProcessExplicitAppUserModelID(kAppUserModelId);
    if (FAILED(set)) {
        qCWarning(lcDesktop) << "Could not set the AppUserModelID, HRESULT" << Qt::hex << set;
    } else {
        PWSTR current = nullptr;
        if (SUCCEEDED(::GetCurrentProcessExplicitAppUserModelID(&current)) && current != nullptr) {
            qCDebug(lcDesktop) << "AppUserModelID is" << QString::fromWCharArray(current);
            ::CoTaskMemFree(current);
        }
    }
#endif

    // The name Wayland compositors and GNOME match a window to its desktop
    // file by. It is the file's name without ".desktop".
    QGuiApplication::setDesktopFileName(QString::fromLatin1(kAppId));

#ifndef Q_OS_MACOS
    // macOS takes the Dock icon from the bundle, drawn on the system's
    // rounded square; setting a window icon would replace it with a bare one.
    QIcon icon;
    for (const int size : {16, 20, 24, 32, 48, 64, 128, 256}) {
        icon.addFile(QStringLiteral(":/cubeulator/icons/icon-%1.png").arg(size), QSize(size, size));
    }
    if (!icon.isNull()) {
        QGuiApplication::setWindowIcon(icon);
    }
#endif
}

}  // namespace

AppInfo::AppInfo(QObject* parent) : QObject(parent) {}

QString AppInfo::name() const {
    return QStringLiteral("Cubeulator");
}

QString AppInfo::version() const {
    return QString::fromUtf8(cube::version_string.data(),
                             static_cast<qsizetype>(cube::version_string.size()));
}

QString AppInfo::build() const {
    return QString::fromUtf8(cube::git_describe.data(),
                             static_cast<qsizetype>(cube::git_describe.size()));
}

void DesktopServices::registerWithQml() {
    qmlRegisterSingletonInstance("Cubeulator", 1, 0, "KeepAwake", &keepAwake_);
    qmlRegisterSingletonInstance("Cubeulator", 1, 0, "AppInfo", &info_);
}

DesktopIntegration::DesktopIntegration(QGuiApplication& /*app*/, QObject* parent) : QObject(parent) {
    applyIdentity();
}

void DesktopIntegration::prepare(QQmlApplicationEngine& engine) {
    engine.setInitialProperties(geometry_.initialProperties());
}

void DesktopIntegration::attach(QQmlApplicationEngine& engine) {
    if (engine.rootObjects().isEmpty()) {
        return;
    }
    geometry_.track(qobject_cast<QQuickWindow*>(engine.rootObjects().constFirst()));
}

}  // namespace cube::app
