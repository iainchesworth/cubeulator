#include <catch2/catch_test_macros.hpp>

#include "controllers/direct_scan.hpp"
#include "drawn_corner.hpp"
#include "drawn_cube.hpp"
#include "solve_controller.hpp"

#include <cube/nxn/cube.hpp>
#include <cube/nxn/outside_view.hpp>
#include <cube/nxn/scan_assembly.hpp>
#include <cube/solver/cube.hpp>
#include <cube/solver/notation.hpp>
#include <cube/solver/solver.hpp>
#include <cube/vision/direct_scan.hpp>

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QPointF>
#include <QTemporaryDir>
#include <QVariantMap>

#include <opencv2/imgcodecs.hpp>

#include <array>
#include <cstddef>
#include <optional>

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

TEST_CASE("DirectScan scans a cube of another odd size: its dots, its sides and its cube",
          "[qt][controllers][direct-scan][nxn]") {
    DirectScan scan;
    CHECK(scan.size() == 3);
    CHECK(scan.defaultSize() == 3);
    int size_changes = 0;
    QObject::connect(&scan, &DirectScan::sizeChanged, [&size_changes]() { ++size_changes; });

    // Only a size from 2 to 9 is taken.
    scan.setSize(1);
    scan.setSize(10);
    scan.setSize(11);
    scan.setSize(3);
    CHECK(scan.size() == 3);
    CHECK(size_changes == 0);

    scan.setSize(5);
    CHECK(scan.size() == 5);
    CHECK(size_changes == 1);

    const cube::nxn::NCube cube = cube::nxn::random_scramble({.size = 5, .seed = 21, .moves = 100});
    int completed = 0;
    QObject::connect(&scan, &DirectScan::scanCompleted, [&completed]() { ++completed; });

    cube::test::Pose pose;
    pose.pitch = 360.0F * 0.6F / 5.0F;
    bool dotted = false;
    for (const Face face : kAllFaces) {
        const cv::Mat frame = draw_side(cube::nxn::outside_view(cube, face), pose);
        hold(scan, frame, kFramesToCapture + 3);
        // A dot on every one of the 25 stickers of the side in view.
        dotted = dotted || scan.stickerPoints().size() == 25;
        scan.processFrame(room({640, 360}));
    }
    CHECK(dotted);

    REQUIRE(completed == 1);
    CHECK(scan.complete());
    CHECK(scan.sidesRead() == 6);
    REQUIRE(scan.cubeN().has_value());
    CHECK(scan.cubeN()->size() == 5);
    // Not a 3x3: nothing for the 3x3's answer.
    CHECK_FALSE(scan.cube().has_value());
    for (const QVariant& side : scan.sides()) {
        CHECK(side.toMap().value("colors").toList().size() == 25);
    }
    // The review is of the cube it made, at its size.
    const auto reading = scan.reviewReading();
    REQUIRE(reading.has_value());
    CHECK(reading->size == 5);
    CHECK(reading->stickers.size() == 150);

    // Setting the size starts a scan over.
    scan.setSize(3);
    CHECK(size_changes == 2);
    CHECK(scan.sidesRead() == 0);
    CHECK_FALSE(scan.complete());
}

TEST_CASE("the size of the cube is found from the cube held up, and a chosen size stands",
          "[qt][controllers][direct-scan][nxn]") {
    for (const int size : {2, 3, 4, 5}) {
        CAPTURE(size);
        // A cube whose six flat sides make one cube (a 2x2's often make two).
        std::optional<cube::nxn::NCube> unambiguous;
        for (unsigned seed = 70; !unambiguous.has_value(); ++seed) {
            const cube::nxn::NCube tried =
                cube::nxn::random_scramble({.size = size, .seed = seed, .moves = 25 * size});
            cube::nxn::ScannedSides sides;
            for (std::size_t i = 0; i < 6; ++i) {
                sides.at(i) = cube::nxn::outside_view(tried, kAllFaces.at(i));
            }
            if (cube::nxn::assemble_all_scanned_sides(size, sides, 8).size() == 1) {
                unambiguous = tried;
            }
        }
        const cube::nxn::NCube& cube = *unambiguous;
        DirectScan scan;
        CHECK(scan.autoSize());
        CHECK(scan.size() == 3);
        int completed = 0;
        QObject::connect(&scan, &DirectScan::scanCompleted, [&completed]() { ++completed; });

        // The first frames of the first side go to finding the size, so it is held longer.
        cube::test::Pose pose;
        pose.pitch = 180.0F / static_cast<float>(size);
        for (const Face face : kAllFaces) {
            hold(scan, draw_side(cube::nxn::outside_view(cube, face), pose), kFramesToCapture + 9);
            scan.processFrame(room({640, 360}));
        }
        // Found from the first frames, not asked, and the scan is of that size.
        CHECK(scan.size() == size);
        CHECK(scan.autoSize());
        REQUIRE(completed == 1);
        REQUIRE(scan.cubeN().has_value());
        CHECK(scan.cubeN()->size() == size);
    }

    // A size that is chosen stands whatever is held up, until Auto is chosen again.
    const cube::nxn::NCube five = cube::nxn::random_scramble({.size = 5, .seed = 8, .moves = 100});
    cube::test::Pose pose;
    pose.pitch = 180.0F / 5.0F;
    DirectScan scan;
    scan.setSize(4);
    CHECK_FALSE(scan.autoSize());
    hold(scan, draw_side(cube::nxn::outside_view(five, Face::F), pose), 20);
    CHECK(scan.size() == 4);
    scan.useAutoSize();
    CHECK(scan.autoSize());
    CHECK(scan.size() == 3);
    hold(scan, draw_side(cube::nxn::outside_view(five, Face::F), pose), 20);
    CHECK(scan.size() == 5);

    // A new scan with no size asked for is found again; the size of the last is not kept.
    scan.setSize(2);
    scan.useDefaultSize();
    CHECK(scan.autoSize());
    CHECK(scan.size() == 3);
}

