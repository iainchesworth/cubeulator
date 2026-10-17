#include <catch2/catch_test_macros.hpp>

#include "deterministic_rng.hpp"
#include "drawn_corner.hpp"
#include "drawn_cube.hpp"
#include "must_have.hpp"

#include <cube/nxn/cube.hpp>
#include <cube/nxn/geometry.hpp>
#include <cube/nxn/outside_view.hpp>
#include <cube/nxn/scan_assembly.hpp>
#include <cube/vision/corner_fit.hpp>
#include <cube/vision/corner_view.hpp>
#include <cube/vision/direct_scan.hpp>
#include <cube/vision/prepared_frame.hpp>

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

#include <array>
#include <cmath>
#include <cstddef>
#include <optional>
#include <vector>

// The corner reader on cubes of N x N: two or three sides in one view, read as lattices
// of N x N, and what the corner views say of the cubes six flat sides make. Drawn
// (drawn_corner.hpp), because no recording of a bigger cube held on a corner exists.

namespace {

using cube::nxn::NCube;
using cube::solver::Color;
using cube::solver::Face;
using cube::test::Corner3Pose;
using cube::test::CornerPose;
using cube::test::Drawing;
using cube::vision::CornerRead;
using cube::vision::GridEdge;
using cube::vision::SessionColours;
using cube::vision::SideScan;

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

// The cells along an edge of an N x N side, in the order the edge is walked.
std::vector<std::size_t> edge_cells(GridEdge edge, int size) {
    const auto n = static_cast<std::size_t>(size);
    std::vector<std::size_t> cells;
    for (std::size_t t = 0; t < n; ++t) {
        switch (edge) {
            case GridEdge::Top:
                cells.push_back(t);
                break;
            case GridEdge::Right:
                cells.push_back((t * n) + n - 1);
                break;
            case GridEdge::Bottom:
                cells.push_back((n * n) - 1 - t);
                break;
            case GridEdge::Left:
                cells.push_back((n - 1 - t) * n);
                break;
        }
    }
    return cells;
}

// A pose that fits a side of `size` stickers in the frame: the 3x3's pitch scaled.
CornerPose corner_pose_for(int size) {
    const float pitch = (150.0F / static_cast<float>(size)) + 10.0F;
    return {
        .edge_x = 320.0F,
        .edge_y = 180.0F,
        .pitch = pitch,
        .left_width = 0.75F * pitch,
        .right_width = 0.6F * pitch,
        .tilt = 0.1F * pitch,
    };
}

// Whether `made` is `truth` or the same cube held another way up (the scan puts a side where
// its centre colour says it goes).
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): the cube made, then the cube it should be
bool is_cube(const NCube& made, const NCube& truth) {
    const cube::nxn::Geometry& geometry = cube::nxn::Geometry::of(truth.size());
    for (std::size_t rotation = 0; rotation < geometry.rotations().size(); ++rotation) {
        NCube held = truth;
        held.apply(geometry.rotation_permutation(rotation));
        if (held == made) {
            return true;
        }
    }
    return false;
}

NCube scramble(int size, unsigned seed, int moves) {
    return cube::nxn::random_scramble({.size = size, .seed = seed, .moves = moves});
}

}  // namespace

TEST_CASE("two sides of a bigger cube turned corner-on are two lattices that share an edge",
          "[corner][nxn]") {
    for (const int size : {2, 4, 5, 6}) {
        CAPTURE(size);
        const NCube cube = scramble(size, 3U, 25 * size);
        const cv::Mat frame =
            cube::test::draw_cube_corner(cube, Face::F, corner_pose_for(size), Drawing{});
        const std::optional<CornerRead> read = cube::vision::read_corner(frame, size);
        const CornerRead& corner = ::cube::test::must_have(read);

        CHECK(corner.size == size);
        REQUIRE(corner.sides.size() == 2);
        REQUIRE(corner.edges.size() == 1);
        const auto& edge = corner.edges.front();
        const auto& first = corner.sides.at(edge.first);
        const auto& second = corner.sides.at(edge.second);
        const auto cells_first = edge_cells(edge.on_first, size);
        const auto cells_second = edge_cells(edge.on_second, size);
        for (std::size_t t = 0; t < cells_first.size(); ++t) {
            const cv::Point2f gap = first.centres.at(cells_first.at(t)) -
                                    second.centres.at(cells_second.at(cells_second.size() - 1 - t));
            CHECK(std::hypot(gap.x, gap.y) < 1.2F * first.pitch);
        }
        for (const auto& side : corner.sides) {
            CHECK(side.lab.size() == static_cast<std::size_t>(size * size));
            for (const bool found : side.found) {
                CHECK(found);
            }
        }
    }
}

