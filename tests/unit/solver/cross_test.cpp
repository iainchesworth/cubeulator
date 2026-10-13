#include <catch2/catch_test_macros.hpp>

#include <random>

#include <cube/solver/cross.hpp>

using cube::solver::cross_solved;
using cube::solver::Cube;
using cube::solver::kAllFaces;
using cube::solver::Move;
using cube::solver::solve_cross;

namespace {

Cube scramble(unsigned seed, int move_count) {
    std::mt19937 rng{seed};
    std::uniform_int_distribution<std::size_t> face_dist{0, kAllFaces.size() - 1};
    std::uniform_int_distribution<int> turn_dist{1, 3};

    Cube cube = Cube::solved();
    for (int i = 0; i < move_count; ++i) {
        cube = cube.apply(Move{.face = kAllFaces.at(face_dist(rng)), .turns = turn_dist(rng)});
    }
    return cube;
}

}  // namespace

TEST_CASE("a solved cube's cross is already solved", "[cross]") {
    REQUIRE(cross_solved(Cube::solved()));
    const auto solution = solve_cross(Cube::solved());
    REQUIRE(solution.has_value());
    REQUIRE(solution->empty());
}

TEST_CASE("solve_cross solves the cross for a corpus of random scrambles", "[cross][slow]") {
    for (unsigned seed = 0; seed < 60; ++seed) {
        CAPTURE(seed);
        Cube cube = scramble(seed, 25);
        const auto solution = solve_cross(cube);
        REQUIRE(solution.has_value());
        REQUIRE(solution->size() <= 8);

        for (const Move& move : *solution) {
            cube = cube.apply(move);
        }
        REQUIRE(cross_solved(cube));
    }
}
