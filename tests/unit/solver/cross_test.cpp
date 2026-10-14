#include <catch2/catch_test_macros.hpp>

#include <random>

#include <cube/solver/cross.hpp>
#include <cube/solver/solver.hpp>

using cube::solver::cross_solved;
using cube::solver::Cube;
using cube::solver::kAllFaces;
using cube::solver::Move;
using cube::solver::random_scramble;
using cube::solver::solve_cross;

TEST_CASE("a solved cube's cross is already solved", "[cross]") {
    REQUIRE(cross_solved(Cube::solved()));
    const auto solution = solve_cross(Cube::solved());
    REQUIRE(solution.has_value());
    REQUIRE(solution->empty());
}

TEST_CASE("solve_cross solves the cross for a corpus of random scrambles", "[cross][slow]") {
    for (unsigned seed = 0; seed < 60; ++seed) {
        CAPTURE(seed);
        Cube cube = random_scramble(seed);
        const auto solution = solve_cross(cube);
        REQUIRE(solution.has_value());
        REQUIRE(solution->size() <= 8);

        for (const Move& move : *solution) {
            cube = cube.apply(move);
        }
        REQUIRE(cross_solved(cube));
    }
}
