// The two layouts either side of Theme.wideBreakpoint, and whether what each
// shows fits where it is shown.

#include "app.hpp"

#include "app_runtime.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <cube/solver/solver.hpp>

#include <QQuickItem>
#include <QQuickWindow>

#include <vector>

namespace {

using cube::systest::App;
using cube::systest::Layout;

struct Screen {
    const char* name;
    // The screen is put up by App::go, with whatever it needs behind it.
    bool needs_solution;
};

constexpr Screen kScreens[] = {
    {"launch", false}, {"permission", false}, {"scan", false},
    {"solve", true},   {"settings", false},   {"complete", false},
};

// Puts `screen` up the way the tests that cannot walk to it do.
void show(App& app, const Screen& screen) {
    if (screen.needs_solution) {
        // One solution serves every size; the controller keeps it.
        auto& solve = app.runtime().solve();
        if (solve.totalMoves() == 0) {
            solve.solveCube(cube::solver::random_scramble(21));
        }
        REQUIRE(app.wait_for([&]() { return !solve.isSolving(); }, 180'000));
    }
    app.go(screen.name);
    REQUIRE(app.screen() == screen.name);
    app.settle();
}

}  // namespace

TEST_CASE("the breakpoint is where Theme says, and every screen follows it", "[system][layout]") {
    const int breakpoint = App::fresh().theme().property("wideBreakpoint").toInt();
    REQUIRE(breakpoint == 720);

    for (const Screen& screen : kScreens) {
        App& app = App::fresh();
        show(app, screen);
        INFO("screen " << screen.name);

        app.resize({breakpoint - 1, 640});
        CHECK(app.layout() == Layout::narrow);
        QQuickItem* item = app.screen_item();
        REQUIRE(item != nullptr);
        // The screens that choose a layout say which, by the same rule.
        if (item->property("wide").isValid()) {
            CHECK_FALSE(item->property("wide").toBool());
        }

        app.resize({breakpoint, 640});
        CHECK(app.layout() == Layout::wide);
        item = app.screen_item();
        REQUIRE(item != nullptr);
        if (item->property("wide").isValid()) {
            CHECK(item->property("wide").toBool());
        }
    }
}

TEST_CASE("only one layout's controls are on show at a time", "[system][layout]") {
    // Each screen builds both layouts and hides one. A control that answers
    // to the same objectName in both must be visible in exactly one.
    const Layout layout = GENERATE(Layout::narrow, Layout::wide);
    DYNAMIC_SECTION("in the " << cube::systest::to_string(layout) << " layout") {
        App& app = App::fresh();
        app.resize(layout == Layout::wide ? cube::systest::kDesktop : cube::systest::kPhone);

        for (const char* name : {"launch.startScan", "launch.settings", "launch.language"}) {
            INFO("control " << name);
            const auto all = app.find_all(name);
            CHECK(all.size() == 2);
            int visible = 0;
            for (QQuickItem* item : all) {
                visible += item->isVisible() ? 1 : 0;
            }
            CHECK(visible == 1);
        }
    }
}

TEST_CASE("every control on every screen is inside the window", "[system][layout]") {
    // The failure this catches is a footer pushed below the fold on a short
    // window, or a column wider than a phone. It looks at where each control
    // is, not how it looks.
    struct Size {
        const char* what;
        QSize size;
    };
    const std::vector<Size> sizes = {
        {"small phone", {375, 667}}, {"phone", {390, 844}},   {"just narrow", {719, 640}},
        {"just wide", {720, 640}},   {"desktop", {900, 640}}, {"large", {1280, 800}},
    };

    for (const Screen& screen : kScreens) {
        for (const Size& size : sizes) {
            App& app = App::fresh();
            app.resize(size.size);
            show(app, screen);
            INFO("screen " << screen.name << " at " << size.what << " " << size.size.width() << "x"
                           << size.size.height());

            const QRect window(QPoint(0, 0), app.window().size());
            for (QQuickItem* control : app.visible_controls()) {
                const QRect box = control->mapRectToScene(control->boundingRect()).toAlignedRect();
                INFO("control " << app.control_key(control).toStdString() << " at " << box.x() << ","
                                << box.y() << " " << box.width() << "x" << box.height());
                // A control scrolled out of a list is reached by scrolling,
                // not by the window being the wrong size.
                bool in_flickable = false;
                for (QQuickItem* up = control->parentItem(); up != nullptr; up = up->parentItem()) {
                    in_flickable = in_flickable ||
                                   QByteArray(up->metaObject()->className()).startsWith("QQuickFlickable");
                }
                if (!in_flickable) {
                    CHECK(window.contains(box));
                }
            }
        }
    }
}
