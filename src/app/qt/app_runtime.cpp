#include "app_runtime.hpp"

#include "controllers/frame_image_provider.hpp"

#include <QtQml/qqml.h>
#include <QQuickWindow>

namespace cube::app {

AppRuntime::AppRuntime() : solve_(scan_session_) {
    // Registered imperatively via qmlRegisterSingletonInstance rather than
    // QML_ELEMENT: cube_app_controllers is a separate target from
    // cube_app_qml specifically so its controllers are linkable into a
    // Catch2 test binary without pulling in QQmlEngine, and QML_ELEMENT's
    // compile-time registration only works for sources compiled directly
    // into a qt_add_qml_module target.
    qmlRegisterSingletonInstance("Cubeulator", 1, 0, "CameraController", &camera_);
    qmlRegisterSingletonInstance("Cubeulator", 1, 0, "ScanSession", &scan_session_);
    qmlRegisterSingletonInstance("Cubeulator", 1, 0, "DirectScan", &direct_scan_);
    qmlRegisterSingletonInstance("Cubeulator", 1, 0, "SettingsModel", &settings_);
    // Needs a real reference to scan_session_ (cube::model::ScanProgress
    // can't cross into QML), so - like the controllers above - it is
    // constructed directly rather than being QML-engine-owned.
    qmlRegisterSingletonInstance("Cubeulator", 1, 0, "SolveController", &solve_);

    QObject::connect(&direct_scan_, &DirectScan::scanCompleted, &solve_, [this]() {
        if (const auto cube = direct_scan_.cube(); cube.has_value()) {
            solve_.solveCube(*cube);
        }
    });

    // addImageProvider takes ownership; direct_scan_ outlives engine_ (engine_
    // is declared - and so destroyed first - after it).
    engine_.addImageProvider(
        "scanFrame", new FrameImageProvider([this]() { return direct_scan_.latestFrame(); }));
}

bool AppRuntime::load() {
    engine_.loadFromModule("Cubeulator", "Main");
    return !engine_.rootObjects().isEmpty();
}

QQuickWindow* AppRuntime::window() const {
    const auto roots = engine_.rootObjects();
    return roots.isEmpty() ? nullptr : qobject_cast<QQuickWindow*>(roots.first());
}

}  // namespace cube::app
