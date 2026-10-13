// Tests that load the app's own QML need a QGuiApplication, where the
// controller tests get by on QCoreApplication (qt_test_main.cpp).
//
// No window is ever shown: the 3D cube is read back from its nodes' own
// transforms, which need neither a GPU nor a display, so the offscreen
// platform is enough and the suite runs the same on a headless CI machine.
#include "qml_test_app.hpp"

#include <catch2/catch_session.hpp>

#include <QCoreApplication>
#include <QGuiApplication>
#include <QQmlEngine>
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
#ifndef Q_OS_WIN
    // Windows keeps its own platform plugin: it is the only one deployed
    // beside the binaries there, and it needs no display for this either.
    if (!qEnvironmentVariableIsSet("QT_QPA_PLATFORM")) {
        qputenv("QT_QPA_PLATFORM", "offscreen");
    }
#endif
    // The app's settings singleton reads its store on creation; keep the
    // tests away from the store the real app writes.
    QCoreApplication::setOrganizationName("Cubeulator");
    QCoreApplication::setApplicationName("Cubeulator-qml-tests");

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
