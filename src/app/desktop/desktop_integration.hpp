#pragma once

#include "keep_awake.hpp"
#include "window_geometry.hpp"

#include <QObject>
#include <QString>

class QGuiApplication;
class QQmlApplicationEngine;

namespace cube::app {

// The app's name and version for QML, until E3b's About screen. Read-only.
class AppInfo : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString name READ name CONSTANT FINAL)
    Q_PROPERTY(QString version READ version CONSTANT FINAL)
    Q_PROPERTY(QString build READ build CONSTANT FINAL)

public:
    explicit AppInfo(QObject* parent = nullptr);

    [[nodiscard]] QString name() const;
    [[nodiscard]] QString version() const;
    // `git describe` at build time: the release tag, or the commit.
    [[nodiscard]] QString build() const;

    // Asks for the placeholder About (DesktopShell.qml). The Settings
    // screen's About row calls this until E3b puts a real About screen on
    // the navigation stack, at which point both go away.
    Q_INVOKABLE void showAbout() { emit aboutRequested(); }

signals:
    void aboutRequested();
};

// Everything E4a adds to start-up, behind one object so that main.cpp
// carries three lines of it (docs/design/platforms.md):
//
//   * the process's identity: the Windows AppUserModelID that groups the
//     taskbar button with the Start menu shortcut, the desktop file name
//     Wayland and GNOME match a window to its launcher with, and the window
//     icon;
//   * the remembered window geometry;
//   * the KeepAwake and AppInfo singletons for QML.
//
//   cube::app::DesktopIntegration desktop(app);
//   ...
//   desktop.prepare(engine);              // before loadFromModule
//   engine.loadFromModule(...);
//   desktop.attach(engine);               // after
class DesktopIntegration : public QObject {
    Q_OBJECT

public:
    explicit DesktopIntegration(QGuiApplication& app, QObject* parent = nullptr);

    // Registers the singletons and sets the window's initial geometry.
    void prepare(QQmlApplicationEngine& engine);
    // Starts remembering the window's geometry.
    void attach(QQmlApplicationEngine& engine);

    [[nodiscard]] KeepAwake& keepAwake() { return keepAwake_; }

private:
    KeepAwake keepAwake_;
    AppInfo info_;
    WindowGeometry geometry_;
};

// The identifier on every platform (docs/design/platforms.md, "Identity").
inline constexpr const char* kAppId = "com.iainchesworthlabs.cubeulator";
// Windows only: groups the taskbar button with its shortcut.
inline constexpr const wchar_t* kAppUserModelId = L"IainChesworthLabs.Cubeulator";

}  // namespace cube::app
