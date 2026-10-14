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
    // The app's menu is a menu bar at the top of the screen with the standard
    // window commands in it, and full screen has the chord that goes with it
    // (macOS). The platform says so; the QML does not ask which OS it is on.
    Q_PROPERTY(bool nativeMenuBar READ nativeMenuBar CONSTANT FINAL)

public:
    explicit AppInfo(QObject* parent = nullptr);

    [[nodiscard]] QString name() const;
    [[nodiscard]] QString version() const;
    // `git describe` at build time: the release tag, or the commit.
    [[nodiscard]] QString build() const;
    [[nodiscard]] bool nativeMenuBar() const;

    // Asks for the placeholder About (DesktopShell.qml). The Settings
    // screen's About row calls this until E3b puts a real About screen on
    // the navigation stack, at which point both go away.
    Q_INVOKABLE void showAbout() { emit aboutRequested(); }

signals:
    void aboutRequested();
};

// What QML reads from the desktop shell: KeepAwake and AppInfo, registered as
// singletons. This is the part of the desktop integration that the screens
// depend on, so it is owned by AppRuntime and a test that boots Main.qml has it
// exactly as the app does; the system tests fail on any QML warning, and
// DesktopShell.qml reads both.
class DesktopServices {
public:
    DesktopServices() = default;
    DesktopServices(const DesktopServices&) = delete;
    DesktopServices& operator=(const DesktopServices&) = delete;

    // Registers KeepAwake and AppInfo for the one engine of this process.
    void registerWithQml();

    [[nodiscard]] KeepAwake& keepAwake() { return keepAwake_; }
    [[nodiscard]] AppInfo& info() { return info_; }

private:
    KeepAwake keepAwake_;
    AppInfo info_;
};

// What only the real application does with its window, and so what main()
// keeps and a test does not (a test window must not write its geometry into the
// developer's settings or take the process's taskbar identity):
//
//   * the process's identity: the Windows AppUserModelID that groups the
//     taskbar button with the Start menu shortcut, the desktop file name
//     Wayland and GNOME match a window to its launcher with, and the window
//     icon;
//   * the remembered window geometry.
//
//   cube::app::DesktopIntegration desktop(app);   // before the AppRuntime,
//                                                 // so that it outlives it
//   cube::app::AppRuntime runtime;
//   desktop.prepare(runtime.engine());            // before load()
//   runtime.load();
//   desktop.attach(runtime.engine());             // after
//
// What of this a platform wants is its own to say (interfaces/desktop_shell.hpp,
// Traits): on a phone, where the OS owns the window, the geometry is left
// alone and the identity step is a no-op, so main() carries no #ifdef.
class DesktopIntegration : public QObject {
    Q_OBJECT

public:
    explicit DesktopIntegration(QGuiApplication& app, QObject* parent = nullptr);

    // Sets the root window's initial geometry.
    void prepare(QQmlApplicationEngine& engine);
    // Starts remembering the window's geometry.
    void attach(QQmlApplicationEngine& engine);

private:
    WindowGeometry geometry_;
};

// The identifier on every platform (docs/design/platforms.md, "Identity").
inline constexpr const char* kAppId = "com.iainchesworthlabs.cubeulator";
// Windows only: groups the taskbar button with its shortcut. Handed to the
// platform's implementation, which the other platforms ignore.
inline constexpr const char* kAppUserModelId = "IainChesworthLabs.Cubeulator";

}  // namespace cube::app
