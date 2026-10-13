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

// Merges every run of adjacent moves on the same face into one move, across
// stage boundaries as well as within a stage: turn counts add mod 4, and a
// sum of 0 removes the pair outright - which can bring the moves either side
// of it together, so this repeats until no two adjacent moves share a face
// (e.g. "F2 | F2" at a stage boundary vanishes, "U | U" reads "U2"). Linear
// in the move count.
//
// The steps keep their stages and their order, and a step may end up with no
// moves at all. A move merged across a stage boundary stays with the earlier
// stage (the one it began in), so it belongs to exactly one step. The net
// effect of the whole sequence on a cube is unchanged, but a merge that
// straddles a boundary changes the cube *at* that boundary: only the end of
// the solution is guaranteed, not that each stage's work is complete where its
// moves end. solve() applies this to everything it returns; it's public so a
// Solution from elsewhere can be tidied the same way.
[[nodiscard]] Solution merge_adjacent_moves(Solution solution);

// Solves `scrambled` via the beginner (layer-by-layer) method: cross, white
// corners, middle edges, 2-look OLL, 2-look PLL - the same staged structure
// the UI teaches, not an optimal/minimal-move solve. The returned moves have
// no two adjacent moves on the same face, even across a stage boundary (see
// merge_adjacent_moves). Fails with
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
