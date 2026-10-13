#pragma once

// Corner-on views of a whole cube, for tests of the corner reader. Its own
// header because it needs cube::solver::outside_view, whose name the
// square-on tests' own outside_view would otherwise make ambiguous.

#include "drawn_cube.hpp"

#include <cube/solver/cube.hpp>
#include <cube/solver/outside_view.hpp>

#include <opencv2/core.hpp>

#include <array>

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
                                              const CornerPose& pose = {}) {
    const solver::Face neighbour = solver::right_of(left);
    std::array<solver::Color, 9> right = solver::outside_view(cube, neighbour);
    for (int i = 0; i < (3 - edge_towards(neighbour, left) + 4) % 4; ++i) {
        right = turned_clockwise(right);
    }
    return draw_corner(solver::outside_view(cube, left), right, pose);
}

}  // namespace cube::test