TEST_CASE("a bigger cube's three sides in one view are three lattices", "[corner][nxn]") {
    for (const int size : {2, 4, 5}) {
        CAPTURE(size);
        const NCube cube = scramble(size, 5U, 25 * size);
        Corner3Pose pose;
        pose.edge = 1.1F * (150.0F + (5.0F * static_cast<float>(size)));
        const cv::Mat frame =
            cube::test::draw_cube_corner3(cube, Face::U, Face::F, Face::R, pose, Drawing{});
        const std::optional<CornerRead> read = cube::vision::read_corner(frame, size);
        const CornerRead& corner = ::cube::test::must_have(read);
        CHECK(corner.sides.size() == 3);
        CHECK(corner.edges.size() == 3);
    }
}

TEST_CASE("a corner view fits the cube of any size it was drawn from, and not one a turn away",
          "[corner][nxn]") {
    const SessionColours colours = drawn_colours();
    for (const int size : {2, 4, 5}) {
        CAPTURE(size);
        const NCube cube = scramble(size, 9U, 25 * size);
        for (const Face left : cube::solver::kAllFaces) {
            INFO("left side " << static_cast<int>(left));
            const auto read = cube::vision::read_corner(
                cube::test::draw_cube_corner(cube, left, corner_pose_for(size), Drawing{}), size);
            const CornerRead& corner = ::cube::test::must_have(read);

            // A turn of the layer against the left side's right edge, or of the right
            // side's layer against the left, changes stickers along the shared edge.
            NCube turned = cube;
            turned.apply(
                cube::nxn::LayerMove{.face = cube::solver::right_of(left), .depth = 0, .turns = 1});
            const std::vector<NCube> cubes = {cube, turned};
            const auto fits = cube::vision::fit_corner_each(corner, cubes, colours);
            REQUIRE(fits.size() == 2);
            CHECK(fits.at(0).fits);
            CHECK_FALSE(fits.at(1).fits);
        }
    }
}

TEST_CASE("corner views settle a bigger cube whose flat sides fit more than one way",
          "[corner][nxn]") {
    for (const int size : {2, 4, 5}) {
        CAPTURE(size);
        const std::optional<NCube> ambiguous = cube::test::cube_whose_flat_sides_fit_several(size);
        const NCube cube = ::cube::test::must_have(ambiguous);

        SideScan scan(size);
        const Drawing drawing;
        for (const Face face : cube::solver::kAllFaces) {
            const std::vector<Color> stickers = cube::nxn::outside_view(cube, face);
            cube::test::Pose pose;
            pose.pitch = 180.0F / static_cast<float>(size);
            for (int frame = 0; frame < 12; ++frame) {
                scan.add(
                    cube::vision::read_side(cube::test::draw_side(stickers, pose, drawing), size),
                    33.0F);
            }
            scan.add(std::nullopt);
        }
        REQUIRE(scan.sides().size() == 6);
        const auto before = scan.scanned_n();
        REQUIRE_FALSE(before.has_value());
        CHECK(before.error() == cube::CubeError::ambiguous_scan);

        // The corners, one after another, each view held a few frames.
        for (const Face vertical : {Face::U, Face::D}) {
            for (const Face depth : {Face::F, Face::B}) {
                for (const Face side : {Face::L, Face::R}) {
                    Corner3Pose pose;
                    pose.edge = 1.1F * (150.0F + (5.0F * static_cast<float>(size)));
                    const cv::Mat frame =
                        cube::test::draw_cube_corner3(cube, vertical, depth, side, pose, drawing);
                    if (frame.empty()) {
                        continue;
                    }
                    for (int i = 0; i < 3; ++i) {
                        scan.add(std::nullopt);
                        scan.add_corner(cube::vision::read_corner(frame, size), 33.0F);
                    }
                }
            }
        }
        const auto after = scan.scanned_n();
        REQUIRE(after.has_value());
        CHECK(is_cube(after->cube, cube));
    }
}
