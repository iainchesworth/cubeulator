#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <random>
#include <string>
#include <vector>

#include <cube/solver/notation.hpp>
#include <cube/solver/solver.hpp>

using cube::solver::Cube;
using cube::solver::Face;
using cube::solver::merge_adjacent_moves;
using cube::solver::Move;
using cube::solver::parse_algorithm;
using cube::solver::random_scramble;
using cube::solver::Solution;
using cube::solver::solve;
using cube::solver::SolveStep;
using cube::solver::Stage;
using cube::solver::to_string;

namespace {

constexpr std::array<Stage, 5> kStagesInOrder = {Stage::Cross, Stage::WhiteCorners,
                                                 Stage::MiddleEdges, Stage::Oll, Stage::Pll};

// A Solution with one step per stage, in solving order, from one algorithm
// string per stage ("" for a stage with no moves).
Solution solution_of(const std::array<std::string, 5>& per_stage) {
    Solution solution;
    for (std::size_t i = 0; i < per_stage.size(); ++i) {
        const auto moves = parse_algorithm(per_stage[i]);
        REQUIRE(moves.has_value());
        solution.steps.push_back(SolveStep{kStagesInOrder[i], *moves});
    }
    return solution;
}

std::vector<Move> flatten(const Solution& solution) {
    std::vector<Move> moves;
    for (const SolveStep& step : solution.steps) {
        moves.insert(moves.end(), step.moves.begin(), step.moves.end());
    }
    return moves;
}

// Each step's moves as notation, e.g. {"R F2", "", "U", "", ""}.
std::array<std::string, 5> notation_per_stage(const Solution& solution) {
    REQUIRE(solution.steps.size() == 5);
    std::array<std::string, 5> out;
    for (std::size_t i = 0; i < out.size(); ++i) {
        out[i] = to_string(solution.steps[i].moves);
    }
    return out;
}

bool has_adjacent_same_face(const std::vector<Move>& moves) {
    for (std::size_t i = 1; i < moves.size(); ++i) {
        if (moves[i].face == moves[i - 1].face) {
            return true;
        }
    }
    return false;
}

Cube apply_all(Cube cube, const std::vector<Move>& moves) {
    for (const Move& move : moves) {
        cube = cube.apply(move);
    }
    return cube;
}

}  // namespace

