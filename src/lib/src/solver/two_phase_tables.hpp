#pragma once

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <thread>
#include <vector>

#include "cubie.hpp"

// The coordinates and lookup tables of the two-phase solver (two_phase.cpp).
//
// Two-phase works in "coordinates": small integers that stand for one aspect
// of the cube, so a move is a table lookup instead of a shuffle.
//
//   Phase 1 works in G0 -> G1, G1 = <U, D, R2, L2, F2, B2>:
//     twist  (3^7  = 2187)   how the corners are turned
//     flip   (2^11 = 2048)   how the edges are flipped
//     slice  (C(12,4) = 495) which four positions hold the middle-layer edges
//     G1 is exactly twist = flip = slice = 0.
//   Phase 2 works inside G1 with its 10 moves:
//     cperm  (8!  = 40320)   the corners' permutation
//     uedge  (8!  = 40320)   the U/D-layer edges' permutation
//     sperm  (4!  = 24)      the middle-layer edges' permutation
//
// Where each table is produced, and why:
//
//   * The phase 1 move tables and the slice numbering (180 KB) are evaluated
//     by the compiler, in two_phase_tables.cpp alone, and sit in the binary's
//     read-only data: no first-use cost, no guard, nothing to warm.
//   * The phase 2 move tables (1.6 MB) and the four pruning tables (4 MB) are
//     built on first use, once, thread-safely (phase2_tables()). They are
//     past what a build can spare for a compile-time evaluator: the phase 2
//     move tables alone take 22 to 41 s on GCC and Clang and are not finished
//     by MSVC after five minutes and 3.8 GB, and each pruning table takes two
//     to three minutes and 3 to 5 GB. The measurements are in
//     docs/solver-methods.md; the same generators build them at run time.

