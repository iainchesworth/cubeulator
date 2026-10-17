// A misread scan put right by hand: the camera is shown a cube (drawn, with one
// sticker the wrong colour where the test wants a misread), the review is started
// from what it read, and the person's corrections make the cube it should have
// been - the whole way from frames to a cube the solver is given.
#include <catch2/catch_test_macros.hpp>

#include "colour_name.hpp"
#include "controllers/colour_entry.hpp"
#include "controllers/direct_scan.hpp"
#include "drawn_cube.hpp"
#include "solve_controller.hpp"

#include <cube/solver/cube.hpp>
#include <cube/solver/notation.hpp>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

#include <array>
#include <cstddef>
#include <cstdint>

using cube::app::ColourEntry;
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

constexpr int kFramesToCapture = static_cast<int>(SideScan::kMinReads) + 1;

void hold(DirectScan& scan, const cv::Mat& frame, int frames) {
    for (int i = 0; i < frames; ++i) {
        scan.processFrame(frame);
    }
}

// The six sides of `cube`, each shown square-on to the camera, with `misread`
// (a side of the cube and an index into it as the camera sees it) drawn the colour
// `as` instead of its own.
struct Misread {
    Face face;
    std::size_t index;
    Color as;
};

void show_cube(DirectScan& scan, const Cube& cube, const Misread* misread = nullptr) {
    for (const Face face : kAllFaces) {
        auto side = cube::test::outside_view(cube, face);
        if (misread != nullptr && misread->face == face) {
            side.at(misread->index) = misread->as;
        }
        hold(scan, draw_side(side, Pose{}), kFramesToCapture + 3);
        scan.processFrame(room({640, 360}));
    }
}

QString state_of(const ColourEntry& entry) {
    return entry.status().value("state").toString();
}

bool wait_until_solved(SolveController& solve) {
    QElapsedTimer waited;
    waited.start();
    while (solve.isSolving() && waited.elapsed() < 60'000) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    }
    return !solve.isSolving();
}

}  // namespace

TEST_CASE("there is nothing to review before a cube is shown", "[qt][controllers][review]") {
    SolveController solve;
    DirectScan scan;
    ColourEntry entry(solve, &scan);
    CHECK_FALSE(scan.reviewable());
    CHECK_FALSE(entry.beginFromScan());
    CHECK_FALSE(entry.fromScan());

    ColourEntry no_scan(solve);
    CHECK_FALSE(no_scan.beginFromScan());
}

TEST_CASE("a scan that made a cube can be looked over as it was read", "[qt][controllers][review]") {
    const Cube cube = cube::test::scramble(3);
    SolveController solve;
    DirectScan scan;
    ColourEntry entry(solve, &scan);
    show_cube(scan, cube);
    REQUIRE(scan.complete());
    CHECK(scan.reviewable());

    REQUIRE(entry.beginFromScan());
    CHECK(entry.fromScan());
    CHECK(entry.upright());
    CHECK(entry.size() == 3);
    CHECK(state_of(entry) == "valid");
    // Looking is not changing: nothing to lose by leaving, nothing to undo.
    CHECK_FALSE(entry.touched());
    CHECK_FALSE(entry.canUndo());
    // Every side shows the cube's own sticker colours, however it is turned in the
    // reading (the sides are named by the middle one).
    for (const Face face : kAllFaces) {
        const QString letter = QString::fromLatin1("UDLRFB").mid(static_cast<int>(face), 1);
        CHECK(entry.centres().value(letter).toString() == colour_name(cube.face(face)[4]));
        CHECK(entry.paintedOn(letter) == 9);
    }
    // And it can be solved from here, as the scan would have.
    CHECK(entry.solve().value("ok").toBool());
    CHECK(wait_until_solved(solve));
}

