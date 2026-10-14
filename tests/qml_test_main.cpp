// Tests that load the app's own QML need a QGuiApplication, where the
// controller tests get by on QCoreApplication (qt_test_main.cpp).
//
// No window is ever shown: the 3D cube is read back from its nodes' own
// transforms, which need neither a GPU nor a display, so the offscreen
// platform is enough and the suite runs the same on a headless CI machine.
#include "qml_test_app.hpp"
#include "test_platform.hpp"

#include <catch2/catch_session.hpp>

#include <QCoreApplication>
#include <QGuiApplication>
#include <QQmlEngine>
#include <QTemporaryDir>
#include <QtGlobal>

namespace {
cube::app::AppControllers* g_controllers = nullptr;
QQmlEngine* g_engine = nullptr;
}  // namespace

namespace cube::test {
cube::app::AppControllers& app_controllers() {
    return *g_controllers;
}
QQmlEngine& qml_engine() {
    return *g_engine;
}
}  // namespace cube::test

int main(int argc, char* argv[]) {
    // No window is ever shown, so no display is needed; which plugin stands
    // in is the platform's to say.
    cube::test::apply_default_qpa_platform();
    // The app's settings singleton reads its store on creation; keep the
    // tests away from the store the real app writes.
    QCoreApplication::setOrganizationName("Cubeulator");
    QCoreApplication::setApplicationName("Cubeulator-qml-tests");
    // The history of solves is a file in the app's own data folder, which the
    // controllers open when they are made and a solve stepped through writes.
    // A directory of this process's own, so that it is not a person's.
    QTemporaryDir data_dir;
    if (!qEnvironmentVariableIsSet("CUBEULATOR_DATA_DIR")) {
        qputenv("CUBEULATOR_DATA_DIR", data_dir.path().toLocal8Bit());
    }

    QGuiApplication app(argc, argv);

    // The engine goes first: it is made after the controllers and so torn
    // down before them, as in the app.
    cube::app::AppControllers controllers;
    controllers.registerWithQml();
    g_controllers = &controllers;
    QQmlEngine engine;
    controllers.addImageProviders(engine);
    g_engine = &engine;

    const int result = Catch::Session().run(argc, argv);
    g_engine = nullptr;
    g_controllers = nullptr;
    return result;
}
