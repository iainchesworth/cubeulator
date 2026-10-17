#include <catch2/catch_test_macros.hpp>

#include "drawn_cube.hpp"

#include <cube/solver/cube.hpp>
#include <cube/solver/hold.hpp>
#include <cube/solver/solver.hpp>
#include <cube/vision/direct_scan.hpp>
#include <cube/vision/held_cube.hpp>

#include <opencv2/core.hpp>

#include <array>
#include <cstddef>
#include "must_have.hpp"

using cube::solver::all_holds;
using cube::solver::camera_view;
using cube::solver::Color;
using cube::solver::Cube;
using cube::solver::Face;
using cube::solver::Hold;
using cube::solver::kStatedHold;
using cube::solver::Move;
using cube::vision::colour_distance;
using cube::vision::HoldReading;
using cube::vision::kHoldClearDistance;
using cube::vision::kHoldFitDistance;
using cube::vision::read_hold;
using cube::vision::SessionColours;

namespace {

// Six colours as one scan might have measured them, indexed by solver::Color
// (white, yellow, green, blue, orange, red). Red and orange are the close
// pair, as they are on a real cube.
[[nodiscard]] SessionColours session() {
    SessionColours colours{};
    colours.at(static_cast<std::size_t>(Color::White)) = {92.0F, 0.0F, 3.0F};
    colours.at(static_cast<std::size_t>(Color::Yellow)) = {86.0F, -6.0F, 72.0F};
    colours.at(static_cast<std::size_t>(Color::Green)) = {56.0F, -52.0F, 34.0F};
    colours.at(static_cast<std::size_t>(Color::Blue)) = {36.0F, 8.0F, -52.0F};
    colours.at(static_cast<std::size_t>(Color::Orange)) = {62.0F, 40.0F, 50.0F};
    colours.at(static_cast<std::size_t>(Color::Red)) = {50.0F, 46.0F, 44.0F};
    return colours;
}

// What the camera would measure of `cube` held as `hold`: each sticker its
// colour as the session measured it, pushed about by `wobble` Lab units in a
// pattern that differs from sticker to sticker.
[[nodiscard]] std::array<cv::Vec3f, 9> seen(const Cube& cube, const Hold& hold,
                                            float wobble = 0.0F) {
    const SessionColours colours = session();
    const std::array<Color, 9> shown = camera_view(cube, hold);
    std::array<cv::Vec3f, 9> lab{};
    for (std::size_t cell = 0; cell < 9; ++cell) {
        const auto sign = [cell](std::size_t shift) {
            return ((cell >> shift) & 1U) == 0U ? 1.0F : -1.0F;
        };
        lab.at(cell) = colours.at(static_cast<std::size_t>(shown.at(cell))) +
                       cv::Vec3f(sign(0) * wobble, sign(1) * wobble, sign(2) * wobble);
    }
    return lab;
}

}  // namespace

TEST_CASE("a side square to the camera says how the cube is held", "[vision][held-cube]") {
    for (const unsigned seed : {3U, 4U, 5U}) {
        const Cube cube = cube::solver::random_scramble(seed);
        for (const Hold& hold : all_holds()) {
            for (const float wobble : {0.0F, 1.5F}) {
                const HoldReading reading = read_hold(seen(cube, hold, wobble), cube, session());
                REQUIRE(reading.shows(hold));
                CHECK(reading.distance < kHoldFitDistance);
                // Anything else it shows looks exactly the same to the camera.
                for (const Hold& other : reading.holds) {
                    CHECK(camera_view(cube, other) == camera_view(cube, hold));
                }
            }
        }
    }
}

TEST_CASE("a side that looks the same turned leaves the way up open", "[vision][held-cube]") {
    // Every side of a solved cube is one colour: it shows which side faces
    // the camera, and nothing about which of four ways up.
    const Cube solved = Cube::solved();
    const HoldReading reading = read_hold(seen(solved, kStatedHold), solved, session());
    REQUIRE(reading.holds.size() == 4);
    for (const Hold& hold : reading.holds) {
        CHECK(hold.front == kStatedHold.front);
    }
}

TEST_CASE("a side that is not on the cube shows no hold", "[vision][held-cube]") {
    const Cube cube = cube::solver::random_scramble(6);
    int changed = 0;
    for (const Hold& hold : all_holds()) {
        for (const Face face : cube::solver::kAllFaces) {
            const Cube moved = cube.apply(Move{.face = face, .turns = 1});
            if (camera_view(moved, hold) == camera_view(cube, hold)) {
                continue;  // the move did not touch the side the camera sees
            }
            if (cube::solver::side_at(hold, cube::solver::Place::Back) == face) {
                // The side that was turned: the cube before the move shows
                // the very same nine colours, held a quarter turn round.
                CHECK(!read_hold(seen(moved, hold), cube, session()).holds.empty());
                continue;
            }
            ++changed;
            const std::array<cv::Vec3f, 9> lab = seen(moved, hold, 1.0F);
            const HoldReading before = read_hold(lab, cube, session());
            const HoldReading after = read_hold(lab, moved, session());
            // The cube after the move explains it, and explains it clearly
            // better than the cube before - which is how a move is told from
            // a misread, even where only one sticker of the side changed.
            REQUIRE(after.shows(hold));
            CHECK(before.distance >= after.distance + kHoldClearDistance);
        }
    }
    CHECK(changed > 40);

    // Nine stickers that are no side of this cube at all.
    std::array<cv::Vec3f, 9> stripes{};
    for (std::size_t cell = 0; cell < 9; ++cell) {
        stripes.at(cell) = session().at(cell % 6);
    }
    CHECK(read_hold(stripes, cube, session()).holds.empty());
}

