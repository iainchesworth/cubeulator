#include <cube/nxn/strategy.hpp>

#include <algorithm>
#include <span>
#include <utility>

namespace cube::nxn {

Strategy default_strategy(int size) {
    if (size == 2) {
        return std::move(Strategy{"2x2: one search"}.then("corners", solve_corners()));
    }
    if (size == 3) {
        return std::move(Strategy{"3x3: the 3x3 solver"}.then("core", solve_core()));
    }
    Strategy strategy{size % 2 == 1 ? "odd: fixed centres, reduction"
                                    : "even: free centres, reduction"};
    if (size % 2 == 1) {
        strategy.then("plan", plan(FixedCentres{}))
            .then("parity", make_wings_even())
            .then("centres", solve_centres())
            .then("plan_edges", plan_edges(FixedCentres{}));
    } else {
        strategy.then("plan", plan(FreeCentres{}))
            .then("parity", make_wings_even())
            .then("centres", solve_centres())
            .then("plan_edges", plan_edges(FreeCentres{}));
    }
    strategy.then("edges", solve_edges()).then("core", solve_core());
    return strategy;
}

Result<Solution> solve(const NCube& scrambled, const Strategy& strategy,
                       solver::SolveMethod method) {
    Context context{.cube = scrambled, .steps = {}, .method = method, .targets = std::nullopt};
    for (const Strategy::Entry& entry : strategy.stages()) {
        if (Result<void> result = entry.run(context); !result) {
            return std::unexpected(result.error());
        }
    }
    // The 3x3 solver's moves are merged already, in the way its own stages
    // expect; merging them again would merge turns it chose not to (R L R'
    // is L), so a solution that is only the 3x3's is returned as it was
    // made. Anything a reduction added has to be merged with it.
    const bool only_core = std::ranges::all_of(
        context.steps, [](const Step& step) { return step.stage == Stage::Core; });
    if (!only_core) {
        merge_steps(context.steps, scrambled.size());
    }

    NCube check = scrambled;
    for (const Step& step : context.steps) {
        check.apply(std::span<const LayerMove>{step.moves});
    }
    if (!check.is_solved()) {
        return std::unexpected(CubeError::no_solution);
    }
    return Solution{.steps = std::move(context.steps)};
}

Result<Solution> solve(const NCube& scrambled, solver::SolveMethod method) {
    return solve(scrambled, default_strategy(scrambled.size()), method);
}

}  // namespace cube::nxn
