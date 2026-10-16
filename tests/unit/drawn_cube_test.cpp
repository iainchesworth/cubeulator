// The test helpers that draw a cube (drawn_cube.hpp), for a cube of any size:
// that a drawing has every sticker where the pose says it is and in the colour
// it was given, that the camera's view of a side is the side the cube has at
// that place, and that the turns of a side in its own plane are what they say.
// The 3x3 tests that were written first lean on all of it, so what they lean
// on is held here at every size from 2 to 9.
#include <catch2/catch_test_macros.hpp>

#include "drawn_cube.hpp"

#include <cube/nxn/cube.hpp>
#include <cube/nxn/geometry.hpp>
#include <cube/solver/cube.hpp>

#include <opencv2/core.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <span>
#include <vector>

namespace {

using cube::nxn::NCube;
using cube::solver::Color;
using cube::solver::Face;
using cube::test::Corner3Pose;
using cube::test::CornerPose;
using cube::test::Drawing;
using cube::test::Pose;

constexpr std::array<Color, 6> kColours = {
    Color::White, Color::Yellow, Color::Red, Color::Orange, Color::Green, Color::Blue,
};

// A side with no two neighbours alike in a row, for any size.
std::vector<Color> pattern(int size) {
    std::vector<Color> side;
    side.reserve(static_cast<std::size_t>(size) * static_cast<std::size_t>(size));
    for (int row = 0; row < size; ++row) {
        for (int col = 0; col < size; ++col) {
            side.push_back(kColours.at(static_cast<std::size_t>(row + (2 * col)) % 6));
        }
    }
    return side;
}

// The same side with every colour `by` places along.
std::vector<Color> recoloured(std::vector<Color> side, std::size_t by) {
    for (Color& colour : side) {
        colour = kColours.at((static_cast<std::size_t>(colour) + by) % 6);
    }
    return side;
}

// Whether the pixel at `at` is `expected`, within the rounding of a drawing.
bool pixel_is(const cv::Mat& frame, cv::Point2f at, const cv::Scalar& expected) {
    const auto& pixel = frame.at<cv::Vec3b>(cvRound(at.y), cvRound(at.x));
    for (int channel = 0; channel < 3; ++channel) {
        if (std::abs(static_cast<int>(pixel(channel)) - cvRound(expected(channel))) > 3) {
            return false;
        }
    }
    return true;
}

cv::Scalar plastic() {
    return {16, 16, 16};
}

// A pose that puts an N x N side comfortably in the 640 x 360 frame.
Pose fitting(int size) {
    Pose pose;
    pose.pitch = 280.0F / static_cast<float>(size);
    return pose;
}

}  // namespace

TEST_CASE("a side of any size is drawn sticker for sticker", "[drawn_cube]") {
    for (int size = cube::nxn::kMinSize; size <= cube::nxn::kMaxSize; ++size) {
        CAPTURE(size);
        const std::vector<Color> side = pattern(size);
        const Pose pose = fitting(size);
        const cv::Mat frame = cube::test::draw_side(side, pose);
        const float middle = 0.5F * static_cast<float>(size - 1);
        for (int row = 0; row < size; ++row) {
            for (int col = 0; col < size; ++col) {
                CAPTURE(row, col);
                const float column = static_cast<float>(col) - middle;
                const float line = static_cast<float>(row) - middle;
                CHECK(pixel_is(
                    frame, cube::test::place(pose, column, line),
                    cube::test::paint(side.at(static_cast<std::size_t>((row * size) + col)))));
                // Between this sticker and the next one along, and the next one
                // down, it is the plastic of the cube.
                if (col + 1 < size) {
                    CHECK(pixel_is(frame, cube::test::place(pose, column + 0.5F, line), plastic()));
                }
                if (row + 1 < size) {
                    CHECK(pixel_is(frame, cube::test::place(pose, column, line + 0.5F), plastic()));
                }
            }
        }
    }
}

