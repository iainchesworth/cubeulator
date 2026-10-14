#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include <cube/error.hpp>
#include <cube/profiling.hpp>
#include <cube/solver/cube.hpp>

#include "bfs_search.hpp"

namespace cube::solver::detail {

// What a piece search is looking for, in the terms of the signature it runs
// on (see SignatureKey): the first-two-layers stages ask for "these pieces
// are home", which is a short list of keys, not a predicate over a cube.
struct PieceGoal {
    // How many 6-bit piece codes SignatureKey::rest holds, counted from the
    // low bits (push_code puts the newest there). SignatureKey::cross always
    // holds four.
    std::size_t rest_codes = 0;
    // Every key a cube satisfying the stage's postcondition can have.
    std::vector<SignatureKey> keys;
};

namespace piece_search_detail {

// Where each of `count` packed 6-bit facelet codes goes under `map`.
[[nodiscard]] inline std::uint64_t map_codes(std::uint64_t packed, std::size_t count,
                                             const Cube::FaceletMap& map) noexcept {
    std::uint64_t out = 0;
    for (std::size_t i = 0; i < count; ++i) {
        const unsigned shift = static_cast<unsigned>(6 * i);
        out |= std::uint64_t{map.at((packed >> shift) & 63U)} << shift;
    }
    return out;
}

[[nodiscard]] inline std::uint64_t hash_of(const SignatureKey& key) noexcept {
    std::uint64_t h = key.cross * 0x9E3779B97F4A7C15ULL;
    h ^= key.rest + 0xC2B2AE3D27D4EB4FULL + (h << 6) + (h >> 2);
    h ^= h >> 33;
    h *= 0xFF51AFD7ED558CCDULL;
    h ^= h >> 33;
    return h;
}

struct Node {
    SignatureKey key;
    std::int32_t parent;
    std::uint8_t face;
    std::uint8_t turns;
    std::uint8_t depth;
};

// Nodes live in one arena, in discovery order; the table is an open-addressed
// index over it (slot = arena index + 1, 0 = empty) kept at most half full.
// One allocation that grows by doubling, where the search this replaces made
// one per visited signature.
class Arena {
public:
    Arena() : slots_(1U << 12, 0U) {}

    [[nodiscard]] std::size_t size() const noexcept { return nodes_.size(); }
    [[nodiscard]] const Node& at(std::size_t i) const { return nodes_.at(i); }

    // Appends the node unless its key is already present. True if appended.
    bool insert(const Node& node) {
        if ((nodes_.size() + 1) * 2 > slots_.size()) {
            grow();
        }
        const std::size_t mask = slots_.size() - 1;
        for (std::size_t slot = hash_of(node.key) & mask;; slot = (slot + 1) & mask) {
            const std::uint32_t held = slots_.at(slot);
            if (held == 0U) {
                nodes_.push_back(node);
                slots_.at(slot) = static_cast<std::uint32_t>(nodes_.size());
                return true;
            }
            if (nodes_.at(held - 1).key == node.key) {
                return false;
            }
        }
    }

private:
    void grow() {
        slots_.assign(slots_.size() * 2, 0U);
        const std::size_t mask = slots_.size() - 1;
        for (std::size_t i = 0; i < nodes_.size(); ++i) {
            std::size_t slot = hash_of(nodes_.at(i).key) & mask;
            while (slots_.at(slot) != 0U) {
                slot = (slot + 1) & mask;
            }
            slots_.at(slot) = static_cast<std::uint32_t>(i + 1);
        }
    }

