#include <catch2/catch_test_macros.hpp>

#include <cube/solver/solver.hpp>

using cube::solver::Cube;
using cube::solver::random_scramble;
using cube::solver::solve;
using cube::solver::Stage;

TEST_CASE("solve fully solves a corpus of random scrambles via the beginner method", "[solve][slow]") {
    for (unsigned seed = 0; seed < 3; ++seed) {
        CAPTURE(seed);
        const Cube scrambled = random_scramble(seed, 25);
        const auto solution = solve(scrambled);
        REQUIRE(solution.has_value());

        Cube cube = scrambled;
        for (const auto& step : solution->steps) {
            for (const auto& move : step.moves) {
                cube = cube.apply(move);
            }
        }
        REQUIRE(cube.is_solved());
    }
}

TEST_CASE("solve produces every stage in order for a scramble that needs each one", "[solve][slow]") {
    const Cube scrambled = random_scramble(0, 25);
    const auto solution = solve(scrambled);
    REQUIRE(solution.has_value());

    std::vector<Stage> stages;
    for (const auto& step : solution->steps) {
        stages.push_back(step.stage);
    }
    REQUIRE(stages == std::vector<Stage>{Stage::Cross, Stage::WhiteCorners, Stage::MiddleEdges, Stage::Oll,
                                          Stage::Pll});
}

// solve()'s is_physically_valid() guard (see solver.cpp) isn't exercised
// here with a genuinely invalid Cube: the public API has no way to
// construct one (Cube::solved() and Cube::apply() both always produce
// physically valid states by construction - see cube.hpp), which is a
// deliberate design choice, not a test gap. is_physically_valid() itself
// is covered directly in cube_test.cpp.
TEST_CASE("solve accepts an already-solved cube and every stage needs zero moves", "[solve]") {
    const auto solution = solve(Cube::solved());
    REQUIRE(solution.has_value());
    for (const auto& step : solution->steps) {
        REQUIRE(step.moves.empty());
    }
}
