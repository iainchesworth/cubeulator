// The system tests need a QGuiApplication on a platform that can draw: the
// whole point is that the real QML is rendered, 3D cube included. Where that
// platform comes from is the environment's business - the desktop session on
// a developer's machine, Xvfb with Mesa's software GL in CI (see
// docs/testing.md) - so nothing here picks one.
#include "app.hpp"
#include "diagnostics.hpp"
#include "reach.hpp"
#include "test_platform.hpp"

#include <catch2/catch_session.hpp>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QSettings>
#include <QTemporaryDir>

#include <cstdio>

namespace {

// system.reach: judges the report the walk left behind. Needs no display,
// which is why it can be its own ctest entry: the walk is the one that draws.
int check_reach(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        std::fprintf(stderr, "no reach report at %s: run the system.walk test first\n",
                     qPrintable(path));
        return 2;
    }
    const QJsonObject report = QJsonDocument::fromJson(file.readAll()).object();
    std::fputs(qPrintable(report.value("summary").toString()), stdout);
    if (report.value("filtered").toBool()) {
        std::fprintf(stderr, "the report is from a filtered run; run the whole walk\n");
        return 2;
    }
    const QJsonArray gaps = report.value("gaps").toArray();
    for (const QJsonValue& gap : gaps) {
        std::fprintf(stderr, "GAP %s\n", qPrintable(gap.toString()));
    }
    // New screens, new states and new buttons arrive as a failure here until
    // a test visits them; that is the point. A state the environment cannot
    // show goes in reach.cpp with its reason, not in a skipped assertion.
    return gaps.isEmpty() ? 0 : 1;
}

}  // namespace

int main(int argc, char* argv[]) {
    for (int i = 1; i + 1 < argc; ++i) {
        if (QByteArray(argv[i]) == "--check-reach") {
            return check_reach(QString::fromLocal8Bit(argv[i + 1]));
        }
    }

    // Which platforms need a session variable for this, and what to tell
    // someone who has none, is the platform's to say (test_platform.hpp).
    if (const QString problem = cube::test::display_problem(QString::fromLocal8Bit(argv[0]));
        !problem.isEmpty()) {
        std::fprintf(stderr, "cube_system_tests draws the real app and %s\n", qPrintable(problem));
        return 2;
    }

    // The words the tests look for are English, whatever language the machine
    // is in: the app takes its language from this before the device
    // (docs/i18n.md). A run that wants another sets it.
    if (!qEnvironmentVariableIsSet("CUBEULATOR_LOCALE")) {
        qputenv("CUBEULATOR_LOCALE", "en");
    }
    // The basic render loop, and vsync off. Two things went wrong with the
    // threaded loop and vsync on, on Qt 6.12 (Windows, D3D11; the same suite
    // passed on Qt 6.8):
    //
    // - Animations and QML Timers did not advance: a Timer reported running
    //   and never fired, so the scan screen never handed over to the solution
    //   and the journey waited out its timeout, a case at a time. Vsync off
    //   cured this with the threaded loop kept; the [timers] case in
    //   journey_test.cpp checks it holds, so a regression is named, not a
    //   timeout.
    // - One walk in ten or so died with an access violation on the render
    //   thread (a call through a freed connection, from Qt6Quick inside the
    //   scene-graph sync, while the main thread waited), always at the press
    //   that leaves the launch screen - the one that destroys its 3D view.
    //   The basic loop renders on the GUI thread, so there is no second
    //   thread to race; it does not do this.
    //
    // A test wants frames that happen when it asks, on its own thread, at the
    // pace it sets, so it pins both. The application is run with whatever the
    // platform picks; a person using it is exposed to the second of these, at
    // the press that starts a scan, and nothing here says how likely that is.
    if (!qEnvironmentVariableIsSet("QSG_RENDER_LOOP")) {
        qputenv("QSG_RENDER_LOOP", "basic");
    }
    if (!qEnvironmentVariableIsSet("QSG_NO_VSYNC")) {
        qputenv("QSG_NO_VSYNC", "1");
    }

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

    Catch::Session session;
    if (const int error = session.applyCommandLine(argc, argv); error != 0) {
        return error;
    }
    // The walk is the whole suite bar the relaunch case and the tour; anything
    // narrower says so in its report, so a developer's single case cannot be
    // mistaken for a gate.
    const auto& specs = session.configData().testsOrTags;
    const bool filtered =
        !(specs.empty() || (specs.size() == 1 && specs.front() == "~[.restart]~[tour]"));
    const int result = session.run();

    if (auto* booted = cube::systest::App::booted(); booted != nullptr) {
        const QString report = booted->reach().summary();
        std::fputs(qPrintable(report), stdout);

        const QString out = QFileInfo(cube::systest::App::screenshot_dir()).absolutePath();
        QDir().mkpath(out);
        QFile json(out + "/reach.json");
        if (json.open(QIODevice::WriteOnly)) {
            QJsonObject document = booted->reach().to_json();
            document.insert("filtered", filtered);
            json.write(QJsonDocument(document).toJson());
        }
        QFile text(out + "/reach.txt");
        if (text.open(QIODevice::WriteOnly)) {
            text.write(report.toUtf8());
        }
    }
    cube::systest::App::shutdown();
    return result;
}