namespace cube::solver::detail {

inline constexpr int kTwistCount = 2187;
inline constexpr int kFlipCount = 2048;
inline constexpr int kSliceCount = 495;
inline constexpr int kPermCount = 40320;
inline constexpr int kSlicePermCount = 24;

// Phase 1 may use any face turn; phase 2 only these ten, as indices into the
// 18-move scheme (face * 3 + turns - 1, faces in the order U D L R F B):
// U, U2, U', D, D2, D', L2, R2, F2, B2.
inline constexpr int kPhase2MoveCount = 10;
inline constexpr std::array<std::uint8_t, kPhase2MoveCount> kPhase2Moves = {0, 1, 2,  3,  4,
                                                                            5, 7, 10, 13, 16};

// ---------------------------------------------------------------------------
// Coordinates
// ---------------------------------------------------------------------------

[[nodiscard]] constexpr int twist_of(const CubieCube& c) noexcept {
    int value = 0;
    for (std::size_t i = 0; i < 7; ++i) {
        value = value * 3 + c.co[i];
    }
    return value;
}

[[nodiscard]] constexpr int flip_of(const CubieCube& c) noexcept {
    int value = 0;
    for (std::size_t i = 0; i < 11; ++i) {
        value = value * 2 + c.eo[i];
    }
    return value;
}

// The middle-layer edges' positions as a number: the 12-bit mask of which
// positions hold a slice edge (piece 8..11), numbered so the solved mask -
// positions 8..11 - is 0, in descending mask order.
struct SliceNumbering {
    std::array<std::uint16_t, 4096> index_of_mask{};
    std::array<std::uint16_t, kSliceCount> mask_of_index{};
};

[[nodiscard]] constexpr SliceNumbering make_slice_numbering() noexcept {
    SliceNumbering n;
    int next = 0;
    for (int mask = 4095; mask >= 0; --mask) {  // 0xF00 is the largest 4-bit mask: index 0
        if (std::popcount(static_cast<unsigned>(mask)) == 4) {
            n.index_of_mask[static_cast<std::size_t>(mask)] = static_cast<std::uint16_t>(next);
            n.mask_of_index[static_cast<std::size_t>(next)] = static_cast<std::uint16_t>(mask);
            ++next;
        }
    }
    return n;
}

// Evaluated by the compiler in two_phase_tables.cpp.
[[nodiscard]] const SliceNumbering& slice_numbering() noexcept;

[[nodiscard]] inline int slice_of(const CubieCube& c) noexcept {
    unsigned mask = 0;
    for (std::size_t pos = 0; pos < 12; ++pos) {
        if (c.ep[pos] >= 8) {
            mask |= 1u << pos;
        }
    }
    return slice_numbering().index_of_mask[mask];
}

// Rank of a permutation of 0..n-1 among all n! of them in lexicographic
// order, identity = 0 (n <= 8). Each digit of the factorial-base number is the
// count of values still to come that are smaller than this one, which is the
// number of unplaced values below it.
[[nodiscard]] constexpr int rank_permutation(const std::uint8_t* p, std::size_t n) noexcept {
    unsigned unplaced = (1u << n) - 1u;
    int rank = 0;
    for (std::size_t i = 0; i < n; ++i) {
        const unsigned bit = 1u << p[i];
        rank = rank * static_cast<int>(n - i) + std::popcount(unplaced & (bit - 1u));
        unplaced &= ~bit;
    }
    return rank;
}

[[nodiscard]] constexpr int cperm_of(const CubieCube& c) noexcept {
    return rank_permutation(c.cp.data(), 8);
}

[[nodiscard]] constexpr int uedge_of(const CubieCube& c) noexcept {
    return rank_permutation(c.ep.data(), 8);  // phase 2 only: pieces 0..7 sit in positions 0..7
}

[[nodiscard]] constexpr int sperm_of(const CubieCube& c) noexcept {
    std::array<std::uint8_t, 4> p{};
    for (std::size_t i = 0; i < 4; ++i) {
        p[i] = static_cast<std::uint8_t>(c.ep[8 + i] - 8);
    }
    return rank_permutation(p.data(), 4);
}

// ---------------------------------------------------------------------------
// Phase 1 move tables - evaluated at compile time
// ---------------------------------------------------------------------------
//
// [coordinate * 18 + move]: where each coordinate goes under each of the 18
// moves. A coordinate is well defined exactly because a move's effect on it
// does not depend on anything else, so each entry is "take a cube that
// carries just this coordinate, turn it, read the coordinate back".
//
// These loops are written for the compiler's evaluator, not for speed at run
// time: that evaluator pays for every array access and call (MSVC's roughly
// 300 bytes and 4 microseconds each), so each entry is plain arithmetic on
// the coordinate and a few reads of the move's own arrays, and only the six
// quarter turns are worked out that way - a half turn and a counter-clockwise
// turn are the quarter turn applied twice and three times, which is two
// lookups in the table itself. The result is the same table a direct
// per-entry multiply() gives; tests/unit/solver/two_phase_test.cpp checks
// every entry against exactly that.

constexpr void fill_twist_moves(std::uint16_t* out) noexcept {
    // The eighth corner's twist is whatever makes the total 0 mod 3.
    std::array<std::uint8_t, static_cast<std::size_t>(kTwistCount) * 8> digit{};
    for (int v = 0; v < kTwistCount; ++v) {
        unsigned rest = static_cast<unsigned>(v);
        unsigned sum = 0;
        std::uint8_t* const d = digit.data() + static_cast<std::size_t>(v) * 8;
        for (int i = 6; i >= 0; --i) {
            d[i] = static_cast<std::uint8_t>(rest % 3);
            sum += rest % 3;
            rest /= 3;
        }
        d[7] = static_cast<std::uint8_t>((3 - sum % 3) % 3);
    }
    for (std::size_t face = 0; face < kQuarterTurns.size(); ++face) {
        const std::uint8_t* const from = kQuarterTurns[face].cp.data();
        const std::uint8_t* const plus = kQuarterTurns[face].co.data();
        for (int v = 0; v < kTwistCount; ++v) {
            // multiply(): the corner at position i afterwards is the one that
            // was at from[i] before, twisted by plus[i] more.
            const std::uint8_t* const d = digit.data() + static_cast<std::size_t>(v) * 8;
            out[static_cast<std::size_t>(v) * kMoveCount + face * 3] = static_cast<std::uint16_t>(
                ((d[from[0]] + plus[0]) % 3) * 729 + ((d[from[1]] + plus[1]) % 3) * 243 +
                ((d[from[2]] + plus[2]) % 3) * 81 + ((d[from[3]] + plus[3]) % 3) * 27 +
                ((d[from[4]] + plus[4]) % 3) * 9 + ((d[from[5]] + plus[5]) % 3) * 3 +
                ((d[from[6]] + plus[6]) % 3));
        }
    }
}

constexpr void fill_flip_moves(std::uint16_t* out) noexcept {
    // The twelfth edge's flip is whatever makes the total 0 mod 2.
    std::array<std::uint8_t, static_cast<std::size_t>(kFlipCount) * 12> bit{};
    for (int v = 0; v < kFlipCount; ++v) {
        unsigned rest = static_cast<unsigned>(v);
        unsigned sum = 0;
        std::uint8_t* const b = bit.data() + static_cast<std::size_t>(v) * 12;
        for (int i = 10; i >= 0; --i) {
            b[i] = static_cast<std::uint8_t>(rest & 1u);
            sum += rest & 1u;
            rest >>= 1;
        }
        b[11] = static_cast<std::uint8_t>(sum & 1u);
    }
    for (std::size_t face = 0; face < kQuarterTurns.size(); ++face) {
        const std::uint8_t* const from = kQuarterTurns[face].ep.data();
        const std::uint8_t* const plus = kQuarterTurns[face].eo.data();
        for (int v = 0; v < kFlipCount; ++v) {
            const std::uint8_t* const b = bit.data() + static_cast<std::size_t>(v) * 12;
            out[static_cast<std::size_t>(v) * kMoveCount + face * 3] = static_cast<std::uint16_t>(
                ((b[from[0]] ^ plus[0]) << 10) | ((b[from[1]] ^ plus[1]) << 9) |
                ((b[from[2]] ^ plus[2]) << 8) | ((b[from[3]] ^ plus[3]) << 7) |
                ((b[from[4]] ^ plus[4]) << 6) | ((b[from[5]] ^ plus[5]) << 5) |
                ((b[from[6]] ^ plus[6]) << 4) | ((b[from[7]] ^ plus[7]) << 3) |
                ((b[from[8]] ^ plus[8]) << 2) | ((b[from[9]] ^ plus[9]) << 1) |
                (b[from[10]] ^ plus[10]));
        }
    }
}

constexpr void fill_slice_moves(std::uint16_t* out, const SliceNumbering& n) noexcept {
    for (std::size_t face = 0; face < kQuarterTurns.size(); ++face) {
        const std::uint8_t* const from = kQuarterTurns[face].ep.data();
        for (int v = 0; v < kSliceCount; ++v) {
            // A position holds a slice edge afterwards if the position it
            // takes its piece from did.
            const unsigned mask = n.mask_of_index[static_cast<std::size_t>(v)];
            const unsigned moved =
                ((mask >> from[0]) & 1u) | (((mask >> from[1]) & 1u) << 1) |
                (((mask >> from[2]) & 1u) << 2) | (((mask >> from[3]) & 1u) << 3) |
                (((mask >> from[4]) & 1u) << 4) | (((mask >> from[5]) & 1u) << 5) |
                (((mask >> from[6]) & 1u) << 6) | (((mask >> from[7]) & 1u) << 7) |
                (((mask >> from[8]) & 1u) << 8) | (((mask >> from[9]) & 1u) << 9) |
                (((mask >> from[10]) & 1u) << 10) | (((mask >> from[11]) & 1u) << 11);
            out[static_cast<std::size_t>(v) * kMoveCount + face * 3] = n.index_of_mask[moved];
        }
    }
}

// Completes the table from its quarter-turn columns: X2 is X then X, X' is X
// then X then X. `count` rows of kMoveCount.
constexpr void fill_derived_turns(std::uint16_t* table, std::size_t count) noexcept {
    for (std::size_t v = 0; v < count; ++v) {
        std::uint16_t* const row = table + v * kMoveCount;
        for (std::size_t face = 0; face < 6; ++face) {
            const std::size_t quarter = face * 3;
            row[quarter + 1] = table[static_cast<std::size_t>(row[quarter]) * kMoveCount + quarter];
            row[quarter + 2] =
                table[static_cast<std::size_t>(row[quarter + 1]) * kMoveCount + quarter];
        }
    }
}

// The phase 1 move tables, as one object so that the compiler evaluates them
// in a single constant expression (two_phase_tables.cpp).
struct Phase1MoveTables {
    std::array<std::uint16_t, static_cast<std::size_t>(kTwistCount) * kMoveCount> twist{};
    std::array<std::uint16_t, static_cast<std::size_t>(kFlipCount) * kMoveCount> flip{};
    std::array<std::uint16_t, static_cast<std::size_t>(kSliceCount) * kMoveCount> slice{};

