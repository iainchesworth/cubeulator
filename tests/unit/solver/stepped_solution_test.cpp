#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <string>
#include <vector>

#include <cube/solver/notation.hpp>
#include <cube/solver/solver.hpp>
#include <cube/solver/stepped_solution.hpp>

using cube::solver::Cube;
using cube::solver::merge_adjacent_moves;
using cube::solver::Move;
using cube::solver::MoveCount;
using cube::solver::parse_algorithm;
using cube::solver::random_scramble;
using cube::solver::Solution;
using cube::solver::solve;
using cube::solver::SolveStep;
using cube::solver::Stage;
using cube::solver::StageRange;
using cube::solver::SteppedSolution;
using cube::solver::to_string;

namespace {

SteppedSolution stepped_for(unsigned seed) {
    const Cube scrambled = random_scramble(seed, MoveCount{25});
    const auto solution = solve(scrambled);
    REQUIRE(solution.has_value());
    return SteppedSolution::from(scrambled, *solution);
}

// The invariants a SteppedSolution has to keep once adjacent same-face moves
// are merged across stage boundaries (which can leave a stage empty): one
// range per stage in solving order, the ranges tile [0, move_count) with no
// gap or overlap, each move's stage is the stage of the range it falls in,
// and no two neighbouring moves share a face.
void require_well_formed(SteppedSolution stepped) {
    const std::vector<Stage> stages = {
        Stage::Cross, Stage::WhiteCorners, Stage::MiddleEdges, Stage::Oll, Stage::Pll,
    };
    const std::vector<StageRange>& ranges = stepped.stage_ranges();

    REQUIRE(ranges.size() == stages.size());
    int expected_start = 0;
    for (std::size_t i = 0; i < ranges.size(); ++i) {
        CAPTURE(i);
        REQUIRE(ranges.at(i).stage == stages.at(i));
        REQUIRE(ranges.at(i).start_index == expected_start);
        REQUIRE(ranges.at(i).end_index >= ranges.at(i).start_index);
        expected_start = ranges.at(i).end_index;
    }
    REQUIRE(expected_start == stepped.move_count());

    const std::vector<Move>& moves = stepped.moves();
    REQUIRE(static_cast<int>(moves.size()) == stepped.move_count());
    for (std::size_t i = 1; i < moves.size(); ++i) {
        CAPTURE(i);
        REQUIRE(moves.at(i).face != moves.at(i - 1).face);
    }

    for (const StageRange& range : ranges) {
        for (int index = range.start_index; index < range.end_index; ++index) {
            CAPTURE(index);
            while (stepped.current_index() < index) {
                stepped.next();
            }
            REQUIRE(stepped.current_stage() == range.stage);
        }
    }
}

}  // namespace

TEST_CASE("SteppedSolution starts at the scrambled state, not yet at the end",
          "[stepped_solution][slow]") {
    const SteppedSolution stepped = stepped_for(0);
    REQUIRE(stepped.at_start());
    REQUIRE(stepped.current_index() == 0);
    if (stepped.move_count() > 0) {
        REQUIRE_FALSE(stepped.at_end());
    }
}

