#include <catch2/catch_test_macros.hpp>

#include <cube/solver/solver.hpp>
#include <cube/solver/stepped_solution.hpp>

using cube::solver::Cube;
using cube::solver::random_scramble;
using cube::solver::solve;
using cube::solver::SteppedSolution;

namespace {

SteppedSolution stepped_for(unsigned seed) {
    const Cube scrambled = random_scramble(seed, 25);
    const auto solution = solve(scrambled);
    REQUIRE(solution.has_value());
    return SteppedSolution::from(scrambled, *solution);
}

}  // namespace

TEST_CASE("SteppedSolution starts at the scrambled state, not yet at the end", "[stepped_solution]") {
    const SteppedSolution stepped = stepped_for(0);
    REQUIRE(stepped.at_start());
    REQUIRE(stepped.current_index() == 0);
    if (stepped.move_count() > 0) {
        REQUIRE_FALSE(stepped.at_end());
    }
}

TEST_CASE("stepping forward through every move reaches a solved cube", "[stepped_solution]") {
    SteppedSolution stepped = stepped_for(0);
    const int total = stepped.move_count();

    for (int i = 0; i < total; ++i) {
        REQUIRE_FALSE(stepped.at_end());
        stepped.next();
    }
    REQUIRE(stepped.at_end());
    REQUIRE(stepped.current_index() == total);
    REQUIRE(stepped.current_cube().is_solved());
}

TEST_CASE("next then previous returns to the same cube state", "[stepped_solution]") {
    SteppedSolution stepped = stepped_for(1);
    REQUIRE(stepped.move_count() > 0);

    const Cube before = stepped.current_cube();
    stepped.next();
    REQUIRE(stepped.current_index() == 1);
    stepped.previous();
    REQUIRE(stepped.current_index() == 0);
    REQUIRE(stepped.current_cube() == before);
}

TEST_CASE("next and previous are no-ops past the ends", "[stepped_solution]") {
    SteppedSolution stepped = stepped_for(2);
    stepped.previous();
    REQUIRE(stepped.at_start());

    while (!stepped.at_end()) {
        stepped.next();
    }
    const Cube solved_state = stepped.current_cube();
    stepped.next();
    REQUIRE(stepped.at_end());
    REQUIRE(stepped.current_cube() == solved_state);
}

TEST_CASE("current_move applied to current_cube reproduces the next cube in sequence", "[stepped_solution]") {
    SteppedSolution stepped = stepped_for(0);
    REQUIRE(stepped.move_count() > 0);

    const Cube before = stepped.current_cube();
    const auto move = stepped.current_move();
    stepped.next();
    REQUIRE(stepped.current_cube() == before.apply(move));
}

TEST_CASE("stage_ranges cover every move exactly once, in order, with no gaps", "[stepped_solution]") {
    const SteppedSolution stepped = stepped_for(0);
    int expected_start = 0;
    for (const auto& range : stepped.stage_ranges()) {
        REQUIRE(range.start_index == expected_start);
        REQUIRE(range.end_index >= range.start_index);
        expected_start = range.end_index;
    }
    REQUIRE(expected_start == stepped.move_count());
}
