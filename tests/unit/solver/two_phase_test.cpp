#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <random>
#include <thread>
#include <vector>

#include <cube/solver/notation.hpp>
#include <cube/solver/solver.hpp>
#include <cube/solver/two_phase.hpp>

#include "cubie.hpp"
#include "two_phase_tables.hpp"

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

// The tables the solver reads, built the obvious way: for every coordinate
// value, make a cube that carries it, turn that cube with the move's cubie,
// and read the coordinate back; then breadth-first search for the pruning
// distances. This is what the solver itself did before its tables were
// produced by tuned generators (and partly by the compiler); it is kept here,
// slow and plain, as the thing the real tables must equal.
namespace reference {

using cube::solver::detail::CubieCube;
using cube::solver::detail::kMoveCount;
using cube::solver::detail::kPhase2Moves;
using cube::solver::detail::move_cubie;
using cube::solver::detail::multiply;

constexpr std::size_t kTwistCount = 2187;
constexpr std::size_t kFlipCount = 2048;
constexpr std::size_t kSliceCount = 495;
constexpr std::size_t kPermCount = 40320;
constexpr std::size_t kSlicePermCount = 24;
constexpr std::size_t kPhase2MoveCount =
    static_cast<std::size_t>(cube::solver::detail::kPhase2MoveCount);
constexpr std::array<int, 9> kFactorial = {1, 1, 2, 6, 24, 120, 720, 5040, 40320};

int twist_value(const CubieCube& c) {
    int value = 0;
    for (std::size_t i = 0; i < 7; ++i) {
        value = value * 3 + c.co[i];
    }
    return value;
}

void set_twist(CubieCube& c, int value) {
    int sum = 0;
    for (int i = 6; i >= 0; --i) {
        c.co[static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(value % 3);
        sum += value % 3;
        value /= 3;
    }
    c.co[7] = static_cast<std::uint8_t>((3 - sum % 3) % 3);
}

int flip_value(const CubieCube& c) {
    int value = 0;
    for (std::size_t i = 0; i < 11; ++i) {
        value = value * 2 + c.eo[i];
    }
    return value;
}

void set_flip(CubieCube& c, int value) {
    int sum = 0;
    for (int i = 10; i >= 0; --i) {
        c.eo[static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(value % 2);
        sum += value % 2;
        value /= 2;
    }
    c.eo[11] = static_cast<std::uint8_t>(sum % 2);
}

struct SliceNumbering {
    std::array<std::uint16_t, 4096> index_of_mask{};
    std::array<std::uint16_t, kSliceCount> mask_of_index{};
};

SliceNumbering slice_numbering() {
    SliceNumbering n;
    int next = 0;
    for (int mask = 4095; mask >= 0; --mask) {
        if (std::popcount(static_cast<unsigned>(mask)) == 4) {
            n.index_of_mask[static_cast<std::size_t>(mask)] = static_cast<std::uint16_t>(next);
            n.mask_of_index[static_cast<std::size_t>(next)] = static_cast<std::uint16_t>(mask);
            ++next;
        }
    }
    return n;
}

int slice_value(const CubieCube& c, const SliceNumbering& n) {
    unsigned mask = 0;
    for (std::size_t pos = 0; pos < 12; ++pos) {
        if (c.ep[pos] >= 8) {
            mask |= 1u << pos;
        }
    }
    return n.index_of_mask[mask];
}

void set_slice(CubieCube& c, int value, const SliceNumbering& n) {
    const unsigned mask = n.mask_of_index[static_cast<std::size_t>(value)];
    std::uint8_t next_slice = 8;
    std::uint8_t next_other = 0;
    for (std::size_t pos = 0; pos < 12; ++pos) {
        c.ep[pos] = ((mask >> pos) & 1u) != 0 ? next_slice++ : next_other++;
    }
}

int permutation_rank(const std::uint8_t* p, std::size_t n) {
    int rank = 0;
    for (std::size_t i = 0; i < n; ++i) {
        int smaller = 0;
        for (std::size_t j = i + 1; j < n; ++j) {
            smaller += p[j] < p[i] ? 1 : 0;
        }
        rank = rank * static_cast<int>(n - i) + smaller;
    }
    return rank;
}

void unrank_permutation(int rank, std::uint8_t* p, std::size_t n) {
    std::array<std::uint8_t, 8> unused{};
    for (std::size_t i = 0; i < n; ++i) {
        unused[i] = static_cast<std::uint8_t>(i);
    }
    std::size_t remaining = n;
    for (std::size_t i = 0; i < n; ++i) {
        const int place = kFactorial[n - 1 - i];
        const auto pick = static_cast<std::size_t>(rank / place);
        rank %= place;
        p[i] = unused[pick];
        for (std::size_t j = pick; j + 1 < remaining; ++j) {
            unused[j] = unused[j + 1];
        }
        --remaining;
    }
}

template <class Set, class Get>
std::vector<std::uint16_t> move_table(std::size_t count, std::size_t moves,
                                      const std::vector<std::size_t>& indices, Set set, Get get) {
    std::vector<std::uint16_t> table(count * moves);
    for (std::size_t m = 0; m < moves; ++m) {
        const CubieCube& move = move_cubie(indices[m]);
        for (std::size_t v = 0; v < count; ++v) {
            CubieCube c;
            set(c, static_cast<int>(v));
            table[v * moves + m] = static_cast<std::uint16_t>(get(multiply(c, move)));
        }
    }
    return table;
}

std::vector<std::size_t> all_moves() {
    std::vector<std::size_t> indices(kMoveCount);
    for (std::size_t i = 0; i < kMoveCount; ++i) {
        indices[i] = i;
    }
    return indices;
}

std::vector<std::size_t> phase2_moves() {
    return std::vector<std::size_t>(kPhase2Moves.begin(), kPhase2Moves.end());
}

std::vector<std::uint8_t> build_prune(std::size_t first_count, std::size_t second_count,
                                      std::size_t move_count,
                                      const std::vector<std::uint16_t>& first_move,
                                      const std::vector<std::uint16_t>& second_move) {
    constexpr std::uint8_t kUnset = 0xFF;
    std::vector<std::uint8_t> table(first_count * second_count, kUnset);
    table[0] = 0;
    for (std::uint8_t depth = 0; depth < kUnset - 1; ++depth) {
        std::size_t added = 0;
        for (std::size_t index = 0; index < table.size(); ++index) {
            if (table[index] != depth) {
                continue;
            }
            const std::size_t first = index / second_count;
            const std::size_t second = index % second_count;
            for (std::size_t m = 0; m < move_count; ++m) {
                const std::size_t next = first_move[first * move_count + m] * second_count +
                                         second_move[second * move_count + m];
                if (table[next] == kUnset) {
                    table[next] = static_cast<std::uint8_t>(depth + 1);
                    ++added;
                }
            }
        }
        if (added == 0) {
            break;
        }
    }
    return table;
}

}  // namespace reference

template <class A, class B>
bool same_contents(const A& a, const B& b) {
    return std::ranges::equal(a, b);
}

}  // namespace

TEST_CASE("permutation_of_rank is rank_permutation's inverse, in rank order", "[two-phase]") {
    // The first-use tables take each permutation in turn by its rank (they do
    // not step one with std::next_permutation, which GCC 15 at -O3 misreads
    // into a stringop-overflow). Every one of the 4! and 8! must be a real
    // permutation, numbered by rank_permutation() as the rank it was asked
    // for, and later ranks must come later in lexicographic order.
    using cube::solver::detail::permutation_of_rank;
    using cube::solver::detail::rank_permutation;

    for (std::size_t rank = 0; rank < 24; ++rank) {
        const auto p = permutation_of_rank<4>(rank);
        unsigned seen = 0;
        for (const std::uint8_t v : p) {
            seen |= 1u << v;
        }
        REQUIRE(seen == 0xFu);
        CHECK(rank_permutation(p.data(), 4) == static_cast<int>(rank));
    }

    std::array<std::uint8_t, 8> previous{};
    for (std::size_t rank = 0; rank < 40320; ++rank) {
        const auto p = permutation_of_rank<8>(rank);
        unsigned seen = 0;
        for (const std::uint8_t v : p) {
            seen |= 1u << v;
        }
        REQUIRE(seen == 0xFFu);
        REQUIRE(rank_permutation(p.data(), 8) == static_cast<int>(rank));
        if (rank > 0) {
            REQUIRE(
                std::lexicographical_compare(previous.begin(), previous.end(), p.begin(), p.end()));
        }
        previous = p;
    }
    // The first is the identity and the last is the reverse.
    CHECK(permutation_of_rank<8>(0) == std::array<std::uint8_t, 8>{0, 1, 2, 3, 4, 5, 6, 7});
    CHECK(permutation_of_rank<8>(40319) == std::array<std::uint8_t, 8>{7, 6, 5, 4, 3, 2, 1, 0});
}

TEST_CASE("the compile-time tables equal a direct construction, entry for entry", "[two-phase]") {
    using namespace reference;
    const cube::solver::detail::Phase1MoveTables& built =
        cube::solver::detail::phase1_move_tables();

    // The slice numbering both sides index with.
    const SliceNumbering numbering = slice_numbering();
    const auto& compiled_numbering = cube::solver::detail::slice_numbering();
    CHECK(same_contents(numbering.index_of_mask, compiled_numbering.index_of_mask));
    CHECK(same_contents(numbering.mask_of_index, compiled_numbering.mask_of_index));

    CHECK(same_contents(built.twist, move_table(
                                         kTwistCount, kMoveCount, all_moves(),
                                         [](CubieCube& c, int v) { set_twist(c, v); },
                                         [](const CubieCube& c) { return twist_value(c); })));
    CHECK(same_contents(built.flip, move_table(
                                        kFlipCount, kMoveCount, all_moves(),
                                        [](CubieCube& c, int v) { set_flip(c, v); },
                                        [](const CubieCube& c) { return flip_value(c); })));
    CHECK(same_contents(
        built.slice, move_table(
                         kSliceCount, kMoveCount, all_moves(),
                         [&numbering](CubieCube& c, int v) { set_slice(c, v, numbering); },
                         [&numbering](const CubieCube& c) { return slice_value(c, numbering); })));
}

TEST_CASE("the first-use tables equal a direct construction, entry for entry",
          "[two-phase][slow]") {
    using namespace reference;
    const cube::solver::detail::Phase2Tables& built = cube::solver::detail::phase2_tables();

    // Phase 2 move tables: permutations of the corners, of the U/D-layer
    // edges, and of the four middle-layer edges.
    const auto cperm = move_table(
        kPermCount, kPhase2MoveCount, phase2_moves(),
        [](CubieCube& c, int v) { unrank_permutation(v, c.cp.data(), 8); },
        [](const CubieCube& c) { return permutation_rank(c.cp.data(), 8); });
    const auto uedge = move_table(
        kPermCount, kPhase2MoveCount, phase2_moves(),
        [](CubieCube& c, int v) { unrank_permutation(v, c.ep.data(), 8); },
        [](const CubieCube& c) { return permutation_rank(c.ep.data(), 8); });
    const auto sperm = move_table(
        kSlicePermCount, kPhase2MoveCount, phase2_moves(),
        [](CubieCube& c, int v) {
            std::array<std::uint8_t, 4> p{};
            unrank_permutation(v, p.data(), 4);
            for (std::size_t k = 0; k < 4; ++k) {
                c.ep[8 + k] = static_cast<std::uint8_t>(8 + p[k]);
            }
        },
        [](const CubieCube& c) {
            std::array<std::uint8_t, 4> p{};
            for (std::size_t k = 0; k < 4; ++k) {
                p[k] = static_cast<std::uint8_t>(c.ep[8 + k] - 8);
            }
            return permutation_rank(p.data(), 4);
        });
    CHECK(same_contents(built.cperm_move, cperm));
    CHECK(same_contents(built.uedge_move, uedge));
    CHECK(same_contents(built.sperm_move, sperm));

    // The pruning tables, built from the reference's own move tables rather
    // than the tables under test, so an error in a move table cannot hide
    // behind a pruning table that was built from it.
    const SliceNumbering numbering = slice_numbering();
    const auto twist = move_table(
        kTwistCount, kMoveCount, all_moves(), [](CubieCube& c, int v) { set_twist(c, v); },
        [](const CubieCube& c) { return twist_value(c); });
    const auto flip = move_table(
        kFlipCount, kMoveCount, all_moves(), [](CubieCube& c, int v) { set_flip(c, v); },
        [](const CubieCube& c) { return flip_value(c); });
    const auto slice = move_table(
        kSliceCount, kMoveCount, all_moves(),
        [&numbering](CubieCube& c, int v) { set_slice(c, v, numbering); },
        [&numbering](const CubieCube& c) { return slice_value(c, numbering); });
    CHECK(same_contents(built.twist_slice_prune,
                        build_prune(kTwistCount, kSliceCount, kMoveCount, twist, slice)));
    CHECK(same_contents(built.flip_slice_prune,
                        build_prune(kFlipCount, kSliceCount, kMoveCount, flip, slice)));
    CHECK(same_contents(built.cperm_sperm_prune,
                        build_prune(kPermCount, kSlicePermCount, kPhase2MoveCount, cperm, sperm)));
    CHECK(same_contents(built.uedge_sperm_prune,
                        build_prune(kPermCount, kSlicePermCount, kPhase2MoveCount, uedge, sperm)));
}

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
    // First use builds about 5.6 MB of tables; a person's solve and a
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
