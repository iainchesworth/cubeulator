#include <cube/solver/solver.hpp>

#include <cstddef>
#include <random>
#include <utility>
#include <vector>

#include <cube/profiling.hpp>
#include <cube/solver/cfop.hpp>
#include <cube/solver/corners.hpp>
#include <cube/solver/cross.hpp>
#include <cube/solver/middle_edges.hpp>
#include <cube/solver/oll.hpp>
#include <cube/solver/pll.hpp>
#include <cube/solver/two_phase.hpp>

namespace cube::solver {

Cube random_scramble(unsigned seed, int move_count) noexcept {
    std::mt19937 rng{seed};
    std::uniform_int_distribution<std::size_t> face_dist{0, kAllFaces.size() - 1};
    std::uniform_int_distribution<int> turn_dist{1, 3};

    Cube cube = Cube::solved();
    for (int i = 0; i < move_count; ++i) {
        cube = cube.apply(Move{kAllFaces.at(face_dist(rng)), turn_dist(rng)});
    }
    return cube;
}

namespace {

Result<void> run_stage(Solution& solution, Cube& cube, Stage stage,
                       Result<std::vector<Move>> (*solve_fn)(const Cube&)) {
    Result<std::vector<Move>> moves = solve_fn(cube);
    if (!moves.has_value()) {
        return std::unexpected(moves.error());
    }
    for (const Move& m : *moves) {
        cube = cube.apply(m);
    }
    solution.steps.push_back(SolveStep{stage, std::move(*moves)});
    return {};
}

}  // namespace

Solution merge_adjacent_moves(Solution solution) {
    CUBE_ZONE_SCOPED_N("solver.merge_adjacent_moves");
    // One stack over the whole solution rather than a pass per step. The
    // kept moves never have two neighbours on the same face, so an incoming
    // move can only merge with the top of the stack; when that pair cancels,
    // the entry beneath becomes the top and meets the next incoming move,
    // which is how "R U U' R'" collapses completely in one pass. Each entry
    // remembers the step it began in and never changes it, so a move merged
    // across a boundary stays with the earlier stage.
    struct Entry {
        std::size_t step;
        Move move;
    };
    std::vector<Entry> kept;

    for (std::size_t step = 0; step < solution.steps.size(); ++step) {
        for (const Move& move : solution.steps.at(step).moves) {
            const int turns = ((move.turns % 4) + 4) % 4;
            if (turns == 0) {
                continue;
            }
            if (kept.empty() || kept.back().move.face != move.face) {
                kept.push_back(Entry{step, Move{move.face, turns}});
                continue;
            }
            const int merged = (kept.back().move.turns + turns) % 4;
            if (merged == 0) {
                kept.pop_back();
            } else {
                kept.back().move.turns = merged;
            }
        }
    }

    for (SolveStep& step : solution.steps) {
        step.moves.clear();
    }
    for (const Entry& entry : kept) {
        solution.steps.at(entry.step).moves.push_back(entry.move);
    }
    return solution;
}

namespace {

Result<Solution> solve_beginner(const Cube& scrambled) {
    if (!scrambled.is_physically_valid()) {
        return std::unexpected(CubeError::invalid_cube_state);
    }

    Solution solution;
    Cube cube = scrambled;

    if (Result<void> r = run_stage(solution, cube, Stage::Cross, solve_cross); !r.has_value()) {
        return std::unexpected(r.error());
    }
    if (Result<void> r = run_stage(solution, cube, Stage::WhiteCorners, solve_corners);
        !r.has_value()) {
        return std::unexpected(r.error());
    }
    if (Result<void> r = run_stage(solution, cube, Stage::MiddleEdges, solve_middle_edges);
        !r.has_value()) {
        return std::unexpected(r.error());
    }
    if (Result<void> r = run_stage(solution, cube, Stage::Oll, solve_oll); !r.has_value()) {
        return std::unexpected(r.error());
    }
    if (Result<void> r = run_stage(solution, cube, Stage::Pll, solve_pll); !r.has_value()) {
        return std::unexpected(r.error());
    }

    if (!cube.is_solved()) {
        return std::unexpected(CubeError::no_solution);
    }

    // After the is_solved() check, not before: merging only rewrites the
    // sequence into an equivalent one, so the check above is of the moves
    // the stage solvers actually produced.
    return merge_adjacent_moves(std::move(solution));
}

}  // namespace

Result<Solution> solve(const Cube& scrambled, SolveMethod method) {
    CUBE_ZONE_SCOPED_N("solver.solve");
    switch (method) {
        case SolveMethod::Beginner:
            return solve_beginner(scrambled);
        case SolveMethod::Intermediate:
            return solve_cfop(scrambled);
        case SolveMethod::Advanced:
            return solve_two_phase(scrambled);
    }
    return solve_beginner(scrambled);
}

void prepare(SolveMethod method) {
    if (method == SolveMethod::Advanced) {
        prepare_two_phase();
    }
}

}  // namespace cube::solver
