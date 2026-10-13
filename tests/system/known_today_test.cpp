// What the app does today that the experience plan says it should not.
//
// Each case asserts the behaviour as it IS. They are written to start failing
// the day the thing is fixed - which is the prompt to turn the case into a
// proper journey step and delete it from here. Every one names the line of
// docs/app-experience-plan.md it comes from. None of them is fixed by this
// workstream: the solve and scan lanes own the first four, the experience
// lane the rest, each in its own phase.
//
// Tagged [known-today] so `cube_system_tests "[known-today]"` lists them.

#include "app.hpp"
#include "fake_camera.hpp"
#include "scan_script.hpp"

#include "app_runtime.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cube/solver/solver.hpp>

#include <QQuickItem>
#include <QQuickWindow>

using cube::systest::App;
using cube::systest::FakeCamera;

namespace {

// Gets to the solve screen at its last move, the quickest honest way: a cube
// handed straight to the solver, the screen put up as AppState would.
void solved_screen_at_end(App& app) {
    auto& solve = app.runtime().solve();
    solve.solveCube(cube::solver::random_scramble(5));
    REQUIRE(app.wait_for([&]() { return !solve.isSolving(); }, 180'000));
    app.go("solve");
    app.settle();
    for (int presses = 0; !solve.atEnd() && presses <= solve.totalMoves(); ++presses) {
        app.click_button("Next move");
    }
    REQUIRE(solve.atEnd());
}

}  // namespace

TEST_CASE("known today: the solve screen has no way out", "[system][known-today]") {
    // EXPECTED TO CHANGE - plan, "What was found" / Journey: "the Solve
    // screen has no way out - no back, no cancel, no scan another". Owner:
    // solve lane (SolveScreen.qml), then E3d.
    App& app = App::fresh();
    solved_screen_at_end(app);

    // The only controls are Previous and a disabled "Solved!".
    QStringList labels;
    for (QQuickItem* control : app.visible_controls()) {
        labels << control->property("text").toString();
    }
    labels.sort();
    CHECK(labels == QStringList({"Previous", "Solved!"}));
    CHECK_FALSE(app.find_button("Solved!")->property("enabled").toBool());

    // Pressing every key a person might reach for goes nowhere.
    for (const Qt::Key key : {Qt::Key_Escape, Qt::Key_Back, Qt::Key_Return, Qt::Key_Space}) {
        app.key(key);
        CHECK(app.screen() == "solve");
    }
}

TEST_CASE("known today: nothing ever leads to the complete screen", "[system][known-today]") {
    // EXPECTED TO CHANGE - plan, Journey: "Nothing ever navigates to the
    // Complete screen". Owner: solve lane. The screen itself draws (the tour
    // puts it up with App::go); there is just no way to arrive.
    App& app = App::fresh();
    solved_screen_at_end(app);
    app.pump(500);
    CHECK(app.screen() == "solve");
    CHECK_FALSE(app.find_button("Scan another") != nullptr);
    CHECK_FALSE(app.find_button("Review solution") != nullptr);
}

TEST_CASE("known today: buttons that do nothing", "[system][known-today]") {
    // EXPECTED TO CHANGE - plan, Journey: "'Solve history', 'Review
    // solution' and the 'English' button do nothing or the wrong thing".
    // Owner: experience lane, E1 decides what stays (decision 6).
    App& app = App::fresh();

    app.click("launch.solveHistory");
    app.pump(200);
    CHECK(app.screen() == "launch");

    // "English" is a language picker with one language; it opens settings.
    app.click("launch.language");
    REQUIRE(app.wait_for([&]() { return app.screen() == "settings"; }));
    app.go("launch");

    app.go("complete");
    app.click("complete.review");
    app.pump(200);
    CHECK(app.screen() == "complete");
}

TEST_CASE("known today: there is no back, and nothing takes keyboard focus",
          "[system][known-today]") {
    // EXPECTED TO CHANGE - plan, Journey ("No back handling (Android back,
    // Esc)") and Accessibility ("buttons are Item + MouseArea, not
    // focusable"). Owner: experience lane, E2a.
    App& app = App::fresh();
    app.click("launch.settings");
    REQUIRE(app.wait_for([&]() { return app.screen() == "settings"; }));

    for (const Qt::Key key : {Qt::Key_Escape, Qt::Key_Back}) {
        app.key(key);
        CHECK(app.screen() == "settings");
    }

    // Tab walks through nothing: no control is a focus item.
    for (int i = 0; i < 4; ++i) {
        app.key(Qt::Key_Tab);
    }
    QQuickItem* focused = app.window().activeFocusItem();
    const bool on_a_control =
        focused != nullptr &&
        QByteArray(focused->metaObject()->className()).startsWith("AppButton");
    CHECK_FALSE(on_a_control);
}

TEST_CASE("known today: settings that drive a controller no screen uses", "[system][known-today]") {
    // EXPECTED TO CHANGE - plan, Settings: "'Detect hands with AI' and
    // 'Auto-advance' drive ScanSession, which no screen starts". Owner:
    // scan lane (R6 removes ScanSession) and E3a.
    App& app = App::fresh();
    auto& legacy = app.runtime().scanSession();
    app.click("launch.settings");
    REQUIRE(app.wait_for([&]() { return app.screen() == "settings"; }));

    const bool before = legacy.autoAdvance();
    app.click("settings.autoAdvance");
    CHECK(legacy.autoAdvance() == !before);

    // A whole scan goes by without it being consulted: the scan screen's
    // controller is DirectScan, and the legacy session never runs.
    app.go("launch");
    app.click("launch.startScan");
    REQUIRE(app.wait_for([&]() { return app.runtime().directScan().running(); }));
    CHECK_FALSE(legacy.running());
}
