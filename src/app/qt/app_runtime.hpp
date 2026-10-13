#pragma once

#include "controllers/camera_controller.hpp"
#include "controllers/direct_scan.hpp"
#include "controllers/scan_session.hpp"
#include "controllers/settings_model.hpp"
#include "solve_controller.hpp"

#include <QQmlApplicationEngine>

class QQuickWindow;

namespace cube::app {

// Everything the application is made of, built the way the application builds
// it: the five controllers, their registration as QML singletons, the preview
// image provider, and the engine that loads Main.qml.
//
// main() and the system tests (tests/system) construct this one class, so a
// test boots exactly what a user boots and nothing is wired by hand twice.
// The two differ only in what sits underneath - the real platform camera in
// the app, a scripted one in the tests (tests/system/fake_camera.cpp) - and
// in the QPA platform the QGuiApplication was started on.
//
// The controllers are registered with qmlRegisterSingletonInstance, which is
// process-global: construct at most one AppRuntime per process.
class AppRuntime {
public:
    AppRuntime();
    AppRuntime(const AppRuntime&) = delete;
    AppRuntime& operator=(const AppRuntime&) = delete;

    // Loads "Main" from the "Cubeulator" module. False if it produced no root
    // object (a QML error: the engine has already logged it).
    [[nodiscard]] bool load();

    // The root object's window, once load() has succeeded.
    [[nodiscard]] QQuickWindow* window() const;

    [[nodiscard]] QQmlApplicationEngine& engine() noexcept { return engine_; }
    [[nodiscard]] CameraController& camera() noexcept { return camera_; }
    [[nodiscard]] ScanSession& scanSession() noexcept { return scan_session_; }
    [[nodiscard]] DirectScan& directScan() noexcept { return direct_scan_; }
    [[nodiscard]] SettingsModel& settings() noexcept { return settings_; }
    [[nodiscard]] SolveController& solve() noexcept { return solve_; }

private:
    // Declaration order is destruction order in reverse: the engine goes
    // first, then the controllers that depend on the ones declared before
    // them (solve_ holds a reference to scan_session_).
    CameraController camera_;
    ScanSession scan_session_;
    // The scan screen's controller. ScanSession is the previous scan
    // pipeline, still registered because the settings screen binds to it,
    // but no screen starts it.
    DirectScan direct_scan_;
    SettingsModel settings_;
    SolveController solve_;
    QQmlApplicationEngine engine_;
};

}  // namespace cube::app
