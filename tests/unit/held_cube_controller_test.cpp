// HeldCube: the camera's part in the solve screen, driven by drawn frames.
//
// A cube is scanned from drawings of its six sides, handed to the solver,
// and then shown to the camera the way a person holding it would show it -
// camera_view() of a hold, drawn the same way the scan's frames were. No
// camera and no recording: frames go in through DirectScan::processFrame.
#include <catch2/catch_test_macros.hpp>

#include "controllers/direct_scan.hpp"
#include "controllers/held_cube.hpp"
#include "controllers/scan_session.hpp"
#include "drawn_cube.hpp"
#include "solve_controller.hpp"

#include <cube/solver/cube.hpp>
#include <cube/solver/hold.hpp>
#include <cube/solver/solver.hpp>
#include <cube/vision/direct_scan.hpp>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QObject>

#include <array>
#include <optional>

using cube::app::DirectScan;
using cube::app::HeldCube;
using cube::solver::all_holds;
using cube::solver::camera_view;
using cube::solver::Color;
using cube::solver::Cube;
using cube::solver::Face;
using cube::solver::Hold;
using cube::solver::kAllFaces;
using cube::solver::kStatedHold;
using cube::solver::Move;
using cube::solver::Place;
using cube::solver::side_at;
using cube::test::draw_side;
using cube::test::Pose;
using cube::test::room;
using cube::vision::SideScan;

namespace {

// Frames a side has to be held for before it counts, in a scan and here: the
// first frame has nothing to be steady against.
constexpr int kFramesToCapture = static_cast<int>(SideScan::kMinReads) + 1;
constexpr int kFramesToTrust = HeldCube::kFramesToTrust + 1;
constexpr int kFramesToDoubt = HeldCube::kFramesToDoubt + 1;

// The app's camera and solver with a cube scanned, solved and being watched.
struct Session {
    DirectScan scan;
    cube::app::ScanSession legacy;
    SolveController solve{legacy};
    HeldCube held{scan, solve};
    int holds_changed = 0;
    int moves_seen = 0;

