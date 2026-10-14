// The two layouts either side of Theme.wideBreakpoint, and whether what each
// shows fits where it is shown.

#include "app.hpp"

#include "app_runtime.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <cube/solver/solver.hpp>

#include <QQuickItem>
#include <QQuickWindow>

#include <set>
#include <string>
#include <string_view>
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
            solve.solveCube(cube::systest::scramble(21));
        }
        REQUIRE(app.wait_for([&]() { return !solve.isSolving(); }, 180'000));
    }
    app.go(screen.name);
    REQUIRE(app.screen() == screen.name);
    // Where things are does not wait for the fade-in: a pause is enough.
    app.pump(80);
}

// A case that is going to `screen` starts there rather than on the launch
// screen it would leave at once (see App::Start).
[[nodiscard]] App::Start start_for(const Screen& screen) {
    return std::string_view(screen.name) == "launch" ? App::Start::launch : App::Start::elsewhere;
}

}  // namespace

TEST_CASE("the breakpoint is where Theme says, and every screen follows it", "[system][layout]") {
    const int breakpoint = App::fresh().theme().property("wideBreakpoint").toInt();
    REQUIRE(breakpoint == 720);

    for (const Screen& screen : kScreens) {
        App& app = App::fresh(start_for(screen));
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

TEST_CASE("every control on every screen is inside the window, bar the known few",
          "[system][layout]") {
    // The failure this catches is a footer pushed below the fold on a short
    // window, or a column wider than a phone. It looks at where each control
    // is, not how it looks.
    //
    // The complete screen still has it, found by this case (plan, "What was
    // found": "phone layouts sized by subtraction"): both buttons are below
    // a 1280x800 window. It is listed here as it is, so a new overflow fails
    // and so does the day this one is fixed. The permission screen had it
    // too, on a phone, until the translations work rebuilt it; this case
    // said so by failing. Owner: experience lane, E3a on the kit.
    const std::set<std::string> known = {
        "complete.review @ 1280x800",
        "complete.scanAnother @ 1280x800",
    };

    struct Size {
        const char* what;
        QSize size;
    };
    const std::vector<Size> sizes = {
        {"small phone", {375, 667}}, {"phone", {390, 844}},   {"just narrow", {719, 640}},
        {"just wide", {720, 640}},   {"desktop", {900, 640}}, {"large", {1280, 800}},
    };

    std::set<std::string> overflowing;
    std::set<std::string> on_show;
    for (const Screen& screen : kScreens) {
        // The screen is brought up once and the window is taken through the
        // sizes: bringing a screen up is what costs on a software renderer
        // (the launch and solve screens draw a 3D cube), and a window that
        // is resized is a thing people do.
        App& app = App::fresh(start_for(screen));
        show(app, screen);
        for (const Size& size : sizes) {
            app.resize(size.size);
            app.pump(80);

            const QRect window(QPoint(0, 0), app.window().size());
            for (QQuickItem* control : app.visible_controls()) {
                // A control scrolled out of a list is reached by scrolling,
                // not by the window being the wrong size.
                bool in_flickable = false;
                for (QQuickItem* up = control->parentItem(); up != nullptr; up = up->parentItem()) {
                    in_flickable =
                        in_flickable ||
                        QByteArray(up->metaObject()->className()).startsWith("QQuickFlickable");
                }
                if (in_flickable) {
                    continue;
                }
                const std::string key =
                    (app.control_key(control) + " @ " + QString::number(size.size.width()) + "x" +
                     QString::number(size.size.height()))
                        .toStdString();
                on_show.insert(key);
                const QRect box = control->mapRectToScene(control->boundingRect()).toAlignedRect();
                if (!window.contains(box)) {
                    overflowing.insert(key);
                    UNSCOPED_INFO(key << " is at " << box.x() << "," << box.y() << " "
                                      << box.width() << "x" << box.height() << " in a " << size.what
                                      << " window");
                }
            }
        }
    }

    for (const std::string& key : overflowing) {
        INFO("a control outside the window that is not in the known list: " << key);
        CHECK(known.count(key) == 1);
    }
    for (const std::string& key : known) {
        // Only judge the ones this platform put on show at all.
        if (on_show.count(key) == 1) {
            INFO("no longer outside the window; remove it from the known list: " << key);
            CHECK(overflowing.count(key) == 1);
        }
    }
}