TEST_CASE("one sticker of the wrong colour is told apart even for red and orange",
          "[vision][held-cube]") {
    // The narrowest gap sets how small a difference has to be noticed. On
    // the recorded scans red and orange were between 7.8 and 17.9 apart;
    // this session's are inside that range, towards the narrow end.
    const float gap = colour_distance(session().at(static_cast<std::size_t>(Color::Red)),
                                      session().at(static_cast<std::size_t>(Color::Orange)));
    REQUIRE(gap > 7.8F);
    REQUIRE(gap < 17.9F);
    CHECK(gap / 9.0F > kHoldClearDistance);
}

TEST_CASE("a sticker with a finger across it is known not to have been found",
          "[vision][held-cube]") {
    const Cube cube = cube::solver::random_scramble(6);
    const std::array<Color, 9> side = camera_view(cube, kStatedHold);
    const cube::test::Pose pose{};

    const auto clear = cube::vision::read_side(cube::test::draw_side(side, pose));
    const auto& clear_value = ::cube::test::must_have(clear);
    CHECK(clear_value.stickers_found == 9);
    for (const bool found : clear_value.found) {
        CHECK(found);
    }

    for (const int covered : {0, 2, 3, 5, 6, 8}) {
        INFO("finger across sticker " << covered);
        cv::Mat frame = cube::test::draw_side(side, pose);
        cube::test::lay_finger(frame, pose, covered);
        const auto read = cube::vision::read_side(frame);
        const auto& read_value = ::cube::test::must_have(read);
        CHECK(read_value.stickers_found == 8);
        for (int cell = 0; cell < 9; ++cell) {
            CHECK(read_value.found.at(static_cast<std::size_t>(cell)) == (cell != covered));
        }
    }
}

TEST_CASE("a sticker that was not found says nothing about how the cube is held",
          "[vision][held-cube]") {
    // What is sampled under a finger is the finger. Skin sits near orange and
    // red, so over one of those it passes for the sticker; over any other
    // colour, counted, it makes the side look like no side of the cube. Left
    // out, the other eight say which side it is every time.
    const Cube cube = cube::solver::random_scramble(6);
    int spoiled = 0;
    for (const Hold& hold : all_holds()) {
        cube::vision::SideRead read;
        const std::array<cv::Vec3f, 9> measured = seen(cube, hold, 1.0F);
        read.lab.assign(measured.begin(), measured.end());
        read.found.assign(9, true);
        read.stickers_found = 8;
        read.found.at(5) = false;
        read.lab.at(5) = {68.0F, 14.0F, 20.0F};  // skin

        const HoldReading found_only = read_hold(read, cube, session());
        const HoldReading all_nine = read_hold(read.lab, cube, session());
        CHECK(found_only.shows(hold));
        CHECK(found_only.distance <= all_nine.distance);
        spoiled += all_nine.shows(hold) ? 0 : 1;
        // Over green or blue there is no mistaking it for the sticker.
        const Color under = camera_view(cube, hold).at(5);
        if (under == Color::Green || under == Color::Blue) {
            CHECK_FALSE(all_nine.shows(hold));
        }
        // And it is not counted as a sticker of the wrong colour either.
        CHECK(cube::vision::stickers_off(read, camera_view(cube, hold), session()) == 0);
    }
    CHECK(spoiled >= 4);
}

TEST_CASE("stickers of the wrong colour are counted against the session's own colours",
          "[vision][held-cube]") {
    const Cube cube = cube::solver::random_scramble(6);
    const std::array<Color, 9> expected = camera_view(cube, kStatedHold);
    cube::vision::SideRead read;
    const std::array<cv::Vec3f, 9> measured = seen(cube, kStatedHold, 1.5F);
    read.lab.assign(measured.begin(), measured.end());
    read.found.assign(9, true);
    read.stickers_found = 9;
    CHECK(cube::vision::stickers_off(read, expected, session()) == 0);

    // A glint: one sticker washed out towards white.
    const std::size_t glint = expected.at(0) == Color::White ? 1 : 0;
    read.lab.at(glint) = {96.0F, 0.0F, 1.0F};
    CHECK(cube::vision::stickers_off(read, expected, session()) ==
          (expected.at(glint) == Color::White ? 0 : 1));

    // A different side altogether.
    const std::array<Color, 9> other = camera_view(cube, Hold{.up = Face::R, .front = Face::U});
    CHECK(cube::vision::stickers_off(read, other, session()) >= 2);
}
