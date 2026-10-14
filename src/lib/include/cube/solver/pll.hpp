#pragma once

#include <vector>

#include <cube/error.hpp>
#include <cube/solver/cube.hpp>

namespace cube::solver {

// A move sequence that permutes the last layer's corners and edges into
// their solved positions, assuming oll_solved(start) already holds (F2L
// solved, U face fully oriented). Solved in two passes - corners first,
// then edges - via the standard 2-look algorithms (A-perm, U-perm)
// composed by a bounded search over whole algorithms; see
// src/lib/src/solver/last_layer.hpp for why this is guaranteed to finish
// in milliseconds on any input (the app solves interactively on a phone).
// The result leaves the cube fully solved: Cube::is_solved() is the final
// target directly, since with F2L and orientation already correct,
// permuting both last-layer piece types is all that remains.
[[nodiscard]] Result<std::vector<Move>> solve_pll(const Cube& start);

}  // namespace cube::solver
