#include <catch2/catch_test_macros.hpp>

#include <random>

#include <cube/solver/corners.hpp>
#include <cube/solver/cross.hpp>
#include <cube/solver/middle_edges.hpp>

using cube::solver::Cube;
using cube::solver::f2l_solved;
using cube::solver::kAllFaces;
using cube::solver::Move;
using cube::solver::solve_corners;
using cube::solver::solve_cross;
using cube::solver::solve_middle_edges;

namespace {

Cube scramble(unsigned seed, int move_count) {
    std::mt19937 rng{seed};
    std::uniform_int_distribution<std::size_t> face_dist{0, kAllFaces.size() - 1};
    std::uniform_int_distribution<int> turn_dist{1, 3};

    Cube cube = Cube::solved();
    for (int i = 0; i < move_count; ++i) {
        cube = cube.apply(Move{kAllFaces[face_dist(rng)], turn_dist(rng)});
    }
    return cube;
}

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

TEST_CASE("solve_middle_edges completes F2L for a corpus of random scrambles", "[middle_edges]") {
    for (unsigned seed = 0; seed < 40; ++seed) {
        CAPTURE(seed);
        Cube cube = scramble(seed, 25);

        REQUIRE(apply_all(cube, solve_cross(cube)).has_value());
        REQUIRE(apply_all(cube, solve_corners(cube)).has_value());
        REQUIRE(apply_all(cube, solve_middle_edges(cube)).has_value());

        REQUIRE(f2l_solved(cube));
        REQUIRE(cube.is_physically_valid());
    }
}
