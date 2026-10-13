#pragma once

#include <cube/error.hpp>
#include <cube/solver/cube.hpp>
#include <cube/solver/solver.hpp>

namespace cube::solver {

// Solves `scrambled` with Kociemba's two-phase algorithm and returns a
// near-shortest solution: about 20 moves, against the beginner method's
// hundred. This is the engine behind SolveMethod::Advanced.
//
// Phase 1 turns the cube, with any face turn, into one that only the moves
// <U, D, R2, L2, F2, B2> are needed to finish (every edge oriented, every
// corner untwisted, the four middle-layer edges somewhere in the middle
// layer); phase 2 then finishes with just those moves. Each phase is an
// iterative-deepening search guided by small pruning tables, so the first
// solution comes back in milliseconds and the search then spends a fixed
// budget looking for a shorter one.
//
// The result has two steps, Stage::Reduce (phase 1) and Stage::Finish
// (phase 2). Like every solve(), it has no two adjacent moves on one face.
//
// Fails with CubeError::invalid_cube_state if `scrambled` is not physically
// valid and CubeError::unsolvable_state if it is valid but unreachable (a
// twisted corner, a flipped edge, a swapped pair, or centres out of order).
//
// The first call builds about 6 MB of lookup tables, which takes a fraction
// of a second; prepare_two_phase() does that up front. Every later call is
// bounded by a fixed node budget, not by the clock, so the same cube always
// gets the same solution on every device.
[[nodiscard]] Result<Solution> solve_two_phase(const Cube& scrambled);

// Builds the two-phase lookup tables now instead of on the first solve, so
// the person is not kept waiting for them. Safe to call from any thread, any
// number of times; the work happens once.
void prepare_two_phase();

}  // namespace cube::solver
