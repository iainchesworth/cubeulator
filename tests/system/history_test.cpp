// History: the button on Home that used to do nothing, the list it opens, and
// the two switches in Settings that go with it (docs/design/screens/history.md).
//
// Solves are kept the way the app keeps them - the solve controller stepped
// through a cube, the complete screen opened - rather than by writing the
// history's file: what is under test is that a person's solve arrives in the
// list, and that what the list offers works.
//
// The camera is the fake one; no picture is taken, kept or looked at here.

#include "app.hpp"
#include "diagnostics.hpp"
#include "fake_camera.hpp"

#include "app_runtime.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <cube/nxn/cube.hpp>
#include <cube/solver/solver.hpp>

#include <QQuickItem>
#include <QQuickWindow>
#include <QSettings>
#include <QVariantMap>

namespace {

using cube::systest::App;
using cube::systest::FakeCamera;
using cube::systest::Layout;

[[nodiscard]] QSize size_for(Layout layout) {
    return layout == Layout::wide ? cube::systest::kDesktop : cube::systest::kPhone;
}

// Keeps a solve as a person would have: scanned, solved, `turns` turns taken -
// or all of them, and the complete screen opened, if `finish`. `seconds` is
// the clock the complete screen would show.
void keep_a_solve(App& app, unsigned seed, int turns, bool finish, int seconds) {
    auto& solve = app.runtime().solve();
    solve.solveCube(cube::systest::scramble(seed));
    REQUIRE(app.wait_for([&]() { return !solve.isSolving(); }, 180'000));
    app.app_state().setProperty("elapsedSeconds", seconds);
    for (int i = 0; i < turns; ++i) {
        solve.nextMove();
    }
    if (finish) {
        while (!solve.atEnd()) {
            solve.nextMove();
        }
        app.go("complete");
        app.go("launch");
    }
}

// The same for a cube of another size, scrambled by layer turns.
void keep_a_big_solve(App& app, int size, unsigned seed, int turns, bool finish, int seconds) {
    auto& solve = app.runtime().solve();
    solve.solveCube(
        cube::nxn::random_scramble({.size = size, .seed = seed, .moves = 40 + (8 * size)}));
    REQUIRE(app.wait_for([&]() { return !solve.isSolving(); }, 180'000));
    app.app_state().setProperty("elapsedSeconds", seconds);
    for (int i = 0; i < turns; ++i) {
        solve.nextMove();
    }
    if (finish) {
        while (!solve.atEnd()) {
            solve.nextMove();
        }
        app.go("complete");
        app.go("launch");
    }
}

[[nodiscard]] bool is_a_control(App& app, const QString& object_name) {
    for (QQuickItem* control : app.visible_controls()) {
        if (app.control_key(control) == object_name) {
            return true;
        }
    }
    return false;
}

void open_history(App& app) {
    app.click("launch.solveHistory");
    REQUIRE(app.wait_for([&]() { return app.screen() == "history"; }));
    app.pump(80);
}

}  // namespace

TEST_CASE("History opens from Home, says so when nothing is kept, and goes back",
          "[system][history]") {
    const Layout layout = GENERATE(Layout::wide, Layout::narrow);
    DYNAMIC_SECTION("in the " << cube::systest::to_string(layout) << " layout") {
        App& app = App::fresh();
        static_cast<void>(cube::systest::take_qml_warnings());
        app.resize(size_for(layout));
        REQUIRE(app.runtime().history().count() == 0);

        // The button that used to do nothing.
        open_history(app);
        CHECK(app.find("history.title") != nullptr);
        CHECK(app.find_text("History") != nullptr);
        // history.empty: the words, the way on, and nothing to clear.
        CHECK(app.find("history.empty") != nullptr);
        CHECK(app.find_text("No solves yet") != nullptr);
        CHECK(app.find("history.list") == nullptr);
        CHECK(app.find("history.off") == nullptr);
        CHECK(app.find("history.clear") == nullptr);
        CHECK(app.find("history.back") != nullptr);

        app.click("history.back");
        REQUIRE(app.wait_for([&]() { return app.screen() == "launch"; }));

        // Scan a cube, there, does what it does on Home.
        open_history(app);
        app.click("history.empty.scan");
        REQUIRE(app.wait_for([&]() { return app.screen() == "scan"; }));
        REQUIRE(app.wait_for([&]() { return app.runtime().directScan().running(); }));

        const auto warnings = cube::systest::take_qml_warnings();
        for (const auto& warning : warnings) {
            UNSCOPED_INFO(warning.where.toStdString() << ": " << warning.message.toStdString());
        }
        CHECK(warnings.empty());
    }
}

TEST_CASE("solves are listed newest first, and deleted or cleared only after asking",
          "[system][history]") {
    const Layout layout = GENERATE(Layout::wide, Layout::narrow);
    DYNAMIC_SECTION("in the " << cube::systest::to_string(layout) << " layout") {
        App& app = App::fresh();
        static_cast<void>(cube::systest::take_qml_warnings());
        app.resize(size_for(layout));
        auto& history = app.runtime().history();

        keep_a_solve(app, 31, 0, true, 760);  // finished, 12:40
        const int finished_turns = app.runtime().solve().totalMoves();
        keep_a_solve(app, 32, 2, false, 125);  // left at turn 2
        const int unfinished_turns = app.runtime().solve().totalMoves();
        REQUIRE(history.count() == 2);

        open_history(app);
        CHECK(app.find("history.list") != nullptr);
        CHECK(app.find("history.empty") == nullptr);
        CHECK(app.find("history.clear") != nullptr);

        // Newest first; each says what it is.
        CHECK(app.find("history.row1") != nullptr);
        CHECK(app.find("history.row2") != nullptr);
        CHECK(app.find_text(QStringLiteral("Unfinished · turn 2 of %1").arg(unfinished_turns)) !=
              nullptr);
        CHECK(app.find_text(QStringLiteral("%1 turns · 12:40").arg(finished_turns)) != nullptr);

        // Every row goes somewhere: the unfinished one carries on, the
        // finished one opens its solution (below).
        CHECK(is_a_control(app, "history.row1"));
        CHECK(is_a_control(app, "history.row2"));
        CHECK(is_a_control(app, "history.delete1"));
        CHECK(is_a_control(app, "history.delete2"));

        // The finished one opens the solution to that cube, worked out again,
        // under the date it was solved; back comes back to the list.
        app.click("history.row2");
        REQUIRE(app.wait_for([&]() { return app.screen() == "solution"; }));
        QObject* reviewed = qvariant_cast<QObject*>(app.app_state().property("solutionSource"));
        REQUIRE(reviewed != nullptr);
        REQUIRE(app.wait_for([&]() { return !reviewed->property("isSolving").toBool(); }, 60'000));
        CHECK(reviewed != &app.runtime().solve());
        CHECK(reviewed->property("totalMoves").toInt() == finished_turns);
        app.settle();
        CHECK(app.find("solution.subtitle") != nullptr);
        CHECK(app.find("solution.cube") != nullptr);
        // The solve being followed was not touched by the review.
        CHECK(app.runtime().solve().totalMoves() == unfinished_turns);
        CHECK(app.runtime().solve().moveIndex() == 2);
        app.click("solution.back");
        REQUIRE(app.wait_for([&]() { return app.screen() == "history"; }));
        CHECK(app.find("history.row2") != nullptr);

        // Delete asks first, and the safe choice leaves it be.
        CHECK(app.find("history.deleteDialog") == nullptr);
        app.click("history.delete2");
        CHECK(app.find("history.deleteDialog") != nullptr);
        CHECK(app.find_text("Delete this solve?") != nullptr);
        CHECK(history.count() == 2);
        app.click("history.deleteDialog.keep");
        CHECK(app.find("history.deleteDialog") == nullptr);
        CHECK(history.count() == 2);

        // Delete the finished one.
        app.click("history.delete2");
        app.click("history.deleteDialog.confirm");
        CHECK(app.find("history.deleteDialog") == nullptr);
        REQUIRE(history.count() == 1);
        CHECK(history.entries().first().toMap().value("finished").toBool() == false);
        // A screen reader is told.
        QQuickItem* screen = app.screen_item();
        REQUIRE(screen != nullptr);
        CHECK(screen->property("announcement").toString() == "Solve deleted.");
        CHECK(app.find("history.row2") == nullptr);

        // Clear asks first, and says how many.
        app.click("history.clear");
        CHECK(app.find("history.clearDialog") != nullptr);
        CHECK(app.find_text("Clear all history?") != nullptr);
        CHECK(app.find_text("1 solve will be deleted from this device.") != nullptr);
        app.click("history.clearDialog.keep");
        CHECK(app.find("history.clearDialog") == nullptr);
        CHECK(history.count() == 1);

        app.click("history.clear");
        app.click("history.clearDialog.confirm");
        CHECK(app.find("history.clearDialog") == nullptr);
        CHECK(history.count() == 0);
        // The unfinished solve went too, and with it anything to carry on.
        CHECK(history.carryOnId() == 0);
        // And the screen is the empty state.
        CHECK(app.find("history.empty") != nullptr);
        CHECK(app.find("history.list") == nullptr);
        CHECK(app.find("history.clear") == nullptr);
        CHECK(screen->property("announcement").toString() == "History cleared.");

        const auto warnings = cube::systest::take_qml_warnings();
        for (const auto& warning : warnings) {
            UNSCOPED_INFO(warning.where.toStdString() << ": " << warning.message.toStdString());
        }
        CHECK(warnings.empty());
    }
}

TEST_CASE("an unfinished solve is carried on from the turn it was left at", "[system][history]") {
    const Layout layout = GENERATE(Layout::wide, Layout::narrow);
    DYNAMIC_SECTION("in the " << cube::systest::to_string(layout) << " layout") {
        App& app = App::fresh();
        static_cast<void>(cube::systest::take_qml_warnings());
        app.resize(size_for(layout));
        auto& solve = app.runtime().solve();
        auto& history = app.runtime().history();

        // An older one, left; then the one that can be carried on.
        keep_a_solve(app, 33, 1, false, 30);
        keep_a_solve(app, 34, 3, false, 125);
        const cube::solver::Cube at_turn_three = solve.currentCube();
        const int total = solve.totalMoves();
        const auto method = solve.method();
        REQUIRE(history.count() == 2);

        // Something else is in the controller, as it would be after a restart
        // or another scan: the resume has to come from the history.
        solve.solveCube(cube::systest::scramble(99));
        REQUIRE(app.wait_for([&]() { return !solve.isSolving(); }, 180'000));
        CHECK(history.count() == 2);

        open_history(app);
        // Only the newest unfinished one carries on. The older is listed, and
        // opens its solution for review instead: another scan has left it
        // behind.
        CHECK(is_a_control(app, "history.row1"));
        CHECK(is_a_control(app, "history.row2"));
        app.click("history.row2");
        REQUIRE(app.wait_for([&]() { return app.screen() == "solution"; }));
        CHECK(solve.moveIndex() == 0);  // the controller's solve is the 99 seed's, untouched
        app.click("solution.back");
        REQUIRE(app.wait_for([&]() { return app.screen() == "history"; }));

        app.click("history.row1");
        REQUIRE(app.wait_for([&]() { return app.screen() == "solve"; }));
        REQUIRE(app.wait_for([&]() { return !solve.isSolving(); }, 180'000));
        app.settle();

        // The same cube, the same method, the same turn.
        CHECK(solve.method() == method);
        CHECK(solve.totalMoves() == total);
        CHECK(solve.moveIndex() == 3);
        CHECK(solve.currentCube() == at_turn_three);
        // The clock carries on from the time it had, and there is no scan this
        // session for the camera to be matched against.
        const int clock = app.app_state().property("elapsedSeconds").toInt();
        CHECK(clock >= 125);
        CHECK(clock < 125 + 60);
        CHECK_FALSE(FakeCamera::instance().capturing());
        // It is the same entry, still the one that carries on.
        CHECK(history.count() == 2);

        // The next turn is the entry's next turn.
        app.click("solve.next");
        REQUIRE(app.wait_for([&]() { return solve.moveIndex() == 4; }, 15'000));
        CHECK(history.entries().first().toMap().value("turn").toInt() == 4);
        CHECK(history.count() == 2);

        // And finishing finishes that entry, with the time that was on show.
        while (!solve.atEnd()) {
            solve.nextMove();
        }
        app.click("solve.next");
        REQUIRE(app.wait_for([&]() { return app.screen() == "complete"; }));
        REQUIRE(history.count() == 2);
        const QVariantMap done = history.entries().first().toMap();
        CHECK(done.value("finished").toBool());
        CHECK(done.value("turn").toInt() == total);
        CHECK(done.value("seconds").toInt() >= 125);

        // What is left to carry on is the older one, which now can be: it is
        // the newest unfinished solve.
        CHECK(history.carryOnId() != 0);
        app.go("launch");
        open_history(app);
        CHECK(history.entries().at(0).toMap().value("carryOn").toBool() == false);
        CHECK(history.entries().at(1).toMap().value("carryOn").toBool());

        // Pressing it carries on.
        app.click("history.row2");
        REQUIRE(app.wait_for([&]() { return app.screen() == "solve"; }));
        REQUIRE(app.wait_for([&]() { return !solve.isSolving(); }, 180'000));
        CHECK(solve.moveIndex() == 1);
        CHECK(solve.scannedCube().has_value());
        CHECK(*solve.scannedCube() == cube::systest::scramble(33));

        const auto warnings = cube::systest::take_qml_warnings();
        for (const auto& warning : warnings) {
            UNSCOPED_INFO(warning.where.toStdString() << ": " << warning.message.toStdString());
        }
        CHECK(warnings.empty());
    }
}

TEST_CASE("a solve scanned again after carrying on starts a history entry of its own",
          "[system][history]") {
    App& app = App::fresh();
    auto& solve = app.runtime().solve();
    auto& history = app.runtime().history();

    keep_a_solve(app, 35, 2, false, 50);
    REQUIRE(history.count() == 1);

    // Another cube: scanned, solved, and its first turn taken.
    keep_a_solve(app, 36, 1, false, 10);
    REQUIRE(history.count() == 2);
    // The first is as it was left; the second is the one to carry on.
    const QVariantMap newest = history.entries().first().toMap();
    const QVariantMap older = history.entries().last().toMap();
    CHECK(newest.value("turn").toInt() == 1);
    CHECK(older.value("turn").toInt() == 2);
    CHECK_FALSE(older.value("finished").toBool());
    CHECK(newest.value("carryOn").toBool());
    CHECK_FALSE(older.value("carryOn").toBool());
    CHECK(solve.moveIndex() == 1);
}

TEST_CASE("Settings can switch history off and clear it, and History says so",
          "[system][history][settings]") {
    const Layout layout = GENERATE(Layout::wide, Layout::narrow);
    DYNAMIC_SECTION("in the " << cube::systest::to_string(layout) << " layout") {
        App& app = App::fresh(App::Start::elsewhere);
        static_cast<void>(cube::systest::take_qml_warnings());
        app.resize(size_for(layout));
        auto& history = app.runtime().history();
        REQUIRE(history.keep());

        keep_a_solve(app, 37, 2, false, 40);
        REQUIRE(history.count() == 1);

        app.go("settings");
        app.pump(80);
        // On, with something to clear.
        QQuickItem* clear = app.find("settings.clearHistory");
        REQUIRE(clear != nullptr);
        CHECK(clear->property("enabled").toBool());
        CHECK(app.find("settings.keepHistory") != nullptr);
        CHECK(app.find_text("Keep a history of solves") != nullptr);

        // Off: nothing more is added, and nothing is deleted.
        app.click("settings.keepHistory");
        REQUIRE(app.wait_for([&]() { return !history.keep(); }));
        QSettings store;
        store.sync();
        CHECK_FALSE(store.value("history/keep", true).toBool());
        keep_a_solve(app, 38, 1, false, 5);
        CHECK(history.count() == 1);

        // History says it is off, with what was kept still listed beneath.
        app.go("history");
        app.pump(80);
        CHECK(app.find("history.off") != nullptr);
        CHECK(app.find_text("History is off") != nullptr);
        CHECK(app.find("history.list") != nullptr);
        CHECK(app.find("history.empty") == nullptr);
        // Open Settings goes to Settings, and back comes back here.
        app.click("history.off.open");
        REQUIRE(app.wait_for([&]() { return app.screen() == "settings"; }));
        app.click("settings.back");
        REQUIRE(app.wait_for([&]() { return app.screen() == "history"; }));

        // Clear history, from Settings, asks first.
        app.go("settings");
        app.pump(80);
        app.click("settings.clearHistory");
        CHECK(app.find("settings.clearDialog") != nullptr);
        CHECK(app.find_text("1 solve will be deleted from this device.") != nullptr);
        app.click("settings.clearDialog.keep");
        CHECK(app.find("settings.clearDialog") == nullptr);
        CHECK(history.count() == 1);
        app.click("settings.clearHistory");
        app.click("settings.clearDialog.confirm");
        CHECK(app.find("settings.clearDialog") == nullptr);
        CHECK(history.count() == 0);
        // Nothing to clear now.
        clear = app.find("settings.clearHistory");
        REQUIRE(clear != nullptr);
        CHECK_FALSE(clear->property("enabled").toBool());

        // Off with nothing kept: the empty state is the off one.
        app.go("history");
        app.pump(80);
        CHECK(app.find("history.off") != nullptr);
        CHECK(app.find("history.empty") == nullptr);
        CHECK(app.find("history.list") == nullptr);
        app.click("history.off.open");
        REQUIRE(app.wait_for([&]() { return app.screen() == "settings"; }));

        // On again; the next solve is kept.
        app.click("settings.keepHistory");
        REQUIRE(app.wait_for([&]() { return history.keep(); }));
        keep_a_solve(app, 39, 1, false, 5);
        CHECK(history.count() == 1);

        const auto warnings = cube::systest::take_qml_warnings();
        for (const auto& warning : warnings) {
            UNSCOPED_INFO(warning.where.toStdString() << ": " << warning.message.toStdString());
        }
        CHECK(warnings.empty());
    }
}

TEST_CASE("back and Esc leave History, and close a dialog first", "[system][history]") {
    App& app = App::fresh();
    keep_a_solve(app, 40, 1, false, 5);
    open_history(app);

    // A dialog up: Esc is its safe choice, and the screen stays.
    app.click("history.delete1");
    REQUIRE(app.find("history.deleteDialog") != nullptr);
    app.key(Qt::Key_Escape);
    CHECK(app.wait_for([&]() { return app.find("history.deleteDialog") == nullptr; }, 3000));
    CHECK(app.screen() == "history");
    CHECK(app.runtime().history().count() == 1);

    // Then Esc leaves.
    app.key(Qt::Key_Escape);
    CHECK(app.wait_for([&]() { return app.screen() == "launch"; }, 3000));
}

TEST_CASE("a cube of another size is listed with its size, reviewed and carried on as it was kept",
          "[system][history]") {
    const Layout layout = GENERATE(Layout::wide, Layout::narrow);
    DYNAMIC_SECTION("in the " << cube::systest::to_string(layout) << " layout") {
        App& app = App::fresh();
        static_cast<void>(cube::systest::take_qml_warnings());
        app.resize(size_for(layout));
        auto& solve = app.runtime().solve();
        auto& history = app.runtime().history();

        // A finished 4x4, then a 3x3 and a 5x5 left part-way.
        keep_a_big_solve(app, 4, 71, 0, true, 760);  // 12:40
        const int four_turns = solve.totalMoves();
        keep_a_solve(app, 35, 2, false, 90);
        const int three_turns = solve.totalMoves();
        keep_a_big_solve(app, 5, 72, 3, false, 125);
        const int five_turns = solve.totalMoves();
        const auto five_moves = solve.plan().layer_turns();
        REQUIRE(history.count() == 3);

        // Something else is in the controller, as after a restart.
        solve.solveCube(cube::systest::scramble(99));
        REQUIRE(app.wait_for([&]() { return !solve.isSolving(); }, 180'000));

        open_history(app);
        // A 3x3 says nothing of its size; the others do.
        CHECK(app.find_text(QStringLiteral("5×5 · Unfinished · turn 3 of %1").arg(five_turns)) !=
              nullptr);
        CHECK(app.find_text(QStringLiteral("4×4 · %1 turns · 12:40").arg(four_turns)) != nullptr);
        CHECK(app.find_text(QStringLiteral("Unfinished · turn 2 of %1").arg(three_turns)) !=
              nullptr);

        // The 4x4 opens its solution as it was kept: there at once, in the view
        // that draws a 4x4.
        app.click("history.row3");
        REQUIRE(app.wait_for([&]() { return app.screen() == "solution"; }));
        QObject* reviewed = qvariant_cast<QObject*>(app.app_state().property("solutionSource"));
        REQUIRE(reviewed != nullptr);
        CHECK_FALSE(reviewed->property("isSolving").toBool());
        CHECK(reviewed->property("size").toInt() == 4);
        CHECK(reviewed->property("totalMoves").toInt() == four_turns);
        app.settle();
        QQuickItem* cube = app.find("solution.cube");
        REQUIRE(cube != nullptr);
        CHECK(cube->property("cubeSize").toInt() == 4);
        CHECK(app.find("solution.turn1") != nullptr);
        CHECK(solve.size() == 3);  // the controller's own solve is untouched
        app.click("solution.back");
        REQUIRE(app.wait_for([&]() { return app.screen() == "history"; }));

        // The 5x5 carries on from turn 3, on the solution it kept.
        app.click("history.row1");
        REQUIRE(app.wait_for([&]() { return app.screen() == "solve"; }));
        CHECK_FALSE(solve.isSolving());
        app.settle();
        CHECK(solve.size() == 5);
        CHECK(solve.moveIndex() == 3);
        CHECK(solve.totalMoves() == five_turns);
        CHECK(solve.plan().layer_turns() == five_moves);
        QQuickItem* five = app.find("solve.cube");
        REQUIRE(five != nullptr);
        CHECK(five->property("cubeSize").toInt() == 5);
        CHECK(app.find("solve.rescan") == nullptr);

        // The next turn is the entry's next turn.
        app.click("solve.next");
        REQUIRE(app.wait_for([&]() { return solve.moveIndex() == 4; }, 15'000));
        CHECK(history.entries().first().toMap().value("turn").toInt() == 4);
        CHECK(history.count() == 3);

        const auto warnings = cube::systest::take_qml_warnings();
        for (const auto& warning : warnings) {
            UNSCOPED_INFO(warning.where.toStdString() << ": " << warning.message.toStdString());
        }
        CHECK(warnings.empty());
    }
}
