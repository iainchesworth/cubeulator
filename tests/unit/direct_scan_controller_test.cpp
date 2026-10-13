#include <catch2/catch_test_macros.hpp>

#include "controllers/direct_scan.hpp"
#include "controllers/scan_session.hpp"
#include "drawn_cube.hpp"
#include "solve_controller.hpp"

#include <cube/solver/cube.hpp>
#include <cube/solver/solver.hpp>
#include <cube/vision/direct_scan.hpp>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QPointF>
#include <QVariantMap>

#include <array>
#include <cstddef>

using cube::app::DirectScan;
using cube::solver::Color;
using cube::solver::Cube;
using cube::solver::Face;
using cube::solver::kAllFaces;
using cube::test::draw_side;
using cube::test::outside_view;
using cube::test::Pose;
using cube::test::room;
using cube::vision::SideScan;

namespace {

constexpr std::array<Color, 9> kMixed = {Color::Green, Color::Orange, Color::Blue,
                                         Color::Blue,  Color::White,  Color::Yellow,
                                         Color::Green, Color::Red,    Color::White};

// Frames it takes for a side held still to count as captured: the first has
// nothing to be steady against.
constexpr int kFramesToCapture = static_cast<int>(SideScan::kMinReads) + 1;

void hold(DirectScan& scan, const cv::Mat& frame, int frames) {
    for (int i = 0; i < frames; ++i) {
        scan.processFrame(frame);
    }
}

void show_cube(DirectScan& scan, const Cube& cube) {
    for (const Face face : kAllFaces) {
        hold(scan, draw_side(outside_view(cube, face), Pose{}), kFramesToCapture + 3);
        scan.processFrame(room({640, 360}));  // the cube turns to its next side
    }
}

}  // namespace

TEST_CASE("DirectScan shows where the stickers are and which sides are read",
          "[qt][controllers][direct-scan]") {
    DirectScan scan;
    int side_changes = 0;
    QObject::connect(&scan, &DirectScan::sidesChanged, [&side_changes]() { ++side_changes; });

    scan.processFrame(room({640, 360}));
    CHECK(scan.stickerPoints().isEmpty());
    CHECK_FALSE(scan.reading());
    CHECK(scan.frameWidth() == 640);
    CHECK(scan.frameHeight() == 360);

    const Pose pose{.centre = {300.0F, 170.0F}};
    const cv::Mat frame = draw_side(kMixed, pose);
    scan.processFrame(frame);
    REQUIRE(scan.stickerPoints().size() == 9);
    const QPointF centre = scan.stickerPoints().at(4).toPointF();
    CHECK(std::abs(centre.x() - 300.0) < 2.0);
    CHECK(std::abs(centre.y() - 170.0) < 2.0);
    // Seen, but one frame is not yet holding still.
    CHECK_FALSE(scan.reading());
    CHECK(scan.sidesRead() == 0);
    CHECK(side_changes == 0);

    hold(scan, frame, kFramesToCapture - 1);
    CHECK(scan.reading());
    REQUIRE(scan.sidesRead() == 1);
    CHECK(side_changes > 0);
    const QVariantMap side = scan.sides().front().toMap();
    CHECK(side.value("inView").toBool());
    CHECK(side.value("colors").toList().size() == 9);
    CHECK_FALSE(scan.conflicted());
    CHECK_FALSE(scan.cube().has_value());

    scan.processFrame(room({640, 360}));
    CHECK(scan.stickerPoints().isEmpty());
    CHECK_FALSE(scan.sides().front().toMap().value("inView").toBool());
}

TEST_CASE("DirectScan finishes with the cube it was shown, once",
          "[qt][controllers][direct-scan]") {
    const Cube cube = cube::solver::random_scramble(3);
    DirectScan scan;
    int completed = 0;
    QObject::connect(&scan, &DirectScan::scanCompleted, [&completed]() { ++completed; });

    show_cube(scan, cube);

    REQUIRE(completed == 1);
    REQUIRE(scan.cube().has_value());
    CHECK(*scan.cube() == cube);
    CHECK(scan.sidesRead() == 6);

    // Frames keep arriving until the screen changes; the scan stays finished.
    hold(scan, draw_side(kMixed, Pose{}), kFramesToCapture + 3);
    CHECK(completed == 1);
    CHECK(*scan.cube() == cube);

    scan.restart();
    CHECK(scan.sidesRead() == 0);
    CHECK_FALSE(scan.cube().has_value());
}

TEST_CASE("a scanned cube handed to SolveController is solved for stepping",
          "[qt][controllers][direct-scan]") {
    const Cube cube = cube::solver::random_scramble(5);
    DirectScan scan;
    cube::app::ScanSession legacy;
    SolveController solver(legacy);
    int ready = 0;
    QObject::connect(&solver, &SolveController::solveReady, [&ready]() { ++ready; });
    QObject::connect(&scan, &DirectScan::scanCompleted, &solver,
                     [&]() { solver.solveCube(*scan.cube()); });

    show_cube(scan, cube);

    REQUIRE(ready == 1);
    // The solve runs on a worker; its result lands through the event loop.
    QElapsedTimer waited;
    waited.start();
    while (solver.isSolving() && waited.elapsed() < 60'000) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    }
    REQUIRE_FALSE(solver.isSolving());
    CHECK(solver.totalMoves() > 0);
    CHECK(solver.atStart());
}
