#pragma once

#include <cube/error.hpp>
#include <cube/nxn/cube.hpp>
#include <cube/nxn/geometry.hpp>
#include <cube/nxn/solution.hpp>
#include <cube/solver/cube.hpp>
#include <cube/solver/solver.hpp>

// The 3x3 solver's interface, on the N x N library.
//
// The app holds, scans and shows a 3x3 as a cube::solver::Cube and wants
// cube::solver::Solution back (its stepped solution, history and review are
// written in them), so these take and return those. What runs underneath is
// the N x N library: the cube is lifted into an NCube, the size-3 strategy
// (a single core stage) solves it, and the moves come back as 3x3 moves.
//
// They are the 3x3 solver's contract and nothing more. The same stages, the
// same moves in the same order, and the same error for a cube that cannot be
// solved - including a cube whose centres are out of order, which the library's
// own solve(NCube) would read as a cube held in another orientation and solve.
// tests/unit/nxn/cube3_test.cpp holds that equal over a corpus, per method.

namespace cube::nxn {

// Solves a cube of any size the way the app holds it. At every size but 3 this
// is the library's solve(): a cube is read in whatever orientation and colours
// it has. At size 3 it is solve_3x3's contract exactly - the cube is read as it
// is held, so one whose centres are not where the 3x3 solvers' scheme puts them
// is refused, as the app always has - and the moves come back as layer turns
// (all of depth 0). tests/unit/nxn/cube3_test.cpp holds the two equal.
[[nodiscard]] Result<Solution> solve_held(
    const NCube& scrambled, solver::SolveMethod method = solver::SolveMethod::Beginner);

// Solves `scrambled` with `method`, as cube::solver::solve does.
[[nodiscard]] Result<solver::Solution> solve_3x3(
    const solver::Cube& scrambled, solver::SolveMethod method = solver::SolveMethod::Beginner);

// Does the one-off setup a solve of `size` by `method` needs now, off the
// moment a person is waiting: the method's tables, and for a size above 3 the
// cycle library and the centre search's action set. Safe from any thread, any
// number of times.
void prepare(int size, solver::SolveMethod method);

}  // namespace cube::nxn
