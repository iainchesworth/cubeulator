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
// cross first, then corners - matching the standard 2-look technique,
// though (like every other stage) the exact sequence is discovered by
// search rather than a memorised algorithm table.
[[nodiscard]] Result<std::vector<Move>> solve_oll(const Cube& start);

}  // namespace cube::solver