    constexpr explicit Phase1MoveTables(const SliceNumbering& numbering) noexcept {
        fill_twist_moves(twist.data());
        fill_derived_turns(twist.data(), kTwistCount);
        fill_flip_moves(flip.data());
        fill_derived_turns(flip.data(), kFlipCount);
        fill_slice_moves(slice.data(), numbering);
        fill_derived_turns(slice.data(), kSliceCount);
    }
};

// Evaluated by the compiler in two_phase_tables.cpp.
[[nodiscard]] const Phase1MoveTables& phase1_move_tables() noexcept;

// ---------------------------------------------------------------------------
// Phase 2 move tables and the pruning tables - built on first use
// ---------------------------------------------------------------------------

// The permutation of 0..N-1 with the given rank (lexicographic order,
// identity = 0): the inverse of rank_permutation(), in the same bitmask style.
//
// This is how the loops below get each permutation in turn, rather than
// stepping one with std::next_permutation: at -O3 GCC 15 vectorises the
// library's reverse of a suffix whose length it cannot see, and reports a
// stringop-overflow into these 4- and 8-byte arrays that cannot happen; the
// Release leg builds with -Werror. Nothing here reverses, shifts or copies a
// range - each digit picks one set bit - so there is no loop for it to
// misjudge.
template <std::size_t N>
[[nodiscard]] constexpr std::array<std::uint8_t, N> permutation_of_rank(std::size_t rank) noexcept {
    static_assert(N >= 1 && N <= 8, "the factorials below stop at 8!");
    constexpr std::array<std::size_t, 9> kFactorial = {1, 1, 2, 6, 24, 120, 720, 5040, 40320};
    std::array<std::uint8_t, N> p{};
    unsigned unplaced = (1u << N) - 1u;
    for (std::size_t i = 0; i < N; ++i) {
        const std::size_t place = kFactorial[N - 1 - i];
        // The digit is how many unplaced values come before this one.
        std::size_t before = rank / place;
        rank %= place;
        unsigned bits = unplaced;
        for (; before > 0; --before) {
            bits &= bits - 1u;  // drop the lowest set bit
        }
        const auto value = static_cast<unsigned>(std::countr_zero(bits));
        p[i] = static_cast<std::uint8_t>(value);
        unplaced &= ~(1u << value);
    }
    return p;
}

// The eight-element permutation move table, [rank * 10 + index into
// kPhase2Moves], for the corners (`edges` false) or the U/D-layer edges. The
// ten moves keep the U/D edges among positions 0..7, so both are the same
// computation on a different half of the cubie. Only U, D and the four half
// turns need a rank taken; U2, U', D2 and D' are U and D composed.
inline void fill_perm_moves(std::uint16_t* out, bool edges) noexcept {
    constexpr std::array<std::size_t, 6> kBase = {0, 3, 6, 7, 8, 9};  // U D L2 R2 F2 B2
    std::array<std::uint8_t, 8> q{};
    for (std::size_t rank = 0; rank < static_cast<std::size_t>(kPermCount); ++rank) {
        const std::array<std::uint8_t, 8> p = permutation_of_rank<8>(rank);
        for (const std::size_t i : kBase) {
            const CubieCube& move = move_cubie(kPhase2Moves[i]);
            const std::uint8_t* const from = edges ? move.ep.data() : move.cp.data();
            for (std::size_t pos = 0; pos < 8; ++pos) {
                q[pos] = p[from[pos]];  // multiply(): `pos` takes the piece that was at from[pos]
            }
            out[rank * kPhase2MoveCount + i] =
                static_cast<std::uint16_t>(rank_permutation(q.data(), 8));
        }
    }
    for (std::size_t rank = 0; rank < static_cast<std::size_t>(kPermCount); ++rank) {
        std::uint16_t* const row = out + rank * kPhase2MoveCount;
        for (const std::size_t i : {std::size_t{0}, std::size_t{3}}) {  // U, D
            row[i + 1] = out[std::size_t{row[i]} * kPhase2MoveCount + i];
            row[i + 2] = out[std::size_t{row[i + 1]} * kPhase2MoveCount + i];
        }
    }
}

// The middle-layer edges' permutation (4! = 24) over the ten moves.
inline void fill_sperm_moves(std::uint16_t* out) noexcept {
    std::array<std::uint8_t, 4> q{};
    for (std::size_t rank = 0; rank < static_cast<std::size_t>(kSlicePermCount); ++rank) {
        const std::array<std::uint8_t, 4> p = permutation_of_rank<4>(rank);
        for (std::size_t i = 0; i < static_cast<std::size_t>(kPhase2MoveCount); ++i) {
            const CubieCube& move = move_cubie(kPhase2Moves[i]);
            for (std::size_t k = 0; k < 4; ++k) {
                q[k] = p[move.ep[8 + k] - 8u];
            }
            out[rank * kPhase2MoveCount + i] =
                static_cast<std::uint16_t>(rank_permutation(q.data(), 4));
        }
    }
}

// The exact distance from the goal (both coordinates 0) to every pair of
// coordinates, [first * SecondCount + second], by breadth-first search. The
// move set is closed under inversion, so distance *from* the goal is
// distance *to* it, and every pair is reachable, so the table fills
// completely; the empty-frontier exit is a guard, not the normal ending.
//
// A level can be found two ways: expand every pair at the current depth
// (cost: frontier x moves), or ask every still-unfilled pair whether any
// neighbour is at the current depth (cost: unfilled x a few probes). The
// first wins while the frontier is small and the second once most of the table
// is filled, where the frontier is hundreds of thousands of pairs and the
// unfilled few; this picks the cheaper at each depth. Either way the pairs
// found are exactly those at depth + 1 - a pair's distance is a property of the
// pair - so the table is the same whichever is chosen.
template <std::size_t FirstCount, std::size_t SecondCount, std::size_t MoveCount>
inline void fill_prune(std::uint8_t* table, const std::uint16_t* first_move,
                       const std::uint16_t* second_move) {
    constexpr std::size_t kTotal = FirstCount * SecondCount;
    constexpr std::uint8_t kUnset = 0xFF;
    // Probes an unfilled pair typically needs before it finds a neighbour at
    // the current depth (measured: 3 to 5 on these tables).
    constexpr std::size_t kProbesPerUnfilled = 4;

    std::fill_n(table, kTotal, kUnset);
    table[0] = 0;
    std::vector<std::uint32_t> frontier{0};
    std::vector<std::uint32_t> next;
    frontier.reserve(kTotal / 2);
    next.reserve(kTotal / 2);
    std::size_t filled = 1;
    for (std::uint8_t depth = 0; filled < kTotal && depth < kUnset - 1; ++depth) {
        const auto mark = static_cast<std::uint8_t>(depth + 1);
        next.clear();
        if (frontier.size() * MoveCount <= (kTotal - filled) * kProbesPerUnfilled) {
            for (const std::uint32_t index : frontier) {
                const std::size_t first = index / SecondCount;
                const std::size_t second = index % SecondCount;
                const std::uint16_t* const first_to = first_move + first * MoveCount;
                const std::uint16_t* const second_to = second_move + second * MoveCount;
                for (std::size_t m = 0; m < MoveCount; ++m) {
                    const std::size_t to = first_to[m] * SecondCount + second_to[m];
                    if (table[to] == kUnset) {
                        table[to] = mark;
                        next.push_back(static_cast<std::uint32_t>(to));
                    }
                }
            }
        } else {
            for (std::size_t first = 0; first < FirstCount; ++first) {
                const std::uint16_t* const first_to = first_move + first * MoveCount;
                std::uint8_t* const row = table + first * SecondCount;
                for (std::size_t second = 0; second < SecondCount; ++second) {
                    if (row[second] != kUnset) {
                        continue;
                    }
                    const std::uint16_t* const second_to = second_move + second * MoveCount;
                    for (std::size_t m = 0; m < MoveCount; ++m) {
                        if (table[first_to[m] * SecondCount + second_to[m]] == depth) {
                            row[second] = mark;
                            next.push_back(
                                static_cast<std::uint32_t>(first * SecondCount + second));
                            break;
                        }
                    }
                }
            }
        }
        if (next.empty()) {
            break;
        }
        filled += next.size();
        frontier.swap(next);
    }
}

// Runs the tasks at the same time - all but the first on threads of their
// own - and returns when every one is done, rethrowing a failure (the first
// by position) on the calling thread. A thread that cannot be started simply
// has its task run here instead, so this can only ever be slower, never wrong.
// The tasks must not touch each other's outputs.
inline void run_concurrently(const std::vector<std::function<void()>>& tasks) {
    std::vector<std::exception_ptr> failures(tasks.size());
    auto guarded = [&tasks, &failures](std::size_t i) {
        try {
            tasks[i]();
        } catch (...) {
            failures[i] = std::current_exception();
        }
    };
    std::vector<std::thread> workers;
    workers.reserve(tasks.size());
    const bool can_overlap = std::thread::hardware_concurrency() != 1;
    for (std::size_t i = 1; i < tasks.size(); ++i) {
        if (can_overlap) {
            try {
                workers.emplace_back(guarded, i);
                continue;
            } catch (...) {
                // Out of threads: fall through and do the work here.
            }
        }
        guarded(i);
    }
    guarded(0);
    for (std::thread& worker : workers) {
        worker.join();
    }
    for (const std::exception_ptr& failure : failures) {
        if (failure) {
            std::rethrow_exception(failure);
        }
    }
}

// Everything phase 2 and the pruning need beyond the compile-time part.
struct Phase2Tables {
    // [coordinate * 10 + index into kPhase2Moves]
    std::array<std::uint16_t, static_cast<std::size_t>(kPermCount) * kPhase2MoveCount> cperm_move{};
    std::array<std::uint16_t, static_cast<std::size_t>(kPermCount) * kPhase2MoveCount> uedge_move{};
    std::array<std::uint16_t, static_cast<std::size_t>(kSlicePermCount) * kPhase2MoveCount>
        sperm_move{};
    // Exact distance to the goal in a pair of coordinates, [first * second_count + second].
    std::array<std::uint8_t, static_cast<std::size_t>(kTwistCount) * kSliceCount>
        twist_slice_prune{};
    std::array<std::uint8_t, static_cast<std::size_t>(kFlipCount) * kSliceCount> flip_slice_prune{};
    std::array<std::uint8_t, static_cast<std::size_t>(kPermCount) * kSlicePermCount>
        cperm_sperm_prune{};
    std::array<std::uint8_t, static_cast<std::size_t>(kPermCount) * kSlicePermCount>
        uedge_sperm_prune{};