TEST_CASE("a bigger cube whose flat sides fit several cubes is settled by corner views",
          "[qt][controllers][direct-scan][nxn]") {
    for (const int size : {2, 4, 5}) {
        CAPTURE(size);
        const auto found = cube::test::cube_whose_flat_sides_fit_several(size);
        REQUIRE(found.has_value());
        const cube::nxn::NCube& cube = *found;

        DirectScan scan;
        scan.setSize(size);
        int completed = 0;
        QObject::connect(&scan, &DirectScan::scanCompleted, [&completed]() { ++completed; });

        cube::test::Pose pose;
        pose.pitch = 180.0F / static_cast<float>(size);
        for (const Face face : kAllFaces) {
            hold(scan, draw_side(cube::nxn::outside_view(cube, face), pose), kFramesToCapture + 3);
            scan.processFrame(room({640, 360}));
        }
        REQUIRE(scan.sidesRead() == 6);
        // Six sides, none of them wrong, and a way of putting them together that is not
        // the only one: the scan waits to be shown two sides at once.
        REQUIRE(scan.ambiguous());
        CHECK(completed == 0);
        CHECK_FALSE(scan.cubeN().has_value());
        CHECK(scan.settlingCorner().isEmpty());

        cube::test::Corner3Pose corner;
        corner.edge = 1.1F * (150.0F + (5.0F * static_cast<float>(size)));
        for (const Face vertical : {Face::U, Face::D}) {
            for (const Face depth : {Face::F, Face::B}) {
                for (const Face side : {Face::L, Face::R}) {
                    const cv::Mat frame = cube::test::draw_cube_corner3(
                        cube, vertical, depth, side, corner, cube::test::Drawing{});
                    if (!frame.empty() && completed == 0) {
                        scan.processFrame(room({640, 360}));
                        hold(scan, frame, kFramesToCapture + 4);
                    }
                }
            }
        }
        REQUIRE(completed == 1);
        REQUIRE(scan.cubeN().has_value());
        CHECK(scan.cubeN()->size() == size);
        CHECK_FALSE(scan.ambiguous());
    }
}

TEST_CASE("a scan of a 5x5 is recorded at its size and replays to the same cube",
          "[qt][controllers][direct-scan][nxn]") {
    QTemporaryDir folder;
    REQUIRE(folder.isValid());
    const cube::nxn::NCube cube = cube::nxn::random_scramble({.size = 5, .seed = 33, .moves = 100});

    DirectScan scan;
    scan.setSize(5);
    scan.setRecordingRoot(folder.path());
    cube::test::Pose pose;
    pose.pitch = 360.0F * 0.6F / 5.0F;
    for (const Face face : kAllFaces) {
        hold(scan, draw_side(cube::nxn::outside_view(cube, face), pose), kFramesToCapture + 3);
        scan.processFrame(room({640, 360}));
    }
    REQUIRE(scan.complete());

    const QStringList scans = QDir(folder.path()).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    REQUIRE(scans.size() == 1);
    const QDir recorded(QDir(folder.path()).filePath(scans.front()));
    QFile manifest(recorded.filePath("manifest.json"));
    REQUIRE(manifest.open(QIODevice::ReadOnly));
    // The manifest says what it is, so that cube_scan reads it as one.
    CHECK(manifest.readAll().contains("\"size\""));

    SideScan replay(5);
    for (const QString& frame : recorded.entryList({"rec_*.jpg"}, QDir::Files, QDir::Name)) {
        const cv::Mat bgr = cv::imread(recorded.filePath(frame).toStdString(), cv::IMREAD_COLOR);
        REQUIRE_FALSE(bgr.empty());
        replay.add(cube::vision::read_side(bgr, 5));
    }
    const auto read = replay.cube_n();
    REQUIRE(read.has_value());
    CHECK(*read == *scan.cubeN());
}
