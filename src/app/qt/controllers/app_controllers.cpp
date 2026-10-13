#include "controllers/app_controllers.hpp"

#include "controllers/frame_image_provider.hpp"

#include <QtQml/qqml.h>
#include <QObject>
#include <QQmlEngine>

namespace cube::app {

AppControllers::AppControllers() {
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
    qmlRegisterSingletonInstance("Cubeulator", 1, 0, "SettingsModel", &settings);
    qmlRegisterSingletonInstance("Cubeulator", 1, 0, "PlatformFacts", &platform);
    qmlRegisterSingletonInstance("Cubeulator", 1, 0, "SolveController", &solve);
    qmlRegisterSingletonInstance("Cubeulator", 1, 0, "HeldCube", &held);
}

void AppControllers::addImageProviders(QQmlEngine& engine) {
    // The engine takes ownership of the provider; the provider only borrows
    // directScan, which is why the controllers have to outlive the engine.
    engine.addImageProvider("scanFrame",
                            new FrameImageProvider([this]() { return directScan.latestFrame(); }));
}

}  // namespace cube::app
