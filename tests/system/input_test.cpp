// The same app, driven by other kinds of input: a finger, then a keyboard.

#include "app.hpp"
#include "fake_camera.hpp"
#include "reach.hpp"

#include "app_runtime.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <cube/solver/solver.hpp>

#include <QQuickItem>
#include <QQuickWindow>
#include <QtTest/QTest>

using cube::systest::App;
using cube::systest::FakeCamera;

TEST_CASE("touch drives the launch, scan and settings screens like the mouse does",
          "[system][input][touch]") {
    App& app = App::fresh();
    app.resize(cube::systest::kPhone);

    app.tap("launch.settings");
    REQUIRE(app.wait_for([&]() { return app.screen() == "settings"; }));
    app.settle();
    app.tap("settings.theme.2");
    REQUIRE(app.wait_for([&]() { return app.theme().property("effectiveDark").toBool(); }));
    app.tap("settings.theme.1");
    REQUIRE(app.wait_for([&]() { return !app.theme().property("effectiveDark").toBool(); }));
    app.tap("settings.back");
    REQUIRE(app.wait_for([&]() { return app.screen() == "launch"; }));

    app.tap("launch.startScan");
    REQUIRE(app.wait_for([&]() { return app.screen() == "scan"; }));
    REQUIRE(app.wait_for([&]() { return app.runtime().directScan().running(); }));
    QQuickItem* cancel = app.find_button("Cancel");
    REQUIRE(cancel != nullptr);
    app.tap(cancel);
    REQUIRE(app.wait_for([&]() { return app.screen() == "launch"; }));
    REQUIRE(app.wait_for([&]() { return !FakeCamera::instance().capturing(); }));
}

TEST_CASE("the 3D cube turns under a drag, on every screen that has one", "[system][input][cube]") {
    const cube::systest::Layout layout =
        GENERATE(cube::systest::Layout::wide, cube::systest::Layout::narrow);
    DYNAMIC_SECTION("in the " << cube::systest::to_string(layout) << " layout") {
        App& app = App::fresh();
        app.resize(layout == cube::systest::Layout::wide ? cube::systest::kDesktop
                                                         : cube::systest::kPhone);
        // Idle spin off, so any turn seen is the drag's.
        app.settings_state().setProperty("cubeAutoRotate", false);
        app.settle();

        // The launch screen's.
        REQUIRE(app.cube_in_view() != nullptr);
        CHECK(app.drag_cube() > 200);

        // The settings preview, which only the wide layout has.
        app.click("launch.settings");
        REQUIRE(app.wait_for([&]() { return app.screen() == "settings"; }));
        app.settle();
        if (layout == cube::systest::Layout::wide) {
            REQUIRE(app.cube_in_view() != nullptr);
            CHECK(app.drag_cube() > 200);
        } else {
            CHECK(app.cube_in_view() == nullptr);
        }
        app.click("settings.back");
        REQUIRE(app.wait_for([&]() { return app.screen() == "launch"; }));

        // The solve screen's: dragging turns the view to look round the back.
        auto& solve = app.runtime().solve();
        if (solve.totalMoves() == 0) {
            solve.solveCube(cube::systest::scramble(31));
        }
        REQUIRE(app.wait_for([&]() { return !solve.isSolving(); }, 180'000));
        app.go("solve");
        app.settle();
        REQUIRE(app.find("solve.cube") != nullptr);
        CHECK(app.drag_cube() > 200);
    }
}
