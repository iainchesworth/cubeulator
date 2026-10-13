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
            const std::size_t from = move.cp[q];
            for (std::size_t o = 0; o < 3; ++o) {
                t.corner[m][from * 3 + o] = static_cast<std::uint8_t>(q * 3 + (o + move.co[q]) % 3);
            }
        }
        for (std::size_t q = 0; q < detail::kEdgeCount; ++q) {
            const std::size_t from = move.ep[q];
            for (std::size_t o = 0; o < 2; ++o) {
                t.edge[m][from * 2 + o] = static_cast<std::uint8_t>(q * 2 + (o + move.eo[q]) % 2);
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
    for (std::size_t i = 0; i < tracked.size(); ++i) {
        std::uint64_t code = 0;
        if (tracked[i].corner) {
            for (std::size_t pos = 0; pos < detail::kCornerCount; ++pos) {
                if (cube.cp[pos] == tracked[i].id) {
                    code = pos * 3 + cube.co[pos];
                }
            }
        } else {
            for (std::size_t pos = 0; pos < detail::kEdgeCount; ++pos) {
                if (cube.ep[pos] == tracked[i].id) {
                    code = pos * 2 + cube.eo[pos];
                }
            }
        }
        state |= code << (5 * i);
    }
    return state;
}

// Every tracked piece at home and unturned.
std::uint64_t solved_state(std::span<const TrackedPiece> tracked) noexcept {
    std::uint64_t state = 0;
    for (std::size_t i = 0; i < tracked.size(); ++i) {
        const std::uint64_t code = tracked[i].corner ? tracked[i].id * 3u : tracked[i].id * 2u;
        state |= code << (5 * i);
    }
    return state;
}

std::uint64_t step(const PieceTransitions& t, std::span<const TrackedPiece> tracked,
                   std::uint64_t state, std::size_t move) noexcept {
    std::uint64_t next = 0;
    for (std::size_t i = 0; i < tracked.size(); ++i) {
        const std::size_t code = (state >> (5 * i)) & 31u;
        const std::uint64_t moved = tracked[i].corner ? t.corner[move][code] : t.edge[move][code];
        next |= moved << (5 * i);
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

    std::int32_t add(std::uint64_t s, std::int32_t from, std::uint8_t m) {
        const auto i = static_cast<std::int32_t>(state.size());
        index.emplace(s, i);
        state.push_back(s);
        parent.push_back(from);
        move.push_back(m);
        return i;
    }
};

// Move indices of the shortest sequence, over the faces in `face_mask` (bit
// n = face n, in Face order), that puts every tracked piece home - or
// nullopt if there is none within `max_side` moves from each end or the
// search grows past `node_limit` states. The empty sequence means they
// already are.
std::optional<std::vector<std::uint8_t>> shortest_sequence(const CubieCube& cube,
                                                           std::span<const TrackedPiece> tracked,
                                                           unsigned face_mask, int max_side,
                                                           std::size_t node_limit) {
    const PieceTransitions& transitions = kTransitions;
    const std::uint64_t start = pack_state(cube, tracked);
    const std::uint64_t goal = solved_state(tracked);
    if (start == goal) {
        return std::vector<std::uint8_t>{};
    }

    std::vector<std::uint8_t> moves;
    for (std::size_t m = 0; m < kMoveCount; ++m) {
        if (((face_mask >> (m / 3)) & 1u) != 0) {
            moves.push_back(static_cast<std::uint8_t>(m));
        }
    }

    Frontier forward;
    Frontier backward;
    forward.add(start, -1, 0);
    backward.add(goal, -1, 0);

    // Forward from the meeting state to `start`'s end is the forward path;
    // from the meeting state back to the goal, each backward move is undone:
    // the turns of one face 1, 2, 3 invert to 3, 2, 1.
    auto join = [&](std::int32_t f, std::int32_t b) {
        std::vector<std::uint8_t> path;
        for (std::int32_t i = f; forward.parent[static_cast<std::size_t>(i)] >= 0;
             i = forward.parent[static_cast<std::size_t>(i)]) {
            path.push_back(forward.move[static_cast<std::size_t>(i)]);
        }
        std::ranges::reverse(path);
        for (std::int32_t i = b; backward.parent[static_cast<std::size_t>(i)] >= 0;
             i = backward.parent[static_cast<std::size_t>(i)]) {
            const std::uint8_t m = backward.move[static_cast<std::size_t>(i)];
            path.push_back(static_cast<std::uint8_t>(m / 3 * 3 + (2 - m % 3)));
        }
        return path;
    };

    while (forward.depth < max_side || backward.depth < max_side) {
        // Grow whichever side is smaller; that keeps both near the square
        // root of the whole search.
        const std::size_t forward_width = forward.state.size() - forward.layer_start;
        const std::size_t backward_width = backward.state.size() - backward.layer_start;
        const bool grow_forward = forward.depth < max_side &&
                                  (backward.depth >= max_side || forward_width <= backward_width);
        Frontier& mine = grow_forward ? forward : backward;
        Frontier& theirs = grow_forward ? backward : forward;

        const std::size_t begin = mine.layer_start;
        const std::size_t end = mine.state.size();
        mine.layer_start = end;
        for (std::size_t i = begin; i < end; ++i) {
            for (const std::uint8_t m : moves) {
                const std::uint64_t next = step(transitions, tracked, mine.state[i], m);
                if (mine.index.contains(next)) {
                    continue;
                }
                const std::int32_t added = mine.add(next, static_cast<std::int32_t>(i), m);
                const auto meet = theirs.index.find(next);
                if (meet != theirs.index.end()) {
                    return grow_forward ? join(added, meet->second) : join(meet->second, added);
                }
            }
            if (forward.state.size() + backward.state.size() > node_limit) {
                return std::nullopt;
            }
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
    return 1u << static_cast<unsigned>(face);
}
constexpr unsigned kAllButBottom = face_bit(Face::U) | face_bit(Face::L) | face_bit(Face::R) |
                                   face_bit(Face::F) | face_bit(Face::B);
constexpr unsigned kAllFacesMask = kAllButBottom | face_bit(Face::D);

// ---------------------------------------------------------------------------
// Cross
// ---------------------------------------------------------------------------

// The four cross edges are DR, DF, DL, DB (pieces 4..7) and the white face
// is the bottom, as for the beginner method.
constexpr std::array<TrackedPiece, 4> kCrossPieces = {{
    {false, detail::kDR},
    {false, detail::kDF},
    {false, detail::kDL},
    {false, detail::kDB},
}};

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
constexpr std::array<Slot, kSlotCount> kSlots = {{
    {{Face::F, Face::R}},
    {{Face::F, Face::L}},
    {{Face::B, Face::L}},
    {{Face::B, Face::R}},
}};

constexpr std::uint8_t slot_corner(std::size_t slot) noexcept {
    return static_cast<std::uint8_t>(4 + slot);
}
constexpr std::uint8_t slot_edge(std::size_t slot) noexcept {
    return static_cast<std::uint8_t>(8 + slot);
}

bool slot_solved(const CubieCube& cube, std::size_t slot) noexcept {
    const std::size_t corner = slot_corner(slot);
    const std::size_t edge = slot_edge(slot);
    return cube.cp[corner] == corner && cube.co[corner] == 0 && cube.ep[edge] == edge &&
           cube.eo[edge] == 0;
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
    return face_bit(kSlots[slot].sides[0]) | face_bit(kSlots[slot].sides[1]);
}

struct PairPlan {
    std::size_t slot = kSlotCount;
    std::vector<std::uint8_t> moves;
};

// The shortest way to finish `slot`'s pair that leaves the cross and every
// slot in `done` as it is. `mask` is the faces it may turn.
std::optional<std::vector<std::uint8_t>> plan_pair(const CubieCube& cube, std::size_t slot,
                                                   const std::array<bool, kSlotCount>& done,
                                                   unsigned mask, int max_side,
                                                   std::size_t node_limit) {
    std::array<TrackedPiece, kMaxTracked> tracked{};
    std::size_t count = 0;
    for (const TrackedPiece& piece : kCrossPieces) {
        tracked[count++] = piece;
    }
    for (std::size_t s = 0; s < kSlotCount; ++s) {
        if (done[s]) {
            tracked[count++] = {true, slot_corner(s)};
            tracked[count++] = {false, slot_edge(s)};
        }
    }
    tracked[count++] = {true, slot_corner(slot)};
    tracked[count++] = {false, slot_edge(slot)};
    return shortest_sequence(cube, std::span<const TrackedPiece>(tracked.data(), count), mask,
                             max_side, node_limit);
}

// The faces a person would turn for this pair: the top, the pair's own slot,
// and any other slot one of its pieces is currently stuck in (that is where
// it has to be taken out of).
unsigned natural_faces(const CubieCube& cube, std::size_t slot) noexcept {
    unsigned mask = face_bit(Face::U) | sides_mask(slot);
    for (std::size_t pos = 0; pos < detail::kCornerCount; ++pos) {
        if (cube.cp[pos] == slot_corner(slot) && corner_position_slot(pos) < kSlotCount) {
            mask |= sides_mask(corner_position_slot(pos));
        }
    }
    for (std::size_t pos = 0; pos < detail::kEdgeCount; ++pos) {
        if (cube.ep[pos] == slot_edge(slot) && edge_position_slot(pos) < kSlotCount) {
            mask |= sides_mask(edge_position_slot(pos));
        }
    }
    return mask;
}

// Builds the first two layers on top of a solved cross, appending to `out`.
// Returns false only if no pair could be planned at all - which the wider
// retry below makes a bug, not a state.
bool solve_f2l(CubieCube& cube, std::vector<std::uint8_t>& out) {
    std::array<bool, kSlotCount> done{};
    std::size_t remaining = 0;
    for (std::size_t s = 0; s < kSlotCount; ++s) {
        done[s] = slot_solved(cube, s);
        if (!done[s]) {
            ++remaining;
        }
    }

    while (remaining > 0) {
        PairPlan best;
        // Take the pair that is cheapest right now. First with the faces a
        // person would use; only if none of them can be done that way, with
        // every side but the bottom.
        for (const bool widened : {false, true}) {
            for (std::size_t slot = 0; slot < kSlotCount; ++slot) {
                if (done[slot]) {
                    continue;
                }
                const unsigned mask = widened ? kAllButBottom : natural_faces(cube, slot);
                const int max_side = widened ? 8 : 7;
                const std::size_t node_limit = widened ? 6'000'000 : 2'500'000;
                std::optional<std::vector<std::uint8_t>> moves =
                    plan_pair(cube, slot, done, mask, max_side, node_limit);
                if (moves.has_value() &&
                    (best.slot == kSlotCount || moves->size() < best.moves.size())) {
                    best.slot = slot;
                    best.moves = std::move(*moves);
                }
            }
            if (best.slot != kSlotCount) {
                break;
            }
        }
        if (best.slot == kSlotCount) {
            return false;
        }

        cube = apply_moves(cube, best.moves);
        out.insert(out.end(), best.moves.begin(), best.moves.end());
        for (std::size_t s = 0; s < kSlotCount; ++s) {
            if (!done[s] && slot_solved(cube, s)) {
                done[s] = true;
                --remaining;
            }
        }
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
    std::optional<std::vector<std::uint8_t>> cross =
        shortest_sequence(cube, kCrossPieces, kAllFacesMask, 5, 4'000'000);
    if (!cross.has_value()) {
        return std::unexpected(CubeError::no_solution);
    }
    cube = apply_moves(cube, *cross);
    solution.steps.push_back(SolveStep{Stage::Cross, to_moves(*cross)});

    // --- F2L: the four pairs.
    std::vector<std::uint8_t> f2l;
    if (!solve_f2l(cube, f2l)) {
        return std::unexpected(CubeError::no_solution);
    }
    solution.steps.push_back(SolveStep{Stage::F2l, to_moves(f2l)});

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
    solution.steps.push_back(SolveStep{Stage::Oll, std::vector<Move>(oll->begin(), oll->end())});

    const std::optional<std::span<const Move>> pll = pll_algorithm(cube);
    if (!pll.has_value()) {
        return std::unexpected(CubeError::no_solution);
    }
    for (const Move& move : *pll) {
        cube = multiply(cube, move_cubie(move_index(move)));
    }
    solution.steps.push_back(SolveStep{Stage::Pll, std::vector<Move>(pll->begin(), pll->end())});

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
    Result<Solution> stages = detail::solve_cfop_stages(scrambled);
    if (!stages.has_value()) {
        return std::unexpected(stages.error());
    }
    return merge_adjacent_moves(std::move(*stages));
}

}  // namespace cube::solver
