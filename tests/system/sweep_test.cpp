// Every control on every screen, pressed, in both layouts.
//
// The journey cases press what the journey needs. These press the rest, so
// that a control which is on screen is also one a test has used - which is
// what the reach gate asks - and so that none of them takes the app down.

#include "sweep.hpp"
#include "app.hpp"

#include "app_runtime.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <cube/solver/solver.hpp>

#include <QQuickItem>

namespace {

using cube::systest::App;
using cube::systest::Layout;
using cube::systest::press_every_control;

[[nodiscard]] QSize size_for(Layout layout) {
    return layout == Layout::wide ? cube::systest::kDesktop : cube::systest::kPhone;
}

}  // namespace

TEST_CASE("every control on the launch, permission and complete screens can be pressed",
          "[system][sweep]") {
    const Layout layout = GENERATE(Layout::narrow, Layout::wide);
    const std::string name = GENERATE(as<std::string>{}, "launch", "permission", "complete");
    const QString screen = QString::fromStdString(name);
    DYNAMIC_SECTION(name << " in the " << cube::systest::to_string(layout) << " layout") {
        App& app = App::fresh(name == "launch" ? App::Start::launch : App::Start::elsewhere);
        app.resize(size_for(layout));
        app.go(screen);
        app.pump(80);

        const QStringList pressed = press_every_control(app, screen);
        INFO("pressed " << pressed.join(", ").toStdString());
        CHECK(pressed.size() >= 2);
        // Still a screen of the app, and the window still draws.
        CHECK(app.screen() == screen);
        CHECK_FALSE(app.grab().isNull());
    }
}

TEST_CASE("every control on the solve screen can be pressed", "[system][sweep]") {
    const Layout layout = GENERATE(Layout::narrow, Layout::wide);
    DYNAMIC_SECTION("in the " << cube::systest::to_string(layout) << " layout") {
        App& app = App::fresh(App::Start::elsewhere);
        auto& solve = app.runtime().solve();
        if (solve.totalMoves() == 0) {
            solve.solveCube(cube::systest::scramble(41));
        }
        REQUIRE(app.wait_for([&]() { return !solve.isSolving(); }, 180'000));
        app.resize(size_for(layout));
        app.go("solve");
        app.pump(120);

        // One step in, so Previous has something to undo: a step lands when
        // its turn has played, which a sweep that moves straight on from
        // Next would leave behind (Rescan takes the screen away).
        app.click("solve.next");
        REQUIRE(app.wait_for([&]() { return solve.moveIndex() >= 1; }, 15'000));

        // A method button solves on from the cube in hand, which starts a new
        // solution at its first turn: Previous would have nothing to undo for
        // the rest of the sweep. So after one, wait for the new solution and
        // go one step in again, as above.
        cube::systest::SweepOptions options;
        options.after_press = [&app, &solve](const QString& key) {
            if (key.startsWith("solve.method.")) {
                REQUIRE(app.wait_for([&]() { return !solve.isSolving(); }, 60'000));
                app.click("solve.next");
                REQUIRE(app.wait_for([&]() { return solve.moveIndex() >= 1; }, 15'000));
            }
        };
        const QStringList pressed = press_every_control(app, "solve", options);
        INFO("pressed " << pressed.join(", ").toStdString());
        // Next, Previous (enabled by Next), the three methods, Rescan and the cube.
        CHECK(pressed.contains("solve.next"));
        CHECK(pressed.contains("solve.previous"));
        CHECK(pressed.contains("solve.method.0"));
        CHECK(pressed.contains("solve.method.1"));
        CHECK(pressed.contains("solve.method.2"));
        CHECK(pressed.contains("solve.rescan"));
        CHECK(app.screen() == "solve");
    }
}

TEST_CASE("every control on the solution screen can be pressed", "[system][sweep][solution]") {
    const Layout layout = GENERATE(Layout::narrow, Layout::wide);
    DYNAMIC_SECTION("in the " << cube::systest::to_string(layout) << " layout") {
        App& app = App::fresh(App::Start::elsewhere);
        auto& solve = app.runtime().solve();
        if (solve.totalMoves() == 0) {
            solve.solveCube(cube::systest::scramble(41));
        }
        REQUIRE(app.wait_for([&]() { return !solve.isSolving(); }, 180'000));
        app.resize(size_for(layout));
        app.go("solution");
        app.pump(120);

        // Back leaves, and has its case of its own (journey_test.cpp); Play
        // is pressed, and the Next pressed after it stops what it started.
        // Previous is disabled until Next has been pressed: a second pass.
        cube::systest::SweepOptions options;
        options.skip.insert("solution.back");
        const QStringList pressed = press_every_control(app, "solution", options);
        INFO("pressed " << pressed.join(", ").toStdString());
        CHECK(pressed.contains("solution.next"));
        CHECK(pressed.contains("solution.previous"));
        CHECK(pressed.contains("solution.play"));
        // A row of the list: one key for all of them (App::control_key).
        CHECK(pressed.contains("solution.list.row"));
        CHECK(app.screen() == "solution");
        CHECK_FALSE(app.grab().isNull());
    }
}

TEST_CASE("every control on the settings screen can be pressed", "[system][sweep][settings]") {
    const Layout layout = GENERATE(Layout::narrow, Layout::wide);
    DYNAMIC_SECTION("in the " << cube::systest::to_string(layout) << " layout") {
        App& app = App::fresh(App::Start::elsewhere);
        app.resize(size_for(layout));
        app.go("settings");
        app.pump(80);

        // The speed chips are enabled only while the cube turns by itself.
        app.settings_state().setProperty("cubeAutoRotate", true);

        // "Back" leaves; it has a case of its own (settings_test.cpp).
        cube::systest::SweepOptions options;
        options.skip.insert("settings.back");
        // Each language chip switches the whole app; English again, so the
        // rest of the sweep (and the checks after it) read what they expect.
        options.after_press = [&app](const QString& key) {
            if (key.startsWith("settings.language.")) {
                app.reset_language();
            }
        };
        const QStringList pressed = press_every_control(app, "settings", options);
        INFO("pressed " << pressed.join(", ").toStdString());
        REQUIRE(pressed.size() > 10);
        CHECK(app.screen() == "settings");

        // The last press in each chip row stuck.
        CHECK(app.settings_state().property("shaderStyleIndex").toInt() == 2);
        CHECK(app.settings_state().property("rotateSpeedIndex").toInt() == 2);
        CHECK(app.settings_state().property("themeMode").toString() == "dark");
        CHECK(app.settings_state().property("solveMethod").toInt() == 2);
        // The toggles were pressed twice: back where they started.
        CHECK(app.settings_state().property("cubeAutoRotate").toBool());
        CHECK_FALSE(app.settings_state().property("statsForNerds").toBool());
    }
}
