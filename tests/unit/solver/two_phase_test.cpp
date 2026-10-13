#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <random>
#include <thread>
#include <vector>

#include <cube/solver/notation.hpp>
#include <cube/solver/solver.hpp>
#include <cube/solver/two_phase.hpp>

#include "cubie.hpp"

using cube::CubeError;
using cube::solver::Color;
using cube::solver::Cube;
using cube::solver::Face;
using cube::solver::kAllFaces;
using cube::solver::Move;
using cube::solver::parse_algorithm;
using cube::solver::prepare;
using cube::solver::prepare_two_phase;
using cube::solver::random_scramble;
using cube::solver::Solution;
using cube::solver::solve;
using cube::solver::solve_two_phase;
using cube::solver::SolveMethod;
using cube::solver::Stage;

namespace {

Cube apply_all(Cube cube, const std::vector<Move>& moves) {
    for (const Move& move : moves) {
        cube = cube.apply(move);
    }
    return cube;
}

std::vector<Move> flatten(const Solution& solution) {
    std::vector<Move> moves;
    for (const auto& step : solution.steps) {
        moves.insert(moves.end(), step.moves.begin(), step.moves.end());
    }
    return moves;
}

bool has_adjacent_same_face(const std::vector<Move>& moves) {
    for (std::size_t i = 1; i < moves.size(); ++i) {
        if (moves[i].face == moves[i - 1].face) {
            return true;
        }
    }
    return false;
}

Cube scrambled_by(const char* algorithm) {
    const auto moves = parse_algorithm(algorithm);
    REQUIRE(moves.has_value());
    return apply_all(Cube::solved(), *moves);
}

// A cube is in the phase-1 target group <U, D, R2, L2, F2, B2> when every
// corner is untwisted, every edge unflipped, and the four middle-layer edges
// are in the middle layer (in any order).
bool in_phase_two_group(const Cube& cube) {
    const auto cubie = cube::solver::detail::to_cubie(cube);
    if (!cubie.has_value()) {
        return false;
    }
    for (const auto twist : cubie->co) {
        if (twist != 0) {
            return false;
        }
    }
    for (const auto flip : cubie->eo) {
        if (flip != 0) {
            return false;
        }
    }
    for (std::size_t pos = 8; pos < 12; ++pos) {
        if (cubie->ep[pos] < 8) {
            return false;
        }
    }
    return true;
}

}  // namespace

TEST_CASE("two-phase solves an already-solved cube with no moves", "[two-phase]") {
    const auto solution = solve_two_phase(Cube::solved());
    REQUIRE(solution.has_value());
    CHECK(flatten(*solution).empty());
}

TEST_CASE("two-phase finds the one-move solution to a one-move scramble", "[two-phase]") {
    for (const Face face : kAllFaces) {
        for (int turns = 1; turns <= 3; ++turns) {
            const Cube cube = Cube::solved().apply(Move{face, turns});
            const auto solution = solve_two_phase(cube);
            REQUIRE(solution.has_value());
            const std::vector<Move> moves = flatten(*solution);
            REQUIRE(moves.size() == 1);
            CHECK(moves[0].face == face);
            CHECK(moves[0].turns == 4 - turns);
        }
    }
}

TEST_CASE("two-phase solves a corpus of random scrambles in about twenty moves",
          "[two-phase][slow]") {
    constexpr int kCount = 40;
    double total = 0;
    std::size_t longest = 0;
    for (unsigned seed = 1; seed <= kCount; ++seed) {
        const Cube cube = random_scramble(seed, 100);
        const auto solution = solve_two_phase(cube);
        REQUIRE(solution.has_value());

        const std::vector<Move> moves = flatten(*solution);
        CHECK(apply_all(cube, moves).is_solved());
        CHECK_FALSE(has_adjacent_same_face(moves));

        REQUIRE(solution->steps.size() == 2);
        CHECK(solution->steps[0].stage == Stage::Reduce);
        CHECK(solution->steps[1].stage == Stage::Finish);

        total += static_cast<double>(moves.size());
        longest = std::max(longest, moves.size());
    }
    // Measured: about 20.3 on average and 22 at worst over thousands of
    // scrambles. The bounds leave room for the sample, not for a regression
    // to the beginner method's hundred.
    CHECK(total / kCount < 22.0);
    CHECK(longest <= 26);
}

