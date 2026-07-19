#pragma once

#include <cstdint>
#include <vector>

#include <cube/error.hpp>
#include <cube/solver/cube.hpp>

namespace cube::solver {

// The beginner-method stage a SolveStep belongs to, in solving order.
enum class Stage : std::uint8_t { Cross, WhiteCorners, MiddleEdges, Oll, Pll };

// One stage's worth of moves - kept separate per stage (rather than one
// flat move list) so a caller can label each step for teaching purposes
// (see the solve-visualization plan: the UI shows which stage the user is
// currently on, not just a bare move counter).
struct SolveStep {
    Stage stage = Stage::Cross;
    std::vector<Move> moves;
};

struct Solution {
    std::vector<SolveStep> steps;
};

// Solves `scrambled` via the beginner (layer-by-layer) method: cross, white
// corners, middle edges, 2-look OLL, 2-look PLL - the same staged structure
// the UI teaches, not an optimal/minimal-move solve. Fails with
// CubeError::invalid_cube_state if `scrambled` isn't physically valid
// (guards against a malformed read, e.g. from a future vision pipeline);
// any other failure is a solver bug, not a legitimate dead end, since every
// physically valid cube is solvable.
[[nodiscard]] Result<Solution> solve(const Cube& scrambled);

// A pseudo-random scramble, deterministic for a given seed - used both by
// tests and (until a real vision pipeline exists) as the app's stand-in
// scramble source.
[[nodiscard]] Cube random_scramble(unsigned seed, int move_count = 25) noexcept;

}  // namespace cube::solver