TEST_CASE("a 3x3 is drawn the way it always was", "[drawn_cube]") {
    // The array overload and the one for any size are one function.
    const std::vector<Color> side = recoloured(pattern(3), 1);
    std::array<Color, 9> as_array{};
    std::ranges::copy(side, as_array.begin());
    const cv::Mat a = cube::test::draw_side(as_array, Pose{});
    const cv::Mat b = cube::test::draw_side(side, Pose{});
    CHECK(cv::norm(a, b, cv::NORM_INF) == 0.0);
    // And the default pose has the side's backing square 1.62 sticker spacings
    // from its centre, which a different formula for the backing would move.
    const Pose pose;
    CHECK(pixel_is(a, cube::test::place(pose, 1.55F, 0.0F), plastic()));
    CHECK_FALSE(pixel_is(a, cube::test::place(pose, 1.7F, 0.0F), plastic()));
}

TEST_CASE("only the rows in view are drawn", "[drawn_cube]") {
    for (const int size : {3, 5, 6}) {
        CAPTURE(size);
        const std::vector<Color> side = pattern(size);
        const Pose pose = fitting(size);
        const cv::Mat frame = cube::test::draw_side(side, pose, Drawing{.rows = 2});
        const float middle = 0.5F * static_cast<float>(size - 1);
        for (int row = 0; row < size; ++row) {
            const cv::Point2f at =
                cube::test::place(pose, -middle, static_cast<float>(row) - middle);
            const bool drawn = row < 2;
            CHECK(pixel_is(frame, at,
                           drawn ? cube::test::paint(side.at(static_cast<std::size_t>(row * size)))
                                 : plastic()));
        }
    }
}

TEST_CASE("the wordmark is on the centre sticker of an odd size and nowhere on an even one",
          "[drawn_cube]") {
    // The first of the wordmark's four squares, a little up and left of the centre.
    constexpr float kAlong = -1.5F * 0.13F;
    for (int size = cube::nxn::kMinSize; size <= cube::nxn::kMaxSize; ++size) {
        CAPTURE(size);
        const auto cells = static_cast<std::size_t>(size);
        const std::vector<Color> side(cells * cells, Color::White);
        Drawing drawing;
        drawing.logo_on_centre = Color::White;
        const Pose pose = fitting(size);
        const cv::Mat frame = cube::test::draw_side(side, pose, drawing);
        const cv::Point2f at = cube::test::place(pose, kAlong, kAlong);
        const bool drawn = size % 2 == 1;
        // Green is the wordmark's first colour; white is the sticker.
        CHECK(pixel_is(frame, at, cube::test::paint(drawn ? Color::Green : Color::White)));
    }
}

TEST_CASE("a finger lies across the sticker it was asked to", "[drawn_cube]") {
    for (const int size : {3, 4, 5, 7}) {
        CAPTURE(size);
        const std::vector<Color> side = pattern(size);
        const Pose pose = fitting(size);
        cv::Mat frame = cube::test::draw_side(side, pose);
        const int cell = ((size * size) / 2) + 1;
        const int cell_row = cell / size;
        cube::test::lay_finger(frame, pose, cell, size);
        const float middle = 0.5F * static_cast<float>(size - 1);
        const cv::Point2f at = cube::test::place(pose, static_cast<float>(cell % size) - middle,
                                                 static_cast<float>(cell_row) - middle);
        CHECK(pixel_is(frame, at, cv::Scalar(140, 170, 222)));
        // Not across the stickers in the other rows.
        const int other = cell % size;  // the same column, top row
        const cv::Point2f top =
            cube::test::place(pose, static_cast<float>(other) - middle, -middle);
        if (cell_row != 0) {
            CHECK(
                pixel_is(frame, top, cube::test::paint(side.at(static_cast<std::size_t>(other)))));
        }
    }
}

TEST_CASE("a side turned a quarter turn clockwise has its top-left at the top-right",
          "[drawn_cube]") {
    for (int size = cube::nxn::kMinSize; size <= cube::nxn::kMaxSize; ++size) {
        CAPTURE(size);
        const std::vector<Color> side = recoloured(pattern(size), 2);
        const std::vector<Color> once = cube::test::turned_clockwise(side);
        const auto n = static_cast<std::size_t>(size);
        REQUIRE(once.size() == side.size());
        // Every corner goes round one place, clockwise.
        CHECK(once.at(n - 1) == side.at(0));
        CHECK(once.at((n * n) - 1) == side.at(n - 1));
        CHECK(once.at((n - 1) * n) == side.at((n * n) - 1));
        CHECK(once.at(0) == side.at((n - 1) * n));
        // And four of them bring every sticker back.
        std::vector<Color> round = side;
        for (int turn = 0; turn < 4; ++turn) {
            round = cube::test::turned_clockwise(round);
        }
        CHECK(round == side);
    }
}

