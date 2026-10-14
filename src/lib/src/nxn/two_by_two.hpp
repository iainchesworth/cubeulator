#pragma once

#include <vector>

#include <cube/error.hpp>
#include <cube/nxn/cube.hpp>

namespace cube::nxn::detail {

// Solves a 2x2 by a search from both ends. Only U, R and F are turned, which
// keeps one corner (the one at down-back-left) where it is, so the goal is
// unique: each face the colour of that corner's side of the cube. At most 11
// turns in half-turn metric; both ends together never hold more than a few
// hundred thousand states.
//
// Fails with invalid_cube_state if the corners cannot be read as a scheme,
// and unsolvable_state if no sequence reaches the goal (a twisted corner).
[[nodiscard]] Result<std::vector<LayerMove>> solve_two_by_two(const NCube& scrambled);

}  // namespace cube::nxn::detail
