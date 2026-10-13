// The journey: launch, scan, solve, and the ways out of it that the app has
// today. Everything is done through the window - presses sent to controls
// found by objectName - and every observation is of what a person sees.
//
// What the app cannot do yet is in known_today_test.cpp, asserted as it is
// and marked to change.

#include "app.hpp"
#include "diagnostics.hpp"
#include "fake_camera.hpp"
#include "scan_script.hpp"

#include "app_runtime.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <cube/solver/solver.hpp>

#include <QQuickItem>
#include <QQuickWindow>

namespace {

using cube::systest::App;
using cube::systest::FakeCamera;
using cube::systest::Layout;

constexpr int kScanTimeoutMs = 60'000;
// The solve runs on a worker; it is a few seconds in an optimised build and
// longer under coverage instrumentation.
constexpr int kSolveTimeoutMs = 180'000;

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

TEST_CASE("the app opens on the launch screen and draws it", "[system][launch]") {
    App& app = App::fresh();
    static_cast<void>(cube::systest::take_qml_warnings());

    CHECK(app.screen() == "launch");
    CHECK(app.window().title() == "Cubeulator");
    CHECK(app.layout() == Layout::wide);
    CHECK(app.find("launch.startScan") != nullptr);
    CHECK(app.find("launch.settings") != nullptr);

    // The camera the app asked for at start-up is the one that was plugged in.
    REQUIRE(app.wait_for([&]() { return !app.runtime().camera().busy(); }));
    CHECK(app.runtime().camera().devices().size() == 1);
    CHECK(app.runtime().camera().selectedDeviceId() == "fake:0");
    // Nothing opened the camera just to show the launch screen.
    CHECK(FakeCamera::instance().starts() == 0);

    app.settle();
    CHECK_FALSE(app.grab().isNull());

    const auto warnings = cube::systest::take_qml_warnings();
    for (const auto& warning : warnings) {
        UNSCOPED_INFO(warning.where.toStdString() << ": " << warning.message.toStdString());
    }
    CHECK(warnings.empty());
}

TEST_CASE("a cube is scanned and solved, from launch to the last move", "[system][journey]") {
    const Layout layout = GENERATE(Layout::wide, Layout::narrow);
    DYNAMIC_SECTION("in the " << cube::systest::to_string(layout) << " layout") {
        App& app = App::fresh();
        static_cast<void>(cube::systest::take_qml_warnings());
        app.resize(size_for(layout));
        REQUIRE(app.layout() == layout);

        const cube::solver::Cube cube = cube::solver::random_scramble(7);
        auto& scan = app.runtime().directScan();
        auto& solve = app.runtime().solve();

        start_scan_from_launch(app);
        // Opening the scan screen opened the camera, once.
        CHECK(FakeCamera::instance().starts() == 1);
        CHECK(scan.sidesRead() == 0);
        CHECK(app.find_text("0 of 6 sides read") != nullptr);

        // The first side, held square to the camera.
        const auto first = cube::test::outside_view(cube, cube::solver::kAllFaces[0]);
        cube::systest::show_side(first);
        REQUIRE(app.wait_for([&]() { return scan.sidesRead() == 1; }, kScanTimeoutMs));
        CHECK(scan.stickerPoints().size() == 9);
        CHECK(app.wait_for([&]() { return app.find_text("1 of 6 sides read") != nullptr; }));

        // The rest of the cube, a side at a time.
        cube::systest::show_empty_room();
        for (std::size_t i = 1; i < cube::solver::kAllFaces.size(); ++i) {
            cube::systest::show_side(cube::test::outside_view(cube, cube::solver::kAllFaces[i]));
            cube::systest::show_empty_room();
        }
        REQUIRE(app.wait_for([&]() { return scan.complete(); }, kScanTimeoutMs));
        REQUIRE(scan.cube().has_value());
        CHECK(*scan.cube() == cube);

        // A finished scan stays up long enough to be seen, then the solution
        // replaces it.
        CHECK(app.screen() == "scan");
        CHECK(app.find_text("Cube read") != nullptr);
        REQUIRE(app.wait_for([&]() { return app.screen() == "solve"; }, kSolveTimeoutMs));
        REQUIRE_FALSE(solve.isSolving());
        REQUIRE(solve.totalMoves() > 0);
        CHECK(solve.atStart());
        // Leaving the scan screen let go of the camera.
        REQUIRE(app.wait_for([&]() { return !FakeCamera::instance().capturing(); }));
        CHECK(FakeCamera::instance().stops() == 1);

        // Previous is off at the start, Next move is on.
        QQuickItem* previous = app.find_button("Previous");
        QQuickItem* next = app.find_button("Next move");
        REQUIRE(previous != nullptr);
        REQUIRE(next != nullptr);
        CHECK_FALSE(previous->property("enabled").toBool());
        CHECK(next->property("enabled").toBool());
        app.settle();

        // Every move, one press each.
        const int total = solve.totalMoves();
        int taken = 0;
        while (!solve.atEnd() && taken <= total) {
            app.click_button("Next move");
            ++taken;
            CHECK(solve.moveIndex() == taken);
        }
        CHECK(taken == total);
        CHECK(solve.atEnd());
        CHECK(app.screen() == "solve");

        // And back a move, then forward again.
        app.click_button("Previous");
        CHECK(solve.moveIndex() == total - 1);
        CHECK_FALSE(solve.atEnd());
        app.click_button("Next move");
        CHECK(solve.atEnd());

        // The last move leaves a button that says so and does nothing.
        QQuickItem* done = app.find_button("Solved!");
        REQUIRE(done != nullptr);
        CHECK_FALSE(done->property("enabled").toBool());

        const auto warnings = cube::systest::take_qml_warnings();
        for (const auto& warning : warnings) {
            UNSCOPED_INFO(warning.where.toStdString() << ": " << warning.message.toStdString());
        }
        CHECK(warnings.empty());
    }
}

TEST_CASE("cancelling mid-scan returns to launch and lets go of the camera", "[system][scan]") {
    App& app = App::fresh();
    auto& scan = app.runtime().directScan();
    const cube::solver::Cube cube = cube::solver::random_scramble(11);

    start_scan_from_launch(app);
    cube::systest::show_side(cube::test::outside_view(cube, cube::solver::kAllFaces[0]));
    REQUIRE(app.wait_for([&]() { return scan.sidesRead() == 1; }, kScanTimeoutMs));
    REQUIRE(FakeCamera::instance().capturing());

    app.click_button("Cancel");
    REQUIRE(app.wait_for([&]() { return app.screen() == "launch"; }));
    REQUIRE(app.wait_for([&]() { return !FakeCamera::instance().capturing(); }));
    CHECK(FakeCamera::instance().stops() == 1);
    CHECK_FALSE(scan.running());

    // Starting again begins from nothing: the half-read cube is forgotten.
    start_scan_from_launch(app);
    CHECK(scan.sidesRead() == 0);
    CHECK(FakeCamera::instance().starts() == 2);
}

TEST_CASE("start over forgets the sides read and keeps scanning", "[system][scan]") {
    App& app = App::fresh();
    auto& scan = app.runtime().directScan();
    const cube::solver::Cube cube = cube::solver::random_scramble(13);

    start_scan_from_launch(app);

    // Nothing read yet: there is nothing to start over from.
    QQuickItem* start_over = app.find_button("Start over");
    REQUIRE(start_over != nullptr);
    CHECK_FALSE(start_over->property("enabled").toBool());

    cube::systest::show_side(cube::test::outside_view(cube, cube::solver::kAllFaces[0]));
    cube::systest::show_empty_room();
    cube::systest::show_side(cube::test::outside_view(cube, cube::solver::kAllFaces[1]));
    REQUIRE(app.wait_for([&]() { return scan.sidesRead() == 2; }, kScanTimeoutMs));
    CHECK(start_over->property("enabled").toBool());

    // Put the camera on an empty room first, or the side still in view is
    // read again the moment the scan is cleared.
    FakeCamera::instance().clear_script();
    cube::systest::show_empty_room();
    app.pump(150);
    app.click_button("Start over");
    REQUIRE(app.wait_for([&]() { return scan.sidesRead() == 0; }));
    CHECK(app.screen() == "scan");
    CHECK(scan.running());
    CHECK(FakeCamera::instance().capturing());
    CHECK(app.find_text("0 of 6 sides read") != nullptr);
    CHECK_FALSE(start_over->property("enabled").toBool());
}

TEST_CASE("a camera that will not start is explained, and can be left", "[system][scan][error]") {
    App& app = App::fresh();
    auto& scan = app.runtime().directScan();

    FakeCamera::instance().fail_next_start(cube::CubeError::camera_start_failed);
    app.click("launch.startScan");
    REQUIRE(app.wait_for([&]() { return app.screen() == "scan"; }));
    REQUIRE(app.wait_for([&]() { return !scan.cameraError().isEmpty(); }));

    CHECK_FALSE(scan.running());
    CHECK(scan.cameraError() == "the camera could not be started");
    CHECK(app.find_text("Camera unavailable") != nullptr);
    // The reason is shown to the person, not only logged.
    CHECK(app.find_text("the camera could not be started") != nullptr);
    app.settle();
    CHECK_FALSE(app.grab().isNull());

    app.click_button("Cancel");
    REQUIRE(app.wait_for([&]() { return app.screen() == "launch"; }));

    // The next attempt is not poisoned by the last.
    start_scan_from_launch(app);
    CHECK(scan.cameraError().isEmpty());
    CHECK(app.find_text("Camera unavailable") == nullptr);
}

TEST_CASE("with no camera plugged in the settings say so, and refreshing finds one",
          "[system][settings][error]") {
    App& app = App::fresh();
    auto& camera = app.runtime().camera();

    FakeCamera::instance().set_devices({});
    app.click("launch.settings");
    REQUIRE(app.wait_for([&]() { return app.screen() == "settings"; }));
    app.click("settings.refreshCameras");
    REQUIRE(app.wait_for([&]() { return !camera.busy(); }));
    REQUIRE(app.wait_for([&]() {
        return app.find_text("No camera was detected. Plug one in, then refresh.") != nullptr;
    }));

    FakeCamera::instance().reset();
    app.click("settings.refreshCameras");
    REQUIRE(app.wait_for([&]() { return !camera.busy() && camera.devices().size() == 1; }));
    CHECK(app.find_text("Choose which device Cubeulator reads the cube's faces from.") != nullptr);
    CHECK(app.find("settings.camera.0") != nullptr);
}
