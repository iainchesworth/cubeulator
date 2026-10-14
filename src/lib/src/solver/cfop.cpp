#include <cube/solver/cfop.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include <cube/profiling.hpp>
#include <cube/solver/search_work.hpp>

#include "cfop_last_layer.hpp"
#include "cfop_stages.hpp"
#include "cubie.hpp"

namespace cube::solver {

namespace {

using detail::CubieCube;
using detail::kMoveCount;

// ---------------------------------------------------------------------------
// Searching for a short sequence over a handful of tracked pieces
// ---------------------------------------------------------------------------
//
// The cross and each F2L pair only care where a few specific pieces end up,
// and everything else on the cube is free to be disturbed on the way. So a
// search state is just those pieces' (position, orientation) - at most 12 of
// them, 5 bits each, packed in one word - and a move is a lookup per piece.
// That makes the state space small enough to search from both ends at once:
// forward from where the pieces are, backward from where they should be, and
// stop when the two meet. Meeting in the middle is what turns a 14-move
// search into two 7-move ones.

struct TrackedPiece {
    bool corner;
    std::uint8_t id;  // the piece, named by the position it occupies when solved
};

// Where a piece goes, and how it turns, when each of the 18 moves is made:
// indexed [move][position * orientations + orientation], giving the new
// code in the same form.
struct PieceTransitions {
    std::array<std::array<std::uint8_t, 24>, kMoveCount> corner{};
    std::array<std::array<std::uint8_t, 24>, kMoveCount> edge{};
};

constexpr PieceTransitions make_piece_transitions() noexcept {
    PieceTransitions t;
    for (std::size_t m = 0; m < kMoveCount; ++m) {
        const CubieCube& move = detail::move_cubie(m);
        // multiply(): the piece at position q afterwards is the one that
        // was at move.cp[q] before, turned by move.co[q] more.
        for (std::size_t q = 0; q < detail::kCornerCount; ++q) {
            const std::size_t from = move.cp.at(q);
            for (std::size_t o = 0; o < 3; ++o) {
                t.corner.at(m).at((from * 3) + o) =
                    static_cast<std::uint8_t>((q * 3) + ((o + move.co.at(q)) % 3));
            }
        }
        for (std::size_t q = 0; q < detail::kEdgeCount; ++q) {
            const std::size_t from = move.ep.at(q);
            for (std::size_t o = 0; o < 2; ++o) {
                t.edge.at(m).at((from * 2) + o) =
                    static_cast<std::uint8_t>((q * 2) + ((o + move.eo.at(q)) % 2));
            }
        }
    }
    return t;
}

// Built by the compiler from the move cubies, so there is no first-use cost
// and nothing to guard.
constexpr PieceTransitions kTransitions = make_piece_transitions();

constexpr std::size_t kMaxTracked = 12;  // 12 pieces * 5 bits = 60 bits

std::uint64_t pack_state(const CubieCube& cube, std::span<const TrackedPiece> tracked) noexcept {
    std::uint64_t state = 0;
    std::size_t i = 0;
    for (const TrackedPiece& piece : tracked) {
        std::uint64_t code = 0;
        if (piece.corner) {
            for (std::size_t pos = 0; pos < detail::kCornerCount; ++pos) {
                if (cube.cp.at(pos) == piece.id) {
                    code = (pos * 3) + cube.co.at(pos);
                }
            }
        } else {
            for (std::size_t pos = 0; pos < detail::kEdgeCount; ++pos) {
                if (cube.ep.at(pos) == piece.id) {
                    code = (pos * 2) + cube.eo.at(pos);
                }
            }
        }
        state |= code << (5 * i);
        ++i;
    }
    return state;
}

// Every tracked piece at home and unturned.
std::uint64_t solved_state(std::span<const TrackedPiece> tracked) noexcept {
    std::uint64_t state = 0;
    std::size_t i = 0;
    for (const TrackedPiece& piece : tracked) {
        const std::uint64_t code = piece.corner ? piece.id * 3U : piece.id * 2U;
        state |= code << (5 * i);
        ++i;
    }
    return state;
}

// A packed state of the tracked pieces, and the move turned on it.
struct Turn {
    std::uint64_t state = 0;
    std::size_t move = 0;
};

std::uint64_t step(const PieceTransitions& t, std::span<const TrackedPiece> tracked,
                   const Turn& turn) noexcept {
    std::uint64_t next = 0;
    std::size_t i = 0;
    for (const TrackedPiece& piece : tracked) {
        const std::size_t code = (turn.state >> (5 * i)) & 31U;
        const std::uint64_t moved =
            piece.corner ? t.corner.at(turn.move).at(code) : t.edge.at(turn.move).at(code);
        next |= moved << (5 * i);
        ++i;
    }
    return next;
}

struct Frontier {
    std::unordered_map<std::uint64_t, std::int32_t> index;
    std::vector<std::uint64_t> state;
    std::vector<std::int32_t> parent;
    std::vector<std::uint8_t> move;
    std::size_t layer_start = 0;  // first node of the layer not yet expanded
    int depth = 0;

