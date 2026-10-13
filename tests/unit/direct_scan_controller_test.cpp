#include <catch2/catch_test_macros.hpp>

#include "controllers/direct_scan.hpp"
#include "controllers/scan_session.hpp"
#include "drawn_cube.hpp"
#include "solve_controller.hpp"

#include <cube/solver/cube.hpp>
#include <cube/solver/solver.hpp>
#include <cube/vision/direct_scan.hpp>

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QPointF>
#include <QTemporaryDir>
#include <QVariantMap>

#include <opencv2/imgcodecs.hpp>

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

TEST_CASE("a side being held fills towards counting", "[qt][controllers][direct-scan]") {
    DirectScan scan;
    const cv::Mat frame = draw_side(kMixed, Pose{});

    scan.processFrame(frame);
    CHECK(scan.captureProgress() == 0.0);

    qreal last = 0.0;
    for (int i = 1; i < kFramesToCapture - 1; ++i) {
        scan.processFrame(frame);
        CHECK(scan.captureProgress() > last);
        CHECK(scan.captureProgress() < 1.0);
        last = scan.captureProgress();
    }

    // Captured: its chip shows it now, so there is nothing left to fill.
    scan.processFrame(frame);
    REQUIRE(scan.sidesRead() == 1);
    CHECK(scan.captureProgress() == 0.0);
}

TEST_CASE("the sixth side is on show before the scan is announced complete",
          "[qt][controllers][direct-scan]") {
    // A listener that left the scan screen the instant the scan completed
    // used to do so before the last side had ever been reported.
    DirectScan scan;
    int sides_when_completed = -1;
    bool complete_when_completed = false;
    QObject::connect(&scan, &DirectScan::scanCompleted, [&]() {
        sides_when_completed = scan.sidesRead();
        complete_when_completed = scan.complete();
    });
    int sides_changed_while_complete = 0;
    QObject::connect(&scan, &DirectScan::sidesChanged, [&]() {
        sides_changed_while_complete += scan.complete() && scan.sidesRead() == 6 ? 1 : 0;
    });

    CHECK_FALSE(scan.complete());
    show_cube(scan, cube::solver::random_scramble(9));

    CHECK(sides_when_completed == 6);
    CHECK(complete_when_completed);
    CHECK(sides_changed_while_complete == 1);
    // Nothing is being read any more, so nothing is marked on the picture.
    CHECK(scan.stickerPoints().isEmpty());
    CHECK_FALSE(scan.reading());

    scan.restart();
    CHECK_FALSE(scan.complete());
}

TEST_CASE("a scan stores nothing unless a developer turns recording on",
          "[qt][controllers][direct-scan]") {
    // The permission screen promises that nothing is stored. That holds
    // because recording has no switch in the app: it is off unless a
    // developer names a folder for it.
    DirectScan scan;
    CHECK(scan.recordingRoot().isEmpty());

    QTemporaryDir folder;
    REQUIRE(folder.isValid());
    show_cube(scan, cube::solver::random_scramble(13));
    REQUIRE(scan.complete());
    CHECK(QDir(folder.path()).isEmpty());
}

TEST_CASE("a recorded scan replays to the cube it read", "[qt][controllers][direct-scan]") {
    const Cube cube = cube::solver::random_scramble(14);
    QTemporaryDir folder;
    REQUIRE(folder.isValid());

    DirectScan scan;
    int announced = 0;
    QObject::connect(&scan, &DirectScan::recordingRootChanged, [&announced]() { ++announced; });
    scan.setRecordingRoot(folder.path());
    CHECK(scan.recordingRoot() == folder.path());
    CHECK(announced == 1);

    show_cube(scan, cube);
    REQUIRE(scan.complete());

    // One folder for the one scan, holding the frames the reader worked on
    // and nothing larger.
    const QStringList scans = QDir(folder.path()).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    REQUIRE(scans.size() == 1);
    const QDir recorded(QDir(folder.path()).filePath(scans.front()));
    CHECK(recorded.exists("manifest.json"));
    CHECK_FALSE(recorded.exists("fullres"));
    const QStringList frames = recorded.entryList({"rec_*.jpg"}, QDir::Files, QDir::Name);
    REQUIRE_FALSE(frames.isEmpty());

    // What was written is enough to reach the same cube again.
    SideScan replay;
    for (const QString& frame : frames) {
        const cv::Mat bgr = cv::imread(recorded.filePath(frame).toStdString(), cv::IMREAD_COLOR);
        REQUIRE_FALSE(bgr.empty());
        replay.add(cube::vision::read_side(bgr));
    }
    const auto read = replay.cube();
    REQUIRE(read.has_value());
    CHECK(*read == cube);
}

TEST_CASE("each scan is recorded to its own folder", "[qt][controllers][direct-scan]") {
    QTemporaryDir folder;
    REQUIRE(folder.isValid());
    DirectScan scan;
    scan.setRecordingRoot(folder.path());

    hold(scan, draw_side(kMixed, Pose{}), kFramesToCapture);
    scan.restart();
    // Folders are named to the millisecond; let the clock move on.
    QElapsedTimer waited;
    waited.start();
    while (waited.elapsed() < 5) {
    }
    hold(scan, draw_side(kMixed, Pose{}), kFramesToCapture);
    scan.setRecordingRoot(QString());

    CHECK(QDir(folder.path()).entryList(QDir::Dirs | QDir::NoDotAndDotDot).size() == 2);
    CHECK(scan.recordingRoot().isEmpty());
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

    // Solving has started, and nothing has asked for the solve screen: the
    // finished scan stays on show while the solution is worked out.
    REQUIRE(solver.isSolving());
    CHECK(ready == 0);

    // The solve runs on a worker; its result lands through the event loop.
    QElapsedTimer waited;
    waited.start();
    while (solver.isSolving() && waited.elapsed() < 60'000) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    }
    REQUIRE_FALSE(solver.isSolving());
    CHECK(solver.totalMoves() > 0);
    CHECK(solver.atStart());
    CHECK(ready == 0);
}
