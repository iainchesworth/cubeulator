#include <cube/nxn/cube3.hpp>

#include <utility>

#include <cube/nxn/commutator.hpp>
#include <cube/nxn/cube.hpp>
#include <cube/nxn/strategy.hpp>

#include "centres.hpp"

namespace cube::nxn {

Result<Solution> solve_held(const NCube& scrambled, solver::SolveMethod method) {
    if (scrambled.size() != 3) {
        return solve(scrambled, method);
    }
    // Held as it is: see CoreView::AsHeld.
    Strategy strategy{"3x3 as the app holds it"};
    strategy.then("core", solve_core(CoreView::AsHeld));
    return solve(scrambled, strategy, method);
}

Result<solver::Solution> solve_3x3(const solver::Cube& scrambled, solver::SolveMethod method) {
    Result<Solution> solved = solve_held(NCube::from_cube3(scrambled), method);
    if (!solved) {
        return std::unexpected(solved.error());
    }

    solver::Solution out;
    out.steps.reserve(solved->steps.size());
    for (const Step& step : solved->steps) {
        solver::SolveStep core{.stage = step.core_stage, .moves = {}};
        core.moves.reserve(step.moves.size());
        for (const LayerMove& move : step.moves) {
            core.moves.push_back(solver::Move{.face = move.face, .turns = move.turns});
        }
        out.steps.push_back(std::move(core));
    }
    return out;
}

void prepare(int size, solver::SolveMethod method) {
    solver::prepare(method);
    if (size >= 4 && size <= kMaxSize) {
        // The cycles the wings are built from, and the centre search's
        // actions (which are made from them).
        (void)CycleLibrary::of(size);
        detail::prepare_centre_search(size);
    }
}

}  // namespace cube::nxn
