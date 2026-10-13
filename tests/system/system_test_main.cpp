// The system tests need a QGuiApplication on a platform that can draw: the
// whole point is that the real QML is rendered, 3D cube included. Where that
// platform comes from is the environment's business - the desktop session on
// a developer's machine, Xvfb with Mesa's software GL in CI (see
// docs/testing.md) - so nothing here picks one.
#include "app.hpp"
#include "diagnostics.hpp"
#include "reach.hpp"

#include <catch2/catch_session.hpp>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QSettings>
#include <QTemporaryDir>

#include <cstdio>

int main(int argc, char* argv[]) {
#if defined(Q_OS_LINUX)
    if (!qEnvironmentVariableIsSet("QT_QPA_PLATFORM") && !qEnvironmentVariableIsSet("DISPLAY") &&
        !qEnvironmentVariableIsSet("WAYLAND_DISPLAY")) {
        std::fprintf(stderr,
                     "cube_system_tests draws the real app and needs a display.\n"
                     "  xvfb-run -a %s     (software GL: LIBGL_ALWAYS_SOFTWARE=1)\n",
                     argv[0]);
        return 2;
    }
#endif

    // The app's settings are a QSettings store keyed by organisation and
    // application name. Point it at a directory that goes away with the
    // process, so a run neither reads the developer's real settings nor
    // leaves its own behind.
    QCoreApplication::setOrganizationName("Cubeulator");
    QCoreApplication::setApplicationName("Cubeulator-system-tests");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QTemporaryDir settings_dir;
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings_dir.path());

    // A run that begins with a setting already in the store: how a second
    // launch is tested (the [restart] case). The first launch's own change is
    // checked in the [settings] cases.
    if (const QString seeded = qEnvironmentVariable("CUBE_SYSTEM_TEST_THEME"); !seeded.isEmpty()) {
        QSettings store;
        store.setValue("appearance/themeMode", seeded);
        store.sync();
    }

    QGuiApplication app(argc, argv);
    cube::systest::install_warning_capture();

    const int result = Catch::Session().run(argc, argv);

    if (auto* booted = cube::systest::App::booted(); booted != nullptr) {
        const QString report = booted->reach().summary();
        std::fputs(qPrintable(report), stdout);

        const QString out = QFileInfo(cube::systest::App::screenshot_dir()).absolutePath();
        QDir().mkpath(out);
        QFile json(out + "/reach.json");
        if (json.open(QIODevice::WriteOnly)) {
            json.write(QJsonDocument(booted->reach().to_json()).toJson());
        }
        QFile text(out + "/reach.txt");
        if (text.open(QIODevice::WriteOnly)) {
            text.write(report.toUtf8());
        }
    }
    cube::systest::App::shutdown();
    return result;
}
