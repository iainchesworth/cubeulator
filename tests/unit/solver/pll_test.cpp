#include <catch2/catch_test_macros.hpp>

#include <random>

#include <cube/solver/corners.hpp>
#include <cube/solver/cross.hpp>
#include <cube/solver/middle_edges.hpp>
#include <cube/solver/oll.hpp>
#include <cube/solver/pll.hpp>
#include <cube/solver/solver.hpp>

using cube::solver::Cube;
using cube::solver::kAllFaces;
using cube::solver::Move;
using cube::solver::random_scramble;
using cube::solver::solve_corners;
using cube::solver::solve_cross;
using cube::solver::solve_middle_edges;
using cube::solver::solve_oll;
using cube::solver::solve_pll;

namespace {

cube::Result<void> apply_all(Cube& cube, const cube::Result<std::vector<Move>>& moves) {
    if (!moves.has_value()) {
        return std::unexpected(moves.error());
    }
    for (const Move& m : *moves) {
        cube = cube.apply(m);
    }
    return {};
}

}  // namespace

TEST_CASE("a solved cube's PLL is already solved", "[pll]") {
    const auto solution = solve_pll(Cube::solved());
    REQUIRE(solution.has_value());
    REQUIRE(solution->empty());
}

TEST_CASE("solve_pll fully solves the cube for a corpus of random scrambles", "[pll][slow]") {
    // This corpus was once capped at 3 seeds because the old per-scramble
    // commutator search could run 80+ minutes on a hard corner permutation
    // (seed 3 was the documented offender). solve_pll now composes fixed
    // 2-look algorithms (see last_layer.hpp) and takes milliseconds on
    // every last-layer state, seed 3 included - the corpus cost below is
    // entirely the earlier searched stages (cross/corners/middle edges)
    // needed to reach a valid PLL start, which is why this stays
    // [slow]-tagged despite PLL itself now being fast.
    for (unsigned seed = 0; seed < 10; ++seed) {
        CAPTURE(seed);
        Cube cube = random_scramble(seed);

        REQUIRE(apply_all(cube, solve_cross(cube)).has_value());
        REQUIRE(apply_all(cube, solve_corners(cube)).has_value());
        REQUIRE(apply_all(cube, solve_middle_edges(cube)).has_value());
        REQUIRE(apply_all(cube, solve_oll(cube)).has_value());
        REQUIRE(apply_all(cube, solve_pll(cube)).has_value());

        REQUIRE(cube.is_solved());
        REQUIRE(cube.is_physically_valid());
    }
}
