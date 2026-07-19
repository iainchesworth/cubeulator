#pragma once

#include <vector>

#include <cube/error.hpp>
#include <cube/solver/cube.hpp>

namespace cube::solver {

// True iff the first layer AND all 4 middle-layer edges are solved - F2L
// complete, ready for oll.hpp's stage.
[[nodiscard]] bool f2l_solved(const Cube& cube) noexcept;

// A move sequence that solves the 4 middle-layer edges, assuming
// first_layer_solved(start) already holds. Each edge is inserted
// independently via a search restricted to {U, and that edge's own two
// side faces} - D is never needed once the first layer is complete.
[[nodiscard]] Result<std::vector<Move>> solve_middle_edges(const Cube& start);

}  // namespace cube::solver