TEST_CASE("the 3x3 overload of a quarter turn is the one for any size", "[drawn_cube]") {
    const std::vector<Color> side = recoloured(pattern(3), 4);
    std::array<Color, 9> as_array{};
    std::ranges::copy(side, as_array.begin());
    const std::array<Color, 9> turned = cube::test::turned_clockwise(as_array);
    const std::vector<Color> turned_any = cube::test::turned_clockwise(side);
    CHECK(std::ranges::equal(turned, turned_any));
}

TEST_CASE("a camera's view of a side shows what the cube has there", "[drawn_cube]") {
    for (int size = cube::nxn::kMinSize; size <= cube::nxn::kMaxSize; ++size) {
        CAPTURE(size);
        const auto n = static_cast<std::size_t>(size);
        const auto solved_colour = [](Face face) { return cube::solver::canonical_color(face); };
        const auto column = [&](const std::vector<Color>& view, std::size_t col) {
            std::vector<Color> out;
            out.reserve(n);
            for (std::size_t row = 0; row < n; ++row) {
                out.push_back(view.at((row * n) + col));
            }
            return out;
        };
        const auto all_are = [](const std::vector<Color>& stickers, Color colour) {
            return std::ranges::all_of(stickers, [colour](Color c) { return c == colour; });
        };

        // R carries F up onto U, U back onto B, and B down onto D and D onto F.
        NCube turned = *NCube::solved(size);
        turned.apply(cube::solver::Move{.face = Face::R, .turns = 1});
        // U and F are as the camera sees them: the column against R is the last.
        CHECK(all_are(column(cube::test::camera_view(turned, Face::U), n - 1),
                      solved_colour(Face::F)));
        CHECK(all_are(column(cube::test::camera_view(turned, Face::F), n - 1),
                      solved_colour(Face::D)));
        // B is seen from behind, so R is on its left.
        CHECK(all_are(column(cube::test::camera_view(turned, Face::B), 0), solved_colour(Face::U)));
        CHECK(all_are(column(cube::test::camera_view(turned, Face::B), n - 1),
                      solved_colour(Face::B)));

        // F carries U onto R and D onto L; F is in front of both.
        NCube front = *NCube::solved(size);
        front.apply(cube::solver::Move{.face = Face::F, .turns = 1});
        // L is as the camera sees it: F is on its right. R is seen from the right, F on its left.
        CHECK(all_are(column(cube::test::camera_view(front, Face::L), n - 1),
                      solved_colour(Face::D)));
        CHECK(all_are(column(cube::test::camera_view(front, Face::R), 0), solved_colour(Face::U)));
        CHECK(all_are(column(cube::test::camera_view(front, Face::R), n - 1),
                      solved_colour(Face::R)));
    }
}

TEST_CASE("a 3x3's camera view is the one the 3x3 tests have always drawn from", "[drawn_cube]") {
    const cube::solver::Cube cube = cube::test::scramble(5);
    const NCube same = NCube::from_cube3(cube);
    for (const Face face : cube::solver::kAllFaces) {
        const std::array<Color, 9> old_way = cube::test::outside_view(cube, face);
        const std::vector<Color> new_way = cube::test::camera_view(same, face);
        CHECK(std::ranges::equal(old_way, new_way));
    }
}

