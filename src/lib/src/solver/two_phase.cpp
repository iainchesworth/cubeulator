#include <cube/solver/two_phase.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

#include "cubie.hpp"

// Kociemba's two-phase algorithm over the cubie model (see cubie.hpp).
//
// Everything is expressed in "coordinates": small integers that stand for
// one aspect of the cube, so a move is a table lookup instead of a shuffle.
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
// Each phase prunes with two tables holding the exact distance to its goal
// in a pair of those coordinates (the distance in a projection is a lower
// bound on the real distance), which is what keeps the search tiny.

namespace cube::solver {

namespace {

using detail::CubieCube;
using detail::kMoveCount;

constexpr int kTwistCount = 2187;
constexpr int kFlipCount = 2048;
constexpr int kSliceCount = 495;
constexpr int kPermCount = 40320;
constexpr int kSlicePermCount = 24;

// Phase 1 may use any face turn; phase 2 only these ten, as indices into the
// 18-move scheme (face * 3 + turns - 1, faces in the order U D L R F B):
// U, U2, U', D, D2, D', L2, R2, F2, B2.
constexpr int kPhase2MoveCount = 10;
constexpr std::array<std::uint8_t, kPhase2MoveCount> kPhase2Moves = {0, 1, 2,  3,  4,
                                                                     5, 7, 10, 13, 16};

// No phase has a longer shortest solution than this (and the real figures are
// 12 and 18), so the search is complete within them.
constexpr int kMaxPhase1Depth = 12;
constexpr int kMaxPhase2Depth = 18;

// The longest a solution may be. 12 + 18 is the most the two phases can add
// up to, so the first solution always fits under it.
constexpr int kMaxTotalLength = kMaxPhase1Depth + kMaxPhase2Depth;

// Node budgets. Counted in search nodes, not seconds, so the answer for a
// given cube is the same on every device and the cost is a known quantity.
// After the first solution is found the search keeps looking for a shorter
// one until `kImprovementNodes` more nodes have been spent; `kHardNodeLimit`
// is a backstop that a correct search never reaches (over 8,000 random and
// short scrambles the first solution took 160 thousand nodes at the median
// and 5.5 million at the worst) and exists so that a bug surfaces as
// CubeError::no_solution instead of an endless loop.
constexpr std::uint64_t kImprovementNodes = 5'000'000;
constexpr std::uint64_t kHardNodeLimit = 60'000'000;

// ---------------------------------------------------------------------------
// Coordinates
// ---------------------------------------------------------------------------

constexpr std::array<int, 9> kFactorial = {1, 1, 2, 6, 24, 120, 720, 5040, 40320};

int twist_of(const CubieCube& c) noexcept {
    int value = 0;
    for (std::size_t i = 0; i < 7; ++i) {
        value = value * 3 + c.co[i];
    }
    return value;
}

void set_twist(CubieCube& c, int value) noexcept {
    int sum = 0;
    for (int i = 6; i >= 0; --i) {
        c.co[static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(value % 3);
        sum += value % 3;
        value /= 3;
    }
    c.co[7] = static_cast<std::uint8_t>((3 - sum % 3) % 3);  // total twist is 0 mod 3
}

int flip_of(const CubieCube& c) noexcept {
    int value = 0;
    for (std::size_t i = 0; i < 11; ++i) {
        value = value * 2 + c.eo[i];
    }
    return value;
}

void set_flip(CubieCube& c, int value) noexcept {
    int sum = 0;
    for (int i = 10; i >= 0; --i) {
        c.eo[static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(value % 2);
        sum += value % 2;
        value /= 2;
    }
    c.eo[11] = static_cast<std::uint8_t>(sum % 2);  // total flip is 0 mod 2
}

// The middle-layer edges' positions as a number: the 12-bit mask of which
// positions hold a slice edge (piece 8..11), numbered so the solved mask -
// positions 8..11 - is 0.
struct SliceNumbering {
    std::array<std::uint16_t, 4096> index_of_mask{};
    std::array<std::uint16_t, kSliceCount> mask_of_index{};
};

const SliceNumbering& slice_numbering() {
    static const SliceNumbering kNumbering = [] {
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
    }();
    return kNumbering;
}

int slice_of(const CubieCube& c) noexcept {
    unsigned mask = 0;
    for (std::size_t pos = 0; pos < 12; ++pos) {
        if (c.ep[pos] >= 8) {
            mask |= 1u << pos;
        }
    }
    return slice_numbering().index_of_mask[mask];
}

// Puts the slice edges (pieces 8..11) where `value` says and the other edges
// (pieces 0..7) in the remaining positions, in order. Orientation and the
// order within each group are left for the caller's other coordinates.
void set_slice(CubieCube& c, int value) noexcept {
    const unsigned mask = slice_numbering().mask_of_index[static_cast<std::size_t>(value)];
    std::uint8_t next_slice = 8;
    std::uint8_t next_other = 0;
    for (std::size_t pos = 0; pos < 12; ++pos) {
        c.ep[pos] = ((mask >> pos) & 1u) != 0 ? next_slice++ : next_other++;
    }
}

// Rank of a permutation of 0..n-1 among all n! of them, identity = 0.
int rank_permutation(const std::uint8_t* p, std::size_t n) noexcept {
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

void unrank_permutation(int rank, std::uint8_t* p, std::size_t n) noexcept {
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

int cperm_of(const CubieCube& c) noexcept {
    return rank_permutation(c.cp.data(), 8);
}

int uedge_of(const CubieCube& c) noexcept {
    return rank_permutation(c.ep.data(), 8);  // phase 2 only: pieces 0..7 sit in positions 0..7
}

int sperm_of(const CubieCube& c) noexcept {
    std::array<std::uint8_t, 4> p{};
    for (std::size_t i = 0; i < 4; ++i) {
        p[i] = static_cast<std::uint8_t>(c.ep[8 + i] - 8);
    }
    return rank_permutation(p.data(), 4);
}

// ---------------------------------------------------------------------------
// Tables
// ---------------------------------------------------------------------------

struct Tables {
    // [coordinate * 18 + move]
    std::vector<std::uint16_t> twist_move;
    std::vector<std::uint16_t> flip_move;
    std::vector<std::uint16_t> slice_move;
    // [coordinate * 10 + index into kPhase2Moves]
    std::vector<std::uint16_t> cperm_move;
    std::vector<std::uint16_t> uedge_move;
    std::vector<std::uint16_t> sperm_move;
    // Exact distance to the goal in a pair of coordinates, [first * second_count + second].
    std::vector<std::uint8_t> twist_slice_prune;
    std::vector<std::uint8_t> flip_slice_prune;
    std::vector<std::uint8_t> cperm_sperm_prune;
    std::vector<std::uint8_t> uedge_sperm_prune;
};

// Breadth-first search outward from the goal (both coordinates 0). The move
// set is closed under inversion, so distance *from* the goal is distance
// *to* it. Every entry of both pair-spaces is reachable, so the table fills
// completely; the `added == 0` exit is a guard, not the normal ending.
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

Tables build_tables() {
    Tables t;

    // --- Phase 1 move tables: apply each of the 18 moves to a cube that
    // carries just this one coordinate. A coordinate is well defined exactly
    // because a move's effect on it does not depend on anything else.
    t.twist_move.resize(static_cast<std::size_t>(kTwistCount) * kMoveCount);
    t.flip_move.resize(static_cast<std::size_t>(kFlipCount) * kMoveCount);
    t.slice_move.resize(static_cast<std::size_t>(kSliceCount) * kMoveCount);
    for (std::size_t m = 0; m < kMoveCount; ++m) {
        const CubieCube& move = detail::move_cubie(m);
        for (int v = 0; v < kTwistCount; ++v) {
            CubieCube c;
            set_twist(c, v);
            t.twist_move[static_cast<std::size_t>(v) * kMoveCount + m] =
                static_cast<std::uint16_t>(twist_of(detail::multiply(c, move)));
        }
        for (int v = 0; v < kFlipCount; ++v) {
            CubieCube c;
            set_flip(c, v);
            t.flip_move[static_cast<std::size_t>(v) * kMoveCount + m] =
                static_cast<std::uint16_t>(flip_of(detail::multiply(c, move)));
        }
        for (int v = 0; v < kSliceCount; ++v) {
            CubieCube c;
            set_slice(c, v);
            t.slice_move[static_cast<std::size_t>(v) * kMoveCount + m] =
                static_cast<std::uint16_t>(slice_of(detail::multiply(c, move)));
        }
    }

    // --- Phase 2 move tables, over the ten G1 moves.
    t.cperm_move.resize(static_cast<std::size_t>(kPermCount) * kPhase2MoveCount);
    t.uedge_move.resize(static_cast<std::size_t>(kPermCount) * kPhase2MoveCount);
    t.sperm_move.resize(static_cast<std::size_t>(kSlicePermCount) * kPhase2MoveCount);
    for (std::size_t i = 0; i < kPhase2MoveCount; ++i) {
        const CubieCube& move = detail::move_cubie(kPhase2Moves[i]);
        for (int v = 0; v < kPermCount; ++v) {
            CubieCube corners;
            unrank_permutation(v, corners.cp.data(), 8);
            t.cperm_move[static_cast<std::size_t>(v) * kPhase2MoveCount + i] =
                static_cast<std::uint16_t>(cperm_of(detail::multiply(corners, move)));

            CubieCube edges;
            unrank_permutation(v, edges.ep.data(), 8);
            t.uedge_move[static_cast<std::size_t>(v) * kPhase2MoveCount + i] =
                static_cast<std::uint16_t>(uedge_of(detail::multiply(edges, move)));
        }
        for (int v = 0; v < kSlicePermCount; ++v) {
            CubieCube c;
            std::array<std::uint8_t, 4> p{};
            unrank_permutation(v, p.data(), 4);
            for (std::size_t k = 0; k < 4; ++k) {
                c.ep[8 + k] = static_cast<std::uint8_t>(8 + p[k]);
            }
            t.sperm_move[static_cast<std::size_t>(v) * kPhase2MoveCount + i] =
                static_cast<std::uint16_t>(sperm_of(detail::multiply(c, move)));
        }
    }

    t.twist_slice_prune =
        build_prune(kTwistCount, kSliceCount, kMoveCount, t.twist_move, t.slice_move);
    t.flip_slice_prune =
        build_prune(kFlipCount, kSliceCount, kMoveCount, t.flip_move, t.slice_move);
    t.cperm_sperm_prune =
        build_prune(kPermCount, kSlicePermCount, kPhase2MoveCount, t.cperm_move, t.sperm_move);
    t.uedge_sperm_prune =
        build_prune(kPermCount, kSlicePermCount, kPhase2MoveCount, t.uedge_move, t.sperm_move);
    return t;
}

const Tables& tables() {
    // Initialised once, thread-safely, by whichever caller gets here first.
    static const Tables kTables = build_tables();
    return kTables;
}

// ---------------------------------------------------------------------------
// Search
// ---------------------------------------------------------------------------

// A move on `face` may follow one on `last`: not the same face again (it
// would merge), and of two opposite faces only in one order (U then D, never
// D then U - they commute, so the other order is the same solution twice).
// Faces are numbered U D L R F B, so opposite faces differ in the low bit.
bool may_follow(int face, int last) noexcept {
    if (face == last) {
        return false;
    }
    return !(last == (face ^ 1) && face < last);
}

class TwoPhaseSearch {
public:
    TwoPhaseSearch(const Tables& tables, const CubieCube& start) : t_(tables), start_(start) {}

    // Searches until nothing shorter can be found or the budget is spent;
    // the answer is then solution(), or found() is false if the node
    // backstop was hit before any solution.
    void run() {
        const int twist = twist_of(start_);
        const int flip = flip_of(start_);
        const int slice = slice_of(start_);
        const int h0 = std::max(prune1(twist, slice), prune1_flip(flip, slice));
        for (int depth = h0; depth <= kMaxPhase1Depth && depth < best_length_ && !stop_; ++depth) {
            search_phase1(twist, flip, slice, depth, 0, -1);
        }
    }

    [[nodiscard]] bool found() const noexcept { return found_; }
    [[nodiscard]] const std::vector<std::uint8_t>& solution() const noexcept { return best_; }
    [[nodiscard]] std::size_t phase1_length() const noexcept { return best_phase1_length_; }

private:
    int prune1(int twist, int slice) const noexcept {
        return t_.twist_slice_prune[static_cast<std::size_t>(twist) * kSliceCount +
                                    static_cast<std::size_t>(slice)];
    }
    int prune1_flip(int flip, int slice) const noexcept {
        return t_.flip_slice_prune[static_cast<std::size_t>(flip) * kSliceCount +
                                   static_cast<std::size_t>(slice)];
    }

    // Counts a node and applies both stopping rules. True means stop.
    bool spend_node() noexcept {
        ++nodes_;
        if (found_ && nodes_ - nodes_at_first_ > kImprovementNodes) {
            stop_ = true;
        }
        if (nodes_ > kHardNodeLimit) {
            stop_ = true;
        }
        return stop_;
    }

    void search_phase1(int twist, int flip, int slice, int depth_left, int depth, int last_face) {
        if (depth_left == 0) {
            // The last move must be one phase 2 cannot make (a quarter turn
            // of L, R, F or B): otherwise the same cube was reachable one
            // move earlier and has already been tried.
            if (depth > 0) {
                const int m = path_[static_cast<std::size_t>(depth - 1)];
                if (m / 3 < 2 || m % 3 == 1) {
                    return;
                }
            }
            finish_with_phase2(depth, last_face);
            return;
        }
        for (int face = 0; face < 6; ++face) {
            if (!may_follow(face, last_face)) {
                continue;
            }
            for (int turn = 0; turn < 3; ++turn) {
                if (spend_node()) {
                    return;
                }
                const auto m = static_cast<std::size_t>(face * 3 + turn);
                const int next_twist =
                    t_.twist_move[static_cast<std::size_t>(twist) * kMoveCount + m];
                const int next_flip = t_.flip_move[static_cast<std::size_t>(flip) * kMoveCount + m];
                const int next_slice =
                    t_.slice_move[static_cast<std::size_t>(slice) * kMoveCount + m];
                const int h =
                    std::max(prune1(next_twist, next_slice), prune1_flip(next_flip, next_slice));
                if (h >= depth_left) {
                    continue;
                }
                path_[static_cast<std::size_t>(depth)] = static_cast<std::uint8_t>(m);
                search_phase1(next_twist, next_flip, next_slice, depth_left - 1, depth + 1, face);
                if (stop_) {
                    return;
                }
            }
        }
    }

    // The phase 1 moves in path_[0, depth) have reached G1. Build that cube
    // and look for the shortest phase 2 that fits under the current bound.
    void finish_with_phase2(int depth, int last_face) {
        CubieCube cube = start_;
        for (int i = 0; i < depth; ++i) {
            cube = detail::multiply(cube, detail::move_cubie(path_[static_cast<std::size_t>(i)]));
        }
        const int cperm = cperm_of(cube);
        const int uedge = uedge_of(cube);
        const int sperm = sperm_of(cube);

        const int max_depth2 = std::min(kMaxPhase2Depth, best_length_ - 1 - depth);
        const int h0 = std::max(prune2_corners(cperm, sperm), prune2_edges(uedge, sperm));
        for (int depth2 = h0; depth2 <= max_depth2; ++depth2) {
            if (search_phase2(cperm, uedge, sperm, depth2, depth, last_face)) {
                best_length_ = depth + depth2;
                best_.assign(path_.begin(), path_.begin() + best_length_);
                best_phase1_length_ = static_cast<std::size_t>(depth);
                if (!found_) {
                    found_ = true;
                    nodes_at_first_ = nodes_;
                }
                return;
            }
            if (stop_) {
                return;
            }
        }
    }

    int prune2_corners(int cperm, int sperm) const noexcept {
        return t_.cperm_sperm_prune[static_cast<std::size_t>(cperm) * kSlicePermCount +
                                    static_cast<std::size_t>(sperm)];
    }
    int prune2_edges(int uedge, int sperm) const noexcept {
        return t_.uedge_sperm_prune[static_cast<std::size_t>(uedge) * kSlicePermCount +
                                    static_cast<std::size_t>(sperm)];
    }

    bool search_phase2(int cperm, int uedge, int sperm, int depth_left, int depth, int last_face) {
        if (depth_left == 0) {
            return cperm == 0 && uedge == 0 && sperm == 0;
        }
        for (std::size_t i = 0; i < kPhase2MoveCount; ++i) {
            const int move = kPhase2Moves[i];
            const int face = move / 3;
            if (!may_follow(face, last_face)) {
                continue;
            }
            if (spend_node()) {
                return false;
            }
            const int next_cperm =
                t_.cperm_move[static_cast<std::size_t>(cperm) * kPhase2MoveCount + i];
            const int next_uedge =
                t_.uedge_move[static_cast<std::size_t>(uedge) * kPhase2MoveCount + i];
            const int next_sperm =
                t_.sperm_move[static_cast<std::size_t>(sperm) * kPhase2MoveCount + i];
            const int h = std::max(prune2_corners(next_cperm, next_sperm),
                                   prune2_edges(next_uedge, next_sperm));
            if (h >= depth_left) {
                continue;
            }
            path_[static_cast<std::size_t>(depth)] = static_cast<std::uint8_t>(move);
            if (search_phase2(next_cperm, next_uedge, next_sperm, depth_left - 1, depth + 1,
                              face)) {
                return true;
            }
            if (stop_) {
                return false;
            }
        }
        return false;
    }

    const Tables& t_;
    CubieCube start_;
    std::array<std::uint8_t, kMaxTotalLength + 1> path_{};

    int best_length_ = kMaxTotalLength + 1;  // a solution must be strictly shorter
    std::vector<std::uint8_t> best_;
    std::size_t best_phase1_length_ = 0;
    bool found_ = false;
    bool stop_ = false;
    std::uint64_t nodes_ = 0;
    std::uint64_t nodes_at_first_ = 0;
};

}  // namespace

void prepare_two_phase() {
    (void)tables();
}

Result<Solution> solve_two_phase(const Cube& scrambled) {
    if (!scrambled.is_physically_valid()) {
        return std::unexpected(CubeError::invalid_cube_state);
    }
    const std::optional<CubieCube> cubie = detail::to_cubie(scrambled);
    if (!cubie.has_value()) {
        return std::unexpected(CubeError::unsolvable_state);
    }

    TwoPhaseSearch search(tables(), *cubie);
    search.run();
    if (!search.found()) {
        return std::unexpected(CubeError::no_solution);
    }

    Solution solution;
    solution.steps.push_back(SolveStep{Stage::Reduce, {}});
    solution.steps.push_back(SolveStep{Stage::Finish, {}});
    const std::vector<std::uint8_t>& indices = search.solution();
    for (std::size_t i = 0; i < indices.size(); ++i) {
        const std::size_t step = i < search.phase1_length() ? 0 : 1;
        solution.steps[step].moves.push_back(detail::move_from_index(indices[i]));
    }

    // Defence in depth: the result is verified against the facelet model,
    // the one the rest of the app trusts, before it is handed out.
    Cube check = scrambled;
    for (const SolveStep& step : solution.steps) {
        for (const Move& move : step.moves) {
            check = check.apply(move);
        }
    }
    if (!check.is_solved()) {
        return std::unexpected(CubeError::no_solution);
    }
    return merge_adjacent_moves(std::move(solution));
}

}  // namespace cube::solver
