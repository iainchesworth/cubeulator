// The screens in more than one language, loaded with the app's own
// controllers and the language manager behind them.
//
// Nothing here needs a real display: the screens are made as items and read
// back (the way the solve lane's cube_qml_tests read the 3D cube), so the
// offscreen platform is enough and the run is the same headless on CI.
#include "screen_app.hpp"
#include "test_platform.hpp"

#include <catch2/catch_session.hpp>

#include <QCoreApplication>
#include <QFont>
#include <QGuiApplication>
#include <QSettings>
#include <QTemporaryDir>
#include <QtGlobal>

namespace {
cube::app::AppControllers* g_controllers = nullptr;
QQmlEngine* g_engine = nullptr;
cube::i18n::LanguageManager* g_language = nullptr;
}  // namespace

namespace cube::i18n_test {
QQmlEngine& engine() {
    return *g_engine;
}
cube::app::AppControllers& controllers() {
    return *g_controllers;
}
cube::i18n::LanguageManager& language() {
    return *g_language;
}
}  // namespace cube::i18n_test

int main(int argc, char* argv[]) {
    // Items that are never shown need no display; which plugin stands in is
    // the platform's to say.
    cube::test::apply_default_qpa_platform();
    QCoreApplication::setOrganizationName("Cubeulator");
    QCoreApplication::setApplicationName("Cubeulator-i18n-screen-tests");
    // The settings singleton reads its store when it is made, and the manager
    // writes language/code: neither may be the developer's.
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QTemporaryDir settings_dir;
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings_dir.path());
    qunsetenv("CUBEULATOR_LOCALE");

    QGuiApplication app(argc, argv);

    // The app's own face (Archivo) is not installed on a test machine and the
    // repository ships none yet, so every string is set in the machine's
    // default: Segoe UI on a Windows runner, DejaVu Sans, a good deal wider, on
    // a Linux one. A layout that holds in one and not the other is a layout
    // that depends on the font, so a developer can say which face to check
    // against: CUBE_I18N_TEST_FONT="DejaVu Sans" on a machine that has it, or
    // any wide face (Verdana) where it does not.
    if (const QString family = qEnvironmentVariable("CUBE_I18N_TEST_FONT"); !family.isEmpty()) {
        // The screens ask for Archivo by name, so it is the name that is
        // substituted; setting the application font would not reach them.
        QFont::insertSubstitution(QStringLiteral("Archivo"), family);
        QFont font = QGuiApplication::font();
        font.setFamily(family);
        QGuiApplication::setFont(font);
    }

    // Controllers first, so that the engine is torn down before them, as in
    // the app.
    cube::app::AppControllers controllers;
    controllers.registerWithQml();
    g_controllers = &controllers;
    QQmlEngine engine;
    controllers.addImageProviders(engine);
    g_engine = &engine;
    g_language = cube::i18n::install(app, engine);

    const int result = Catch::Session().run(argc, argv);
    g_language = nullptr;
    g_engine = nullptr;
    g_controllers = nullptr;
    return result;
}
