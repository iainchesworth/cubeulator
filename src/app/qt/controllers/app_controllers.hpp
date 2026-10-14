#pragma once

#include "controllers/camera_controller.hpp"
#include "controllers/diagnostics.hpp"
#include "controllers/direct_scan.hpp"
#include "controllers/held_cube.hpp"
#include "controllers/platform_facts.hpp"
#include "controllers/solve_history.hpp"
#include "solve_controller.hpp"

#include <filesystem>

class QQmlEngine;

namespace cube::app {

// Every controller the QML side talks to, wired to one another the way the
// app runs them. main.cpp makes one of these; so do the tests that load the
// app's own screens, which is how they come to exercise the real wiring
// rather than a copy of it.
struct AppControllers {
    // The diagnostics channel writes to the per-user place the platform
    // names and starts in the state CUBE_DIAGNOSTICS asks for.
    AppControllers();
    // The same, with the diagnostics channel writing to `diagnostics_dir` and
    // nothing read from the environment: for the tests, which must not write
    // into the user's own.
    explicit AppControllers(std::filesystem::path diagnostics_dir);
    AppControllers(const AppControllers&) = delete;
    AppControllers& operator=(const AppControllers&) = delete;

    // Registers each controller as the "Cubeulator" singleton QML knows it
    // by. Imperative rather than QML_ELEMENT because these are made here,
    // with real references to one another, not by the QML engine.
    void registerWithQml();
    // Gives `engine` the camera preview ("image://scanFrame/..."). The
    // controllers must outlive the engine.
    void addImageProviders(QQmlEngine& engine);

    // The developer diagnostics channel (docs/diagnostics.md). Declared first
    // so that it outlives the controllers that write to it.
    Diagnostics diagnostics;
    CameraController camera;
    // The scan screen's controller.
    DirectScan directScan;
    // What the platform says about itself; QML binds to it instead of
    // testing Qt.platform.os.
    PlatformFacts platform;
    SolveController solve;
    // How the cube is held while it is solved: the solve screen turns its
    // cube to match and words each move from it. It watches the camera's
    // frames against the solver's cube, hence the two it is made from.
    HeldCube held{directScan, solve};
    // The solves kept on this device: it writes them as the solve controller
    // steps through one, and puts the unfinished one back on request. Its file
    // is in the app's own data folder (SolveHistory::defaultFile).
    SolveHistory history{solve};

private:
    // What both constructors do once the members are there.
    void wire();
    // Connects the diagnostics channel to the controllers that write to it
    // and gathers their state for a snapshot.
    void wireDiagnostics();

    // What CUBE_SCAN_RECORD_DIR asked for, kept so that switching the
    // diagnostics' frames off puts it back.
    QString environment_recording_root_;
};

}  // namespace cube::app
