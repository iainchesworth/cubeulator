#include <catch2/catch_test_macros.hpp>

#include <random>

#include <cube/solver/corners.hpp>
#include <cube/solver/cross.hpp>
#include <cube/solver/solver.hpp>

using cube::solver::Cube;
using cube::solver::first_layer_solved;
using cube::solver::kAllFaces;
using cube::solver::Move;
using cube::solver::random_scramble;
using cube::solver::solve_corners;
using cube::solver::solve_cross;

TEST_CASE("a solved cube's first layer is already solved", "[corners]") {
    REQUIRE(first_layer_solved(Cube::solved()));
    const auto solution = solve_corners(Cube::solved());
    REQUIRE(solution.has_value());
    REQUIRE(solution->empty());
}

TEST_CASE("solve_corners completes the first layer for a corpus of random scrambles",
          "[corners][slow]") {
    for (unsigned seed = 0; seed < 40; ++seed) {
        CAPTURE(seed);
        Cube cube = random_scramble(seed);

        const auto cross_moves = solve_cross(cube);
        REQUIRE(cross_moves.has_value());
        for (const Move& m : *cross_moves) {
            cube = cube.apply(m);
        }
        REQUIRE(cube.is_physically_valid());

        const auto corner_moves = solve_corners(cube);
        REQUIRE(corner_moves.has_value());
        for (const Move& m : *corner_moves) {
            cube = cube.apply(m);
        }

        REQUIRE(first_layer_solved(cube));
        REQUIRE(cube.is_physically_valid());
    }
}