    // How a node was reached: from which node, by which move.
    struct Link {
        std::int32_t from = -1;
        std::uint8_t move = 0;
    };

    std::int32_t add(std::uint64_t s, const Link& link) {
        const auto i = static_cast<std::int32_t>(state.size());
        index.emplace(s, i);
        state.push_back(s);
        parent.push_back(link.from);
        move.push_back(link.move);
        return i;
    }
};

// The move indices of the faces in `face_mask` (bit n = face n, in Face order).
std::vector<std::uint8_t> allowed_moves(unsigned face_mask) {
    std::vector<std::uint8_t> moves;
    for (std::size_t m = 0; m < kMoveCount; ++m) {
        if (((face_mask >> (m / 3)) & 1U) != 0) {
            moves.push_back(static_cast<std::uint8_t>(m));
        }
    }
    return moves;
}

// Where the two frontiers met: a node of each.
struct Meeting {
    std::int32_t on_forward = -1;
    std::int32_t on_backward = -1;
};

// The path through a meeting: forward from the start state to the forward
// node, then from the backward node back to the goal, each backward move
// undone - the turns of one face 1, 2, 3 invert to 3, 2, 1.
std::vector<std::uint8_t> join_paths(const Frontier& forward, const Frontier& backward,
                                     const Meeting& meeting) {
    std::vector<std::uint8_t> path;
    for (std::int32_t i = meeting.on_forward; forward.parent.at(static_cast<std::size_t>(i)) >= 0;
         i = forward.parent.at(static_cast<std::size_t>(i))) {
        path.push_back(forward.move.at(static_cast<std::size_t>(i)));
    }
    std::ranges::reverse(path);
    for (std::int32_t i = meeting.on_backward; backward.parent.at(static_cast<std::size_t>(i)) >= 0;
         i = backward.parent.at(static_cast<std::size_t>(i))) {
        const std::uint8_t m = backward.move.at(static_cast<std::size_t>(i));
        path.push_back(static_cast<std::uint8_t>((m / 3 * 3) + (2 - (m % 3))));
    }
    return path;
}

// What expanding one layer of a frontier came to.
enum class Expansion : std::uint8_t { grown, met, over_limit };
struct LayerResult {
    Expansion outcome = Expansion::grown;
    std::int32_t mine = -1;    // the node just added, when the frontiers met
    std::int32_t theirs = -1;  // the other frontier's node it equals
};

// Expands the layer of `mine` that has not been expanded yet. Stops at the
// first state `theirs` already holds, or when the two frontiers together
// pass `node_limit`.
LayerResult expand_layer(const PieceTransitions& transitions, std::span<const TrackedPiece> tracked,
                         std::span<const std::uint8_t> moves, Frontier& mine,
                         const Frontier& theirs, std::size_t node_limit) {
    const std::size_t begin = mine.layer_start;
    const std::size_t end = mine.state.size();
    mine.layer_start = end;
    for (std::size_t i = begin; i < end; ++i) {
        for (const std::uint8_t m : moves) {
            const std::uint64_t next =
                step(transitions, tracked, {.state = mine.state.at(i), .move = m});
            if (mine.index.contains(next)) {
                continue;
            }
            const std::int32_t added =
                mine.add(next, {.from = static_cast<std::int32_t>(i), .move = m});
            const auto meet = theirs.index.find(next);
            if (meet != theirs.index.end()) {
                return {.outcome = Expansion::met, .mine = added, .theirs = meet->second};
            }
        }
        if (mine.state.size() + theirs.state.size() > node_limit) {
            return {.outcome = Expansion::over_limit};
        }
    }
    return {};
}

// Move indices of the shortest sequence, over the faces in `face_mask` (bit
// n = face n, in Face order), that puts every tracked piece home - or
// nullopt if there is none within `max_side` moves from each end or the
// search grows past `node_limit` states. The empty sequence means they
// already are.
// How a search may go: which faces it may turn (bit n = face n), how many
// moves from each end, and how many states it may hold.
struct SearchBounds {
    unsigned face_mask = 0;
    int max_side = 0;
    std::size_t node_limit = 0;
};

std::optional<std::vector<std::uint8_t>> shortest_sequence(const CubieCube& cube,
                                                           std::span<const TrackedPiece> tracked,
                                                           const SearchBounds& bounds) {
    const auto [face_mask, max_side, node_limit] = bounds;
    const std::uint64_t start = pack_state(cube, tracked);
    const std::uint64_t goal = solved_state(tracked);
    if (start == goal) {
        return std::vector<std::uint8_t>{};
    }

    const std::vector<std::uint8_t> moves = allowed_moves(face_mask);
    Frontier forward;
    Frontier backward;
    forward.add(start, {.from = -1, .move = 0});
    backward.add(goal, {.from = -1, .move = 0});
    const detail::SearchWorkScope work(
        [&forward, &backward] { return forward.state.size() + backward.state.size(); });

    while (forward.depth < max_side || backward.depth < max_side) {
        // Grow whichever side is smaller; that keeps both near the square
        // root of the whole search.
        const std::size_t forward_width = forward.state.size() - forward.layer_start;
        const std::size_t backward_width = backward.state.size() - backward.layer_start;
        const bool grow_forward = forward.depth < max_side &&
                                  (backward.depth >= max_side || forward_width <= backward_width);
        Frontier& mine = grow_forward ? forward : backward;
        const Frontier& theirs = grow_forward ? backward : forward;

        const LayerResult layer =
            expand_layer(kTransitions, tracked, moves, mine, theirs, node_limit);
        if (layer.outcome == Expansion::over_limit) {
            return std::nullopt;
        }
        if (layer.outcome == Expansion::met) {
            const Meeting meeting =
                grow_forward ? Meeting{.on_forward = layer.mine, .on_backward = layer.theirs}
                             : Meeting{.on_forward = layer.theirs, .on_backward = layer.mine};
            return join_paths(forward, backward, meeting);
        }
        ++mine.depth;
    }
    return std::nullopt;
}

CubieCube apply_moves(CubieCube cube, std::span<const std::uint8_t> moves) noexcept {
    for (const std::uint8_t m : moves) {
        cube = detail::multiply(cube, detail::move_cubie(m));
    }
    return cube;
}

// Face order is U D L R F B; these are bit positions in a face mask.
constexpr unsigned face_bit(Face face) noexcept {
    return 1U << static_cast<unsigned>(face);
}
constexpr unsigned kAllButBottom = face_bit(Face::U) | face_bit(Face::L) | face_bit(Face::R) |
                                   face_bit(Face::F) | face_bit(Face::B);
constexpr unsigned kAllFacesMask = kAllButBottom | face_bit(Face::D);

// ---------------------------------------------------------------------------
// Cross
// ---------------------------------------------------------------------------

// The four cross edges are DR, DF, DL, DB (pieces 4..7) and the white face
// is the bottom, as for the beginner method.
constexpr std::array<TrackedPiece, 4> kCrossPieces = {
    {
        {.corner = false, .id = detail::kDR},
        {.corner = false, .id = detail::kDF},
        {.corner = false, .id = detail::kDL},
        {.corner = false, .id = detail::kDB},
    },
};

// ---------------------------------------------------------------------------
// F2L
// ---------------------------------------------------------------------------

constexpr std::size_t kSlotCount = 4;

// A first-two-layers slot: the D-layer corner and middle-layer edge that
// belong together, and the two sides that meet there. Slot s holds corner
// 4 + s (DFR, DLF, DBL, DRB) and edge 8 + s (FR, FL, BL, BR).
struct Slot {
    std::array<Face, 2> sides;
};
constexpr std::array<Slot, kSlotCount> kSlots = {
    {
        {{Face::F, Face::R}},
        {{Face::F, Face::L}},
        {{Face::B, Face::L}},
        {{Face::B, Face::R}},
    },
};

constexpr std::uint8_t slot_corner(std::size_t slot) noexcept {
    return static_cast<std::uint8_t>(4 + slot);
}
constexpr std::uint8_t slot_edge(std::size_t slot) noexcept {
    return static_cast<std::uint8_t>(8 + slot);
}

bool slot_solved(const CubieCube& cube, std::size_t slot) noexcept {
    const std::size_t corner = slot_corner(slot);
    const std::size_t edge = slot_edge(slot);
    return cube.cp.at(corner) == corner && cube.co.at(corner) == 0 && cube.ep.at(edge) == edge &&
           cube.eo.at(edge) == 0;
}

// The slot a D-layer corner position (4..7) or a middle-layer edge position
// (8..11) belongs to, or kSlotCount for a U-layer position.
std::size_t corner_position_slot(std::size_t position) noexcept {
    return position >= 4 ? position - 4 : kSlotCount;
}
std::size_t edge_position_slot(std::size_t position) noexcept {
    return position >= 8 ? position - 8 : kSlotCount;
}

unsigned sides_mask(std::size_t slot) noexcept {
    return face_bit(kSlots.at(slot).sides.at(0)) | face_bit(kSlots.at(slot).sides.at(1));
}

struct PairPlan {
    std::size_t slot = kSlotCount;
    std::vector<std::uint8_t> moves;
};

// The shortest way to finish `slot`'s pair that leaves the cross and every
// slot in `done` as it is, within `bounds`.
std::optional<std::vector<std::uint8_t>> plan_pair(const CubieCube& cube, std::size_t slot,
                                                   const std::array<bool, kSlotCount>& done,
                                                   const SearchBounds& bounds) {
    std::array<TrackedPiece, kMaxTracked> tracked{};
    std::size_t count = 0;
    for (const TrackedPiece& piece : kCrossPieces) {
        tracked.at(count++) = piece;
    }
    for (std::size_t s = 0; s < kSlotCount; ++s) {
        if (done.at(s)) {
            tracked.at(count++) = {.corner = true, .id = slot_corner(s)};
            tracked.at(count++) = {.corner = false, .id = slot_edge(s)};
        }
    }
    tracked.at(count++) = {.corner = true, .id = slot_corner(slot)};
    tracked.at(count++) = {.corner = false, .id = slot_edge(slot)};
    return shortest_sequence(cube, std::span<const TrackedPiece>(tracked.data(), count), bounds);
}

// The faces a person would turn for this pair: the top, the pair's own slot,
// and any other slot one of its pieces is currently stuck in (that is where
// it has to be taken out of).
unsigned natural_faces(const CubieCube& cube, std::size_t slot) noexcept {
    unsigned mask = face_bit(Face::U) | sides_mask(slot);
    for (std::size_t pos = 0; pos < detail::kCornerCount; ++pos) {
        if (cube.cp.at(pos) == slot_corner(slot) && corner_position_slot(pos) < kSlotCount) {
            mask |= sides_mask(corner_position_slot(pos));
        }
    }
    for (std::size_t pos = 0; pos < detail::kEdgeCount; ++pos) {
        if (cube.ep.at(pos) == slot_edge(slot) && edge_position_slot(pos) < kSlotCount) {
            mask |= sides_mask(edge_position_slot(pos));
        }
    }
    return mask;
}

// Plans every unsolved slot's pair and keeps the cheapest in `best`, with the
// faces a person would use (`widened` false) or every side but the bottom.
void consider_pairs(const CubieCube& cube, const std::array<bool, kSlotCount>& done, bool widened,
                    PairPlan& best) {
    for (std::size_t slot = 0; slot < kSlotCount; ++slot) {
        if (done.at(slot)) {
            continue;
        }
        const SearchBounds bounds{
            .face_mask = widened ? kAllButBottom : natural_faces(cube, slot),
            .max_side = widened ? 8 : 7,
            .node_limit = widened ? 6'000'000U : 2'500'000U,
        };
        std::optional<std::vector<std::uint8_t>> moves = plan_pair(cube, slot, done, bounds);
        if (moves.has_value() && (best.slot == kSlotCount || moves->size() < best.moves.size())) {
            best.slot = slot;
            best.moves = std::move(*moves);
        }
    }
}

// The pair that is cheapest right now. First with the faces a person would
// use; only if none of them can be done that way, with every side but the
// bottom. `slot` is kSlotCount when no pair could be planned at all.
PairPlan cheapest_pair(const CubieCube& cube, const std::array<bool, kSlotCount>& done) {
    PairPlan best;
    for (const bool widened : {false, true}) {
        consider_pairs(cube, done, widened, best);
        if (best.slot != kSlotCount) {
            break;
        }
    }
    return best;
}

// Marks every slot that is solved now and returns how many are still not.
std::size_t refresh_done(const CubieCube& cube, std::array<bool, kSlotCount>& done) noexcept {
    std::size_t remaining = 0;
    for (std::size_t s = 0; s < kSlotCount; ++s) {
        done.at(s) = done.at(s) || slot_solved(cube, s);
        if (!done.at(s)) {
            ++remaining;
        }
    }
    return remaining;
}

// Builds the first two layers on top of a solved cross, appending to `out`.
// Returns false only if no pair could be planned at all - which the wider
// retry in cheapest_pair makes a bug, not a state.
bool solve_f2l(CubieCube& cube, std::vector<std::uint8_t>& out) {
    std::array<bool, kSlotCount> done{};
    while (refresh_done(cube, done) > 0) {
        const PairPlan best = cheapest_pair(cube, done);
        if (best.slot == kSlotCount) {
            return false;
        }
        cube = apply_moves(cube, best.moves);
        out.insert(out.end(), best.moves.begin(), best.moves.end());
    }
    return true;
}

std::vector<Move> to_moves(std::span<const std::uint8_t> indices) {
    std::vector<Move> moves;
    moves.reserve(indices.size());
    for (const std::uint8_t i : indices) {
        moves.push_back(detail::move_from_index(i));
    }
    return moves;
}

}  // namespace

namespace detail {

Result<Solution> solve_cfop_stages(const Cube& scrambled) {
    if (!scrambled.is_physically_valid()) {
        return std::unexpected(CubeError::invalid_cube_state);
    }
    const std::optional<CubieCube> start = to_cubie(scrambled);
    if (!start.has_value()) {
        return std::unexpected(CubeError::unsolvable_state);
    }

    CubieCube cube = *start;
    Solution solution;

    // --- Cross: the optimum, over every face.
    std::optional<std::vector<std::uint8_t>> cross = shortest_sequence(
        cube, kCrossPieces, {.face_mask = kAllFacesMask, .max_side = 5, .node_limit = 4'000'000});
    if (!cross.has_value()) {
        return std::unexpected(CubeError::no_solution);
    }
    cube = apply_moves(cube, *cross);
    solution.steps.push_back(SolveStep{.stage = Stage::Cross, .moves = to_moves(*cross)});

    // --- F2L: the four pairs.
    std::vector<std::uint8_t> f2l;
    if (!solve_f2l(cube, f2l)) {
        return std::unexpected(CubeError::no_solution);
    }
    solution.steps.push_back(SolveStep{.stage = Stage::F2l, .moves = to_moves(f2l)});

    // --- OLL, then PLL: one table entry each. An entry is missing only for
    // a case a valid cube cannot reach (or a malformed table), and applying
    // nothing in its place would hand back a wrong solution.
    const std::optional<std::span<const Move>> oll = oll_algorithm(cube);
    if (!oll.has_value()) {
        return std::unexpected(CubeError::no_solution);
    }
    for (const Move& move : *oll) {
        cube = multiply(cube, move_cubie(move_index(move)));
    }
    solution.steps.push_back(
        SolveStep{.stage = Stage::Oll, .moves = std::vector<Move>(oll->begin(), oll->end())});

    const std::optional<std::span<const Move>> pll = pll_algorithm(cube);
    if (!pll.has_value()) {
        return std::unexpected(CubeError::no_solution);
    }
    for (const Move& move : *pll) {
        cube = multiply(cube, move_cubie(move_index(move)));
    }
    solution.steps.push_back(
        SolveStep{.stage = Stage::Pll, .moves = std::vector<Move>(pll->begin(), pll->end())});

    // Verified against the facelet model, the one the rest of the app
    // trusts, before it is handed out.
    Cube check = scrambled;
    for (const SolveStep& step : solution.steps) {
        for (const Move& move : step.moves) {
            check = check.apply(move);
        }
    }
    if (!check.is_solved()) {
        return std::unexpected(CubeError::no_solution);
    }
    return solution;
}

}  // namespace detail

Result<Solution> solve_cfop(const Cube& scrambled) {
    CUBE_ZONE_SCOPED_N("solver.cfop");
    Result<Solution> stages = detail::solve_cfop_stages(scrambled);
    if (!stages.has_value()) {
        return std::unexpected(stages.error());
    }
    return merge_adjacent_moves(std::move(*stages));
}

}  // namespace cube::solver
