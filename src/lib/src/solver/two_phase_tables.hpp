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
inline constexpr std::array<std::uint8_t, kPhase2MoveCount> kPhase2Moves = {
    0, 1, 2, 3, 4, 5, 7, 10, 13, 16,
};

// ---------------------------------------------------------------------------
// Coordinates
// ---------------------------------------------------------------------------

[[nodiscard]] constexpr int twist_of(const CubieCube& c) noexcept {
    int value = 0;
    for (std::size_t i = 0; i < 7; ++i) {
        value = (value * 3) + c.co.at(i);
    }
    return value;
}

[[nodiscard]] constexpr int flip_of(const CubieCube& c) noexcept {
    int value = 0;
    for (std::size_t i = 0; i < 11; ++i) {
        value = (value * 2) + c.eo.at(i);
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
            n.index_of_mask.at(static_cast<std::size_t>(mask)) = static_cast<std::uint16_t>(next);
            n.mask_of_index.at(static_cast<std::size_t>(next)) = static_cast<std::uint16_t>(mask);
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
        if (c.ep.at(pos) >= 8) {
            mask |= 1U << pos;
        }
    }
    return slice_numbering().index_of_mask.at(mask);
}

// Rank of a permutation of 0..n-1 among all n! of them in lexicographic
// order, identity = 0 (n <= 8). Each digit of the factorial-base number is the
// count of values still to come that are smaller than this one, which is the
// number of unplaced values below it.
template <std::size_t N>
[[nodiscard]] constexpr int rank_permutation(const std::array<std::uint8_t, N>& p,
                                             std::size_t n) noexcept {
    unsigned unplaced = (1U << n) - 1U;
    int rank = 0;
    for (std::size_t i = 0; i < n; ++i) {
        const unsigned bit = 1U << p.at(i);
        rank = (rank * static_cast<int>(n - i)) + std::popcount(unplaced & (bit - 1U));
        unplaced &= ~bit;
    }
    return rank;
}

[[nodiscard]] constexpr int cperm_of(const CubieCube& c) noexcept {
    return rank_permutation(c.cp, 8);
}

[[nodiscard]] constexpr int uedge_of(const CubieCube& c) noexcept {
    return rank_permutation(c.ep, 8);  // phase 2 only: pieces 0..7 sit in positions 0..7
}

