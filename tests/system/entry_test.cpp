// Another size of cube, through the whole app: Home's way to the screen for it,
// the size chosen, a scramble typed, what is wrong with it said, and the cube
// solved with no camera, its reduction skipped on the way (docs/design/screens/
// review.md, "A cube of another size").
//
// The camera is the fake one and is never asked for a picture here.

#include "app.hpp"
#include "diagnostics.hpp"

#include "app_runtime.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <cube/nxn/cube.hpp>
#include <cube/solver/cube.hpp>

#include <QQuickItem>
#include <QString>

#include <array>
#include <string>
#include <vector>

namespace {

using cube::systest::App;
using cube::systest::Layout;

[[nodiscard]] QSize size_for(Layout layout) {
    return layout == Layout::wide ? cube::systest::kDesktop : cube::systest::kPhone;
}

void open_entry(App& app) {
    app.click("launch.other");
    REQUIRE(app.wait_for([&]() { return app.screen() == "entry"; }));
    app.pump(80);
}

void type_scramble(App& app, const QString& text) {
    QQuickItem* box = app.find("entry.scramble");
    REQUIRE(box != nullptr);
    REQUIRE(box->setProperty("text", text));
}

}  // namespace

TEST_CASE("Home opens the screen for another size, which offers its sizes and goes back",
          "[system][entry]") {
    const Layout layout = GENERATE(Layout::wide, Layout::narrow);
    DYNAMIC_SECTION("in the " << cube::systest::to_string(layout) << " layout") {
        App& app = App::fresh();
        static_cast<void>(cube::systest::take_qml_warnings());
        app.resize(size_for(layout));

        open_entry(app);
        CHECK(app.layout() == layout);
        for (const int size : {2, 3, 4, 5}) {
            INFO("size " << size);
            app.click(QString::fromStdString("entry.size." + std::to_string(size)));
            app.pump(40);
            // Any size offered can be scanned instead.
            CHECK(app.find_all("entry.scan").first()->isVisible());
        }
        CHECK(app.find("entry.size.6") == nullptr);

        app.click("entry.back");
        REQUIRE(app.wait_for([&]() { return app.screen() == "launch"; }));

        // And the 3x3's own way in is still there, from here.
        open_entry(app);
        app.click("entry.size.3");
        app.pump(40);
        app.click("entry.scan");
        REQUIRE(app.wait_for([&]() { return app.screen() == "scan"; }));

        const auto warnings = cube::systest::take_qml_warnings();
        for (const auto& warning : warnings) {
            UNSCOPED_INFO(warning.where.toStdString() << ": " << warning.message.toStdString());
        }
        CHECK(warnings.empty());
    }
}

