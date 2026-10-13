#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <thread>
#include <vector>

#include <cube/solver/cfop.hpp>
#include <cube/solver/cross.hpp>
#include <cube/solver/middle_edges.hpp>
#include <cube/solver/notation.hpp>
#include <cube/solver/oll.hpp>
#include <cube/solver/solver.hpp>

#include "cfop_case_index.hpp"
#include "cfop_last_layer.hpp"
#include "cfop_stages.hpp"
#include "cubie.hpp"

using cube::CubeError;
using cube::solver::Color;
using cube::solver::cross_solved;
using cube::solver::Cube;
using cube::solver::f2l_solved;
using cube::solver::Face;
using cube::solver::kAllFaces;
using cube::solver::Move;
using cube::solver::oll_solved;
using cube::solver::parse_algorithm;
using cube::solver::random_scramble;
using cube::solver::Solution;
using cube::solver::solve;
using cube::solver::solve_cfop;
using cube::solver::SolveMethod;
using cube::solver::Stage;
using cube::solver::detail::CubieCube;
using cube::solver::detail::multiply;
using cube::solver::detail::oll_algorithm;
using cube::solver::detail::pll_algorithm;
using cube::solver::detail::solve_cfop_stages;

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

CubieCube apply_all(CubieCube cubie, const std::vector<Move>& moves) {
    for (const Move& move : moves) {
        cubie = multiply(cubie,
                         cube::solver::detail::move_cubie(cube::solver::detail::move_index(move)));
    }
    return cubie;
}

}  // namespace

// ---------------------------------------------------------------------------
// The last layer tables - exhaustively
// ---------------------------------------------------------------------------