[[nodiscard]] constexpr int sperm_of(const CubieCube& c) noexcept {
    std::array<std::uint8_t, 4> p{};
    for (std::size_t i = 0; i < 4; ++i) {
        p.at(i) = static_cast<std::uint8_t>(c.ep.at(8 + i) - 8);
    }
    return rank_permutation(p, 4);
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

template <std::size_t N>
constexpr void fill_twist_moves(std::array<std::uint16_t, N>& out) noexcept {
    // The eighth corner's twist is whatever makes the total 0 mod 3.
    std::array<std::uint8_t, static_cast<std::size_t>(kTwistCount) * 8> digit{};
    for (int v = 0; v < kTwistCount; ++v) {
        auto rest = static_cast<unsigned>(v);
        unsigned sum = 0;
        const std::size_t base = static_cast<std::size_t>(v) * 8;
        for (std::size_t i = 7; i > 0; --i) {  // corners 6 down to 0
            digit.at(base + i - 1) = static_cast<std::uint8_t>(rest % 3U);
            sum += rest % 3U;
            rest /= 3U;
        }
        digit.at(base + 7) = static_cast<std::uint8_t>((3U - (sum % 3U)) % 3U);
    }
    for (std::size_t face = 0; face < kQuarterTurns.size(); ++face) {
        const auto& from = kQuarterTurns.at(face).cp;
        const auto& plus = kQuarterTurns.at(face).co;
        for (int v = 0; v < kTwistCount; ++v) {
            // multiply(): the corner at position i afterwards is the one that
            // was at from[i] before, twisted by plus[i] more.
            const std::size_t base = static_cast<std::size_t>(v) * 8;
            const auto twisted = [&](std::size_t i) -> unsigned {
                return (static_cast<unsigned>(digit.at(base + from.at(i))) + plus.at(i)) % 3U;
            };
            out.at((static_cast<std::size_t>(v) * kMoveCount) + (face * 3)) =
                static_cast<std::uint16_t>((twisted(0) * 729U) + (twisted(1) * 243U) +
                                           (twisted(2) * 81U) + (twisted(3) * 27U) +
                                           (twisted(4) * 9U) + (twisted(5) * 3U) + twisted(6));
        }
    }
}

template <std::size_t N>
constexpr void fill_flip_moves(std::array<std::uint16_t, N>& out) noexcept {
    // The twelfth edge's flip is whatever makes the total 0 mod 2.
    std::array<std::uint8_t, static_cast<std::size_t>(kFlipCount) * 12> bit{};
    for (int v = 0; v < kFlipCount; ++v) {
        auto rest = static_cast<unsigned>(v);
        unsigned sum = 0;
        const std::size_t base = static_cast<std::size_t>(v) * 12;
        for (std::size_t i = 11; i > 0; --i) {  // edges 10 down to 0
            bit.at(base + i - 1) = static_cast<std::uint8_t>(rest & 1U);
            sum += rest & 1U;
            rest >>= 1U;
        }
        bit.at(base + 11) = static_cast<std::uint8_t>(sum & 1U);
    }
    for (std::size_t face = 0; face < kQuarterTurns.size(); ++face) {
        const auto& from = kQuarterTurns.at(face).ep;
        const auto& plus = kQuarterTurns.at(face).eo;
        for (int v = 0; v < kFlipCount; ++v) {
            const std::size_t base = static_cast<std::size_t>(v) * 12;
            const auto flipped = [&](std::size_t i) -> unsigned {
                return static_cast<unsigned>(bit.at(base + from.at(i))) ^
                       static_cast<unsigned>(plus.at(i));
            };
            out.at((static_cast<std::size_t>(v) * kMoveCount) + (face * 3)) =
                static_cast<std::uint16_t>((flipped(0) << 10U) | (flipped(1) << 9U) |
                                           (flipped(2) << 8U) | (flipped(3) << 7U) |
                                           (flipped(4) << 6U) | (flipped(5) << 5U) |
                                           (flipped(6) << 4U) | (flipped(7) << 3U) |
                                           (flipped(8) << 2U) | (flipped(9) << 1U) | flipped(10));
        }
    }
}

template <std::size_t N>
constexpr void fill_slice_moves(std::array<std::uint16_t, N>& out,
                                const SliceNumbering& n) noexcept {
    for (std::size_t face = 0; face < kQuarterTurns.size(); ++face) {
        const auto& from = kQuarterTurns.at(face).ep;
        for (int v = 0; v < kSliceCount; ++v) {
            // A position holds a slice edge afterwards if the position it
            // takes its piece from did.
            const unsigned mask = n.mask_of_index.at(static_cast<std::size_t>(v));
            const auto held = [&](std::size_t i) -> unsigned { return (mask >> from.at(i)) & 1U; };
            const unsigned moved = held(0) | (held(1) << 1U) | (held(2) << 2U) | (held(3) << 3U) |
                                   (held(4) << 4U) | (held(5) << 5U) | (held(6) << 6U) |
                                   (held(7) << 7U) | (held(8) << 8U) | (held(9) << 9U) |
                                   (held(10) << 10U) | (held(11) << 11U);
            out.at((static_cast<std::size_t>(v) * kMoveCount) + (face * 3)) =
                n.index_of_mask.at(moved);
        }
    }
}

// Completes the table from its quarter-turn columns: X2 is X then X, X' is X
// then X then X. `count` rows of kMoveCount.
template <std::size_t N>
constexpr void fill_derived_turns(std::array<std::uint16_t, N>& table, std::size_t count) noexcept {
    for (std::size_t v = 0; v < count; ++v) {
        const std::size_t row = v * kMoveCount;
        for (std::size_t face = 0; face < 6; ++face) {
            const std::size_t quarter = face * 3;
            table.at(row + quarter + 1) = table.at(
                (static_cast<std::size_t>(table.at(row + quarter)) * kMoveCount) + quarter);
            table.at(row + quarter + 2) = table.at(
                (static_cast<std::size_t>(table.at(row + quarter + 1)) * kMoveCount) + quarter);
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
        fill_twist_moves(twist);
        fill_derived_turns(twist, kTwistCount);
        fill_flip_moves(flip);
        fill_derived_turns(flip, kFlipCount);
        fill_slice_moves(slice, numbering);
        fill_derived_turns(slice, kSliceCount);
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
    unsigned unplaced = (1U << N) - 1U;
    for (std::size_t i = 0; i < N; ++i) {
        const std::size_t place = kFactorial.at(N - 1 - i);
        // The digit is how many unplaced values come before this one.
        std::size_t before = rank / place;
        rank %= place;
        unsigned bits = unplaced;
        for (; before > 0; --before) {
            bits &= bits - 1U;  // drop the lowest set bit
        }
        const auto value = static_cast<unsigned>(std::countr_zero(bits));
        p.at(i) = static_cast<std::uint8_t>(value);
        unplaced &= ~(1U << value);
    }
    return p;
}

// The eight-element permutation move table, [rank * 10 + index into
// kPhase2Moves], for the corners (`edges` false) or the U/D-layer edges. The
// ten moves keep the U/D edges among positions 0..7, so both are the same
// computation on a different half of the cubie. Only U, D and the four half
// turns need a rank taken; U2, U', D2 and D' are U and D composed.
template <std::size_t N>
inline void fill_perm_moves(std::array<std::uint16_t, N>& out, bool edges) noexcept {
    constexpr std::array<std::size_t, 6> kBase = {0, 3, 6, 7, 8, 9};  // U D L2 R2 F2 B2
    std::array<std::uint8_t, 8> q{};
    for (std::size_t rank = 0; rank < static_cast<std::size_t>(kPermCount); ++rank) {
        const std::array<std::uint8_t, 8> p = permutation_of_rank<8>(rank);
        for (const std::size_t i : kBase) {
            const CubieCube& move = move_cubie(kPhase2Moves.at(i));
            for (std::size_t pos = 0; pos < 8; ++pos) {
                // multiply(): `pos` takes the piece that was at from[pos]
                const std::size_t from = edges ? move.ep.at(pos) : move.cp.at(pos);
                q.at(pos) = p.at(from);
            }
            out.at((rank * kPhase2MoveCount) + i) =
                static_cast<std::uint16_t>(rank_permutation(q, 8));
        }
    }
    for (std::size_t rank = 0; rank < static_cast<std::size_t>(kPermCount); ++rank) {
        const std::size_t row = rank * kPhase2MoveCount;
        for (const std::size_t i : {std::size_t{0}, std::size_t{3}}) {  // U, D
            out.at(row + i + 1) = out.at((std::size_t{out.at(row + i)} * kPhase2MoveCount) + i);
            out.at(row + i + 2) = out.at((std::size_t{out.at(row + i + 1)} * kPhase2MoveCount) + i);
        }
    }
}

// The middle-layer edges' permutation (4! = 24) over the ten moves.
template <std::size_t N>
inline void fill_sperm_moves(std::array<std::uint16_t, N>& out) noexcept {
    std::array<std::uint8_t, 4> q{};
    for (std::size_t rank = 0; rank < static_cast<std::size_t>(kSlicePermCount); ++rank) {
        const std::array<std::uint8_t, 4> p = permutation_of_rank<4>(rank);
        for (std::size_t i = 0; i < static_cast<std::size_t>(kPhase2MoveCount); ++i) {
            const CubieCube& move = move_cubie(kPhase2Moves.at(i));
            for (std::size_t k = 0; k < 4; ++k) {
                q.at(k) = p.at(move.ep.at(8 + k) - 8U);
            }
            out.at((rank * kPhase2MoveCount) + i) =
                static_cast<std::uint16_t>(rank_permutation(q, 4));
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
// Distances in a pruning table that has not been reached yet.
inline constexpr std::uint8_t kPruneUnset = 0xFF;

// One level found by expanding every pair at the current depth: each of the
// frontier's neighbours that is still unset is at depth + 1.
template <std::size_t SecondCount, std::size_t MoveCount>
void expand_frontier(auto& table, const auto& first_move, const auto& second_move,
                     const std::vector<std::uint32_t>& frontier, std::uint8_t mark,
                     std::vector<std::uint32_t>& next) {
    for (const std::uint32_t index : frontier) {
        const std::size_t first_base = (index / SecondCount) * MoveCount;
        const std::size_t second_base = (index % SecondCount) * MoveCount;
        for (std::size_t m = 0; m < MoveCount; ++m) {
            const std::size_t to =
                (static_cast<std::size_t>(first_move.at(first_base + m)) * SecondCount) +
                second_move.at(second_base + m);
            if (table.at(to) == kPruneUnset) {
                table.at(to) = mark;
                next.push_back(static_cast<std::uint32_t>(to));
            }
        }
    }
}

// One level found the other way: ask every still-unset pair whether any
// neighbour is at the current depth.
template <std::size_t FirstCount, std::size_t SecondCount, std::size_t MoveCount>
void probe_unfilled(auto& table, const auto& first_move, const auto& second_move,
                    std::uint8_t depth, std::vector<std::uint32_t>& next) {
    const auto mark = static_cast<std::uint8_t>(depth + 1);
    for (std::size_t first = 0; first < FirstCount; ++first) {
        const std::size_t first_base = first * MoveCount;
        const std::size_t row = first * SecondCount;
        for (std::size_t second = 0; second < SecondCount; ++second) {
            if (table.at(row + second) != kPruneUnset) {
                continue;
            }
            const std::size_t second_base = second * MoveCount;
            for (std::size_t m = 0; m < MoveCount; ++m) {
                const std::size_t to =
                    (static_cast<std::size_t>(first_move.at(first_base + m)) * SecondCount) +
                    second_move.at(second_base + m);
                if (table.at(to) == depth) {
                    table.at(row + second) = mark;
                    next.push_back(static_cast<std::uint32_t>(row + second));
                    break;
                }
            }
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
inline void fill_prune(auto& table, const auto& first_move, const auto& second_move) {
    constexpr std::size_t kTotal = FirstCount * SecondCount;
    // Probes an unfilled pair typically needs before it finds a neighbour at
    // the current depth (measured: 3 to 5 on these tables).
    constexpr std::size_t kProbesPerUnfilled = 4;

    table.fill(kPruneUnset);
    table.at(0) = 0;
    std::vector<std::uint32_t> frontier{0};
    std::vector<std::uint32_t> next;
    frontier.reserve(kTotal / 2);
    next.reserve(kTotal / 2);
    std::size_t filled = 1;
    for (std::uint8_t depth = 0; filled < kTotal && depth < kPruneUnset - 1; ++depth) {
        const auto mark = static_cast<std::uint8_t>(depth + 1);
        next.clear();
        if (frontier.size() * MoveCount <= (kTotal - filled) * kProbesPerUnfilled) {
            expand_frontier<SecondCount, MoveCount>(table, first_move, second_move, frontier, mark,
                                                    next);
        } else {
            probe_unfilled<FirstCount, SecondCount, MoveCount>(table, first_move, second_move,
                                                               depth, next);
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
            tasks.at(i)();
        } catch (...) {
            failures.at(i) = std::current_exception();
        }
    };
    std::vector<std::thread> workers;
    workers.reserve(tasks.size());
    const bool can_overlap = std::thread::hardware_concurrency() != 1;
    for (std::size_t i = 1; i < tasks.size(); ++i) {
        bool started = false;
        if (can_overlap) {
            try {
                workers.emplace_back(guarded, i);
                started = true;
            } catch (...) {
                started = false;  // out of threads: do the work here instead
            }
        }
        if (!started) {
            guarded(i);
        }
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
        fill_sperm_moves(sperm_move);  // 24 x 10 entries; both phase 2 tables read it
        run_concurrently({
            [this, &p1] {
                fill_prune<kTwistCount, kSliceCount, kMoveCount>(twist_slice_prune, p1.twist,
                                                                 p1.slice);
            },
            [this, &p1] {
                fill_prune<kFlipCount, kSliceCount, kMoveCount>(flip_slice_prune, p1.flip,
                                                                p1.slice);
            },
            [this] {
                fill_perm_moves(cperm_move, false);
                fill_prune<kPermCount, kSlicePermCount, kPhase2MoveCount>(cperm_sperm_prune,
                                                                          cperm_move, sperm_move);
            },
            [this] {
                fill_perm_moves(uedge_move, true);
                fill_prune<kPermCount, kSlicePermCount, kPhase2MoveCount>(uedge_sperm_prune,
                                                                          uedge_move, sperm_move);
            },
        });
    }
};

// Built on the first call, thread-safely; later calls are a load.
[[nodiscard]] const Phase2Tables& phase2_tables();

}  // namespace cube::solver::detail
