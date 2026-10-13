// The translation tests need the application object the language manager
// takes (a QGuiApplication), and a settings store of their own: the manager
// reads and writes language/code, which must not be the developer's.
#include "test_platform.hpp"

#include <catch2/catch_session.hpp>

#include <QCoreApplication>
#include <QGuiApplication>
#include <QSettings>
#include <QTemporaryDir>
#include <QtGlobal>

int main(int argc, char* argv[]) {
    // Neither the manager nor the catalogues draw anything here, so no
    // display is needed; which plugin stands in is the platform's to say.
    cube::test::apply_default_qpa_platform();
    QCoreApplication::setOrganizationName("Cubeulator");
    QCoreApplication::setApplicationName("Cubeulator-i18n-tests");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QTemporaryDir settings_dir;
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings_dir.path());
    qunsetenv("CUBEULATOR_LOCALE");

    QGuiApplication app(argc, argv);
    return Catch::Session().run(argc, argv);
}
