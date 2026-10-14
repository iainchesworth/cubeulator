#pragma once

#include "controllers/app_controllers.hpp"
#include "desktop/desktop_integration.hpp"

#include <QQmlApplicationEngine>

#include <filesystem>

class QQuickWindow;

namespace cube::app {

// The application as it runs: the controllers (AppControllers - the one place
// they are made and registered), the language (cube::i18n::install) and the
// engine that loads Main.qml.
//
// main() and the system tests (tests/system) construct this one class, so a
// test boots exactly what a user boots and nothing is wired by hand twice.
// The two differ only in what sits underneath - the real platform camera in
// the app, a scripted one in the tests (tests/system/fake_camera.cpp) - and
// in the QPA platform the QGuiApplication was started on.
//
// The controllers are registered with qmlRegisterSingletonInstance, which
// serves one engine per process: construct at most one AppRuntime per
// process, after the QGuiApplication.
class AppRuntime {
public:
    AppRuntime();
    // The same, with the diagnostics channel writing to `diagnostics_dir`
    // rather than to the user's own, and nothing read from the environment
    // (AppControllers): for the system tests, which press every control the
    // Settings screen has, the channel's switches included.
    explicit AppRuntime(std::filesystem::path diagnostics_dir);
    AppRuntime(const AppRuntime&) = delete;
    AppRuntime& operator=(const AppRuntime&) = delete;

    // Loads "Main" from the "Cubeulator" module. False if it produced no root
    // object (a QML error: the engine has already logged it).
    [[nodiscard]] bool load();

    // The root object's window, once load() has succeeded.
    [[nodiscard]] QQuickWindow* window() const;

    [[nodiscard]] QQmlApplicationEngine& engine() noexcept { return engine_; }
    [[nodiscard]] AppControllers& controllers() noexcept { return controllers_; }

    // The controllers by the names the tests use.
    [[nodiscard]] CameraController& camera() noexcept { return controllers_.camera; }
    [[nodiscard]] DirectScan& directScan() noexcept { return controllers_.directScan; }
    [[nodiscard]] SolveController& solve() noexcept { return controllers_.solve; }
    [[nodiscard]] HeldCube& held() noexcept { return controllers_.held; }

private:
    // What both constructors do once the members are there.
    void wire();

    // Declared after the controllers, and so destroyed before them: the
    // engine's image provider borrows directScan.
    AppControllers controllers_;
    // What DesktopShell.qml reads (KeepAwake, AppInfo). Part of the runtime,
    // not of main(), so that a test boots the same Main.qml with the same
    // singletons. Window geometry and the process's identity are not here:
    // they are what only the real application does (desktop_integration.hpp).
    DesktopServices desktop_;
    QQmlApplicationEngine engine_;
};

}  // namespace cube::app