TEST_CASE("solve fully solves a corpus of random scrambles via the beginner method",
          "[solve][slow]") {
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

TEST_CASE("solve produces every stage in order for a scramble that needs each one",
          "[solve][slow]") {
    const Cube scrambled = random_scramble(0, 25);
    const auto solution = solve(scrambled);
    REQUIRE(solution.has_value());

    std::vector<Stage> stages;
    for (const auto& step : solution->steps) {
        stages.push_back(step.stage);
    }
    REQUIRE(stages == std::vector<Stage>{Stage::Cross, Stage::WhiteCorners, Stage::MiddleEdges,
                                         Stage::Oll, Stage::Pll});
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

TEST_CASE("merge_adjacent_moves adds turn counts on a face and drops a cancelled pair", "[solve]") {
    const auto tidied = [](const char* algorithm) {
        return to_string(flatten(merge_adjacent_moves(solution_of({algorithm, "", "", "", ""}))));
    };

    REQUIRE(tidied("R R") == "R2");
    REQUIRE(tidied("R R'") == "");
    REQUIRE(tidied("R2 R2") == "");
    REQUIRE(tidied("R R2") == "R'");
    REQUIRE(tidied("R' R'") == "R2");
    REQUIRE(tidied("F F F") == "F'");
    REQUIRE(tidied("F F F F") == "");
    REQUIRE(tidied("R U F") == "R U F");
    // Opposite faces are not "the same face": they are left alone.
    REQUIRE(tidied("U D U") == "U D U");
}

TEST_CASE("merge_adjacent_moves keeps merging when a cancelled pair joins its neighbours",
          "[solve]") {
    const auto tidied = [](const char* algorithm) {
        return to_string(flatten(merge_adjacent_moves(solution_of({algorithm, "", "", "", ""}))));
    };

    REQUIRE(tidied("R U U' R'") == "");
    REQUIRE(tidied("R U F F' U' R'") == "");
    // The surviving D and D2 meet once R U U' R' has gone, and add up.
    REQUIRE(tidied("D R U U' R' D2") == "D'");
    // ... and a sum of zero there removes that pair too, exposing L L.
    REQUIRE(tidied("L D R U U' R' D' L") == "L2");
}

TEST_CASE("merge_adjacent_moves merges across a stage boundary into exactly one stage", "[solve]") {
    using Notation = std::array<std::string, 5>;

    SECTION("the same half turn ending one stage and starting the next cancels") {
        const Solution tidied = merge_adjacent_moves(solution_of({"R U F2", "F2 D", "", "", ""}));
        REQUIRE(notation_per_stage(tidied) == Notation{"R U", "D", "", "", ""});
    }

    SECTION("a cancelling pair that exposes another pair keeps going into both stages") {
        // F2 F2 goes, which brings U and U' together, which then go too.
        const Solution tidied =
            merge_adjacent_moves(solution_of({"R U F2", "F2 U' D", "", "", ""}));
        REQUIRE(notation_per_stage(tidied) == Notation{"R", "D", "", "", ""});
    }

    SECTION("a cascade across the boundary can empty both stages") {
        const Solution tidied =
            merge_adjacent_moves(solution_of({"R U F", "F' U' R'", "", "", ""}));
        REQUIRE(notation_per_stage(tidied) == Notation{"", "", "", "", ""});
    }

    SECTION("a merged move stays with the earlier stage, so it is in exactly one step") {
        const Solution tidied = merge_adjacent_moves(solution_of({"R F", "F U", "", "", ""}));
        REQUIRE(notation_per_stage(tidied) == Notation{"R F2", "U", "", "", ""});
    }

    SECTION("a stage left with no moves does not separate the stages either side of it") {
        const Solution tidied = merge_adjacent_moves(solution_of({"R", "", "R'", "L", ""}));
        REQUIRE(notation_per_stage(tidied) == Notation{"", "", "", "L", ""});
    }

    SECTION("a stage can lose every move while the stages after it keep theirs") {
        const Solution tidied = merge_adjacent_moves(solution_of({"R", "R R", "L", "", "U"}));
        REQUIRE(notation_per_stage(tidied) == Notation{"R'", "", "L", "", "U"});
    }

    SECTION("every stage stays, in order, whether or not it has moves left") {
        const Solution tidied =
            merge_adjacent_moves(solution_of({"R U F", "F' U' R'", "", "", ""}));
        REQUIRE(tidied.steps.size() == kStagesInOrder.size());
        for (std::size_t i = 0; i < kStagesInOrder.size(); ++i) {
            REQUIRE(tidied.steps[i].stage == kStagesInOrder[i]);
        }
    }
}

TEST_CASE("merge_adjacent_moves drops a move that turns nothing", "[solve]") {
    // Not something a stage solver emits, but "no two adjacent moves share a
    // face" has to hold for any Solution, and a zero-turn move between two R
    // moves leaves them adjacent.
    Solution solution;
    solution.steps.push_back(
        SolveStep{Stage::Cross, {Move{Face::R, 1}, Move{Face::U, 0}, Move{Face::R, 1}}});
    REQUIRE(to_string(flatten(merge_adjacent_moves(solution))) == "R2");
}

TEST_CASE("merge_adjacent_moves never changes what a sequence does to a cube", "[solve]") {
    // Three faces only, so same-face neighbours are common and cascades
    // frequent; sequences split at random points into the five stages.
    constexpr std::array<Face, 3> kFaces = {Face::R, Face::U, Face::F};
    std::mt19937 rng{12345};
    std::uniform_int_distribution<std::size_t> face_dist{0, kFaces.size() - 1};
    std::uniform_int_distribution<int> turn_dist{1, 3};
    std::uniform_int_distribution<int> length_dist{0, 40};

    const Cube start = random_scramble(11, 20);
    for (int trial = 0; trial < 300; ++trial) {
        CAPTURE(trial);
        std::vector<Move> sequence(static_cast<std::size_t>(length_dist(rng)));
        for (Move& move : sequence) {
            move = Move{kFaces[face_dist(rng)], turn_dist(rng)};
        }

        // Four cut points, so the five stages get consecutive slices (some
        // of them empty).
        std::uniform_int_distribution<std::size_t> cut_dist{0, sequence.size()};
        std::array<std::size_t, 4> cuts{};
        for (std::size_t& cut : cuts) {
            cut = cut_dist(rng);
        }
        std::ranges::sort(cuts);

        Solution original;
        std::size_t from = 0;
        for (std::size_t i = 0; i < kStagesInOrder.size(); ++i) {
            const std::size_t to = i < cuts.size() ? cuts[i] : sequence.size();
            original.steps.push_back(
                SolveStep{kStagesInOrder[i],
                          std::vector<Move>(sequence.begin() + static_cast<std::ptrdiff_t>(from),
                                            sequence.begin() + static_cast<std::ptrdiff_t>(to))});
            from = to;
        }
        CAPTURE(to_string(flatten(original)));

        const Solution tidied = merge_adjacent_moves(original);
        const std::vector<Move> moves = flatten(tidied);
        CAPTURE(to_string(moves));

        REQUIRE_FALSE(has_adjacent_same_face(moves));
        REQUIRE(apply_all(start, moves) == apply_all(start, flatten(original)));
        REQUIRE(moves.size() <= flatten(original).size());
        REQUIRE(tidied.steps.size() == original.steps.size());
        // Tidying again finds nothing more to do.
        REQUIRE(flatten(merge_adjacent_moves(tidied)) == moves);
    }
}

TEST_CASE("solve never emits two adjacent moves on one face, across stage boundaries too",
          "[solve][slow]") {
    for (unsigned seed = 0; seed < 10; ++seed) {
        CAPTURE(seed);
        const Cube scrambled = random_scramble(seed, 25);
        const auto solution = solve(scrambled);
        REQUIRE(solution.has_value());
        REQUIRE(solution->steps.size() == kStagesInOrder.size());

        const std::vector<Move> moves = flatten(*solution);
        CAPTURE(to_string(moves));
        REQUIRE_FALSE(has_adjacent_same_face(moves));
        REQUIRE(apply_all(scrambled, moves).is_solved());
    }
}

// random_scramble(7) used to be solved with one stage ending in F2 and the
// next starting with F2 (move indices 27 and 28 of the flattened solution),
// which cancel out.
TEST_CASE("solve tidies the seed 7 scramble's F2 across a stage boundary", "[solve][slow]") {
    const Cube scrambled = random_scramble(7);
    const auto solution = solve(scrambled);
    REQUIRE(solution.has_value());

    const std::vector<Move> moves = flatten(*solution);
    CAPTURE(to_string(moves));
    REQUIRE_FALSE(has_adjacent_same_face(moves));
    REQUIRE(apply_all(scrambled, moves).is_solved());
}

// Short scrambles leave whole stages with nothing to do - and the boundary
// merges that can empty a stage entirely are exactly where "a stage may have
// zero moves" has to keep working.
TEST_CASE("solve tidies short scrambles too, where some stages have nothing to do",
          "[solve][slow]") {
    for (const int move_count : {2, 3, 5}) {
        for (unsigned seed = 0; seed < 4; ++seed) {
            CAPTURE(move_count, seed);
            const Cube scrambled = random_scramble(seed, move_count);
            const auto solution = solve(scrambled);
            REQUIRE(solution.has_value());
            REQUIRE(solution->steps.size() == kStagesInOrder.size());

            const std::vector<Move> moves = flatten(*solution);
            CAPTURE(to_string(moves));
            REQUIRE_FALSE(has_adjacent_same_face(moves));
            REQUIRE(apply_all(scrambled, moves).is_solved());
        }
    }
}
