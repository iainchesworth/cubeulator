// The journey: launch, scan, solve, and the ways out of it that the app has
// today. Everything is done through the window - presses sent to controls
// found by objectName - and every observation is of what a person sees.
//
// What the app cannot do yet is in known_today_test.cpp, asserted as it is
// and marked to change.

#include "app.hpp"
#include "diagnostics.hpp"
#include "fake_camera.hpp"
#include "scan_script.hpp"

#include "app_runtime.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <cube/solver/solver.hpp>

#include <memory>
#include <set>

#include <QCoreApplication>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QRegularExpression>

namespace {

using cube::systest::App;
using cube::systest::FakeCamera;
using cube::systest::Layout;

constexpr int kScanTimeoutMs = 60'000;
// The solve runs on a worker and takes a second or two even under coverage
// instrumentation; a minute is a failure, not a slow machine, and a case that
// waits it out is the failure being reported, not the suite's time budget.
constexpr int kSolveTimeoutMs = 60'000;

[[nodiscard]] QSize size_for(Layout layout) {
    return layout == Layout::wide ? cube::systest::kDesktop : cube::systest::kPhone;
}

void start_scan_from_launch(App& app) {
    REQUIRE(app.screen() == "launch");
    app.click("launch.startScan");
    REQUIRE(app.wait_for([&]() { return app.screen() == "scan"; }));
    REQUIRE(app.wait_for([&]() { return app.runtime().directScan().running(); }));
}

}  // namespace

