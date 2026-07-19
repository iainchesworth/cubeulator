#pragma once

#include <vector>

#include <cube/error.hpp>
#include <cube/solver/cube.hpp>

namespace cube::solver {

// True iff the D-face cross AND all 4 first-layer corners are solved - the
// full first layer complete, ready for middle_edges.hpp's stage.
[[nodiscard]] bool first_layer_solved(const Cube& cube) noexcept;

// A move sequence that solves the 4 first-layer (White) corners, assuming
// cross_solved(start) already holds. Each corner is inserted independently
// via a search restricted to {U, D, and that corner's own two side faces},
// which is what keeps the cross (and any earlier-inserted corner) intact
// without needing hand-authored insertion algorithms.
[[nodiscard]] Result<std::vector<Move>> solve_corners(const Cube& start);

}  // namespace cube::solver
