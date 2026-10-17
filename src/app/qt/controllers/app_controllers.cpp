#include "controllers/app_controllers.hpp"

#include "controllers/frame_image_provider.hpp"

#include <QtQml/qqml.h>
#include <QObject>
#include <QQmlEngine>
#include <QtConcurrentRun>

#include <utility>

#include <cube/diagnostics/json.hpp>
#include <cube/nxn/cube3.hpp>
#include <cube/solver/solver.hpp>

namespace cube::app {

AppControllers::AppControllers() {
    wire();
}

AppControllers::AppControllers(std::filesystem::path diagnostics_dir)
    : diagnostics(std::move(diagnostics_dir)) {
    wire();
}

void AppControllers::wire() {
    // The advanced method's lookup tables take a fraction of a second to
    // build. Do it now, off the UI thread, so that choosing it - on the
    // settings screen, or in the middle of a solve - does not wait for them.
    // Idempotent and thread-safe, and nothing here waits for it.
    (void)QtConcurrent::run([] { cube::nxn::prepare(3, cube::solver::SolveMethod::Advanced); });

    // A finished scan goes straight to the solver; the scan screen stays up
    // meanwhile (see AppState.qml).
    QObject::connect(&directScan, &DirectScan::scanCompleted, &solve, [this]() {
        // A 3x3 goes to the solver as it always has; any other size as the cube it is.
        if (const auto cube = directScan.cube(); cube.has_value()) {
            solve.solveCube(*cube);
        } else if (const auto any = directScan.cubeN(); any.has_value()) {
            solve.solveCube(*any);
        }
    });

    wireDiagnostics();
}

void AppControllers::wireDiagnostics() {
    using cube::diagnostics::Json;
    directScan.setDiagnostics(&diagnostics);
    held.setDiagnostics(&diagnostics);

    // The solve: a record when a solution lands and one for every step, so a
    // log reads as the order things happened in. All of it behind one test,
    // enabled(): nothing is built while the channel is off.
    const auto note_solve = [this](const char* event) {
        if (!diagnostics.enabled()) {
            return;
        }
        Json data = solve.diagnosticState();
        data.set("event", event);
        diagnostics.write("solve", std::move(data));
    };
    QObject::connect(&solve, &SolveController::solutionChanged, &diagnostics,
                     [note_solve]() { note_solve("solution"); });
    QObject::connect(&solve, &SolveController::stateChanged, &diagnostics,
                     [note_solve]() { note_solve("state"); });

    // The snapshot: everything the controllers hold, in one record, so a bad
    // result can be captured while it is on screen.
    QObject::connect(&diagnostics, &Diagnostics::snapshotRequested, &diagnostics, [this]() {
        Json data = Json::object();
        data.set("scan", directScan.diagnosticState());
        data.set("solve", solve.diagnosticState());
        data.set("held", held.diagnosticState());
        diagnostics.write("snapshot", std::move(data));
    });

    // Frames are kept only on the separate switch, and by the recorder the
    // scan screen already has (which is what puts its marker on screen). A
    // root set by CUBE_SCAN_RECORD_DIR is the developer's own and stays.
    environment_recording_root_ = directScan.recordingRoot();
    const auto follow_frames = [this]() {
        const QString root = diagnostics.framesRoot();
        directScan.setRecordingRoot(root.isEmpty() ? environment_recording_root_ : root);
    };
    QObject::connect(&diagnostics, &Diagnostics::framesRootChanged, &directScan, follow_frames);
    follow_frames();
}

void AppControllers::registerWithQml() {
    qmlRegisterSingletonInstance("Cubeulator", 1, 0, "CameraController", &camera);
    qmlRegisterSingletonInstance("Cubeulator", 1, 0, "DirectScan", &directScan);
    qmlRegisterSingletonInstance("Cubeulator", 1, 0, "PlatformFacts", &platform);
    qmlRegisterSingletonInstance("Cubeulator", 1, 0, "SolveController", &solve);
    qmlRegisterSingletonInstance("Cubeulator", 1, 0, "HeldCube", &held);
    qmlRegisterSingletonInstance("Cubeulator", 1, 0, "SolveHistory", &history);
    qmlRegisterSingletonInstance("Cubeulator", 1, 0, "ColourEntry", &colours);
    qmlRegisterSingletonInstance("Cubeulator", 1, 0, "Diagnostics", &diagnostics);
}

void AppControllers::addImageProviders(QQmlEngine& engine) {
    // The engine takes ownership of the provider; the provider only borrows
    // directScan, which is why the controllers have to outlive the engine.
    engine.addImageProvider("scanFrame",
                            new FrameImageProvider([this]() { return directScan.latestFrame(); }));
}

}  // namespace cube::app