TEST_CASE("the last-layer tables solve every one of the 62,208 last-layer states", "[cfop]") {
    // Every arrangement of the four top corners and edges with the first two
    // layers solved: 4! corner orders x 3^3 twists (the fourth is forced) x
    // 4! edge orders x 2^3 flips (the fourth is forced), half of which have
    // mismatched parities and cannot occur on a real cube. Each is run
    // through the two lookups exactly as solve_cfop() does, on the cubie
    // model, and must come out as the identity.
    std::array<std::array<std::uint8_t, 4>, 24> perms{};
    {
        std::array<std::uint8_t, 4> p = {0, 1, 2, 3};
        std::size_t n = 0;
        do {
            perms[n++] = p;
        } while (std::ranges::next_permutation(p).found);
    }

    std::size_t states = 0;
    std::size_t oll_moves = 0;
    std::size_t pll_moves = 0;
    std::size_t oll_longest = 0;
    std::size_t pll_longest = 0;
    bool only_urf = true;

    for (const auto& corners : perms) {
        for (const auto& edges : perms) {
            if (cube::solver::detail::parity_of_four(corners.data()) !=
                cube::solver::detail::parity_of_four(edges.data())) {
                continue;
            }
            for (int ct = 0; ct < 27; ++ct) {
                for (int et = 0; et < 8; ++et) {
                    CubieCube state;
                    int twist_sum = 0;
                    int flip_sum = 0;
                    int c = ct;
                    int e = et;
                    for (std::size_t i = 0; i < 3; ++i) {
                        state.co[i] = static_cast<std::uint8_t>(c % 3);
                        twist_sum += c % 3;
                        c /= 3;
                        state.eo[i] = static_cast<std::uint8_t>(e % 2);
                        flip_sum += e % 2;
                        e /= 2;
                    }
                    state.co[3] = static_cast<std::uint8_t>((3 - twist_sum % 3) % 3);
                    state.eo[3] = static_cast<std::uint8_t>(flip_sum % 2);
                    for (std::size_t i = 0; i < 4; ++i) {
                        state.cp[i] = corners[i];
                        state.ep[i] = edges[i];
                    }
                    ++states;

                    const std::vector<Move>* oll = oll_algorithm(state);
                    REQUIRE(oll != nullptr);
                    state = apply_all(state, *oll);

                    const std::vector<Move>* pll = pll_algorithm(state);
                    REQUIRE(pll != nullptr);
                    state = apply_all(state, *pll);

                    REQUIRE(cube::solver::detail::is_identity(state));

                    oll_moves += oll->size();
                    pll_moves += pll->size();
                    oll_longest = std::max(oll_longest, oll->size());
                    pll_longest = std::max(pll_longest, pll->size());
                    for (const std::vector<Move>* word : {oll, pll}) {
                        for (const Move& m : *word) {
                            only_urf = only_urf && (m.face == Face::U || m.face == Face::R ||
                                                    m.face == Face::F);
                        }
                    }
                }
            }
        }
    }

    CHECK(states == 62'208);

    // Every sequence is one a learner can practise with a right hand and the
    // odd F: <U, R, F> only.
    CHECK(only_urf);

    // The tables hold the shortest sequences over that alphabet: about ten
    // moves to orient and thirteen to permute, against the beginner
    // method's twenty-one and twenty-nine for the same two jobs. The means
    // are over uniformly distributed cases, so they are what a random cube
    // costs on average.
    const auto n = static_cast<double>(states);
    CHECK(static_cast<double>(oll_moves) / n < 10.5);
    CHECK(static_cast<double>(pll_moves) / n < 14.0);
    CHECK(oll_longest <= 13);
    CHECK(pll_longest <= 16);
}

TEST_CASE("the permutation lookup refuses a last layer that is not oriented or not legal",
          "[cfop]") {
    // pll_algorithm() answers only for an oriented last layer whose corner
    // and edge parities agree - what a valid cube always has once OLL is
    // done. Anything else must be refused, not answered from a neighbouring
    // entry, because applying the wrong sequence would scramble the cube.
    CubieCube twisted;
    twisted.co[0] = 1;
    CHECK(pll_algorithm(twisted) == nullptr);

    CubieCube flipped;
    flipped.eo[1] = 1;
    CHECK(pll_algorithm(flipped) == nullptr);

    // One swapped pair of corners with the edges untouched: parities differ.
    CubieCube mismatched;
    std::swap(mismatched.cp[0], mismatched.cp[1]);
    CHECK(pll_algorithm(mismatched) == nullptr);
}

// ---------------------------------------------------------------------------
// The whole solve
// ---------------------------------------------------------------------------

TEST_CASE("cfop solves an already-solved cube with no moves", "[cfop]") {
    const auto solution = solve_cfop(Cube::solved());
    REQUIRE(solution.has_value());
    CHECK(flatten(*solution).empty());
}

TEST_CASE("cfop takes the four stages in order and each one finishes its job", "[cfop][slow]") {
    // Uses the unmerged stages: merge_adjacent_moves may carry a stage's last
    // move into the stage before it, which is right for the finished
    // solution but blurs the boundary this test is about.
    for (unsigned seed = 1; seed <= 40; ++seed) {
        const Cube scrambled = random_scramble(seed, 100);
        const auto stages = solve_cfop_stages(scrambled);
        REQUIRE(stages.has_value());
        REQUIRE(stages->steps.size() == 4);
        CHECK(stages->steps[0].stage == Stage::Cross);
        CHECK(stages->steps[1].stage == Stage::F2l);
        CHECK(stages->steps[2].stage == Stage::Oll);
        CHECK(stages->steps[3].stage == Stage::Pll);

        Cube cube = scrambled;
        cube = apply_all(cube, stages->steps[0].moves);
        CHECK(cross_solved(cube));

        cube = apply_all(cube, stages->steps[1].moves);
        CHECK(f2l_solved(cube));  // cross, corners and middle edges

        cube = apply_all(cube, stages->steps[2].moves);
        CHECK(oll_solved(cube));  // first two layers, and the whole top one colour

        cube = apply_all(cube, stages->steps[3].moves);
        CHECK(cube.is_solved());
    }
}

TEST_CASE("cfop's cross is as short as any cross can be", "[cfop][slow]") {
    // The beginner method's breadth-first search finds the fewest moves that
    // build the cross; cfop's meet-in-the-middle search must find the same
    // number, never a longer one.
    for (unsigned seed = 1; seed <= 15; ++seed) {
        const Cube scrambled = random_scramble(seed, 100);
        const auto cfop = solve_cfop_stages(scrambled);
        const auto optimum = cube::solver::solve_cross(scrambled);
        REQUIRE(cfop.has_value());
        REQUIRE(optimum.has_value());
        CHECK(cfop->steps[0].moves.size() == optimum->size());
    }
}

TEST_CASE("cfop solves a corpus in about fifty-five moves, far fewer than the beginner method",
          "[cfop][slow]") {
    constexpr int kCount = 60;
    double total = 0;
    std::size_t longest = 0;
    for (unsigned seed = 1; seed <= kCount; ++seed) {
        const Cube cube = random_scramble(seed, 100);
        const auto solution = solve_cfop(cube);
        REQUIRE(solution.has_value());

        const std::vector<Move> moves = flatten(*solution);
        CHECK(apply_all(cube, moves).is_solved());
        CHECK_FALSE(has_adjacent_same_face(moves));
        total += static_cast<double>(moves.size());
        longest = std::max(longest, moves.size());
    }
    // Measured: 52 on average, 62 at worst. The beginner method averages
    // about 100.
    CHECK(total / kCount < 56.0);
    CHECK(longest <= 68);
}

TEST_CASE("cfop copes with cubes that are barely scrambled", "[cfop][slow]") {
    // A short scramble leaves pairs already built, pieces already home and
    // whole stages empty - paths a full scramble never takes.
    for (unsigned seed = 1; seed <= 60; ++seed) {
        const Cube cube = random_scramble(1000 + seed, 1 + static_cast<int>(seed % 10));
        const auto solution = solve_cfop(cube);
        REQUIRE(solution.has_value());
        CHECK(apply_all(cube, flatten(*solution)).is_solved());
    }
}

TEST_CASE("cfop solves the standard last-layer cases", "[cfop]") {
    const std::array<const char*, 4> kCases = {
        "R U R' U R U2 R'",                      // Sune
        "F R U R' U' F'",                        // edge orientation
        "R U R' U' R' F R2 U' R' U' R U R' F'",  // T-perm
        "R' F R' B2 R F' R' B2 R2",              // A-perm
    };
    for (const char* algorithm : kCases) {
        const auto moves = parse_algorithm(algorithm);
        REQUIRE(moves.has_value());
        const Cube cube = apply_all(Cube::solved(), *moves);
        const auto solution = solve_cfop(cube);
        REQUIRE(solution.has_value());
        CHECK(apply_all(cube, flatten(*solution)).is_solved());
    }
}

TEST_CASE("cfop reports a valid but unreachable cube as unsolvable", "[cfop]") {
    std::array<std::array<Color, 9>, 6> facelets{};
    const Cube solved = Cube::solved();
    for (const Face face : kAllFaces) {
        facelets[static_cast<std::size_t>(face)] = solved.face(face);
    }
    std::swap(facelets[static_cast<std::size_t>(Face::U)][7],
              facelets[static_cast<std::size_t>(Face::F)][1]);  // one edge flipped
    const auto flipped = Cube::from_facelets(facelets);
    REQUIRE(flipped.has_value());

    const auto solution = solve_cfop(*flipped);
    REQUIRE_FALSE(solution.has_value());
    CHECK(solution.error() == CubeError::unsolvable_state);
}

TEST_CASE("cfop gives the same solution every time and from several threads", "[cfop][slow]") {
    const Cube cube = random_scramble(77, 100);
    const auto reference = solve_cfop(cube);
    REQUIRE(reference.has_value());

    std::array<bool, 4> same{};
    std::vector<std::thread> threads;
    for (std::size_t i = 0; i < same.size(); ++i) {
        threads.emplace_back([&same, &cube, &reference, i] {
            const auto again = solve_cfop(cube);
            same[i] = again.has_value() && flatten(*again) == flatten(*reference);
        });
    }
    for (std::thread& t : threads) {
        t.join();
    }
    for (const bool ok : same) {
        CHECK(ok);
    }
}

TEST_CASE("solve() runs the intermediate method through the same entry point", "[cfop][slow]") {
    const Cube cube = random_scramble(12, 100);
    const auto direct = solve_cfop(cube);
    const auto via_solve = solve(cube, SolveMethod::Intermediate);
    REQUIRE(direct.has_value());
    REQUIRE(via_solve.has_value());
    CHECK(flatten(*direct) == flatten(*via_solve));
    CHECK(via_solve->steps.size() == 4);
}
