#pragma once

#include "controllers/camera_controller.hpp"
#include "controllers/direct_scan.hpp"
#include "controllers/held_cube.hpp"
#include "controllers/platform_facts.hpp"
#include "controllers/settings_model.hpp"
#include "solve_controller.hpp"

class QQmlEngine;

namespace cube::app {

// Every controller the QML side talks to, wired to one another the way the
// app runs them. main.cpp makes one of these; so do the tests that load the
// app's own screens, which is how they come to exercise the real wiring
// rather than a copy of it.
struct AppControllers {
    AppControllers();
    AppControllers(const AppControllers&) = delete;
    AppControllers& operator=(const AppControllers&) = delete;

    // Registers each controller as the "Cubeulator" singleton QML knows it
    // by. Imperative rather than QML_ELEMENT because these are made here,
    // with real references to one another, not by the QML engine.
    void registerWithQml();
    // Gives `engine` the camera preview ("image://scanFrame/..."). The
    // controllers must outlive the engine.
    void addImageProviders(QQmlEngine& engine);

    CameraController camera;
    // The previous scan pipeline: still here because the settings screen
    // binds to it, but no screen starts it.
    // The scan screen's controller.
    DirectScan directScan;
    SettingsModel settings;
    // What the platform says about itself; QML binds to it instead of
    // testing Qt.platform.os.
    PlatformFacts platform;
    SolveController solve;
    // How the cube is held while it is solved: the solve screen turns its
    // cube to match and words each move from it. It watches the camera's
    // frames against the solver's cube, hence the two it is made from.
    HeldCube held{directScan, solve};
};

}  // namespace cube::app
