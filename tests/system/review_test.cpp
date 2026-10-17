// Putting right what the camera misread, through the window: a scan that makes no cube is
// looked over, corrected and left; a cube the scan read is looked over from the solve
// screen and the way back is the solve it was opened from.
//
// Every control the review adds is pressed here, in both layouts - the reach gate
// (reach.cpp) wants every control that was ever on screen to have been pressed
// by something, and the sweeps do not visit this screen.

#include "app.hpp"
#include "diagnostics.hpp"
#include "fake_camera.hpp"
#include "scan_script.hpp"

#include "app_runtime.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <cube/solver/cube.hpp>

#include <QQuickItem>
#include <QString>
#include <QVariant>

namespace {

using cube::systest::App;
using cube::systest::FakeCamera;
using cube::systest::Layout;

constexpr int kScanTimeoutMs = 60'000;
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

TEST_CASE("a scan that made no cube is looked over, put right and left", "[system][review]") {
    const Layout layout = GENERATE(Layout::wide, Layout::narrow);
    DYNAMIC_SECTION("in the " << cube::systest::to_string(layout) << " layout") {
        App& app = App::fresh();
        static_cast<void>(cube::systest::take_qml_warnings());
        app.resize(size_for(layout));
        REQUIRE(app.layout() == layout);
        auto& scan = app.runtime().directScan();
        auto& colours = app.runtime().controllers().colours;

        // Six sides that fit together two ways, so the scan has no cube (the case the
        // drawn frames can stage: they are never misread): it says where to go.
        const auto patterned = cube::test::patterned_cube();
        REQUIRE(patterned.has_value());
        start_scan_from_launch(app);
        CHECK(app.find("scan.checkColours") == nullptr);
        cube::systest::show_cube(*patterned);
        REQUIRE(app.wait_for([&]() { return scan.conflicted(); }, kScanTimeoutMs));
        REQUIRE(app.wait_for([&]() { return app.find("scan.checkColours") != nullptr; }));

        app.click("scan.checkColours");
        REQUIRE(app.wait_for([&]() { return app.screen() == "colours"; }));
        app.pump(80);
        CHECK(colours.fromScan());
        CHECK_FALSE(colours.touched());
        CHECK(colours.edit().status().state != cube::app::EditState::Valid);
        // Looking over the scan let go of the camera.
        REQUIRE(app.wait_for([&]() { return !FakeCamera::instance().capturing(); }));

        // Pick a side and a colour, and paint a sticker: there is something to take back.
        app.click("colours.side.F");
        app.click("colours.colour.1");
        app.click("colours.grid");
        app.pump(40);
        CHECK(colours.touched());
        CHECK(colours.canUndo());

        app.click("colours.undo");
        CHECK_FALSE(colours.touched());
        CHECK(colours.canRedo());
        app.click("colours.redo");
        CHECK(colours.touched());
        app.click("colours.turnSide");
        CHECK(colours.canUndo());
        app.click("colours.revert");
        CHECK_FALSE(colours.touched());

        // A side described in letters: the box opens with the side as it is, takes
        // it as it is (nothing changes), and can be closed without.
        app.click("colours.describe");
        app.pump(120);
        QQuickItem* box = app.find("colours.describeBox");
        REQUIRE(box != nullptr);
        QQuickItem* field = app.find("colours.describeText");
        REQUIRE(field != nullptr);
        CHECK(field->property("text").toString() == colours.sideText("F"));
        app.click("colours.describeApply");
        CHECK(app.find("colours.describeBox") == nullptr);
        app.click("colours.describe");
        app.pump(120);
        REQUIRE(app.find("colours.describeCancel") != nullptr);
        app.click("colours.describeCancel");
        CHECK(app.find("colours.describeBox") == nullptr);
        CHECK_FALSE(colours.touched());

        // And a description that is wrong is said to be, and changes nothing.
        app.click("colours.describe");
        app.pump(120);
        app.find("colours.describeText")->setProperty("text", QStringLiteral("GGG"));
        app.click("colours.describeApply");
        CHECK(app.find("colours.describeError") != nullptr);
        CHECK_FALSE(colours.touched());
        app.click("colours.describeCancel");

        // Out of it, back to a new scan.
        app.click("colours.back");
        app.pump(80);
        if (app.screen() == "colours") {
            app.click("colours.discard.confirm");
        }
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

TEST_CASE("a cube the scan read can be looked over from the solve screen and come back",
          "[system][review]") {
    const Layout layout = GENERATE(Layout::wide, Layout::narrow);
    DYNAMIC_SECTION("in the " << cube::systest::to_string(layout) << " layout") {
        App& app = App::fresh();
        static_cast<void>(cube::systest::take_qml_warnings());
        app.resize(size_for(layout));
        REQUIRE(app.layout() == layout);
        auto& scan = app.runtime().directScan();
        auto& solve = app.runtime().solve();
        auto& colours = app.runtime().controllers().colours;

        const cube::solver::Cube cube = cube::systest::scramble(9);
        start_scan_from_launch(app);
        cube::systest::show_cube(cube);
        REQUIRE(app.wait_for([&]() { return scan.complete(); }, kScanTimeoutMs));
        REQUIRE(app.wait_for([&]() { return app.screen() == "solve"; }, kSolveTimeoutMs));
        REQUIRE(app.wait_for([&]() { return !solve.isSolving(); }, kSolveTimeoutMs));
        app.settle();
        const int total = solve.totalMoves();
        REQUIRE(total > 0);

        // From a scan, the solve screen offers it.
        REQUIRE(app.find("solve.checkColours") != nullptr);
        app.click("solve.checkColours");
        REQUIRE(app.wait_for([&]() { return app.screen() == "colours"; }));
        app.pump(80);
        CHECK(colours.fromScan());
        CHECK(colours.edit().status().state == cube::app::EditState::Valid);
        CHECK_FALSE(colours.touched());
        // The camera was let go of; the solution is still there.
        REQUIRE(app.wait_for([&]() { return !FakeCamera::instance().capturing(); }));
        CHECK(solve.totalMoves() == total);

        // Back is the solve screen it was opened from, not a new scan.
        app.click("colours.back");
        REQUIRE(app.wait_for([&]() { return app.screen() == "solve"; }));
        CHECK(solve.totalMoves() == total);
        REQUIRE(app.wait_for([&]() { return FakeCamera::instance().capturing(); }));

        // And the other way out of it, Scan again, starts a scan from nothing.
        app.click("solve.checkColours");
        REQUIRE(app.wait_for([&]() { return app.screen() == "colours"; }));
        app.pump(80);
        app.click("colours.scan");
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
