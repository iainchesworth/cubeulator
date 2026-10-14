#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <unordered_set>
#include <utility>
#include <vector>

#include <cube/error.hpp>
#include <cube/profiling.hpp>
#include <cube/solver/cube.hpp>

namespace cube::solver::detail {

// A reduced-state key for the BFS visited-set below. `cross` holds the
// cross's 4 edges (see cross_key), present in every post-cross search since
// every later stage must keep the cross intact; `rest` holds whatever
// additional pieces a given search needs to track (already-placed corners,
// already-placed middle edges, ...), 6 bits/piece. Two uint64_t rather than
// one: a single 64-bit key was tried first and didn't have room for
// cross (24 bits) plus up to 8 more tracked pieces (48 bits) at once.
struct SignatureKey {
    std::uint64_t cross = 0;
    std::uint64_t rest = 0;

    friend bool operator==(const SignatureKey&, const SignatureKey&) = default;
};

struct SignatureKeyHash {
    std::size_t operator()(const SignatureKey& key) const noexcept {
        // Explicit because std::size_t is 32 bits on a 32-bit target
        // (WebAssembly): there the low 32 bits of the mix are the hash.
        return static_cast<std::size_t>(
            std::hash<std::uint64_t>{}(key.cross) ^
            (std::hash<std::uint64_t>{}(key.rest) * 0x9E3779B97F4A7C15ULL));
    }
};

// Generic breadth-first search for a short move sequence, using only
// `allowed_faces`, that drives `start` to a state satisfying
// `postcondition`. `signature` must reduce a Cube to whatever subset of
// state actually matters for this search: an under-reduced signature (e.g.
// raw facelet colours that vary with irrelevant pieces) can blow the
// visited-set up from ~10^5 states to ~10^7+ and make the search
// impractically slow - this bit the cross search once already. `postcondition`
// is checked only on the final state of a candidate sequence; intermediate
// moves are free to transiently disturb whatever the postcondition doesn't
// itself gate on, exactly like a real solving algorithm that "restores"
// other pieces by its last move. Fails with CubeError::no_solution if no
// sequence within max_depth satisfies postcondition - callers should treat
// that as a solver bug for any physically valid input, not a legitimate
// dead end.
// A hard ceiling on how many nodes any single search will visit before
// giving up: a signature that under-reduces the true search space (the
// exact mistake that motivated the warning above) turns into unbounded
// memory growth rather than a quick, diagnosable CubeError::no_solution -
// this converts that failure mode into the latter.
inline constexpr std::size_t kMaxVisitedNodes = 2'000'000;

template <typename Postcondition, typename Signature>
Result<std::vector<Move>> bfs_search(const Cube& start, const std::vector<Face>& allowed_faces,
                                     Postcondition postcondition, Signature signature,
                                     int max_depth,
                                     std::size_t max_visited_nodes = kMaxVisitedNodes) {
    CUBE_ZONE_SCOPED_N("solver.bfs_search");
    if (postcondition(start)) {
        return std::vector<Move>{};
    }

    // Nodes live in one growing arena and record only (cube, parent index,
    // arriving move, depth); a found solution is reconstructed by walking
    // the parent chain. The frontier "queue" is just the next unexpanded
    // arena index, since new nodes are always appended in expansion order -
    // identical FIFO semantics to an explicit queue. An earlier version
    // stored the full move path inside every node instead, which put a
    // heap-allocated vector copy on every single enqueue; on searches that
    // legitimately visit 10^5..10^6 nodes (middle edges, first-layer
    // corners), that allocation churn dominated the whole solve's runtime -
    // and this solver must stay interactive on a phone.
    struct Node {
        Cube cube;
        std::int32_t parent;
        Move move;
        std::int32_t depth;
    };

    std::vector<Node> nodes;
    std::unordered_set<SignatureKey, SignatureKeyHash> visited;
    nodes.push_back(Node{start, -1, Move{Face::U, 1}, 0});
    visited.insert(signature(start));

    auto reconstruct = [&nodes](std::size_t leaf, Move last) {
        std::vector<Move> path;
        for (std::int32_t i = static_cast<std::int32_t>(leaf); i > 0;
             i = nodes[static_cast<std::size_t>(i)].parent) {
            path.push_back(nodes[static_cast<std::size_t>(i)].move);
        }
        std::reverse(path.begin(), path.end());
        path.push_back(last);
        return path;
    };

    for (std::size_t head = 0; head < nodes.size(); ++head) {
        if (nodes[head].depth >= max_depth) {
            continue;
        }
        // Copies, not references: the push_back below may reallocate the
        // arena and invalidate anything pointing into it.
        const Cube current = nodes[head].cube;
        const std::int32_t depth = nodes[head].depth;

        for (Face face : allowed_faces) {
            for (int turns = 1; turns <= 3; ++turns) {
                Cube next = current.apply(Move{face, turns});
                if (postcondition(next)) {
                    // The F2L stages' cost is this number, not the move count.
                    CUBE_ZONE_SCOPED_VALUE(visited.size());
                    return reconstruct(head, Move{face, turns});
                }
                if (visited.size() >= max_visited_nodes) {
                    return std::unexpected(CubeError::no_solution);
                }
                if (visited.insert(signature(next)).second) {
                    nodes.push_back(Node{std::move(next), static_cast<std::int32_t>(head),
                                         Move{face, turns}, depth + 1});
                }
            }
        }
    }

    return std::unexpected(CubeError::no_solution);
}

// A 6-bit code for "which of the 54 facelets currently shows colour `a` of
// the edge/corner piece carrying it" - since a (face, index) pair pins down
// both position and orientation, this alone is enough to distinguish every
// reachable state of that one piece.
[[nodiscard]] inline std::uint64_t piece_code_edge(const Cube& cube, Color a, Color b) noexcept {
    const Cube::EdgeLocation loc = cube.locate_edge(a, b);
    return static_cast<std::uint64_t>(loc.face_a) * 9 + static_cast<std::uint64_t>(loc.index_a);
}

[[nodiscard]] inline std::uint64_t piece_code_corner(const Cube& cube, Color a, Color b,
                                                     Color c) noexcept {
    const Cube::CornerLocation loc = cube.locate_corner(a, b, c);
    return static_cast<std::uint64_t>(loc.face_a) * 9 + static_cast<std::uint64_t>(loc.index_a);
}

// Appends one more piece's code to a `rest`-style accumulator (see
// SignatureKey).
[[nodiscard]] inline std::uint64_t push_code(std::uint64_t acc, std::uint64_t code) noexcept {
    return (acc << 6) | code;
}

// The 4 cross edges' combined position, for SignatureKey::cross.
[[nodiscard]] inline std::uint64_t cross_key(const Cube& cube) noexcept {
    std::uint64_t key = 0;
    for (Color other : {Color::Green, Color::Blue, Color::Orange, Color::Red}) {
        key = push_code(key, piece_code_edge(cube, Color::White, other));
    }
    return key;
}

}  // namespace cube::solver::detail