TEST_CASE("a scramble typed for a 5x5 is solved without the camera, and its reduction skipped",
          "[system][entry]") {
    const Layout layout = GENERATE(Layout::wide, Layout::narrow);
    DYNAMIC_SECTION("in the " << cube::systest::to_string(layout) << " layout") {
        App& app = App::fresh();
        static_cast<void>(cube::systest::take_qml_warnings());
        app.resize(size_for(layout));
        auto& solve = app.runtime().solve();

        open_entry(app);
        app.click("entry.size.5");
        app.pump(40);

        // Nothing typed, then something that is not a move: said, and not started.
        app.click("entry.solve");
        app.pump(80);
        CHECK(app.screen() == "entry");
        CHECK(app.find("entry.error") != nullptr);
        type_scramble(app, QStringLiteral("R U Q"));
        CHECK(app.find("entry.error") == nullptr);  // typing clears it
        app.click("entry.solve");
        app.pump(80);
        CHECK(app.screen() == "entry");
        CHECK(app.find("entry.error") != nullptr);
        CHECK_FALSE(solve.isSolving());

        // A scramble.
        type_scramble(app, QStringLiteral("R U R' U' 2R F2 3Rw' 2-3L D2 Bw L' 2U"));
        app.click("entry.solve");
        REQUIRE(app.wait_for([&]() { return app.screen() == "solve"; }));
        REQUIRE(app.wait_for([&]() { return !solve.isSolving(); }, 180'000));
        app.settle();
        CHECK(solve.size() == 5);
        CHECK(solve.totalMoves() > 0);
        CHECK(solve.atStart());

        // No camera for a cube that is not a 3x3, and no rescan from it.
        CHECK(app.find("solve.rescan") == nullptr);

        // A turn, a step back, then the reduction skipped.
        app.click("solve.next");
        REQUIRE(app.wait_for([&]() { return solve.moveIndex() == 1; }));
        app.click("solve.previous");
        REQUIRE(app.wait_for([&]() { return solve.atStart(); }));
        REQUIRE(app.find("solve.skip") != nullptr);
        app.click("solve.skip.button");
        REQUIRE(app.wait_for([&]() { return solve.moveIndex() == solve.coreStartIndex(); }));
        CHECK(solve.plan().current_cube().is_reduced());
        app.pump(80);
        CHECK(app.find("solve.skip") == nullptr);

        const auto warnings = cube::systest::take_qml_warnings();
        for (const auto& warning : warnings) {
            UNSCOPED_INFO(warning.where.toStdString() << ": " << warning.message.toStdString());
        }
        CHECK(warnings.empty());
    }
}

TEST_CASE("a cube typed in colour by colour is painted, checked and solved", "[system][entry]") {
    const Layout layout = GENERATE(Layout::wide, Layout::narrow);
    DYNAMIC_SECTION("in the " << cube::systest::to_string(layout) << " layout") {
        App& app = App::fresh();
        static_cast<void>(cube::systest::take_qml_warnings());
        app.resize(size_for(layout));
        auto& solve = app.runtime().solve();
        auto& colours = app.runtime().controllers().colours;

        open_entry(app);
        app.click("entry.size.4");
        app.pump(40);
        app.click("entry.colours");
        REQUIRE(app.wait_for([&]() { return app.screen() == "colours"; }));
        app.pump(80);
        CHECK(colours.size() == 4);
        CHECK(app.layout() == layout);
        // Wide, the cube is drawn beside the side being painted.
        CHECK((app.find("colours.cube") != nullptr) == (layout == Layout::wide));

        // Every side, every colour, and a sticker.
        for (const char* side : {"U", "F", "R", "B", "L", "D"}) {
            app.click(QString("colours.side.") + side);
            app.pump(30);
        }
        for (const char* colour : {"0", "1", "2", "3", "4", "5", "-1", "2"}) {
            app.click(QString("colours.colour.") + colour);
        }
        app.click("colours.grid");
        app.pump(40);
        CHECK(colours.touched());
        app.click("colours.fillSide");
        CHECK(colours.paintedOn("D") == 16);
        app.click("colours.clearAll");
        CHECK_FALSE(colours.touched());

        // Leaving with colours entered asks first; keep editing, then discard.
        colours.paint("U", 0, 1);
        app.click("colours.back");
        app.pump(80);
        CHECK(app.screen() == "colours");
        app.click("colours.discard.keep");
        app.pump(80);
        CHECK(colours.touched());
        app.click("colours.back");
        app.pump(80);
        app.click("colours.discard.confirm");
        REQUIRE(app.wait_for([&]() { return app.screen() == "entry"; }));

        // And a whole cube, said to be one, and solved with no camera.
        app.click("entry.colours");
        REQUIRE(app.wait_for([&]() { return app.screen() == "colours"; }));
        const cube::nxn::NCube cube =
            cube::nxn::random_scramble({.size = 4, .seed = 5, .moves = 60});
        constexpr std::array<const char*, 6> kLetters = {"U", "D", "L", "R", "F", "B"};
        for (const cube::solver::Face face : cube::solver::kAllFaces) {
            for (int row = 0; row < 4; ++row) {
                for (int col = 0; col < 4; ++col) {
                    const bool across =
                        face == cube::solver::Face::R || face == cube::solver::Face::B;
                    const bool down = face == cube::solver::Face::D;
                    colours.paint(QString::fromLatin1(kLetters.at(static_cast<std::size_t>(face))),
                                  (row * 4) + col,
                                  static_cast<int>(
                                      cube.at(face, down ? 3 - row : row, across ? 3 - col : col)));
                }
            }
        }
        app.pump(80);
        CHECK(colours.status().value("state").toString() == "valid");
        app.click("colours.solve");
        REQUIRE(app.wait_for([&]() { return app.screen() == "solve"; }));
        REQUIRE(app.wait_for([&]() { return !solve.isSolving(); }, 180'000));
        CHECK(solve.size() == 4);
        CHECK(solve.totalMoves() > 0);

        const auto warnings = cube::systest::take_qml_warnings();
        for (const auto& warning : warnings) {
            UNSCOPED_INFO(warning.where.toStdString() << ": " << warning.message.toStdString());
        }
        CHECK(warnings.empty());
    }
}
