#include <catch2/catch_test_macros.hpp>

#include "drawn_corner.hpp"
#include "drawn_cube.hpp"
#include "must_have.hpp"

#include <cube/error.hpp>
#include <cube/solver/cube.hpp>
#include <cube/solver/hold.hpp>
#include <cube/solver/notation.hpp>
#include <cube/solver/outside_view.hpp>
#include <cube/vision/corner_fit.hpp>
#include <cube/vision/corner_view.hpp>
#include <cube/vision/direct_scan.hpp>
#include <cube/vision/prepared_frame.hpp>

#include <opencv2/imgproc.hpp>

#include <array>
#include <cmath>
#include <cstddef>
#include <optional>
#include <string_view>

using cube::solver::Color;
using cube::solver::Cube;
using cube::solver::Face;
using cube::solver::kAllFaces;
using cube::test::draw_corner;
using cube::test::draw_side;
using cube::test::Pose;
using cube::test::room;
using cube::vision::CornerRead;
using cube::vision::fit_corner;
using cube::vision::GridEdge;
using cube::vision::prepare_frame;
using cube::vision::read_corner;
using cube::vision::SessionColours;
using cube::vision::SideScan;

namespace {

// The ambiguous cube of tests/unit/direct_scan_test.cpp: six sides read
// perfectly still put together two ways.
constexpr std::string_view kAmbiguous =
    "R F' D' L L B B' D' R F2 L' U' F2 B B2 U B2 R D' U2 R2 L' L2 F L2";

Cube after(std::string_view algorithm) {
    Cube cube = Cube::solved();
    for (const cube::solver::Move move : *cube::solver::parse_algorithm(algorithm)) {
        cube = cube.apply(move);
    }
    return cube;
}

// What each colour measures in the drawings: the Lab of the paint.
SessionColours drawn_colours() {
    SessionColours colours{};
    for (const Color colour :
         {Color::White, Color::Yellow, Color::Green, Color::Blue, Color::Orange, Color::Red}) {
        const cv::Scalar bgr = cube::test::paint(colour);
        const cv::Mat3f source(
            1, 1,
            cv::Vec3f(static_cast<float>(bgr(0)) / 255.0F, static_cast<float>(bgr(1)) / 255.0F,
                      static_cast<float>(bgr(2)) / 255.0F));
        cv::Mat3f lab;
        cv::cvtColor(source, lab, cv::COLOR_BGR2Lab);
        colours.at(static_cast<std::size_t>(colour)) = lab(0, 0);
    }
    return colours;
}

// The cells along an edge of a 3x3, in the order the edge is walked.
std::array<std::size_t, 3> edge_cells(GridEdge edge) {
    switch (edge) {
        case GridEdge::Top:
            return {0, 1, 2};
        case GridEdge::Right:
            return {2, 5, 8};
        case GridEdge::Bottom:
            return {8, 7, 6};
        case GridEdge::Left:
            return {6, 3, 0};
    }
    return {};
}

cv::Mat corner_frame(const Cube& cube, Face left) {
    return cube::test::draw_cube_corner(cube, left);
}

void show_sides(SideScan& scan, const Cube& cube) {
    constexpr int kFrames = static_cast<int>(SideScan::kMinReads) + 4;
    for (const Face face : kAllFaces) {
        const cv::Mat frame = draw_side(cube::solver::outside_view(cube, face), Pose{});
        for (int i = 0; i < kFrames; ++i) {
            scan.add(cube::vision::read_side(frame));
        }
        scan.add(cube::vision::read_side(room({640, 360})));
    }
}

}  // namespace

TEST_CASE("two sides turned corner-on are two lattices that share an edge", "[corner]") {
    const Cube cube = cube::test::scramble(2);
    const std::optional<CornerRead> read = read_corner(corner_frame(cube, Face::F));
    const CornerRead& corner = ::cube::test::must_have(read);

    REQUIRE(corner.sides.size() == 2);
    REQUIRE(corner.edges.size() == 1);

    // Whichever way round each lattice was indexed, the stickers along the
    // shared edge sit against one another: first with last.
    const auto& edge = corner.edges.front();
    const auto& first = corner.sides.at(edge.first);
    const auto& second = corner.sides.at(edge.second);
    const auto cells_first = edge_cells(edge.on_first);
    const auto cells_second = edge_cells(edge.on_second);
    for (std::size_t t = 0; t < 3; ++t) {
        const cv::Point2f gap =
            first.centres.at(cells_first.at(t)) - second.centres.at(cells_second.at(2 - t));
        CHECK(std::hypot(gap.x, gap.y) < 1.2F * first.pitch);
    }
    for (const auto& side : corner.sides) {
        CHECK(side.pitch > 15.0F);
        for (const bool found : side.found) {
            CHECK(found);
        }
    }
}

