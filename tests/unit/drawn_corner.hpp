#pragma once

// Corner-on views of a whole cube, for tests of the corner reader. Its own
// header because it needs cube::solver::outside_view, whose name the
// square-on tests' own outside_view would otherwise make ambiguous.

#include "drawn_cube.hpp"

#include <cube/nxn/cube.hpp>
#include <cube/nxn/outside_view.hpp>
#include <cube/nxn/scan_assembly.hpp>
#include <cube/solver/cube.hpp>
#include <cube/solver/outside_view.hpp>

#include <opencv2/core.hpp>

#include <array>
#include <cstddef>
#include <optional>
#include <vector>

namespace cube::test {

// Which edge of cube::solver::outside_view(face) is against `neighbour`:
// 0 top, 1 right, 2 bottom, 3 left.
[[nodiscard]] inline int edge_towards(solver::Face face, solver::Face neighbour) {
    if (solver::top_of(face) == neighbour) {
        return 0;
    }
    if (solver::right_of(face) == neighbour) {
        return 1;
    }
    if (solver::opposite(solver::top_of(face)) == neighbour) {
        return 2;
    }
    return 3;
}

// The two sides of `cube` that meet along `left`'s right edge, as a camera
// turned to that corner sees them: `left` as it is, and the side across its
// right edge turned so that its edge against `left` is its left column - side
// by side, rows level, as they are on the cube.
[[nodiscard]] inline cv::Mat draw_cube_corner(const solver::Cube& cube, solver::Face left,
                                              const CornerPose& pose = {},
                                              const Drawing& drawing = {}) {
    const solver::Face neighbour = solver::right_of(left);
    std::array<solver::Color, 9> right = solver::outside_view(cube, neighbour);
    for (int i = 0; i < (3 - edge_towards(neighbour, left) + 4) % 4; ++i) {
        right = turned_clockwise(right);
    }
    return draw_corner(solver::outside_view(cube, left), right, pose, drawing);
}

// The three sides of `cube` that meet at the corner of `a`, `b` and `c`, as
// draw_corner3 takes them: in the cyclic order of its hexagon, each turned so
// that its top edge is against the next side and its left edge against the
// one before. A corner can be walked in two orders, one the corner as the
// camera sees it from outside and the other its mirror; only the first has
// turns that agree, and that is the one returned.
[[nodiscard]] inline std::optional<std::array<std::array<solver::Color, 9>, 3>> corner_sides(
    const solver::Cube& cube, solver::Face a, solver::Face b, solver::Face c) {
    const std::array<std::array<solver::Face, 3>, 2> orders = {
        std::array<solver::Face, 3>{a, b, c},
        std::array<solver::Face, 3>{a, c, b},
    };
    for (const auto& order : orders) {
        std::array<std::array<solver::Color, 9>, 3> sides{};
        bool consistent = true;
        for (std::size_t i = 0; i < 3; ++i) {
            const solver::Face face = order.at(i);
            const solver::Face next = order.at((i + 1) % 3);
            const solver::Face previous = order.at((i + 2) % 3);
            const int turns = (4 - edge_towards(face, next)) % 4;
            consistent = consistent && (edge_towards(face, previous) + turns) % 4 == 3;
            sides.at(i) = solver::outside_view(cube, face);
            for (int turn = 0; turn < turns; ++turn) {
                sides.at(i) = turned_clockwise(sides.at(i));
            }
        }
        if (consistent) {
            return sides;
        }
    }
    return std::nullopt;
}

// The corner of `cube` where `a`, `b` and `c` meet, seen from outside with its
// three sides in one view (an empty picture when they do not meet).
[[nodiscard]] inline cv::Mat draw_cube_corner3(const solver::Cube& cube, solver::Face a,
                                               solver::Face b, solver::Face c,
                                               const Corner3Pose& pose = {},
                                               const Drawing& drawing = {}) {
    const auto sides = corner_sides(cube, a, b, c);
    return sides.has_value() ? draw_corner3(*sides, pose, drawing) : cv::Mat();
}

// The same for a cube of any size: the two sides of `cube` that meet along `left`'s right
// edge, side by side.
[[nodiscard]] inline cv::Mat draw_cube_corner(const nxn::NCube& cube, solver::Face left,
                                              const CornerPose& pose = {},
                                              const Drawing& drawing = {}) {
    const solver::Face neighbour = solver::right_of(left);
    std::vector<solver::Color> right = nxn::outside_view(cube, neighbour);
    for (int i = 0; i < (3 - edge_towards(neighbour, left) + 4) % 4; ++i) {
        right = turned_clockwise(std::span<const solver::Color>(right));
    }
    const std::vector<solver::Color> first = nxn::outside_view(cube, left);
    return draw_corner(std::span<const solver::Color>(first), std::span<const solver::Color>(right),
                       pose, drawing);
}

// The three sides of a cube of any size that meet at the corner of `a`, `b` and `c`
// (see corner_sides): in the cyclic order of the hexagon, each turned so that its top
// edge is against the next side and its left edge against the one before.
[[nodiscard]] inline std::optional<std::array<std::vector<solver::Color>, 3>> corner_sides(
    const nxn::NCube& cube, solver::Face a, solver::Face b, solver::Face c) {
    const std::array<std::array<solver::Face, 3>, 2> orders = {
        std::array<solver::Face, 3>{a, b, c},
        std::array<solver::Face, 3>{a, c, b},
    };
    for (const auto& order : orders) {
        std::array<std::vector<solver::Color>, 3> sides;
        bool consistent = true;
        for (std::size_t i = 0; i < 3; ++i) {
            const solver::Face face = order.at(i);
            const solver::Face next = order.at((i + 1) % 3);
            const solver::Face previous = order.at((i + 2) % 3);
            const int turns = (4 - edge_towards(face, next)) % 4;
            consistent = consistent && (edge_towards(face, previous) + turns) % 4 == 3;
            sides.at(i) = nxn::outside_view(cube, face);
            for (int turn = 0; turn < turns; ++turn) {
                sides.at(i) = turned_clockwise(std::span<const solver::Color>(sides.at(i)));
            }
        }
        if (consistent) {
            return sides;
        }
    }
    return std::nullopt;
}

[[nodiscard]] inline cv::Mat draw_cube_corner3(const nxn::NCube& cube, solver::Face a,
                                               solver::Face b, solver::Face c,
                                               const Corner3Pose& pose = {},
                                               const Drawing& drawing = {}) {
    const auto sides = corner_sides(cube, a, b, c);
    if (!sides.has_value()) {
        return cv::Mat();
    }
    const std::array<std::span<const solver::Color>, 3> spans = {
        std::span<const solver::Color>(sides->at(0)),
        std::span<const solver::Color>(sides->at(1)),
        std::span<const solver::Color>(sides->at(2)),
    };
    return draw_corner3(spans, pose, drawing);
}

// A cube of `size` a few turns from solved whose six flat sides fit more than one cube
// (nothing in six sides square to the camera says which way up each is), the first one
// found from the variations of a right turn, an up turn and a front turn: what a corner
// view is for.
[[nodiscard]] inline std::optional<nxn::NCube> cube_whose_flat_sides_fit_several(int size) {
    for (unsigned variation = 1; variation < 400; ++variation) {
        nxn::NCube cube = *nxn::NCube::solved(size);
        cube.apply(nxn::LayerMove{.face = solver::Face::R, .depth = 0, .turns = 1});
        cube.apply(nxn::LayerMove{
            .face = solver::Face::U, .depth = static_cast<int>(variation % 2), .turns = 2});
        cube.apply(nxn::LayerMove{
            .face = solver::Face::F, .depth = static_cast<int>((variation / 2) % 2), .turns = 1});
        nxn::ScannedSides sides;
        for (std::size_t i = 0; i < 6; ++i) {
            sides.at(i) = nxn::outside_view(cube, solver::kAllFaces.at(i));
        }
        if (nxn::assemble_all_scanned_sides(size, sides, 8).size() > 1) {
            return cube;
        }
    }
    return std::nullopt;
}

}  // namespace cube::test
