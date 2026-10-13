// The screenshot tour: a picture of every screen - and of the states of the
// scan and solve screens - in the narrow and wide layouts, light and dark.
//
// This is the review surface the experience plan names: CI uploads the
// directory, and a designer, a translator or a store-listing author can look
// at what the app looks like without building it. Every picture is of drawn
// frames; nobody is in them.
//
// The test also asserts what a picture can tell a machine: that each one was
// written and is not one flat colour. Whether it is right is for a person.

#include "app.hpp"
#include "fake_camera.hpp"
#include "scan_script.hpp"

#include "app_runtime.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cube/solver/solver.hpp>

#include <QFileInfo>
#include <QImage>
#include <QQuickWindow>

#include <set>
#include <vector>

namespace {

using cube::systest::App;
using cube::systest::FakeCamera;

struct Variant {
    const char* layout;
    QSize size;
    const char* theme;
};

const std::vector<Variant> kVariants = {
    {"narrow", cube::systest::kPhone, "light"},
    {"narrow", cube::systest::kPhone, "dark"},
    {"wide", cube::systest::kDesktop, "light"},
    {"wide", cube::systest::kDesktop, "dark"},
};

struct Tour {
    App& app;
    const Variant& variant;
    std::vector<QString> written;

    void shoot(const QString& name) {
        app.settle();
        written.push_back(app.shot(
            QString("%1-%2-%3")
                .arg(name, QString::fromUtf8(variant.layout), QString::fromUtf8(variant.theme))));
    }
};

}  // namespace

TEST_CASE("the screenshot tour: every screen, both layouts, both themes", "[system][tour]") {
    App& app = App::fresh();
    // Pictures that do not depend on when they were taken: the launch cube
    // holds still. (Its turning is checked in render_test.cpp.)
    app.settings_state().setProperty("cubeAutoRotate", false);

    const cube::solver::Cube cube = cube::solver::random_scramble(3);
    std::vector<QString> all;

    for (const Variant& variant : kVariants) {
        INFO("variant " << variant.layout << " " << variant.theme);
        app.go("launch");
        FakeCamera::instance().reset();
        app.resize(variant.size);
        app.set_theme(QString::fromUtf8(variant.theme));
        Tour tour{app, variant, {}};
        auto& scan = app.runtime().directScan();
        auto& solve = app.runtime().solve();

        // Launch, and the settings opened from it.
        tour.shoot("launch");
        app.click("launch.settings");
        REQUIRE(app.wait_for([&]() { return app.screen() == "settings"; }));
        tour.shoot("settings");
        app.click("settings.back");
        REQUIRE(app.wait_for([&]() { return app.screen() == "launch"; }));

        // The two screens no control on a desktop leads to.
        app.go("permission");
        tour.shoot("permission");
        app.go("complete");
        tour.shoot("complete");
        app.go("launch");

        // A camera that will not start.
        FakeCamera::instance().fail_next_start(cube::CubeError::camera_not_found);
        app.click("launch.startScan");
        REQUIRE(app.wait_for([&]() { return !scan.cameraError().isEmpty(); }));
        tour.shoot("scan-camera-unavailable");
        app.click_button("Cancel");
        REQUIRE(app.wait_for([&]() { return app.screen() == "launch"; }));
        REQUIRE(app.wait_for([&]() { return !scan.starting(); }));

        // The scan, side by side.
        app.click("launch.startScan");
        REQUIRE(app.wait_for([&]() { return scan.running(); }));
        cube::systest::show_empty_room();
        REQUIRE(app.wait_for([&]() { return !scan.settingUp(); }));
        tour.shoot("scan-looking");

        cube::systest::show_side(cube::test::outside_view(cube, cube::solver::kAllFaces[0]), 1);
        REQUIRE(app.wait_for([&]() { return scan.reading() || scan.sidesRead() == 1; }, 30'000));
        tour.shoot("scan-reading");

        FakeCamera::instance().clear_script();
        cube::systest::show_empty_room();
        cube::systest::show_sides(cube, 3);
        REQUIRE(app.wait_for([&]() { return scan.sidesRead() == 3; }, 60'000));
        cube::systest::show_empty_room();
        tour.shoot("scan-three-sides");

        for (std::size_t i = 3; i < cube::solver::kAllFaces.size(); ++i) {
            cube::systest::show_side(cube::test::outside_view(cube, cube::solver::kAllFaces[i]));
            cube::systest::show_empty_room();
        }
        REQUIRE(app.wait_for([&]() { return scan.complete(); }, 60'000));
        // The beat before the solution is short; this picture is taken in it.
        tour.written.push_back(app.shot(
            QString("scan-cube-read-%1-%2")
                .arg(QString::fromUtf8(variant.layout), QString::fromUtf8(variant.theme))));

        // The solution, at the start, part way and done.
        REQUIRE(app.wait_for([&]() { return app.screen() == "solve"; }, 180'000));
        tour.shoot("solve-start");
        const int total = solve.totalMoves();
        REQUIRE(total > 0);
        for (int presses = 0; solve.moveIndex() < total / 2 && presses <= total; ++presses) {
            app.click_button("Next move");
        }
        tour.shoot("solve-middle");
        for (int presses = 0; !solve.atEnd() && presses <= total; ++presses) {
            app.click_button("Next move");
        }
        REQUIRE(solve.atEnd());
        tour.shoot("solve-end");

        app.go("launch");
        REQUIRE(app.wait_for([&]() { return !scan.running() && !scan.starting(); }));

        for (const QString& path : tour.written) {
            INFO("picture " << path.toStdString());
            REQUIRE(QFileInfo::exists(path));
            const QImage image(path);
            REQUIRE_FALSE(image.isNull());
            std::set<QRgb> colours;
            for (int y = 0; y < image.height(); y += 7) {
                for (int x = 0; x < image.width(); x += 7) {
                    colours.insert(image.pixel(x, y));
                }
            }
            // Not a blank window: a screen has text and shapes on it.
            CHECK(colours.size() > 6);
            all.push_back(path);
        }
    }

    // Twelve pictures a variant, four variants.
    CHECK(all.size() == 12 * kVariants.size());
}
