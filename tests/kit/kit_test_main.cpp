// The kit's tests load the app's own kit QML into a QGuiApplication. No
// display is needed: nothing here reads pixels, only items, their properties
// and the accessibility tree a screen reader would be given. The offscreen
// platform and the software scene graph are enough and make the run the same
// on a headless CI machine and on a developer's desktop, where it would
// otherwise flash windows.
#include "kit_fixture.hpp"

#include "kit/bundled_fonts.hpp"

#include <catch2/catch_session.hpp>

#include <QAccessible>
#include <QCoreApplication>
#include <QGuiApplication>
#include <QQmlEngine>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QSettings>
#include <QTemporaryDir>
#include <QtGlobal>

namespace {
QQmlEngine* g_engine = nullptr;
QString g_settingsRoot;
}

namespace cube::test {
QQmlEngine& engine() {
    return *g_engine;
}
QString settingsRoot() {
    return g_settingsRoot;
}
}  // namespace cube::test

int main(int argc, char* argv[]) {
    if (!qEnvironmentVariableIsSet("QT_QPA_PLATFORM")) {
        qputenv("QT_QPA_PLATFORM", "offscreen");
    }
    QCoreApplication::setOrganizationName("Cubeulator");
    QCoreApplication::setApplicationName("Cubeulator-kit-tests");

    // The settings the kit persists go to a throw-away folder, never to the
    // registry or to the store the real app writes, and every run starts
    // from the defaults.
    QTemporaryDir store;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    g_settingsRoot = store.path();
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, g_settingsRoot);

    QQuickWindow::setGraphicsApi(QSGRendererInterface::Software);

    QGuiApplication app(argc, argv);
    // Off by default until a screen reader asks; the tests are the screen
    // reader, so the accessibility tree has to exist.
    QAccessible::setActive(true);
    cube::app::kit::installBundledFonts();

    QQmlEngine engine;
    g_engine = &engine;

    const int result = Catch::Session().run(argc, argv);

    cube::test::releaseFixtures();
    g_engine = nullptr;
    return result;
}
