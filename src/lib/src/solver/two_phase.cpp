#include <cube/solver/two_phase.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

#include <cube/profiling.hpp>

#include "cubie.hpp"
#include "two_phase_tables.hpp"

// Kociemba's two-phase algorithm over the cubie model (see cubie.hpp).
//
// Everything is expressed in "coordinates": small integers that stand for
// one aspect of the cube, so a move is a table lookup instead of a shuffle.
// The coordinates and the tables are described in two_phase_tables.hpp.
//
// Each phase prunes with two tables holding the exact distance to its goal
// in a pair of coordinates (the distance in a projection is a lower bound on
// the real distance), which is what keeps the search tiny.

namespace cube::solver {

namespace detail {

const Phase2Tables& phase2_tables() {
    // Initialised once, thread-safely, by whichever caller gets here first.
    static const Phase2Tables kTables;
    return kTables;
}

}  // namespace detail

namespace {

using detail::CubieCube;
using detail::kMoveCount;
using detail::kPhase2MoveCount;
using detail::kPhase2Moves;
using detail::kSliceCount;
using detail::kSlicePermCount;

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
    TwoPhaseSearch(const detail::Phase1MoveTables& phase1, const detail::Phase2Tables& phase2,
                   const CubieCube& start)
        : p1_(phase1), p2_(phase2), start_(start) {}

    // Searches until nothing shorter can be found or the budget is spent;
    // the answer is then solution(), or found() is false if the node
    // backstop was hit before any solution.
    void run() {
        const int twist = detail::twist_of(start_);
        const int flip = detail::flip_of(start_);
        const int slice = detail::slice_of(start_);
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
        return p2_.twist_slice_prune[static_cast<std::size_t>(twist) * kSliceCount +
                                     static_cast<std::size_t>(slice)];
    }
    int prune1_flip(int flip, int slice) const noexcept {
        return p2_.flip_slice_prune[static_cast<std::size_t>(flip) * kSliceCount +
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
                const int next_twist = p1_.twist[static_cast<std::size_t>(twist) * kMoveCount + m];
                const int next_flip = p1_.flip[static_cast<std::size_t>(flip) * kMoveCount + m];
                const int next_slice = p1_.slice[static_cast<std::size_t>(slice) * kMoveCount + m];
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
        const int cperm = detail::cperm_of(cube);
        const int uedge = detail::uedge_of(cube);
        const int sperm = detail::sperm_of(cube);

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
        return p2_.cperm_sperm_prune[static_cast<std::size_t>(cperm) * kSlicePermCount +
                                     static_cast<std::size_t>(sperm)];
    }
    int prune2_edges(int uedge, int sperm) const noexcept {
        return p2_.uedge_sperm_prune[static_cast<std::size_t>(uedge) * kSlicePermCount +
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
                p2_.cperm_move[static_cast<std::size_t>(cperm) * kPhase2MoveCount + i];
            const int next_uedge =
                p2_.uedge_move[static_cast<std::size_t>(uedge) * kPhase2MoveCount + i];
            const int next_sperm =
                p2_.sperm_move[static_cast<std::size_t>(sperm) * kPhase2MoveCount + i];
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

    const detail::Phase1MoveTables& p1_;
    const detail::Phase2Tables& p2_;
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
    CUBE_ZONE_SCOPED_N("solver.two_phase.prepare");
    (void)detail::phase2_tables();
}

Result<Solution> solve_two_phase(const Cube& scrambled) {
    CUBE_ZONE_SCOPED_N("solver.two_phase");
    if (!scrambled.is_physically_valid()) {
        return std::unexpected(CubeError::invalid_cube_state);
    }
    const std::optional<CubieCube> cubie = detail::to_cubie(scrambled);
    if (!cubie.has_value()) {
        return std::unexpected(CubeError::unsolvable_state);
    }

    TwoPhaseSearch search(detail::phase1_move_tables(), detail::phase2_tables(), *cubie);
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
