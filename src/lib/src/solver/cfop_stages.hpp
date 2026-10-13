#pragma once

#include <cube/error.hpp>
#include <cube/solver/cube.hpp>
#include <cube/solver/solver.hpp>

namespace cube::solver::detail {

// solve_cfop() before its moves are tidied by merge_adjacent_moves(): the
// four stages exactly as they were planned, so the cube after each stage's
// moves is exactly that stage complete (cross; first two layers; last layer
// oriented; solved). A merge can move a stage's last move into the stage
// before it, which is right for the finished solution but would blur the
// boundary a test wants to check.
[[nodiscard]] Result<Solution> solve_cfop_stages(const Cube& scrambled);

}  // namespace cube::solver::detail
