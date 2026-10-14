#pragma once

#include <vector>

#include <cube/error.hpp>
#include <cube/solver/cube.hpp>

namespace cube::solver {

// True iff F2L is complete AND the whole U face shows its own centre
// colour - the last layer fully oriented, ready for pll.hpp's stage.
[[nodiscard]] bool oll_solved(const Cube& cube) noexcept;

// A move sequence that orients the last layer (U face solid), assuming
// f2l_solved(start) already holds. Solved in two passes - edges into a
// cross first, then corners - via the standard 2-look algorithms (edge
// flip, Sune/Anti-Sune) composed by a bounded search over whole
// algorithms; see src/lib/src/solver/last_layer.hpp for why this is
// guaranteed to finish in milliseconds on any input (the app solves
// interactively on a phone).
[[nodiscard]] Result<std::vector<Move>> solve_oll(const Cube& start);

}  // namespace cube::solver
