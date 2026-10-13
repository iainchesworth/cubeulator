#include <cube/solver/solver.hpp>

#include <random>
#include <utility>

#include <cube/solver/corners.hpp>
#include <cube/solver/cross.hpp>
#include <cube/solver/middle_edges.hpp>
#include <cube/solver/oll.hpp>
#include <cube/solver/pll.hpp>

namespace cube::solver {

Cube random_scramble(unsigned seed, int move_count) noexcept {
    std::mt19937 rng{seed};
    std::uniform_int_distribution<std::size_t> face_dist{0, kAllFaces.size() - 1};
    std::uniform_int_distribution<int> turn_dist{1, 3};

    Cube cube = Cube::solved();
    for (int i = 0; i < move_count; ++i) {
        cube = cube.apply(Move{kAllFaces[face_dist(rng)], turn_dist(rng)});
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

Result<Solution> solve(const Cube& scrambled) {
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

    return solution;
}

}  // namespace cube::solver
