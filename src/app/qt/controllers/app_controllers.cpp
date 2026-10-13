#include "controllers/app_controllers.hpp"

#include <QtQml/qqml.h>
#include <QObject>

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
    qmlRegisterSingletonInstance("Cubeulator", 1, 0, "ScanSession", &scanSession);
    qmlRegisterSingletonInstance("Cubeulator", 1, 0, "DirectScan", &directScan);
    qmlRegisterSingletonInstance("Cubeulator", 1, 0, "SettingsModel", &settings);
    qmlRegisterSingletonInstance("Cubeulator", 1, 0, "SolveController", &solve);
    qmlRegisterSingletonInstance("Cubeulator", 1, 0, "HeldCube", &held);
}

}  // namespace cube::app
