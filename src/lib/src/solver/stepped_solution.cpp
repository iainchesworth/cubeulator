#include <cube/solver/stepped_solution.hpp>

namespace cube::solver {

SteppedSolution SteppedSolution::from(const Cube& start, const Solution& solution) {
    SteppedSolution stepped;
    stepped.cubes_.push_back(start);

    Cube cube = start;
    for (const SolveStep& step : solution.steps) {
        const int range_start = static_cast<int>(stepped.moves_.size());
        for (const Move& move : step.moves) {
            cube = cube.apply(move);
            stepped.moves_.push_back(move);
            stepped.stage_per_move_.push_back(step.stage);
            stepped.cubes_.push_back(cube);
        }
        const int range_end = static_cast<int>(stepped.moves_.size());
        stepped.stage_ranges_.push_back(StageRange{step.stage, range_start, range_end});
    }

    return stepped;
}

void SteppedSolution::next() noexcept {
    if (!at_end()) {
        ++index_;
    }
}

void SteppedSolution::previous() noexcept {
    if (!at_start()) {
        --index_;
    }
}

}  // namespace cube::solver
