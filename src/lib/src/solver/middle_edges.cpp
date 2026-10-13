#include <cube/solver/middle_edges.hpp>

#include <array>

#include <cube/solver/corners.hpp>
#include <cube/solver/cross.hpp>

#include "bfs_search.hpp"

namespace cube::solver {

namespace {

struct EdgeSlot {
    Face side_a;
    int side_a_index;
    Face side_b;
    int side_b_index;
};

// Each side face's equatorial (y=0) edge index is (row=1)*3+col - see
// cube.cpp's layout derivation.
constexpr std::array<EdgeSlot, 4> kEdgeSlots = {{
    {Face::F, 5, Face::R, 5},  // FR
    {Face::F, 3, Face::L, 5},  // FL
    {Face::B, 5, Face::R, 3},  // BR
    {Face::B, 3, Face::L, 3},  // BL
}};

bool slot_solved(const Cube& cube, const EdgeSlot& slot) noexcept {
    return cube.at(slot.side_a, slot.side_a_index) == canonical_color(slot.side_a) &&
           cube.at(slot.side_b, slot.side_b_index) == canonical_color(slot.side_b);
}

// Every first-layer corner's code, in a fixed order - present in every
// middle-edge search's signature since the first layer must stay intact
// throughout, exactly like the cross does.
std::uint64_t first_layer_corners_key(const Cube& cube) noexcept {
    static constexpr std::array<std::pair<Face, Face>, 4> kCornerSides = {{
        {Face::F, Face::L},
        {Face::F, Face::R},
        {Face::B, Face::L},
        {Face::B, Face::R},
    }};
    std::uint64_t key = 0;
    for (const auto& [side_a, side_b] : kCornerSides) {
        key = detail::push_code(
            key, detail::piece_code_corner(cube, Color::White, canonical_color(side_a),
                                           canonical_color(side_b)));
    }
    return key;
}

}  // namespace

bool f2l_solved(const Cube& cube) noexcept {
    if (!first_layer_solved(cube)) {
        return false;
    }
    for (const EdgeSlot& slot : kEdgeSlots) {
        if (!slot_solved(cube, slot)) {
            return false;
        }
    }
    return true;
}

Result<std::vector<Move>> solve_middle_edges(const Cube& start) {
    std::vector<Move> solution;
    Cube cube = start;

    auto apply_step = [&](const std::vector<Move>& step) {
        for (const Move& m : step) {
            cube = cube.apply(m);
            solution.push_back(m);
        }
    };

    for (std::size_t i = 0; i < kEdgeSlots.size(); ++i) {
        const EdgeSlot& target = kEdgeSlots[i];
        const Color color_a = canonical_color(target.side_a);
        const Color color_b = canonical_color(target.side_b);

        auto placed_so_far = [&](const Cube& c) {
            if (!first_layer_solved(c)) {
                return false;
            }
            for (std::size_t j = 0; j < i; ++j) {
                if (!slot_solved(c, kEdgeSlots[j])) {
                    return false;
                }
            }
            return true;
        };
        // Folds cross + first-layer corners + already-placed edges (always
        // - they must stay intact) plus, optionally, the current target
        // edge's own position, into a search signature.
        auto make_signature = [&](bool track_target) {
            return [&, track_target](const Cube& c) {
                std::uint64_t rest = first_layer_corners_key(c);
                for (std::size_t j = 0; j < i; ++j) {
                    const EdgeSlot& s = kEdgeSlots[j];
                    rest = detail::push_code(rest,
                                             detail::piece_code_edge(c, canonical_color(s.side_a),
                                                                     canonical_color(s.side_b)));
                }
                if (track_target) {
                    rest = detail::push_code(rest, detail::piece_code_edge(c, color_a, color_b));
                }
                return detail::SignatureKey{detail::cross_key(c), rest};
            };
        };

        // Unlike corners (where a D turn can cycle a misplaced piece
        // through all 4 D-slots), there's no single whole-layer turn that
        // cycles among the 4 middle slots. A single search allowing every
        // side face at once (so any starting slot is reachable) blew the
        // state space up far enough to exhaust memory; splitting into two
        // narrower passes - eject the piece to U using only its CURRENT
        // slot's own two sides, then insert using only the TARGET slot's
        // own two sides - keeps each pass's branching down to 3 faces and
        // both proved fast in testing. Ejection is needed even when the
        // piece is already sitting at the target slot: a piece correctly
        // POSITIONED but wrongly ORIENTED still needs the eject-then-insert
        // round trip to fix its orientation, exactly like a piece in the
        // wrong slot entirely - skipping ejection for "already at target
        // slot" was tried first and left flipped-in-place pieces
        // unfixable by the insert phase alone.
        const Cube::EdgeLocation loc = cube.locate_edge(color_a, color_b);
        const bool in_u_layer = loc.face_a == Face::U || loc.face_b == Face::U;

        if (!slot_solved(cube, target) && !in_u_layer) {
            const std::vector<Face> eject_allowed = {Face::U, loc.face_a, loc.face_b};
            auto eject_postcondition = [&](const Cube& c) {
                if (!placed_so_far(c)) {
                    return false;
                }
                const Cube::EdgeLocation l = c.locate_edge(color_a, color_b);
                return l.face_a == Face::U || l.face_b == Face::U;
            };
            Result<std::vector<Move>> eject_step = detail::bfs_search(
                cube, eject_allowed, eject_postcondition, make_signature(true), 8);
            if (!eject_step.has_value()) {
                return std::unexpected(eject_step.error());
            }
            apply_step(*eject_step);
        }

        auto insert_postcondition = [&](const Cube& c) {
            return placed_so_far(c) && slot_solved(c, target);
        };
        const std::vector<Face> insert_allowed = {Face::U, target.side_a, target.side_b};
        Result<std::vector<Move>> insert_step = detail::bfs_search(
            cube, insert_allowed, insert_postcondition, make_signature(true), 10);
        if (insert_step.has_value()) {
            apply_step(*insert_step);
            continue;
        }

        // Rare fallback, kept as a safety net rather than the default path:
        // retry with every face (including D) available at a slightly
        // larger depth/budget. Not expected to trigger now that ejection
        // always runs first (see above), but a defensive net is cheap and
        // this is off the hot path.
        const std::vector<Face> wide_allowed(kAllFaces.begin(), kAllFaces.end());
        Result<std::vector<Move>> wide_step = detail::bfs_search(
            cube, wide_allowed, insert_postcondition, make_signature(true), 10, 4'000'000);
        if (!wide_step.has_value()) {
            return std::unexpected(wide_step.error());
        }
        apply_step(*wide_step);
    }

    return solution;
}

}  // namespace cube::solver