TEST_CASE("stepping forward through every move reaches a solved cube", "[stepped_solution][slow]") {
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

TEST_CASE("next then previous returns to the same cube state", "[stepped_solution][slow]") {
    SteppedSolution stepped = stepped_for(1);
    REQUIRE(stepped.move_count() > 0);

    const Cube before = stepped.current_cube();
    stepped.next();
    REQUIRE(stepped.current_index() == 1);
    stepped.previous();
    REQUIRE(stepped.current_index() == 0);
    REQUIRE(stepped.current_cube() == before);
}

TEST_CASE("next and previous are no-ops past the ends", "[stepped_solution][slow]") {
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

TEST_CASE("current_move applied to current_cube reproduces the next cube in sequence",
          "[stepped_solution][slow]") {
    SteppedSolution stepped = stepped_for(0);
    REQUIRE(stepped.move_count() > 0);

    const Cube before = stepped.current_cube();
    const auto move = stepped.current_move();
    stepped.next();
    REQUIRE(stepped.current_cube() == before.apply(move));
}

TEST_CASE("stage_ranges cover every move exactly once, in order, with no gaps",
          "[stepped_solution][slow]") {
    const SteppedSolution stepped = stepped_for(0);
    int expected_start = 0;
    for (const auto& range : stepped.stage_ranges()) {
        REQUIRE(range.start_index == expected_start);
        REQUIRE(range.end_index >= range.start_index);
        expected_start = range.end_index;
    }
    REQUIRE(expected_start == stepped.move_count());
}

TEST_CASE("stage_ranges still tile the move list when a merge empties stages",
          "[stepped_solution]") {
    // F2 F2 cancels, then U U' and R R' cancel outwards, emptying both Cross
    // and WhiteCorners; MiddleEdges' D and Oll's D merge into one D2 that
    // stays with MiddleEdges; Oll's R is cancelled by Pll's R', leaving Oll
    // empty between them.
    const std::vector<std::string> algorithms = {"R U F2", "F2 U' R'", "D", "D R", "R' L"};
    const std::vector<Stage> stages = {
        Stage::Cross, Stage::WhiteCorners, Stage::MiddleEdges, Stage::Oll, Stage::Pll,
    };
    Solution solution;
    for (std::size_t i = 0; i < algorithms.size(); ++i) {
        const auto moves = parse_algorithm(algorithms.at(i));
        REQUIRE(moves.has_value());
        solution.steps.push_back(SolveStep{.stage = stages.at(i), .moves = *moves});
    }

    const Cube start = random_scramble(3, MoveCount{10});
    const SteppedSolution untidied = SteppedSolution::from(start, solution);
    REQUIRE(untidied.move_count() == 11);

    SteppedSolution stepped = SteppedSolution::from(start, merge_adjacent_moves(solution));
    REQUIRE(to_string(stepped.moves()) == "D2 L");

    const auto& ranges = stepped.stage_ranges();
    REQUIRE(ranges.size() == 5);
    REQUIRE((ranges.at(0).start_index == 0 && ranges.at(0).end_index == 0));
    REQUIRE((ranges.at(1).start_index == 0 && ranges.at(1).end_index == 0));
    REQUIRE((ranges.at(2).start_index == 0 && ranges.at(2).end_index == 1));
    REQUIRE((ranges.at(3).start_index == 1 && ranges.at(3).end_index == 1));
    REQUIRE((ranges.at(4).start_index == 1 && ranges.at(4).end_index == 2));
    require_well_formed(stepped);

    // The empty stages are never "current": the first move belongs to
    // MiddleEdges and the second to Pll.
    REQUIRE(stepped.current_stage() == Stage::MiddleEdges);
    stepped.next();
    REQUIRE(stepped.current_stage() == Stage::Pll);
    stepped.next();
    REQUIRE(stepped.at_end());

    // Same final cube as the moves it was tidied from.
    Cube expected = start;
    for (const Move& move : untidied.moves()) {
        expected = expected.apply(move);
    }
    REQUIRE(stepped.current_cube() == expected);
}

TEST_CASE("a solved scramble's stage ranges tile the move list and the cube ends solved",
          "[stepped_solution][slow]") {
    // Seed 7 is the scramble that used to end a stage with F2 and start the
    // next with F2; the others are ordinary 25-move scrambles.
    for (const unsigned seed : {0U, 3U, 7U}) {
        CAPTURE(seed);
        SteppedSolution stepped = stepped_for(seed);
        require_well_formed(stepped);

        while (!stepped.at_end()) {
            stepped.next();
        }
        REQUIRE(stepped.current_cube().is_solved());
    }
}

// Short scrambles leave whole stages with nothing to do, and a boundary merge
// can empty a stage that did have moves; either way the ranges must tile.
TEST_CASE("stage ranges tile the move list for short scrambles with empty stages",
          "[stepped_solution][slow]") {
    for (const int move_count : {1, 2, 3}) {
        for (unsigned seed = 0; seed < 3; ++seed) {
            CAPTURE(move_count, seed);
            const Cube scrambled = random_scramble(seed, MoveCount{move_count});
            const auto solution = solve(scrambled);
            REQUIRE(solution.has_value());

            SteppedSolution stepped = SteppedSolution::from(scrambled, *solution);
            require_well_formed(stepped);
            while (!stepped.at_end()) {
                stepped.next();
            }
            REQUIRE(stepped.current_cube().is_solved());
        }
    }
}