TEST_CASE("two sides of a cube seen corner-on have every sticker where the pose puts it",
          "[drawn_cube]") {
    for (int size = cube::nxn::kMinSize; size <= 7; ++size) {
        CAPTURE(size);
        const auto cells = static_cast<float>(size);
        const std::vector<Color> left = pattern(size);
        const std::vector<Color> right = recoloured(pattern(size), 3);
        CornerPose pose;
        pose.pitch = 240.0F / cells;
        pose.left_width = 150.0F / cells;
        pose.right_width = 120.0F / cells;
        const cv::Mat frame = cube::test::draw_corner(left, right, pose);
        const float top = pose.edge_y - (0.5F * cells * pose.pitch);
        for (int row = 0; row < size; ++row) {
            for (int col = 0; col < size; ++col) {
                CAPTURE(row, col);
                const auto index =
                    (static_cast<std::size_t>(row) * static_cast<std::size_t>(size)) +
                    static_cast<std::size_t>(col);
                const float across = static_cast<float>(col) + 0.5F;
                const float down = (static_cast<float>(row) + 0.5F) * pose.pitch;
                // The left side's columns step right and down towards the shared edge.
                const cv::Point2f on_left(
                    (pose.edge_x - (cells * pose.left_width)) + (across * pose.left_width),
                    (top - (cells * pose.tilt)) + (across * pose.tilt) + down);
                CHECK(pixel_is(frame, on_left, cube::test::paint(left.at(index))));
                // The right side's columns step right and up.
                const cv::Point2f on_right(pose.edge_x + (across * pose.right_width),
                                           top - (across * pose.tilt) + down);
                CHECK(pixel_is(frame, on_right, cube::test::paint(right.at(index))));
            }
        }
    }
}

TEST_CASE("three sides of a cube seen from a corner have every sticker where the pose puts it",
          "[drawn_cube]") {
    for (int size = cube::nxn::kMinSize; size <= 7; ++size) {
        CAPTURE(size);
        const auto cells = static_cast<float>(size);
        const std::array<std::vector<Color>, 3> sides = {
            pattern(size),
            recoloured(pattern(size), 2),
            recoloured(pattern(size), 4),
        };
        Corner3Pose pose;
        pose.edge = 150.0F;
        const std::array<std::span<const Color>, 3> views = {
            std::span<const Color>(sides.at(0)),
            std::span<const Color>(sides.at(1)),
            std::span<const Color>(sides.at(2)),
        };
        const cv::Mat frame = cube::test::draw_corner3(views, pose);
        const cv::Point2f up(0.0F, -pose.edge);
        const cv::Point2f down_left(-0.866F * pose.edge, 0.5F * pose.edge);
        const cv::Point2f down_right(0.866F * pose.edge, 0.5F * pose.edge);
        // Each side runs from the vertex along `columns` and down `rows`.
        const std::array<std::array<cv::Point2f, 2>, 3> spans = {
            std::array<cv::Point2f, 2>{down_left, up},
            std::array<cv::Point2f, 2>{down_right, down_left},
            std::array<cv::Point2f, 2>{up, down_right},
        };
        for (std::size_t side = 0; side < 3; ++side) {
            for (int row = 0; row < size; ++row) {
                for (int col = 0; col < size; ++col) {
                    CAPTURE(side, row, col);
                    const cv::Point2f at =
                        pose.vertex +
                        ((static_cast<float>(col) + 0.5F) / cells) * spans.at(side).at(0) +
                        ((static_cast<float>(row) + 0.5F) / cells) * spans.at(side).at(1);
                    CHECK(pixel_is(frame, at,
                                   cube::test::paint(sides.at(side).at(
                                       static_cast<std::size_t>((row * size) + col)))));
                }
            }
        }
    }
}

TEST_CASE("a drawing refuses a count of stickers that is no cube side", "[drawn_cube]") {
    CHECK_THROWS(cube::test::side_length(0));
    CHECK_THROWS(cube::test::side_length(1));
    CHECK_THROWS(cube::test::side_length(8));
    CHECK_THROWS(cube::test::side_length(10));
    CHECK(cube::test::side_length(4) == 2);
    CHECK(cube::test::side_length(9) == 3);
    CHECK(cube::test::side_length(81) == 9);
    const std::vector<Color> seven(7, Color::Red);
    CHECK_THROWS(cube::test::draw_side(seven, Pose{}));
    const std::vector<Color> three_by_three = pattern(3);
    const std::vector<Color> four_by_four = pattern(4);
    CHECK_THROWS(cube::test::draw_corner(three_by_three, four_by_four));
}