    std::vector<Node> nodes_;
    std::vector<std::uint32_t> slots_;
};

}  // namespace piece_search_detail

// bfs_search over piece positions instead of cubes.
//
// The signature a first-two-layers search dedups on is, by construction, the
// position and orientation of a handful of pieces: one 6-bit code each, the
// facelet that shows the piece's first colour. A turn moves each of those
// stickers by a fixed table, and the stage's postcondition is "every one of
// them is home" - so the search needs no cube at all. Turning a node is a
// handful of table lookups on two words instead of a 54-sticker copy and
// twenty locate_* scans, and the visited set is one flat array.
//
// It is the same search: nodes are expanded in the same FIFO order, with the
// same face and turn order, deduplicated on the same key, and the first
// postcondition hit is returned, so it returns the move sequence bfs_search
// returns and visits the same number of signatures. That holds exactly when
// the colour pairs and triples the signature looks up name one piece each -
// a real cube; `search` below sends anything else to bfs_search.
[[nodiscard]] inline Result<std::vector<Move>> piece_search(const SignatureKey& start,
                                                            const std::vector<Face>& allowed_faces,
                                                            const PieceGoal& goal, int max_depth,
                                                            std::size_t max_visited_nodes) {
    CUBE_ZONE_SCOPED_N("solver.piece_search");
    namespace psd = piece_search_detail;

    struct TurnOf {
        Face face;
        std::uint8_t turns;
        const Cube::FaceletMap* map;
    };
    std::vector<TurnOf> turns;
    turns.reserve(allowed_faces.size() * 3);
    for (const Face face : allowed_faces) {
        for (std::uint8_t quarter = 1; quarter <= 3; ++quarter) {
            turns.push_back(TurnOf{face, quarter, &Cube::facelet_map(Move{face, quarter})});
        }
    }

    psd::Arena arena;
    arena.insert(psd::Node{start, -1, 0, 0, 0});

    const auto reconstruct = [&arena](std::size_t leaf, const TurnOf& last) {
        std::vector<Move> path;
        for (auto i = static_cast<std::int32_t>(leaf); i > 0;
             i = arena.at(static_cast<std::size_t>(i)).parent) {
            const psd::Node& node = arena.at(static_cast<std::size_t>(i));
            path.push_back(Move{static_cast<Face>(node.face), node.turns});
        }
        std::reverse(path.begin(), path.end());
        path.push_back(Move{last.face, last.turns});
        return path;
    };

    for (std::size_t head = 0; head < arena.size(); ++head) {
        const psd::Node current = arena.at(head);
        if (current.depth >= max_depth) {
            continue;
        }
        for (const TurnOf& turn : turns) {
            const SignatureKey next{
                psd::map_codes(current.key.cross, 4, *turn.map),
                psd::map_codes(current.key.rest, goal.rest_codes, *turn.map),
            };
            for (const SignatureKey& wanted : goal.keys) {
                if (next == wanted) {
                    // The F2L stages' cost is this number, not the move count.
                    CUBE_ZONE_SCOPED_VALUE(arena.size());
                    return reconstruct(head, turn);
                }
            }
            if (arena.size() >= max_visited_nodes) {
                return std::unexpected(CubeError::no_solution);
            }
            arena.insert(psd::Node{next, static_cast<std::int32_t>(head),
                                   static_cast<std::uint8_t>(turn.face), turn.turns,
                                   static_cast<std::uint8_t>(current.depth + 1)});
        }
    }
    return std::unexpected(CubeError::no_solution);
}

// bfs_search with the piece search as its engine where that is exact.
//
// `goal` must list every signature `postcondition` accepts (each caller
// builds it from signature(Cube::solved()), see the stages), and is checked
// against the reference search by tests/unit/solver/piece_search_test.cpp.
// A cube that is not a real cube - one whose pieces are not each present
// once, which is what Cube::is_solvable() establishes - has no such
// equivalence, so it keeps the generic search and its answers, slow path
// included: nothing the app produces gets there, a scan is assembled only
// into a solvable cube.
template <typename Postcondition, typename Signature>
Result<std::vector<Move>> search(const Cube& start, const std::vector<Face>& allowed_faces,
                                 Postcondition postcondition, Signature signature, int max_depth,
                                 const PieceGoal& goal,
                                 std::size_t max_visited_nodes = kMaxVisitedNodes) {
    if (!start.is_solvable()) {
        return bfs_search(start, allowed_faces, postcondition, signature, max_depth,
                          max_visited_nodes);
    }
    if (postcondition(start)) {
        return std::vector<Move>{};
    }
    return piece_search(signature(start), allowed_faces, goal, max_depth, max_visited_nodes);
}

}  // namespace cube::solver::detail