TEST_CASE("two-phase's first phase ends exactly in the group the second phase needs",
          "[two-phase][slow]") {
    for (unsigned seed = 1; seed <= 25; ++seed) {
        const Cube cube = random_scramble(seed, 100);
        const auto solution = solve_two_phase(cube);
        REQUIRE(solution.has_value());
        REQUIRE(solution->steps.size() == 2);

        const Cube halfway = apply_all(cube, solution->steps[0].moves);
        CHECK(in_phase_two_group(halfway));

        // And phase 2 itself never leaves it: only U, D and half turns.
        Cube state = halfway;
        for (const Move& move : solution->steps[1].moves) {
            state = state.apply(move);
            CHECK(in_phase_two_group(state));
        }
        CHECK(state.is_solved());
    }
}

TEST_CASE("two-phase handles the classic hard states", "[two-phase][slow]") {
    // The superflip is 20 moves from solved and the farthest cube known;
    // the others are well-known awkward symmetric states.
    const std::array<const char*, 4> kStates = {
        "U R2 F B R B2 R U2 L B2 R U' D' R2 F R' L B2 U2 F2",  // superflip
        "U2 D2 R2 L2 F2 B2",                                   // all half turns
        "R U R' U R U2 R'",                                    // Sune
        "R' F R' B2 R F' R' B2 R2",                            // A-perm
    };
    for (const char* algorithm : kStates) {
        const Cube cube = scrambled_by(algorithm);
        const auto solution = solve_two_phase(cube);
        REQUIRE(solution.has_value());
        const std::vector<Move> moves = flatten(*solution);
        CHECK(apply_all(cube, moves).is_solved());
        CHECK(moves.size() <= 24);
    }
}

TEST_CASE("two-phase gives the same solution every time", "[two-phase]") {
    // The search is bounded by a node budget, not the clock, so a given cube
    // gets one answer on every device and on every run.
    const Cube cube = random_scramble(77, 100);
    const auto first = solve_two_phase(cube);
    const auto second = solve_two_phase(cube);
    REQUIRE(first.has_value());
    REQUIRE(second.has_value());
    CHECK(flatten(*first) == flatten(*second));
}

TEST_CASE("two-phase's tables can be built and used from several threads at once",
          "[two-phase][slow]") {
    // First use initialises about 6 MB of tables; a person's solve and a
    // background prepare() racing for it must both come out right.
    std::array<bool, 4> ok{};
    std::vector<std::thread> threads;
    for (std::size_t i = 0; i < ok.size(); ++i) {
        threads.emplace_back([&ok, i] {
            if (i % 2 == 0) {
                prepare_two_phase();
            }
            const Cube cube = random_scramble(static_cast<unsigned>(500 + i), 100);
            const auto solution = solve_two_phase(cube);
            ok[i] = solution.has_value() && apply_all(cube, flatten(*solution)).is_solved();
        });
    }
    for (std::thread& t : threads) {
        t.join();
    }
    for (const bool good : ok) {
        CHECK(good);
    }
}

TEST_CASE("two-phase reports a valid but unreachable cube as unsolvable", "[two-phase]") {
    std::array<std::array<Color, 9>, 6> facelets{};
    const Cube solved = Cube::solved();
    for (const Face face : kAllFaces) {
        facelets[static_cast<std::size_t>(face)] = solved.face(face);
    }
    std::swap(facelets[static_cast<std::size_t>(Face::U)][7],
              facelets[static_cast<std::size_t>(Face::F)][1]);  // one edge flipped
    const auto flipped = Cube::from_facelets(facelets);
    REQUIRE(flipped.has_value());

    const auto solution = solve_two_phase(*flipped);
    REQUIRE_FALSE(solution.has_value());
    CHECK(solution.error() == CubeError::unsolvable_state);
}

TEST_CASE("solve() dispatches on the method and defaults to the beginner method",
          "[two-phase][slow]") {
    const Cube cube = random_scramble(31, 100);

    const auto beginner = solve(cube);
    const auto explicit_beginner = solve(cube, SolveMethod::Beginner);
    REQUIRE(beginner.has_value());
    REQUIRE(explicit_beginner.has_value());
    CHECK(flatten(*beginner) == flatten(*explicit_beginner));
    CHECK(beginner->steps.size() == 5);

    const auto advanced = solve(cube, SolveMethod::Advanced);
    REQUIRE(advanced.has_value());
    CHECK(apply_all(cube, flatten(*advanced)).is_solved());
    CHECK(flatten(*advanced).size() < flatten(*beginner).size() / 3);

    // prepare() is a no-op for the methods with nothing to build, and safe to
    // repeat for the one that has.
    prepare(SolveMethod::Beginner);
    prepare(SolveMethod::Intermediate);
    prepare(SolveMethod::Advanced);
    prepare(SolveMethod::Advanced);
}
