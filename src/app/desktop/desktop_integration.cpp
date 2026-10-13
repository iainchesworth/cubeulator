#include "desktop_integration.hpp"

#include "interfaces/desktop_shell.hpp"

#include <cube/version.hpp>

#include <QtQml/qqml.h>
#include <QGuiApplication>
#include <QIcon>
#include <QLoggingCategory>
#include <QQmlApplicationEngine>
#include <QQuickWindow>

namespace cube::app {

namespace {

// `QT_LOGGING_RULES=cubeulator.desktop.debug=true` shows more.
Q_LOGGING_CATEGORY(lcDesktop, "cubeulator.desktop", QtInfoMsg)

void applyIdentity() {
    namespace shell = cube::platform::desktop_shell;

    // Before the first window exists. If the platform could not, the window is
    // grouped by its executable, as it would be without this, so the app
    // carries on.
    if (const std::string problem = shell::apply_process_identity(kAppUserModelId);
        !problem.empty()) {
        qCWarning(lcDesktop) << "Could not set the process identity:" << problem.c_str();
    }

    // The name Wayland compositors and GNOME match a window to its desktop
    // file by. It is the file's name without ".desktop".
    QGuiApplication::setDesktopFileName(QString::fromLatin1(kAppId));

    // Where the bundle supplies the icon (macOS: the Dock icon, drawn on the
    // system's rounded square) a window icon would replace it with a bare one.
    if (shell::traits().sets_window_icon) {
        QIcon icon;
        for (const int size : {16, 20, 24, 32, 48, 64, 128, 256}) {
            icon.addFile(QStringLiteral(":/cubeulator/icons/icon-%1.png").arg(size),
                         QSize(size, size));
        }
        if (!icon.isNull()) {
            QGuiApplication::setWindowIcon(icon);
        }
    }
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

bool AppInfo::nativeMenuBar() const {
    return cube::platform::desktop_shell::traits().native_menu_bar;
}

void DesktopServices::registerWithQml() {
    qmlRegisterSingletonInstance("Cubeulator", 1, 0, "KeepAwake", &keepAwake_);
    qmlRegisterSingletonInstance("Cubeulator", 1, 0, "AppInfo", &info_);
}

DesktopIntegration::DesktopIntegration(QGuiApplication& /*app*/, QObject* parent)
    : QObject(parent) {
    applyIdentity();
}

void DesktopIntegration::prepare(QQmlApplicationEngine& engine) {
    if (!cube::platform::desktop_shell::traits().remembers_window_geometry) {
        return;
    }
    engine.setInitialProperties(geometry_.initialProperties());
}

void DesktopIntegration::attach(QQmlApplicationEngine& engine) {
    if (!cube::platform::desktop_shell::traits().remembers_window_geometry ||
        engine.rootObjects().isEmpty()) {
        return;
    }
    geometry_.track(qobject_cast<QQuickWindow*>(engine.rootObjects().constFirst()));
}

}  // namespace cube::app
