// What the app does today that the experience plan says it should not.
//
// Each case asserts the behaviour as it IS. They are written to start failing
// the day the thing is fixed - which is the prompt to turn the case into a
// proper journey step and delete it from here. Every one names the line of
// docs/app-experience-plan.md it comes from. None of them is fixed by this
// workstream: the experience lane owns them, each in its own phase.
//
// Two cases that used to be here are gone: the solve lane's Finish step now
// reaches the complete screen, and its Rescan is the way out of the solve
// screen. Both are journey steps now (journey_test.cpp).
//
// Tagged [known-today] so `cube_system_tests "[known-today]"` lists them.

#include "app.hpp"

#include "app_runtime.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <QQuickItem>
#include <QQuickWindow>

using cube::systest::App;

TEST_CASE("known today: buttons that do nothing", "[system][known-today]") {
    // EXPECTED TO CHANGE - plan, Journey: "'Solve history', 'Review
    // solution' and the 'English' button do nothing or the wrong thing".
    // Owner: experience lane, E1 decides what stays (decision 6).
    const cube::systest::Layout layout =
        GENERATE(cube::systest::Layout::wide, cube::systest::Layout::narrow);
    INFO("in the " << cube::systest::to_string(layout) << " layout");
    App& app = App::fresh();
    app.resize(layout == cube::systest::Layout::wide ? cube::systest::kDesktop
                                                     : cube::systest::kPhone);

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
