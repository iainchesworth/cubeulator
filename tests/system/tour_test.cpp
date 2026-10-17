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

#include <cube/nxn/cube.hpp>
#include <cube/solver/solver.hpp>

#include <QFileInfo>
#include <QImage>
#include <QMetaObject>
#include <QQuickItem>
#include <QQuickWindow>
#include <QVariant>

#include <set>
#include <string>
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

// One layout's pictures, light and dark. The two layouts are cases (and ctest
// entries) of their own so that they can run side by side: the tour is the
// longest thing in the suite on a software renderer, and half of it is a
// third of the job's time when it is one entry.
void tour_layout(const std::string& layout) {
    // The pictures are the review surface: the cube as it is, light probe
    // and all.
    App& app = App::fresh(App::Start::launch, App::Scene3D::full);
    // Pictures that do not depend on when they were taken: the launch cube
    // holds still. (Its turning is checked in render_test.cpp.)
    app.settings_state().setProperty("cubeAutoRotate", false);

    const cube::solver::Cube cube = cube::systest::scramble(3);
    std::vector<QString> all;

    std::size_t variants = 0;
    for (const Variant& variant : kVariants) {
        if (layout != variant.layout) {
            continue;
        }
        ++variants;
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

        // The screen no control on a desktop leads to.
        app.go("permission");
        tour.shoot("permission");
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

        // The solution, at the start, part way and done. The pictures want
        // the cube in those places, not the press that gets it there (the
        // journey presses every move), so the controller is stepped directly:
        // a press plays the turn and waits for it, ninety times a variant, and
        // on a software renderer that was most of the tour.
        REQUIRE(app.wait_for([&]() { return app.screen() == "solve"; }, 180'000));
        const int total = solve.totalMoves();
        REQUIRE(total > 0);
        const auto step_to = [&](int index) {
            while (solve.moveIndex() < index) {
                solve.nextMove();
            }
            app.pump(20);
        };
        tour.shoot("solve-start");
        step_to(total / 2);
        tour.shoot("solve-middle");
        step_to(total);
        REQUIRE(solve.atEnd());
        tour.shoot("solve-end");

        // Finish leads to the complete screen.
        app.click("solve.next");
        REQUIRE(app.wait_for([&]() { return app.screen() == "complete"; }));
        tour.shoot("complete");

        // And Review the solution opens the solution: as it was scanned, part
        // way (put there, as the solve is above: the journey presses the
        // rows) and solved (the End key). Its position is its own, so the
        // solve's stays at its end.
        app.click("complete.review");
        REQUIRE(app.wait_for([&]() { return app.screen() == "solution"; }));
        tour.shoot("solution-start");
        REQUIRE(QMetaObject::invokeMethod(app.screen_item(), "jumpTo", Q_ARG(QVariant, total / 2)));
        app.pump(80);
        tour.shoot("solution-middle");
        app.key(Qt::Key_End);
        tour.shoot("solution-end");
        REQUIRE(solve.atEnd());
        app.click("solution.back");
        REQUIRE(app.wait_for([&]() { return app.screen() == "complete"; }));

        app.go("launch");
        REQUIRE(app.wait_for([&]() { return !scan.running() && !scan.starting(); }));

        // The history: the solve just finished, and another left part-way
        // (scanned, solved on its worker, three turns taken); then nothing.
        solve.solveCube(cube::systest::scramble(5));
        REQUIRE(app.wait_for([&]() { return !solve.isSolving(); }, 180'000));
        for (int i = 0; i < 3; ++i) {
            solve.nextMove();
        }
        app.click("launch.solveHistory");
        REQUIRE(app.wait_for([&]() { return app.screen() == "history"; }));
        app.pump(120);
        tour.shoot("history");
        app.runtime().history().clear();
        app.pump(120);
        tour.shoot("history-empty");
        app.click("history.back");
        REQUIRE(app.wait_for([&]() { return app.screen() == "launch"; }));

        // Another size of cube: the entry screen with a 5x5's scramble typed in,
        // the colours screen with the top and front of a 4x4 painted, and the
        // 5x5's solve and review at the first turn of a layer inside the cube.
        const auto type_scramble = [&](const QString& text) {
            QQuickItem* box = app.find("entry.scramble");
            REQUIRE(box != nullptr);
            REQUIRE(box->setProperty("text", text));
            app.pump(40);
        };
        const QString five = QStringLiteral("R U 2R' F2 3Rw Uw' L2 2D B' 2F Rw2 U 3L' D2 Fw");
        app.click("launch.other");
        REQUIRE(app.wait_for([&]() { return app.screen() == "entry"; }));
        app.click("entry.size.5");
        type_scramble(five);
        tour.shoot("entry");

        app.click("entry.size.4");
        app.click("entry.colours");
        REQUIRE(app.wait_for([&]() { return app.screen() == "colours"; }));
        const cube::nxn::NCube four =
            cube::nxn::random_scramble({.size = 4, .seed = 5, .moves = 60});
        auto& entered = app.runtime().controllers().colours;
        for (const char* side : {"U", "F"}) {
            const auto face = side[0] == 'U' ? cube::solver::Face::U : cube::solver::Face::F;
            for (int row = 0; row < 4; ++row) {
                for (int col = 0; col < 4; ++col) {
                    entered.paint(QString::fromLatin1(side), (row * 4) + col,
                                  static_cast<int>(four.at(face, row, col)));
                }
            }
        }
        app.click("colours.side.F");
        tour.shoot("colours");
        app.click("colours.back");
        app.click("colours.discard.confirm");
        REQUIRE(app.wait_for([&]() { return app.screen() == "entry"; }));

        app.click("entry.size.5");
        type_scramble(five);
        app.click("entry.solve");
        REQUIRE(app.wait_for([&]() { return app.screen() == "solve"; }));
        REQUIRE(app.wait_for([&]() { return !solve.isSolving(); }, 180'000));
        REQUIRE(solve.size() == 5);
        // A few turns in, at the next turn of a layer inside the cube.
        for (int i = 0; i < 5 && !solve.atEnd(); ++i) {
            solve.nextMove();
        }
        while (!solve.atEnd() && solve.currentMoveFirst() == 0) {
            solve.nextMove();
        }
        const int inner = solve.moveIndex();
        app.pump(40);
        tour.shoot("solve-big-middle");
        while (!solve.atEnd()) {
            solve.nextMove();
        }
        app.click("solve.next");
        REQUIRE(app.wait_for([&]() { return app.screen() == "complete"; }));
        app.click("complete.review");
        REQUIRE(app.wait_for([&]() { return app.screen() == "solution"; }));
        REQUIRE(QMetaObject::invokeMethod(app.screen_item(), "jumpTo", Q_ARG(QVariant, inner)));
        app.pump(80);
        tour.shoot("solution-big-middle");
        app.click("solution.back");
        REQUIRE(app.wait_for([&]() { return app.screen() == "complete"; }));
        app.go("launch");

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

    // Twenty-one pictures a variant, two variants a layout.
    CHECK(variants == 2);
    CHECK(all.size() == 21 * variants);
}

}  // namespace

TEST_CASE("the screenshot tour: every screen in the narrow layout, light and dark",
          "[system][tour][narrow]") {
    tour_layout("narrow");
}

TEST_CASE("the screenshot tour: every screen in the wide layout, light and dark",
          "[system][tour][wide]") {
    tour_layout("wide");
}