// The scan screen hands over to the solution on a Timer, the elapsed clock is
// another, and every screen fades in. If a session's timers and animations do
// not advance, nothing that waits on them can pass, and it shows up as a
// timeout in whichever journey case waits first. This names it, in seconds.
// See the render loop settings in system_test_main.cpp.
TEST_CASE("QML timers and animations advance in this session", "[system][timers]") {
    App& app = App::fresh();
    QQmlComponent component(&app.runtime().engine());
    component.setData(R"(
        import QtQuick
        Item {
            id: root
            property int ticks: 0
            property real progress: 0
            Timer { interval: 20; running: true; repeat: true; onTriggered: root.ticks += 1 }
            NumberAnimation on progress { from: 0; to: 1; duration: 100 }
        }
    )",
                      QUrl());
    REQUIRE_FALSE(component.isError());
    const std::unique_ptr<QObject> made(component.create());
    auto* probe = qobject_cast<QQuickItem*>(made.get());
    REQUIRE(probe != nullptr);
    probe->setParentItem(app.window().contentItem());

    const bool ticked =
        app.wait_for([&]() { return probe->property("ticks").toInt() >= 5; }, 5'000);
    const bool finished =
        app.wait_for([&]() { return probe->property("progress").toDouble() >= 1.0; }, 5'000);
    INFO("ticks=" << probe->property("ticks").toInt()
                  << " progress=" << probe->property("progress").toDouble());
    CHECK(ticked);
    CHECK(finished);
}

TEST_CASE("the app opens on the launch screen and draws it", "[system][launch]") {
    App& app = App::fresh();
    static_cast<void>(cube::systest::take_qml_warnings());

    CHECK(app.screen() == "launch");
    CHECK(app.window().title() == "Cubeulator");
    CHECK(app.layout() == Layout::wide);
    CHECK(app.find("launch.startScan") != nullptr);
    CHECK(app.find("launch.settings") != nullptr);

    // The camera the app asked for at start-up is the one that was plugged in.
    REQUIRE(app.wait_for([&]() { return !app.runtime().camera().busy(); }));
    CHECK(app.runtime().camera().devices().size() == 1);
    CHECK(app.runtime().camera().selectedDeviceId() == "fake:0");
    // Nothing opened the camera just to show the launch screen.
    CHECK(FakeCamera::instance().starts() == 0);

    app.settle();
    CHECK_FALSE(app.grab().isNull());

    const auto warnings = cube::systest::take_qml_warnings();
    for (const auto& warning : warnings) {
        UNSCOPED_INFO(warning.where.toStdString() << ": " << warning.message.toStdString());
    }
    CHECK(warnings.empty());
}

TEST_CASE("a cube is scanned and solved, from launch to the last move", "[system][journey]") {
    const Layout layout = GENERATE(Layout::wide, Layout::narrow);
    DYNAMIC_SECTION("in the " << cube::systest::to_string(layout) << " layout") {
        App& app = App::fresh();
        static_cast<void>(cube::systest::take_qml_warnings());
        app.resize(size_for(layout));
        REQUIRE(app.layout() == layout);

        const cube::solver::Cube cube = cube::systest::scramble(7);
        auto& scan = app.runtime().directScan();
        auto& solve = app.runtime().solve();

        start_scan_from_launch(app);
        // Opening the scan screen opened the camera, once.
        CHECK(FakeCamera::instance().starts() == 1);
        CHECK(scan.sidesRead() == 0);
        CHECK(app.find_text("0 of 6 sides read") != nullptr);

        // The first side, held square to the camera.
        const auto first = cube::test::outside_view(cube, cube::solver::kAllFaces[0]);
        cube::systest::show_side(first);
        REQUIRE(app.wait_for([&]() { return scan.sidesRead() == 1; }, kScanTimeoutMs));
        CHECK(scan.stickerPoints().size() == 9);
        CHECK(app.wait_for([&]() { return app.find_text("1 of 6 sides read") != nullptr; }));

        // The rest of the cube, a side at a time.
        cube::systest::show_empty_room();
        for (std::size_t i = 1; i < cube::solver::kAllFaces.size(); ++i) {
            cube::systest::show_side(cube::test::outside_view(cube, cube::solver::kAllFaces[i]));
            cube::systest::show_empty_room();
        }
        REQUIRE(app.wait_for([&]() { return scan.complete(); }, kScanTimeoutMs));
        REQUIRE(scan.cube().has_value());
        CHECK(*scan.cube() == cube);

        // A finished scan stays up long enough to be seen, then the solution
        // replaces it.
        CHECK(app.screen() == "scan");
        CHECK(app.find_text("Cube read") != nullptr);
        const bool reached_solve =
            app.wait_for([&]() { return app.screen() == "solve"; }, kSolveTimeoutMs);
        // Where it stopped, if it did: the solve having finished while the
        // screen stayed on the scan is a different failure from a solve that
        // never finished, and a bare "false" says neither.
        QObject& state = app.app_state();
        const QObject* beat = qvariant_cast<QObject*>(state.property("completeBeat"));
        INFO("screen=" << app.screen().toStdString() << " isSolving=" << solve.isSolving()
                       << " totalMoves=" << solve.totalMoves()
                       << " scan.complete=" << scan.complete() << " AppState.scanComplete="
                       << state.property("scanComplete").toBool() << " completeBeat.running="
                       << (beat != nullptr && beat->property("running").toBool())
                       << " AppState.elapsedSeconds=" << state.property("elapsedSeconds").toInt());
        REQUIRE(reached_solve);
        REQUIRE_FALSE(solve.isSolving());
        REQUIRE(solve.totalMoves() > 0);
        CHECK(solve.atStart());
        // The solve screen keeps the camera, to see how the cube is held.
        CHECK(FakeCamera::instance().capturing());

        // Previous is off at the start, Next move is on.
        QQuickItem* previous = app.find("solve.previous");
        QQuickItem* next = app.find("solve.next");
        REQUIRE(previous != nullptr);
        REQUIRE(next != nullptr);
        CHECK_FALSE(previous->property("enabled").toBool());
        CHECK(next->property("enabled").toBool());
        CHECK(next->property("text").toString() == "Next move");
        app.settle();

        // The moves, a press each for the first and last few and for the first
        // of every kind of turn (R, U', F2, ...), and the rest stepped through
        // the controller. A press plays the turn and the step lands when it
        // has finished; the same control is pressed ninety times, and played
        // out on a software renderer that was a third of the walk. The kinds
        // are what differ between presses - which layer, which way, how far -
        // so each one is pressed once.
        const int total = solve.totalMoves();
        constexpr int kPressedAtEachEnd = 3;
        std::set<QString> kinds;
        for (int taken = 1; taken <= total; ++taken) {
            INFO("move " << taken << " of " << total);
            const bool new_kind = kinds.insert(solve.currentMoveNotation()).second;
            const bool pressed =
                new_kind || taken <= kPressedAtEachEnd || taken > total - kPressedAtEachEnd;
            if (pressed) {
                app.click("solve.next");
                REQUIRE(app.wait_for([&]() { return solve.moveIndex() == taken; }, 15'000));
            } else {
                solve.nextMove();
                REQUIRE(solve.moveIndex() == taken);
                app.pump(10);
            }
        }
        CHECK(solve.atEnd());
        CHECK(app.screen() == "solve");

        // And back a move, then forward again.
        app.click("solve.previous");
        REQUIRE(app.wait_for([&]() { return solve.moveIndex() == total - 1; }, 15'000));
        CHECK_FALSE(solve.atEnd());
        app.click("solve.next");
        REQUIRE(app.wait_for([&]() { return solve.atEnd(); }, 15'000));

        // The last step is Finish, and it leads to the complete screen.
        next = app.find("solve.next");
        REQUIRE(next != nullptr);
        CHECK(next->property("text").toString() == "Finish");
        app.click("solve.next");
        REQUIRE(app.wait_for([&]() { return app.screen() == "complete"; }));
        CHECK(app.find("complete.scanAnother") != nullptr);
        CHECK(app.find("complete.review") != nullptr);
        // Leaving the solve screen let go of the camera.
        REQUIRE(app.wait_for([&]() { return !FakeCamera::instance().capturing(); }));

        // And round again: Scan another goes back to scanning from nothing.
        app.click("complete.scanAnother");
        REQUIRE(app.wait_for([&]() { return app.screen() == "scan"; }));
        REQUIRE(app.wait_for([&]() { return scan.running(); }));
        CHECK(scan.sidesRead() == 0);

        const auto warnings = cube::systest::take_qml_warnings();
        for (const auto& warning : warnings) {
            UNSCOPED_INFO(warning.where.toStdString() << ": " << warning.message.toStdString());
        }
        CHECK(warnings.empty());
    }
}

TEST_CASE("the solve screen has a way out that reads the cube again", "[system][solve]") {
    const Layout layout = GENERATE(Layout::wide, Layout::narrow);
    INFO("in the " << cube::systest::to_string(layout) << " layout");
    App& app = App::fresh(App::Start::elsewhere);
    app.resize(size_for(layout));
    auto& scan = app.runtime().directScan();
    auto& solve = app.runtime().solve();

    // A cube handed straight to the solver; the screen put up as AppState
    // would after a scan.
    solve.solveCube(cube::systest::scramble(17));
    REQUIRE(app.wait_for([&]() { return !solve.isSolving(); }, kSolveTimeoutMs));
    app.go("solve");
    app.settle();
    REQUIRE(app.wait_for([&]() { return FakeCamera::instance().capturing(); }));

    // Part-way in, out of step with the cube in the hand.
    app.click("solve.next");
    REQUIRE(app.wait_for([&]() { return solve.moveIndex() == 1; }, 15'000));

    app.click("solve.rescan");
    REQUIRE(app.wait_for([&]() { return app.screen() == "scan"; }));
    REQUIRE(app.wait_for([&]() { return scan.running() && !scan.watching(); }));
    CHECK(scan.sidesRead() == 0);
    CHECK(app.find_text("0 of 6 sides read") != nullptr);
}

TEST_CASE("cancelling mid-scan returns to launch and lets go of the camera", "[system][scan]") {
    App& app = App::fresh();
    auto& scan = app.runtime().directScan();
    const cube::solver::Cube cube = cube::systest::scramble(11);

    start_scan_from_launch(app);
    cube::systest::show_side(cube::test::outside_view(cube, cube::solver::kAllFaces[0]));
    REQUIRE(app.wait_for([&]() { return scan.sidesRead() == 1; }, kScanTimeoutMs));
    REQUIRE(FakeCamera::instance().capturing());

    app.click_button("Cancel");
    REQUIRE(app.wait_for([&]() { return app.screen() == "launch"; }));
    REQUIRE(app.wait_for([&]() { return !FakeCamera::instance().capturing(); }));
    CHECK(FakeCamera::instance().stops() == 1);
    CHECK_FALSE(scan.running());

    // Starting again begins from nothing: the half-read cube is forgotten.
    start_scan_from_launch(app);
    CHECK(scan.sidesRead() == 0);
    CHECK(FakeCamera::instance().starts() == 2);
}

TEST_CASE("start over forgets the sides read and keeps scanning", "[system][scan]") {
    const Layout layout = GENERATE(Layout::wide, Layout::narrow);
    INFO("in the " << cube::systest::to_string(layout) << " layout");
    App& app = App::fresh();
    app.resize(size_for(layout));
    auto& scan = app.runtime().directScan();
    const cube::solver::Cube cube = cube::systest::scramble(13);

    start_scan_from_launch(app);

    // Nothing read yet: there is nothing to start over from.
    QQuickItem* start_over = app.find_button("Start over");
    REQUIRE(start_over != nullptr);
    CHECK_FALSE(start_over->property("enabled").toBool());

    cube::systest::show_side(cube::test::outside_view(cube, cube::solver::kAllFaces[0]));
    cube::systest::show_empty_room();
    cube::systest::show_side(cube::test::outside_view(cube, cube::solver::kAllFaces[1]));
    REQUIRE(app.wait_for([&]() { return scan.sidesRead() == 2; }, kScanTimeoutMs));
    CHECK(start_over->property("enabled").toBool());

    // Put the camera on an empty room first, or the side still in view is
    // read again the moment the scan is cleared.
    FakeCamera::instance().clear_script();
    cube::systest::show_empty_room();
    app.pump(150);
    app.click_button("Start over");
    REQUIRE(app.wait_for([&]() { return scan.sidesRead() == 0; }));
    CHECK(app.screen() == "scan");
    CHECK(scan.running());
    CHECK(FakeCamera::instance().capturing());
    CHECK(app.find_text("0 of 6 sides read") != nullptr);
    CHECK_FALSE(start_over->property("enabled").toBool());
}

TEST_CASE("a camera that will not start is explained, and can be left", "[system][scan][error]") {
    App& app = App::fresh();
    auto& scan = app.runtime().directScan();

    FakeCamera::instance().fail_next_start(cube::CubeError::camera_start_failed);
    app.click("launch.startScan");
    REQUIRE(app.wait_for([&]() { return app.screen() == "scan"; }));
    REQUIRE(app.wait_for([&]() { return !scan.cameraError().isEmpty(); }));

    CHECK_FALSE(scan.running());
    CHECK(scan.cameraError() == "the camera could not be started");
    CHECK(app.find_text("Camera unavailable") != nullptr);
    // The reason is shown to the person, not only logged.
    CHECK(app.find_text("the camera could not be started") != nullptr);
    app.settle();
    CHECK_FALSE(app.grab().isNull());

    app.click_button("Cancel");
    REQUIRE(app.wait_for([&]() { return app.screen() == "launch"; }));

    // The next attempt is not poisoned by the last.
    start_scan_from_launch(app);
    CHECK(scan.cameraError().isEmpty());
    CHECK(app.find_text("Camera unavailable") == nullptr);
}

TEST_CASE("with no camera plugged in the settings say so, and refreshing finds one",
          "[system][settings][error]") {
    App& app = App::fresh();
    auto& camera = app.runtime().camera();

    FakeCamera::instance().set_devices({});
    app.click("launch.settings");
    REQUIRE(app.wait_for([&]() { return app.screen() == "settings"; }));
    app.click("settings.refreshCameras");
    REQUIRE(app.wait_for([&]() { return !camera.busy(); }));
    REQUIRE(
        app.wait_for([&]() { return app.find_text(qtTrId("settings.camera.none")) != nullptr; }));

    FakeCamera::instance().reset();
    app.click("settings.refreshCameras");
    REQUIRE(app.wait_for([&]() { return !camera.busy() && camera.devices().size() == 1; }));
    CHECK(app.find_text(qtTrId("settings.camera.choose")) != nullptr);
    CHECK(app.find("settings.camera.0") != nullptr);
}

namespace {

// The solution screen's own state, as its main button and caption are bound to
// it: overview, stepping, playing or end.
[[nodiscard]] QString review_state(App& app) {
    QQuickItem* screen = app.screen_item();
    return screen == nullptr ? QString() : screen->property("reviewState").toString();
}

[[nodiscard]] int review_position(App& app) {
    QQuickItem* screen = app.screen_item();
    return screen == nullptr ? -1 : screen->property("position").toInt();
}

[[nodiscard]] QString text_of(App& app, const char* name) {
    QQuickItem* item = app.find(QString::fromLatin1(name));
    return item == nullptr ? QString() : item->property("text").toString();
}

// What is under test in a play is that it steps on, that it stops, and where
// it ends - not how long 700 ms is - so the screen's one Timer is quickened.
void quicken_play(App& app) {
    QQuickItem* screen = app.screen_item();
    REQUIRE(screen != nullptr);
    bool found = false;
    for (QObject* child : screen->children()) {
        if (child->inherits("QQmlTimer")) {
            REQUIRE(child->setProperty("interval", 20));
            found = true;
        }
    }
    REQUIRE(found);
}

}  // namespace

TEST_CASE("the solution is reviewed from the complete screen, stepped through and left",
          "[system][journey][solution]") {
    const Layout layout = GENERATE(Layout::wide, Layout::narrow);
    DYNAMIC_SECTION("in the " << cube::systest::to_string(layout) << " layout") {
        App& app = App::fresh(App::Start::elsewhere);
        static_cast<void>(cube::systest::take_qml_warnings());
        app.resize(size_for(layout));
        auto& solve = app.runtime().solve();

        // A longer scramble than the walk has: a list long enough to scroll,
        // and every kind of turn in it.
        solve.solveCube(cube::solver::random_scramble(31, cube::solver::MoveCount{25}));
        REQUIRE(app.wait_for([&]() { return !solve.isSolving(); }, kSolveTimeoutMs));
        const int total = solve.totalMoves();
        REQUIRE(total > 12);
        // As the last step of the solve leaves it.
        while (!solve.atEnd()) {
            solve.nextMove();
        }
        const cube::solver::Cube solved = solve.currentCube();
        REQUIRE(solved.is_solved());

        app.go("complete");
        app.settle();
        app.click("complete.review");
        REQUIRE(app.wait_for([&]() { return app.screen() == "solution"; }));
        app.settle();

        // What it opens on: the cube as it was scanned, every stage listed.
        CHECK(text_of(app, "solution.title") == qtTrId("solution.title"));
        CHECK(text_of(app, "solution.summary") ==
              qtTrId("solution.summary", total).arg(QStringLiteral("5")));
        CHECK(text_of(app, "solution.method") == qtTrId("solution.method.beginner"));
        CHECK(text_of(app, "solution.caption") == qtTrId("solution.atStart"));
        CHECK(app.find("solution.stage1") != nullptr);
        CHECK(app.find("solution.cube") != nullptr);
        CHECK(review_state(app) == "overview");
        CHECK_FALSE(app.find("solution.previous")->property("enabled").toBool());
        CHECK(app.find("solution.next")->property("enabled").toBool());
        CHECK(text_of(app, "solution.play") == qtTrId("solution.play"));
        QQuickItem* first = app.find("solution.turn1");
        REQUIRE(first != nullptr);
        CHECK(first->property("selected").toBool());

        // Next and Previous move one turn, and the review's position is not
        // the solve's.
        app.click("solution.next");
        REQUIRE(app.wait_for([&]() { return review_position(app) == 1; }, 15'000));
        CHECK(review_state(app) == "stepping");
        CHECK(text_of(app, "solution.caption") ==
              qtTrId("solve.progress").arg(QStringLiteral("2")).arg(QString::number(total)));
        CHECK(app.find("solution.previous")->property("enabled").toBool());
        CHECK(solve.atEnd());
        CHECK(solve.currentCube() == solved);
        app.click("solution.previous");
        REQUIRE(app.wait_for([&]() { return review_position(app) == 0; }, 15'000));
        CHECK(review_state(app) == "overview");

        // A row is a place to jump to; the selected one says so.
        app.click("solution.turn4");
        REQUIRE(app.wait_for([&]() { return review_position(app) == 3; }));
        CHECK(app.find("solution.turn4")->property("selected").toBool());
        CHECK_FALSE(app.find("solution.turn1")->property("selected").toBool());

        // Play steps on by itself to the end, which is the solved cube, and
        // the same button offers to start again.
        quicken_play(app);
        app.click("solution.play");
        CHECK(review_state(app) == "playing");
        CHECK(text_of(app, "solution.play") == qtTrId("solution.pause"));
        REQUIRE(app.wait_for([&]() { return review_state(app) == "end"; }, 60'000));
        app.settle();
        CHECK(review_position(app) == total);
        CHECK(text_of(app, "solution.caption") == qtTrId("solution.atEnd"));
        CHECK(text_of(app, "solution.play") == qtTrId("solution.startAgain"));
        CHECK_FALSE(app.find("solution.next")->property("enabled").toBool());
        // At the end no row is selected, and the last is in view.
        QQuickItem* last = app.find(QStringLiteral("solution.turn%1").arg(total));
        REQUIRE(last != nullptr);
        CHECK_FALSE(last->property("selected").toBool());

        app.click("solution.play");
        CHECK(review_state(app) == "overview");
        CHECK(review_position(app) == 0);

        // The keys: arrows step, Space plays and pauses, Home and End go to
        // the ends.
        app.key(Qt::Key_Right);
        REQUIRE(app.wait_for([&]() { return review_position(app) == 1; }, 15'000));
        app.key(Qt::Key_Left);
        REQUIRE(app.wait_for([&]() { return review_position(app) == 0; }, 15'000));
        app.key(Qt::Key_End);
        CHECK(review_position(app) == total);
        CHECK(review_state(app) == "end");
        // A row near the end is on screen now, and can be pressed.
        app.settle();
        app.click(QStringLiteral("solution.turn%1").arg(total - 2));
        REQUIRE(app.wait_for([&]() { return review_position(app) == total - 3; }));
        app.key(Qt::Key_Home);
        CHECK(review_position(app) == 0);
        app.key(Qt::Key_Space);
        CHECK(review_state(app) == "playing");
        app.key(Qt::Key_Space);
        CHECK(review_state(app) != "playing");
        app.settle();

        // Through all of it the solve stayed where it was left.
        CHECK(solve.atEnd());
        CHECK(solve.currentCube() == solved);

        // Back is the way it came, by the button and by Escape.
        app.click("solution.back");
        REQUIRE(app.wait_for([&]() { return app.screen() == "complete"; }));
        CHECK(app.find("complete.review") != nullptr);
        app.click("complete.review");
        REQUIRE(app.wait_for([&]() { return app.screen() == "solution"; }));
        app.settle();
        app.key(Qt::Key_Escape);
        REQUIRE(app.wait_for([&]() { return app.screen() == "complete"; }));

        const auto warnings = cube::systest::take_qml_warnings();
        for (const auto& warning : warnings) {
            UNSCOPED_INFO(warning.where.toStdString() << ": " << warning.message.toStdString());
        }
        CHECK(warnings.empty());
    }
}

TEST_CASE("every turn of the solution is worded by the way the cube goes", "[system][solution]") {
    // The row's arrow and direction word are worked out from the turn; the
    // sentence a screen reader reads is the catalogue's. They have to agree,
    // for each of the 18, or a row says one thing to the eye and another to
    // the ear.
    App& app = App::fresh(App::Start::elsewhere);
    static_cast<void>(cube::systest::take_qml_warnings());
    app.go("solution");
    app.settle();
    QQuickItem* screen = app.screen_item();
    REQUIRE(screen != nullptr);

    const QRegularExpression clockwise(QStringLiteral("\\bclockwise\\b"));
    for (const char* side : {"U", "D", "L", "R", "F", "B"}) {
        for (const int turns : {1, 2, 3}) {
            INFO(side << turns);
            QVariant direction;
            QVariant sentence;
            REQUIRE(QMetaObject::invokeMethod(
                screen, "directionOf", Q_RETURN_ARG(QVariant, direction),
                Q_ARG(QVariant, QString::fromLatin1(side)), Q_ARG(QVariant, turns)));
            REQUIRE(QMetaObject::invokeMethod(
                screen, "turnSentence", Q_RETURN_ARG(QVariant, sentence),
                Q_ARG(QVariant, QString::fromLatin1(side)), Q_ARG(QVariant, turns)));
            const QString kind = direction.toString();
            const QString said = sentence.toString();
            const QByteArray id =
                ("turn." + QString::fromLatin1(side) + QString::number(turns)).toUtf8();
            REQUIRE_FALSE(said.isEmpty());
            CHECK(said == qtTrId(id.constData()));
            if (turns == 2) {
                CHECK(kind == "either");
                continue;
            }
            if (kind == "cw") {
                CHECK(said.contains(clockwise));
            } else if (kind == "ccw") {
                CHECK(said.contains(qtTrId("dir.ccw")));
            } else {
                REQUIRE((kind == "left" || kind == "right" || kind == "up" || kind == "down"));
                const QByteArray word = ("dir." + kind).toUtf8();
                CHECK(said.contains(qtTrId(word.constData())));
            }
        }
    }

    const auto warnings = cube::systest::take_qml_warnings();
    for (const auto& warning : warnings) {
        UNSCOPED_INFO(warning.where.toStdString() << ": " << warning.message.toStdString());
    }
    CHECK(warnings.empty());
}
