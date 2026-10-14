#include <catch2/catch_test_macros.hpp>

#include "controllers/direct_scan.hpp"
#include "drawn_corner.hpp"
#include "drawn_cube.hpp"
#include "solve_controller.hpp"

#include <cube/solver/cube.hpp>
#include <cube/solver/notation.hpp>
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
        hold(scan, draw_side(cube::test::outside_view(cube, face), Pose{}), kFramesToCapture + 3);
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
    const Cube cube = cube::test::scramble(3);
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

TEST_CASE("a cube held on two corners is read three sides at a time and finishes once",
          "[qt][controllers][direct-scan]") {
    const Cube cube = cube::test::scramble(3);
    const auto corner = [&cube](Face a, Face b, Face c) {
        return cube::test::draw_corner3({cube::test::outside_view(cube, a),
                                         cube::test::outside_view(cube, b),
                                         cube::test::outside_view(cube, c)});
    };
    DirectScan scan;
    int completed = 0;
    QObject::connect(&scan, &DirectScan::scanCompleted, [&completed]() { ++completed; });

    // Held on the up-right-front corner: not a side square to the camera, so
    // nothing is marked on the picture, but it is three sides being read.
    hold(scan, corner(Face::U, Face::R, Face::F), kFramesToCapture - 1);
    CHECK(scan.stickerPoints().isEmpty());
    CHECK(scan.sidesRead() == 0);
    CHECK(scan.reading());
    CHECK(scan.captureProgress() > 0.0);
    CHECK(scan.captureProgress() < 1.0);

    scan.processFrame(corner(Face::U, Face::R, Face::F));
    REQUIRE(scan.sidesRead() == 3);
    for (const QVariant& side : scan.sides()) {
        CHECK(side.toMap().value("inView").toBool());
    }
    CHECK(completed == 0);
    CHECK_FALSE(scan.conflicted());

    // Turned to the corner across from it, the other three finish it.
    scan.processFrame(room({640, 360}));
    hold(scan, corner(Face::D, Face::L, Face::B), kFramesToCapture);

    REQUIRE(completed == 1);
    REQUIRE(scan.cube().has_value());
    CHECK(*scan.cube() == cube);
    CHECK(scan.sidesRead() == 6);
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
    show_cube(scan, cube::test::scramble(9));

    CHECK(sides_when_completed == 6);
    CHECK(complete_when_completed);
    CHECK(sides_changed_while_complete == 1);
    // Nothing is being read any more, so nothing is marked on the picture.
    CHECK(scan.stickerPoints().isEmpty());
    CHECK_FALSE(scan.reading());

    scan.restart();
    CHECK_FALSE(scan.complete());
}

TEST_CASE("sides that fit two ways are settled when the cube is turned so two show at once",
          "[qt][controllers][direct-scan]") {
    // What libc++'s random_scramble(14) draws: perfectly read, its six flat
    // sides are two cubes (tests/unit/direct_scan_test.cpp).
    Cube cube = Cube::solved();
    for (const cube::solver::Move move : *cube::solver::parse_algorithm(
             "R F' D' L L B B' D' R F2 L' U' F2 B B2 U B2 R D' U2 R2 L' L2 F L2")) {
        cube = cube.apply(move);
    }
    REQUIRE_FALSE(cube::test::sides_make_one_cube(cube));

    DirectScan scan;
    int completed = 0;
    QObject::connect(&scan, &DirectScan::scanCompleted, [&completed]() { ++completed; });
    show_cube(scan, cube);

    // Six sides, no cube, and the reason is not a misread.
    CHECK(scan.sidesRead() == 6);
    CHECK_FALSE(scan.complete());
    CHECK(scan.conflicted());
    CHECK(scan.ambiguous());

    // Turning the cube between sides shows two at once.
    for (const Face face : kAllFaces) {
        scan.processFrame(cube::test::draw_cube_corner(cube, face));
    }

    REQUIRE(scan.complete());
    CHECK_FALSE(scan.ambiguous());
    CHECK(completed == 1);
    REQUIRE(scan.cube().has_value());
    CHECK(*scan.cube() == cube);
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
    show_cube(scan, cube::test::scramble(13));
    REQUIRE(scan.complete());
    CHECK(QDir(folder.path()).isEmpty());
}