TEST_CASE("a frame no side is square in goes from one preparation to both readers", "[corner]") {
    // What the app does with a frame: prepare it once, ask for a side, and only
    // when there is none ask for the corner - the same reads as asking each
    // reader of the frame itself.
    const Cube cube = cube::test::scramble(2);
    const cv::Mat frame = corner_frame(cube, Face::F);
    const auto prepared_read = prepare_frame(frame);
    const auto& prepared = ::cube::test::must_have(prepared_read);

    CHECK_FALSE(cube::vision::read_side(prepared).has_value());
    CHECK_FALSE(cube::vision::read_side(frame).has_value());

    const auto shared_read = read_corner(prepared);
    const auto direct_read = read_corner(frame);
    const auto& shared = ::cube::test::must_have(shared_read);
    const auto& direct = ::cube::test::must_have(direct_read);
    REQUIRE(shared.sides.size() == direct.sides.size());
    REQUIRE(shared.edges.size() == direct.edges.size());
    for (std::size_t i = 0; i < shared.sides.size(); ++i) {
        CHECK(shared.sides.at(i).pitch == direct.sides.at(i).pitch);
        CHECK(shared.sides.at(i).found == direct.sides.at(i).found);
        for (std::size_t cell = 0; cell < 9; ++cell) {
            CHECK(shared.sides.at(i).centres.at(cell) == direct.sides.at(i).centres.at(cell));
            CHECK(shared.sides.at(i).lab.at(cell) == direct.sides.at(i).lab.at(cell));
        }
    }
}

TEST_CASE("a side seen square-on, or an empty room, is not a corner", "[corner]") {
    CHECK_FALSE(read_corner(room({640, 360})).has_value());
    CHECK_FALSE(
        read_corner(draw_side(cube::solver::outside_view(cube::test::scramble(2), Face::F), Pose{}))
            .has_value());
}

TEST_CASE("a corner view fits the cube it was drawn from and not a cube one turn away",
          "[corner]") {
    const Cube cube = cube::test::scramble(2);
    const SessionColours colours = drawn_colours();

    for (const Face left : kAllFaces) {
        INFO("left side " << static_cast<int>(left));
        const auto read = read_corner(corner_frame(cube, left));
        CHECK(fit_corner(::cube::test::must_have(read), cube, colours).fits);
    }

    // A turn of the front, right or back layer changes stickers in this view.
    const auto front_read = read_corner(corner_frame(cube, Face::F));
    const auto& front = ::cube::test::must_have(front_read);
    for (const Face turned : {Face::F, Face::R, Face::U, Face::D}) {
        INFO("turned " << static_cast<int>(turned));
        CHECK_FALSE(fit_corner(front, cube.apply({turned, 1}), colours).fits);
    }
}

TEST_CASE("a corner view with one side turned is not the cube", "[corner]") {
    // Both sides are the cube's own, but the right one is a quarter-turn out
    // from where the edge says it goes: its stickers do not meet the left
    // side's across the shared edge.
    const Cube cube = cube::test::scramble(2);
    const SessionColours colours = drawn_colours();
    // F, and the side to its right turned a quarter out of place.
    std::array<Color, 9> right = cube::solver::outside_view(cube, cube::solver::right_of(Face::F));
    right = cube::test::turned_clockwise(right);
    const auto read = read_corner(draw_corner(cube::solver::outside_view(cube, Face::F), right));
    CHECK_FALSE(fit_corner(::cube::test::must_have(read), cube, colours).fits);
}

TEST_CASE("sides that put together two ways are settled by the corners seen", "[corner]") {
    const Cube cube = after(kAmbiguous);
    REQUIRE_FALSE(cube::test::sides_make_one_cube(cube));

    SideScan scan;
    show_sides(scan, cube);
    REQUIRE(scan.sides().size() == 6);
    const auto before = scan.cube();
    REQUIRE_FALSE(before.has_value());
    CHECK(before.error() == cube::CubeError::ambiguous_scan);

    // A view that touches nothing the candidates disagree on leaves it open;
    // a corner read of nothing changes nothing.
    scan.add_corner(std::nullopt);
    CHECK(scan.corner_reads() == 0);
    CHECK(scan.cube().error() == cube::CubeError::ambiguous_scan);

    for (const Face left : kAllFaces) {
        scan.add_corner(read_corner(corner_frame(cube, left)));
    }
    CHECK(scan.corner_reads() == 6);

    const auto settled = scan.cube();
    REQUIRE(settled.has_value());
    CHECK(*settled == cube);

    scan.reset();
    CHECK(scan.corner_reads() == 0);
}

TEST_CASE("a corner view of the wrong cube does not settle it", "[corner]") {
    // The same six sides, but corner views drawn from the cube's other
    // reading: each side is its own, and none of the edges agree with the
    // candidates the sides make.
    const Cube cube = after(kAmbiguous);
    const Cube elsewhere = cube::test::scramble(5);

    SideScan scan;
    show_sides(scan, cube);
    for (const Face left : kAllFaces) {
        scan.add_corner(read_corner(corner_frame(elsewhere, left)));
    }
    const auto read = scan.cube();
    REQUIRE_FALSE(read.has_value());
    CHECK(read.error() == cube::CubeError::ambiguous_scan);
}