TEST_CASE("a sticker the camera misread is found by the count and put right",
          "[qt][controllers][review]") {
    const Cube cube = cube::test::scramble(5);
    SolveController solve;
    DirectScan scan;
    ColourEntry entry(solve, &scan);

    // The top-left sticker of the front, drawn as the colour the front's middle is
    // not: the scan has six sides and no cube.
    const Color real = cube::test::outside_view(cube, Face::F).at(0);
    const Color wrong = real == Color::Red ? Color::Orange : Color::Red;
    const Misread misread{.face = Face::F, .index = 0, .as = wrong};
    show_cube(scan, cube, &misread);
    REQUIRE_FALSE(scan.complete());
    REQUIRE(scan.conflicted());
    CHECK(scan.reviewable());

    REQUIRE(entry.beginFromScan());
    CHECK(entry.fromScan());
    // The sides could not be turned to fit, so they are as the camera had them.
    CHECK_FALSE(entry.upright());
    CHECK_FALSE(entry.touched());
    // The camera's own colour naming has to give every colour nine stickers, so it
    // moved a borderline sticker to make the count come right: counts hold and the
    // cube does not. The review says so, and marks the sticker it was least sure of.
    const QVariantMap status = entry.status();
    REQUIRE(status.value("state").toString() == "impossible");
    CHECK_FALSE(entry.solve().value("ok").toBool());   // not a cube: never reaches the solver
    CHECK(entry.doubtCount() >= 1);

    // The misread sticker is on the front, top-left as seen from outside.
    CHECK(entry.sides().value("F").toList().at(0).toString() == colour_name(wrong));
    entry.paint("F", 0, static_cast<int>(real));

    // The one it moved is marked, and is not what the cube has there.
    QString doubted_side;
    int doubted_index = -1;
    for (const char* letter : {"U", "L", "R", "F", "B"}) {
        const QVariantList flags = entry.doubts().value(QString::fromLatin1(letter)).toList();
        for (int i = 0; i < flags.size(); ++i) {
            if (flags.at(i).toBool()) {
                doubted_side = QString::fromLatin1(letter);
                doubted_index = i;
            }
        }
    }
    REQUIRE(doubted_index >= 0);
    const Face doubted_face = doubted_side == "U"   ? Face::U
                              : doubted_side == "L" ? Face::L
                              : doubted_side == "R" ? Face::R
                              : doubted_side == "F" ? Face::F
                                                    : Face::B;
    const Color truth =
        cube::test::outside_view(cube, doubted_face).at(static_cast<std::size_t>(doubted_index));
    CHECK(entry.sides().value(doubted_side).toList().at(doubted_index).toString() !=
          colour_name(truth));
    entry.paint(doubted_side, doubted_index, static_cast<int>(truth));
    CHECK(entry.doubtCount() == 0);

    // The pieces do not fit until every side is the way up it is on the cube: the
    // bottom is drawn a half-turn from the library's view of it (drawn_cube.hpp),
    // which is what turning a side is for.
    CHECK(state_of(entry) == "impossible");
    entry.rotateSide("D", 2);
    CHECK(state_of(entry) == "valid");
    CHECK(entry.solve().value("ok").toBool());
    CHECK(wait_until_solved(solve));

    // And all of it can be taken back.
    entry.revert();
    CHECK_FALSE(entry.touched());
    CHECK(state_of(entry) == "impossible");
    CHECK(entry.doubtCount() >= 1);
}

TEST_CASE("sides that fit two ways can still be looked over", "[qt][controllers][review]") {
    // What libc++'s random_scramble(14) draws: perfectly read, its six flat sides are
    // two cubes (direct_scan_controller_test.cpp).
    Cube cube = Cube::solved();
    for (const cube::solver::Move move : *cube::solver::parse_algorithm(
             "R F' D' L L B B' D' R F2 L' U' F2 B B2 U B2 R D' U2 R2 L' L2 F L2")) {
        cube = cube.apply(move);
    }
    SolveController solve;
    DirectScan scan;
    ColourEntry entry(solve, &scan);
    show_cube(scan, cube);
    REQUIRE(scan.ambiguous());

    REQUIRE(entry.beginFromScan());
    CHECK_FALSE(entry.upright());
    CHECK_FALSE(entry.touched());
    // Every colour is there nine times: what is wrong is not a count.
    CHECK(entry.status().value("toGo").toInt() == 0);
    CHECK(state_of(entry) != "miscount");
}