TEST_CASE("a recorded scan replays to the cube it read", "[qt][controllers][direct-scan]") {
    const Cube cube = cube::test::scramble(14);
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

TEST_CASE("only a scan is recorded, never the solve screen's camera",
          "[qt][controllers][direct-scan]") {
    QTemporaryDir folder;
    REQUIRE(folder.isValid());
    DirectScan scan;
    scan.setRecordingRoot(folder.path());

    // Watching from the start: nothing is written at all.
    scan.setWatching(true);
    hold(scan, draw_side(kMixed, Pose{}), kFramesToCapture * 2);
    CHECK(QDir(folder.path()).isEmpty());

    // A scan under way is recorded; turning to watching closes that
    // recording and adds no more frames to it.
    scan.setWatching(false);
    hold(scan, draw_side(kMixed, Pose{}), kFramesToCapture);
    scan.setWatching(true);
    const QStringList scans = QDir(folder.path()).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    REQUIRE(scans.size() == 1);
    const QDir recorded(QDir(folder.path()).filePath(scans.front()));
    const auto written = recorded.entryList({"rec_*.jpg"}, QDir::Files).size();
    CHECK(written == kFramesToCapture);

    hold(scan, draw_side(kMixed, Pose{}), kFramesToCapture * 2);
    CHECK(recorded.entryList({"rec_*.jpg"}, QDir::Files).size() == written);
    CHECK(QDir(folder.path()).entryList(QDir::Dirs | QDir::NoDotAndDotDot).size() == 1);
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
    const Cube cube = cube::test::scramble(5);
    DirectScan scan;
    SolveController solver;
    QObject::connect(&scan, &DirectScan::scanCompleted, &solver,
                     [&]() { solver.solveCube(*scan.cube()); });

    show_cube(scan, cube);

    // Solving has started. When to show the solution is the caller's to
    // decide, so the finished scan can stay on show meanwhile.
    REQUIRE(solver.isSolving());

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

TEST_CASE("an ambiguous cube asks for a corner by colour and showing it finishes the scan",
          "[qt][controllers][direct-scan]") {
    // The first cube held on corners by hand: orange and red sides solid, the
    // other four six and three. Its six sides make twenty cubes.
    const auto patterned = cube::test::patterned_cube();
    REQUIRE(patterned.has_value());
    const Cube& cube = *patterned;

    DirectScan scan;
    int completed = 0;
    QObject::connect(&scan, &DirectScan::scanCompleted, [&completed]() { ++completed; });
    CHECK(scan.settlingCorner().isEmpty());
    show_cube(scan, cube);

    REQUIRE(scan.sidesRead() == 6);
    REQUIRE(scan.ambiguous());
    CHECK(completed == 0);
    // Ambiguous is a way of not being a cube yet, and says so.
    CHECK(scan.conflicted());

    // It names three colours, each a colour of the cube.
    for (int ask = 0; ask < 3 && completed == 0; ++ask) {
        const QStringList corner = scan.settlingCorner();
        REQUIRE(corner.size() == 3);
        std::array<Face, 3> faces{};
        for (int i = 0; i < 3; ++i) {
            const QString name = corner.at(i);
            Color colour = Color::White;
            if (name == "yellow") {
                colour = Color::Yellow;
            } else if (name == "green") {
                colour = Color::Green;
            } else if (name == "blue") {
                colour = Color::Blue;
            } else if (name == "orange") {
                colour = Color::Orange;
            } else if (name == "red") {
                colour = Color::Red;
            } else {
                REQUIRE(name == "white");
            }
            for (const Face face : kAllFaces) {
                if (cube.face(face).at(4) == colour) {
                    faces.at(static_cast<std::size_t>(i)) = face;
                }
            }
        }
        scan.processFrame(room({640, 360}));
        hold(scan, cube::test::draw_cube_corner3(cube, faces.at(0), faces.at(1), faces.at(2)),
             kFramesToCapture + 4);
    }

    REQUIRE(completed == 1);
    REQUIRE(scan.cube().has_value());
    CHECK(*scan.cube() == cube);
    CHECK(scan.settlingCorner().isEmpty());

    // A new scan has asked for nothing yet.
    scan.restart();
    CHECK(scan.settlingCorner().isEmpty());
}