    // Fills the object in place (it is several megabytes: no copies). The
    // four pruning tables are independent of one another, so they are built
    // at the same time: this is the one cost of a first solve, and it is four
    // equal pieces of work.
    Phase2Tables() {
        const Phase1MoveTables& p1 = phase1_move_tables();
        fill_sperm_moves(sperm_move.data());  // 24 x 10 entries; both phase 2 tables read it
        run_concurrently({
            [this, &p1] {
                fill_prune<kTwistCount, kSliceCount, kMoveCount>(twist_slice_prune.data(),
                                                                 p1.twist.data(), p1.slice.data());
            },
            [this, &p1] {
                fill_prune<kFlipCount, kSliceCount, kMoveCount>(flip_slice_prune.data(),
                                                                p1.flip.data(), p1.slice.data());
            },
            [this] {
                fill_perm_moves(cperm_move.data(), false);
                fill_prune<kPermCount, kSlicePermCount, kPhase2MoveCount>(
                    cperm_sperm_prune.data(), cperm_move.data(), sperm_move.data());
            },
            [this] {
                fill_perm_moves(uedge_move.data(), true);
                fill_prune<kPermCount, kSlicePermCount, kPhase2MoveCount>(
                    uedge_sperm_prune.data(), uedge_move.data(), sperm_move.data());
            },
        });
    }
};

// Built on the first call, thread-safely; later calls are a load.
[[nodiscard]] const Phase2Tables& phase2_tables();

}  // namespace cube::solver::detail
