#include "controllers/app_controllers.hpp"

#include "controllers/frame_image_provider.hpp"

#include <QtQml/qqml.h>
#include <QObject>
#include <QQmlEngine>
#include <QtConcurrentRun>

#include <cube/solver/solver.hpp>

namespace cube::app {

AppControllers::AppControllers() {
    // The advanced method's lookup tables take a fraction of a second to
    // build. Do it now, off the UI thread, so that choosing it - on the
    // settings screen, or in the middle of a solve - does not wait for them.
    // Idempotent and thread-safe, and nothing here waits for it.
    (void)QtConcurrent::run([] { cube::solver::prepare(cube::solver::SolveMethod::Advanced); });

    // A finished scan goes straight to the solver; the scan screen stays up
    // meanwhile (see AppState.qml).
    QObject::connect(&directScan, &DirectScan::scanCompleted, &solve, [this]() {
        if (const auto cube = directScan.cube(); cube.has_value()) {
            solve.solveCube(*cube);
        }
    });
}

void AppControllers::registerWithQml() {
    qmlRegisterSingletonInstance("Cubeulator", 1, 0, "CameraController", &camera);
    qmlRegisterSingletonInstance("Cubeulator", 1, 0, "DirectScan", &directScan);
    qmlRegisterSingletonInstance("Cubeulator", 1, 0, "PlatformFacts", &platform);
    qmlRegisterSingletonInstance("Cubeulator", 1, 0, "SolveController", &solve);
    qmlRegisterSingletonInstance("Cubeulator", 1, 0, "HeldCube", &held);
    qmlRegisterSingletonInstance("Cubeulator", 1, 0, "SolveHistory", &history);
}

void AppControllers::addImageProviders(QQmlEngine& engine) {
    // The engine takes ownership of the provider; the provider only borrows
    // directScan, which is why the controllers have to outlive the engine.
    engine.addImageProvider("scanFrame",
                            new FrameImageProvider([this]() { return directScan.latestFrame(); }));
}

}  // namespace cube::app
