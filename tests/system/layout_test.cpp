// The two layouts either side of Theme.wideBreakpoint, and whether what each
// shows fits where it is shown.

#include "app.hpp"
#include "scan_script.hpp"

#include "app_runtime.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <cube/solver/solver.hpp>

#include <QQuickItem>
#include <QQuickWindow>
#include <QRect>

#include <array>
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
        // Only the ones this platform put on show at all, and only said, not
        // failed: the list is of controls that overflow somewhere, and was
        // written from the Windows and Linux fonts. A control that fits on
        // macOS is a control that fits there, not a fix; the day it fits on
        // every platform it comes off the list.
        if (on_show.count(key) == 1 && overflowing.count(key) == 0) {
            WARN(
                "fits in its window on this platform; remove it from the known list when it does "
                "on all of them: "
                << key);
        }
    }
}

TEST_CASE("the solve screen keeps its cube clear of the camera preview and the move label",
          "[system][layout][solve-camera]") {
    // The camera's preview sits in one corner of the cube's stage, its words
    // and the move label in the other. The cube used to be sized without
    // them: on a phone the words ran across its edge and its top was under
    // the move label. What is held is their boxes, not the pixels in them -
    // a cube's box has air round the cube, which it needs to turn a layer
    // in, but a box that is clear is a cube that is clear at any angle.
    //
    // The words are only said once a scan is in hand, so this goes to the
    // solve screen by scanning, as a person does (drawn frames; nobody is in
    // the picture).
    App& app = App::fresh();
    auto& scan = app.runtime().directScan();
    auto& solve = app.runtime().solve();

    app.click("launch.startScan");
    REQUIRE(app.wait_for([&]() { return scan.running(); }, 60'000));
    cube::systest::show_cube(cube::systest::scramble(21));
    REQUIRE(app.wait_for([&]() { return app.screen() == "solve"; }, 180'000));
    REQUIRE(app.wait_for([&]() { return !solve.isSolving(); }, 180'000));

    // The camera has delivered a frame and a scan is in hand, so it has
    // something to say: the words under test are there.
    REQUIRE(app.wait_for(
        [&]() {
            const QQuickItem* words = app.find("solve.words");
            return words != nullptr && !words->property("text").toString().isEmpty();
        },
        60'000));

    // The four things on the stage, by name: the cube, the preview's frame
    // (its bounds are the preview's), the camera's words and the move label.
    constexpr std::array<const char*, 4> kParts = {"solve.cube", "solve.frame", "solve.words",
                                                   "solve.move"};

    // Every part is on show, inside the window, and clear of every other.
    // Returns the cube's box, for the cases that hold it still.
    const auto check_clear = [&]() {
        std::array<QRect, kParts.size()> boxes;
        for (std::size_t i = 0; i < kParts.size(); ++i) {
            INFO(kParts[i] << " is on show");
            const QQuickItem* item = app.find(kParts[i]);
            REQUIRE(item != nullptr);
            REQUIRE(item->width() > 0.0);
            REQUIRE(item->height() > 0.0);
            boxes[i] = item->mapRectToScene(item->boundingRect()).toAlignedRect();
        }
        for (std::size_t i = 0; i < kParts.size(); ++i) {
            CAPTURE(kParts[i], boxes[i].x(), boxes[i].y(), boxes[i].width(), boxes[i].height());
            CHECK(QRect(QPoint(0, 0), app.window().size()).contains(boxes[i]));
            for (std::size_t j = i + 1; j < kParts.size(); ++j) {
                CAPTURE(kParts[j], boxes[j].x(), boxes[j].y(), boxes[j].width(), boxes[j].height());
                CHECK_FALSE(boxes[i].intersects(boxes[j]));
            }
        }
        return boxes[0];
    };

    struct Size {
        const char* what;
        QSize size;
    };
    // The smallest window the app allows (kMinimumWindowSize), the phones,
    // either side of the breakpoint, and the sizes a desktop opens at.
    const std::vector<Size> sizes = {
        {"the smallest window", {360, 560}},
        {"a small phone", {375, 667}},
        {"a phone", {390, 844}},
        {"just narrow", {719, 640}},
        {"just wide", {720, 640}},
        {"the desktop's", {900, 640}},
        {"large", {1280, 800}},
    };

    // The words as they are now, and then as long as the camera's get: in
    // English that is the one that says the side facing you cannot be seen
    // (SolveScreen.qml, cameraWords).
    const QString longest =
        "Matched. The camera cannot see the side facing you: press Next after this move.";
    for (const bool longest_words : {false, true}) {
        if (longest_words) {
            QQuickItem* words = app.find("solve.words");
            REQUIRE(words != nullptr);
            REQUIRE(words->setProperty("text", longest));
        }
        for (const Size& size : sizes) {
            INFO(size.what << ", " << size.size.width() << "x" << size.size.height()
                           << (longest_words ? ", with the longest words" : ""));
            app.resize(size.size);
            app.settle();
            check_clear();
        }
    }

    // The label is not the same width for every move ("Right side" is
    // longer than "Top"), and the cube must not answer to it: held at the
    // smallest window, where there is least room, through the whole
    // solution.
    app.resize({360, 560});
    app.settle();
    const QRect cube_box = check_clear();
    for (int taken = 1; taken <= solve.totalMoves(); ++taken) {
        INFO("after move " << taken << " of " << solve.totalMoves());
        solve.nextMove();
        app.pump(20);
        CHECK(check_clear() == cube_box);
    }

    // The controller is the suite's: the next case that wants a solution
    // takes the one that is there (sweep_test.cpp does), and a solution
    // left at its end is one that has nothing to step through and, solved
    // on from a cube that is already solved, none to switch method on.
    for (int back = solve.moveIndex(); back > 0 && !solve.atStart(); --back) {
        solve.previousMove();
    }
    CHECK(solve.atStart());
}
