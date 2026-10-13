// The translation tests need the application object the language manager
// takes (a QGuiApplication), and a settings store of their own: the manager
// reads and writes language/code, which must not be the developer's.
#include <catch2/catch_session.hpp>

#include <QCoreApplication>
#include <QGuiApplication>
#include <QSettings>
#include <QTemporaryDir>
#include <QtGlobal>

int main(int argc, char* argv[]) {
#ifndef Q_OS_WIN
    // Windows keeps its own platform plugin, the only one deployed beside the
    // binaries there; neither draws anything here.
    if (!qEnvironmentVariableIsSet("QT_QPA_PLATFORM")) {
        qputenv("QT_QPA_PLATFORM", "offscreen");
    }
#endif
    QCoreApplication::setOrganizationName("Cubeulator");
    QCoreApplication::setApplicationName("Cubeulator-i18n-tests");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QTemporaryDir settings_dir;
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings_dir.path());
    qunsetenv("CUBEULATOR_LOCALE");

    QGuiApplication app(argc, argv);
    return Catch::Session().run(argc, argv);
}
