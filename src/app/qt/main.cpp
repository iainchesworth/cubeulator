#include "controllers/camera_controller.hpp"
#include "controllers/frame_image_provider.hpp"
#include "controllers/scan_session.hpp"
#include "controllers/settings_model.hpp"

#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QtQml/qqml.h>

int main(int argc, char* argv[]) {
    QGuiApplication app(argc, argv);

    // Registered imperatively via qmlRegisterSingletonInstance rather than
    // QML_ELEMENT: cube_app_controllers is a separate target from
    // cube_app_qml specifically so its controllers are linkable into a
    // Catch2 test binary without pulling in QQmlEngine, and QML_ELEMENT's
    // compile-time registration only works for sources compiled directly
    // into a qt_add_qml_module target.
    cube::app::CameraController cameraController;
    qmlRegisterSingletonInstance("Cubeulator", 1, 0, "CameraController", &cameraController);

    cube::app::ScanSession scanSession;
    qmlRegisterSingletonInstance("Cubeulator", 1, 0, "ScanSession", &scanSession);

    cube::app::SettingsModel settingsModel;
    qmlRegisterSingletonInstance("Cubeulator", 1, 0, "SettingsModel", &settingsModel);

    QQmlApplicationEngine engine;
    // addImageProvider takes ownership; scanSession outlives engine (engine
    // is declared - and so destroyed first - after scanSession).
    engine.addImageProvider("scanFrame", new cube::app::FrameImageProvider(scanSession));
    engine.loadFromModule("Cubeulator", "Main");

    if (engine.rootObjects().isEmpty()) {
        return -1;
    }

    return QGuiApplication::exec();
}
