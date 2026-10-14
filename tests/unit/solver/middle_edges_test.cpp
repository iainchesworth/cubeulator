#include <catch2/catch_test_macros.hpp>

#include <random>

#include <cube/solver/corners.hpp>
#include <cube/solver/cross.hpp>
#include <cube/solver/middle_edges.hpp>
#include <cube/solver/solver.hpp>

using cube::solver::Cube;
using cube::solver::f2l_solved;
using cube::solver::kAllFaces;
using cube::solver::Move;
using cube::solver::random_scramble;
using cube::solver::solve_corners;
using cube::solver::solve_cross;
using cube::solver::solve_middle_edges;

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

TEST_CASE("a solved cube's F2L is already solved", "[middle_edges]") {
    REQUIRE(f2l_solved(Cube::solved()));
    const auto solution = solve_middle_edges(Cube::solved());
    REQUIRE(solution.has_value());
    REQUIRE(solution->empty());
}

TEST_CASE("solve_middle_edges completes F2L for a corpus of random scrambles",
          "[middle_edges][slow]") {
    for (unsigned seed = 0; seed < 40; ++seed) {
        CAPTURE(seed);
        Cube cube = random_scramble(seed);

        REQUIRE(apply_all(cube, solve_cross(cube)).has_value());
        REQUIRE(apply_all(cube, solve_corners(cube)).has_value());
        REQUIRE(apply_all(cube, solve_middle_edges(cube)).has_value());

        REQUIRE(f2l_solved(cube));
        REQUIRE(cube.is_physically_valid());
    }
}
