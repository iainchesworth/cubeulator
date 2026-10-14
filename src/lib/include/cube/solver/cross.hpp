#pragma once

#include <vector>

#include <cube/error.hpp>
#include <cube/solver/cube.hpp>

namespace cube::solver {

// True iff the D-face cross is solved: all 4 of D's edge stickers are White,
// and each of F/R/B/L's D-adjacent edge sticker matches that face's own
// centre. Corners are not considered - see corners.hpp for the next stage.
[[nodiscard]] bool cross_solved(const Cube& cube) noexcept;

// A short (<=8 move) sequence that solves the White cross on D, found by
// breadth-first search over a reduced state (just the 8 facelets
// cross_solved reads) so the search space stays small regardless of how
// scrambled the rest of the cube is. Fails with CubeError::no_solution if
// no such sequence exists within the search's depth cap - this should never
// happen for a physically valid cube; a caller returning it should treat it
// as a solver bug, not a legitimate "unsolvable" scramble.
[[nodiscard]] Result<std::vector<Move>> solve_cross(const Cube& start);

}  // namespace cube::solver