    explicit Session(const Cube& cube) {
        QObject::connect(&scan, &DirectScan::scanCompleted, &solve,
                         [this]() { solve.solveCube(*scan.cube()); });
        for (const Face face : kAllFaces) {
            show(draw_side(cube::test::outside_view(cube, face), Pose{}), kFramesToCapture + 3);
            look_away();
        }
        REQUIRE(scan.cube().has_value());
        REQUIRE(*scan.cube() == cube);
        REQUIRE(scan.colours().has_value());

        // The solve runs on a worker; its result lands through the event loop.
        QElapsedTimer waited;
        waited.start();
        while (solve.isSolving() && waited.elapsed() < 60'000) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        }
        REQUIRE_FALSE(solve.isSolving());
        REQUIRE(solve.currentCube() == cube);
        REQUIRE(solve.currentMove().has_value());

        QObject::connect(&held, &HeldCube::holdChanged, [this]() { ++holds_changed; });
        QObject::connect(&held, &HeldCube::moveSeen, [this]() { ++moves_seen; });
        scan.setWatching(true);
    }

    void show(const cv::Mat& frame, int frames) {
        for (int i = 0; i < frames; ++i) {
            scan.processFrame(frame);
        }
    }
    // `cube` held as `hold`, as the camera facing the holder sees it.
    void show(const Cube& cube, const Hold& hold, int frames) {
        show(draw_side(camera_view(cube, hold), Pose{}), frames);
    }
    void look_away() { scan.processFrame(room({640, 360})); }
};

// The letter HeldCube names a side by. Face's values run U, D, L, R, F, B.
[[nodiscard]] QString side_letter(Face face) {
    return QString(QChar::fromLatin1("UDLRFB"[static_cast<int>(face)]));
}

// A hold from which the camera can see `move` having been made: the side
// facing it is changed by the move, and is not the side that was turned
// (which only turns on the spot).
[[nodiscard]] std::optional<Hold> hold_that_sees(const Cube& cube, Move move) {
    const Cube moved = cube.apply(move);
    for (const Hold& hold : all_holds()) {
        if (side_at(hold, Place::Back) != move.face &&
            camera_view(moved, hold) != camera_view(cube, hold)) {
            return hold;
        }
    }
    return std::nullopt;
}

}  // namespace

TEST_CASE("the cube is taken to be held as asked until the camera sees it",
          "[qt][controllers][held-cube]") {
    Session session(cube::solver::random_scramble(3));
    CHECK(session.held.hold() == kStatedHold);
    CHECK_FALSE(session.held.seen());
    CHECK(session.held.sight() == "none");
    CHECK(session.held.up() == "U");
    CHECK(session.held.front() == "F");
    CHECK(session.held.places().value("R").toString() == "right");

    // An empty room changes nothing.
    session.show(room({640, 360}), kFramesToDoubt);
    CHECK(session.held.hold() == kStatedHold);
    CHECK_FALSE(session.held.seen());
    CHECK(session.holds_changed == 0);
}

TEST_CASE("a side held square to the camera turns the cube on screen to match",
          "[qt][controllers][held-cube]") {
    const Cube cube = cube::solver::random_scramble(3);
    Session session(cube);

    // Held as asked: nothing turns, but now it has been seen.
    session.show(cube, kStatedHold, kFramesToTrust);
    CHECK(session.held.hold() == kStatedHold);
    CHECK(session.held.seen());
    CHECK(session.held.sight() == "matched");

    for (const Hold& hold : all_holds()) {
        INFO("up " << static_cast<int>(hold.up) << " front " << static_cast<int>(hold.front));
        session.look_away();
        CHECK(session.held.sight() == "none");

        // Not on the strength of a frame or two.
        const Hold before = session.held.hold();
        session.show(cube, hold, kFramesToTrust - 2);
        CHECK(session.held.hold() == before);

        session.show(cube, hold, 2);
        CHECK(session.held.hold() == hold);
        CHECK(session.held.sight() == "matched");
        CHECK(session.held.places().value("U").toString() ==
              (hold.up == Face::U                       ? "top"
               : hold.up == Face::D                     ? "bottom"
               : hold.front == Face::U                  ? "front"
               : hold.front == Face::D                  ? "back"
               : side_at(hold, Place::Right) == Face::U ? "right"
                                                        : "left"));
    }
    CHECK(session.moves_seen == 0);
    // Watching never adds to the scan or changes the cube it produced.
    CHECK(*session.scan.cube() == cube);
    CHECK(session.scan.sidesRead() == 6);
}

TEST_CASE("the move asked for, made in view of the camera, is seen once",
          "[qt][controllers][held-cube]") {
    const Cube cube = cube::solver::random_scramble(3);
    Session session(cube);
    const Move move = *session.solve.currentMove();
    const Cube moved = cube.apply(move);
    const std::optional<Hold> hold = hold_that_sees(cube, move);
    REQUIRE(hold.has_value());

    session.show(cube, *hold, kFramesToTrust);
    REQUIRE(session.held.hold() == *hold);
    CHECK(session.moves_seen == 0);

    // Hands go on the cube, the move is made, and the same side comes back.
    session.look_away();
    session.show(moved, *hold, kFramesToTrust - 2);
    CHECK(session.moves_seen == 0);
    session.show(moved, *hold, 2);
    CHECK(session.moves_seen == 1);
    CHECK(session.held.hold() == *hold);
    // It says so; stepping the solution is the screen's to do.
    CHECK(session.solve.moveIndex() == 0);

    // Held there longer, it is not seen again.
    session.show(moved, *hold, kFramesToDoubt);
    CHECK(session.moves_seen == 1);

    // Once the solution has stepped on, this is simply the cube as expected.
    session.solve.nextMove();
    session.show(moved, *hold, kFramesToTrust);
    CHECK(session.moves_seen == 1);
    CHECK(session.held.sight() == "matched");
    CHECK(session.held.hold() == *hold);
}

TEST_CASE("a move is seen even if the cube is shown a different way round afterwards",
          "[qt][controllers][held-cube]") {
    const Cube cube = cube::solver::random_scramble(3);
    Session session(cube);
    const Move move = *session.solve.currentMove();
    const Cube moved = cube.apply(move);
    const std::optional<Hold> hold = hold_that_sees(cube, move);
    REQUIRE(hold.has_value());
    REQUIRE_FALSE(*hold == kStatedHold);

    // Never shown before the move: the first the camera sees of the cube is
    // after it, held some other way than the screen assumed.
    session.show(moved, *hold, kFramesToTrust);
    CHECK(session.moves_seen == 1);
    CHECK(session.held.hold() == *hold);
    CHECK(session.held.seen());
}

// A hold with the side `move` turns facing the camera, where turning it shows.
[[nodiscard]] std::optional<Hold> hold_facing(const Cube& cube, Move move) {
    for (const Hold& hold : all_holds()) {
        if (side_at(hold, Place::Back) == move.face &&
            camera_view(cube.apply(move), hold) != camera_view(cube, hold)) {
            return hold;
        }
    }
    return std::nullopt;
}

TEST_CASE("the side facing the camera, turned as asked after the cube was seen, is the move",
          "[qt][controllers][held-cube]") {
    // Straight on, a side that has been turned looks exactly like the same
    // side on a cube rolled over instead. But this cube was seen a moment
    // ago, held this way, with this move being asked for - so it is the move.
    const Cube cube = cube::solver::random_scramble(3);
    Session session(cube);
    const Move move = *session.solve.currentMove();
    const Cube moved = cube.apply(move);
    const std::optional<Hold> facing = hold_facing(cube, move);
    REQUIRE(facing.has_value());

    session.show(cube, *facing, kFramesToTrust);
    REQUIRE(session.held.hold() == *facing);
    REQUIRE(session.held.sight() == "matched");
    session.look_away();

    session.show(moved, *facing, kFramesToTrust);
    CHECK(session.moves_seen == 1);
    CHECK(session.held.hold() == *facing);

    session.solve.nextMove();
    session.show(moved, *facing, kFramesToTrust);
    CHECK(session.moves_seen == 1);
    CHECK(session.held.sight() == "matched");
}

TEST_CASE("the same sight decides nothing if the cube had not been seen held that way",
          "[qt][controllers][held-cube]") {
    // With nothing to say the cube was not simply being held the other way
    // round all along, the camera neither steps the solution nor turns the
    // cube on screen.
    const Cube cube = cube::solver::random_scramble(3);
    Session session(cube);
    const Move move = *session.solve.currentMove();
    const Cube moved = cube.apply(move);
    const std::optional<Hold> facing = hold_facing(cube, move);
    REQUIRE(facing.has_value());

    session.held.setHold(*facing);  // assumed, never seen
    session.show(moved, *facing, kFramesToDoubt);
    CHECK(session.moves_seen == 0);
    CHECK(session.held.hold() == *facing);
    CHECK(session.held.sight() == "unsure");
}

TEST_CASE("a turn of some other side, made in view of the camera, is named",
          "[qt][controllers][held-cube]") {
    const Cube cube = cube::solver::random_scramble(3);
    Session session(cube);
    const Move asked = *session.solve.currentMove();

    // Some other side, turned where the camera can see the difference.
    std::optional<Move> wrong;
    std::optional<Hold> hold;
    for (const Face face : kAllFaces) {
        if (face == asked.face) {
            continue;
        }
        if (const auto witness = hold_that_sees(cube, Move{face, 1}); witness.has_value()) {
            wrong = Move{face, 1};
            hold = witness;
            break;
        }
    }
    REQUIRE(wrong.has_value());
    const Cube strayed = cube.apply(*wrong);

    session.show(cube, *hold, kFramesToTrust);
    REQUIRE(session.held.sight() == "matched");
    CHECK(session.held.wrongSide().isEmpty());
    session.look_away();

    session.show(strayed, *hold, kFramesToTrust);
    CHECK(session.held.sight() == "wrong");
    CHECK(session.held.wrongSide() == side_letter(wrong->face));
    // Named, and nothing else: the solution does not move and the cube on
    // screen does not turn.
    CHECK(session.moves_seen == 0);
    CHECK(session.solve.moveIndex() == 0);
    CHECK(session.held.hold() == *hold);

    // Turned back, it is the cube as expected again.
    session.look_away();
    session.show(cube, *hold, kFramesToTrust);
    CHECK(session.held.sight() == "matched");
    CHECK(session.held.wrongSide().isEmpty());
}

TEST_CASE("the right side turned the wrong way is named as that side",
          "[qt][controllers][held-cube]") {
    const Cube cube = cube::solver::random_scramble(3);
    Session session(cube);
    const Move asked = *session.solve.currentMove();
    // The other way, or - for a half turn - only half as far.
    const Move wrong{asked.face, asked.turns == 1 ? 3 : 1};
    const std::optional<Hold> hold = hold_that_sees(cube, wrong);
    REQUIRE(hold.has_value());
    REQUIRE(camera_view(cube.apply(wrong), *hold) != camera_view(cube.apply(asked), *hold));

    session.show(cube, *hold, kFramesToTrust);
    session.look_away();
    session.show(cube.apply(wrong), *hold, kFramesToTrust);
    CHECK(session.held.sight() == "wrong");
    CHECK(session.moves_seen == 0);
    CHECK(session.held.wrongSide() == side_letter(asked.face));
}

TEST_CASE("a side that is not on the cube is doubted and not followed",
          "[qt][controllers][held-cube]") {
    const Cube cube = cube::solver::random_scramble(3);
    Session session(cube);
    session.show(cube, kStatedHold, kFramesToTrust);
    session.look_away();

    // Two moves on from where the solution is: not the cube as it stands,
    // nor as it will stand after the next move.
    const Move next = *session.solve.currentMove();
    Cube strayed = cube.apply(next);
    std::optional<Hold> shown;
    for (const Face face : kAllFaces) {
        const Cube further = strayed.apply(Move{face, 1});
        for (const Hold& hold : all_holds()) {
            const auto view = camera_view(further, hold);
            bool familiar = false;
            for (const Hold& other : all_holds()) {
                familiar = familiar || view == camera_view(cube, other) ||
                           view == camera_view(strayed, other);
            }
            if (!familiar) {
                strayed = further;
                shown = hold;
                break;
            }
        }
        if (shown.has_value()) {
            break;
        }
    }
    REQUIRE(shown.has_value());

    session.show(strayed, *shown, kFramesToDoubt - 2);
    CHECK(session.held.sight() != "unknown");
    session.show(strayed, *shown, 2);
    CHECK(session.held.sight() == "unknown");
    CHECK(session.held.hold() == kStatedHold);
    CHECK(session.moves_seen == 0);
}

TEST_CASE("a side still moving through the picture is not read", "[qt][controllers][held-cube]") {
    const Cube cube = cube::solver::random_scramble(3);
    Session session(cube);
    const Hold hold{Face::F, Face::R};
    const auto side = camera_view(cube, hold);
    for (int i = 0; i < 3 * kFramesToTrust; ++i) {
        // A sticker's width or so from where the frame before had it.
        session.scan.processFrame(
            draw_side(side, Pose{.centre = {i % 2 == 0 ? 280.0F : 340.0F, 180.0F}}));
    }
    CHECK(session.held.hold() == kStatedHold);
    CHECK_FALSE(session.held.seen());
}

TEST_CASE("with no scan there is nothing to match, and watching never starts one",
          "[qt][controllers][held-cube]") {
    DirectScan scan;
    cube::app::ScanSession legacy;
    SolveController solve(legacy);
    HeldCube held(scan, solve);
    int completed = 0;
    QObject::connect(&scan, &DirectScan::scanCompleted, [&completed]() { ++completed; });

    scan.setWatching(true);
    const Cube cube = cube::solver::random_scramble(4);
    for (const Face face : kAllFaces) {
        for (int i = 0; i < kFramesToCapture + 3; ++i) {
            scan.processFrame(draw_side(cube::test::outside_view(cube, face), Pose{}));
        }
        scan.processFrame(room({640, 360}));
    }
    // The stickers are found and shown, and that is all.
    CHECK(completed == 0);
    CHECK(scan.sidesRead() == 0);
    CHECK_FALSE(scan.cube().has_value());
    CHECK(held.hold() == kStatedHold);
    CHECK_FALSE(held.seen());

    scan.processFrame(draw_side(cube::test::outside_view(cube, Face::U), Pose{}));
    CHECK(scan.stickerPoints().size() == 9);
    CHECK(scan.read().has_value());
}

TEST_CASE("a new cube to solve starts from the hold the screen asks for",
          "[qt][controllers][held-cube]") {
    const Cube cube = cube::solver::random_scramble(3);
    Session session(cube);
    const Hold hold{Face::L, Face::D};
    session.show(cube, hold, kFramesToTrust);
    REQUIRE(session.held.hold() == hold);
    REQUIRE(session.held.seen());

    session.solve.solveCube(cube::solver::random_scramble(5));
    QElapsedTimer waited;
    waited.start();
    while (session.solve.isSolving() && waited.elapsed() < 60'000) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    }
    REQUIRE_FALSE(session.solve.isSolving());
    CHECK(session.held.hold() == kStatedHold);
    CHECK_FALSE(session.held.seen());
}
