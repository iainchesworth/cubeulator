#pragma once

#include <array>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <queue>
#include <unordered_set>
#include <utility>
#include <vector>

#include <cube/error.hpp>
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
        return std::hash<std::uint64_t>{}(key.cross) ^ (std::hash<std::uint64_t>{}(key.rest) * 0x9E3779B97F4A7C15ULL);
    }
};

// True iff every cross edge, F2L corner, and middle edge is currently
// sitting somewhere other than the U face. A *single* outer-layer turn
// moves all three of D/middle/U-adjacent rows of that face together, so a
// disturbing move sequence can genuinely relocate an F2L piece up into the
// U layer, not just shuffle it within F2L. If that's happened at the
// moment a commutator's U-toggle fires, the toggle scrambles that piece
// together with the real last-layer pieces, and inverse(P) - being purely
// position-based - restores whatever piece the toggle left in that F2L
// slot's path, not necessarily the original one. This check is what makes
// commutator_search's "always restores F2L" guarantee actually true rather
// than assumed; skipping it produced fully wrong solutions that reported
// success (confirmed empirically before this check was added).
[[nodiscard]] inline bool f2l_pieces_off_u(const Cube& cube) noexcept {
    static constexpr std::array<std::array<Face, 2>, 4> kCornerSideFaces = {{
        {Face::F, Face::L},
        {Face::F, Face::R},
        {Face::B, Face::L},
        {Face::B, Face::R},
    }};
    static constexpr std::array<std::array<Face, 2>, 4> kEdgeSideFaces = {{
        {Face::F, Face::R},
        {Face::F, Face::L},
        {Face::B, Face::R},
        {Face::B, Face::L},
    }};
    for (const auto& sides : kCornerSideFaces) {
        const Cube::CornerLocation loc =
            cube.locate_corner(Color::White, canonical_color(sides[0]), canonical_color(sides[1]));
        if (loc.face_a == Face::U || loc.face_b == Face::U || loc.face_c == Face::U) {
            return false;
        }
    }
    for (Color other : {Color::Green, Color::Blue, Color::Orange, Color::Red}) {
        const Cube::EdgeLocation loc = cube.locate_edge(Color::White, other);
        if (loc.face_a == Face::U || loc.face_b == Face::U) {
            return false;
        }
    }
    for (const auto& sides : kEdgeSideFaces) {
        const Cube::EdgeLocation loc = cube.locate_edge(canonical_color(sides[0]), canonical_color(sides[1]));
        if (loc.face_a == Face::U || loc.face_b == Face::U) {
            return false;
        }
    }
    return true;
}

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
                                      Postcondition postcondition, Signature signature, int max_depth,
                                      std::size_t max_visited_nodes = kMaxVisitedNodes) {
    if (postcondition(start)) {
        return std::vector<Move>{};
    }

    struct Node {
        Cube cube;
        std::vector<Move> path;
    };

    std::queue<Node> frontier;
    std::unordered_set<SignatureKey, SignatureKeyHash> visited;
    frontier.push(Node{start, {}});
    visited.insert(signature(start));

    while (!frontier.empty()) {
        Node node = std::move(frontier.front());
        frontier.pop();
        if (static_cast<int>(node.path.size()) >= max_depth) {
            continue;
        }

        for (Face face : allowed_faces) {
            for (int turns = 1; turns <= 3; ++turns) {
                Cube next = node.cube.apply(Move{face, turns});
                if (postcondition(next)) {
                    std::vector<Move> solution = node.path;
                    solution.push_back(Move{face, turns});
                    return solution;
                }
                if (visited.size() >= max_visited_nodes) {
                    return std::unexpected(CubeError::no_solution);
                }
                if (visited.insert(signature(next)).second) {
                    std::vector<Move> path = node.path;
                    path.push_back(Move{face, turns});
                    frontier.push(Node{std::move(next), std::move(path)});
                }
            }
        }
    }

    return std::unexpected(CubeError::no_solution);
}

// Searches for a "conjugate + toggle" sequence P, U^n, inverse(P), where P
// is built from `disturbing_faces` (up to `max_p_length` moves, each
// re-tried at every U^n). Since U never touches F2L and inverse(P) exactly
// undoes whatever P did to it, ANY such sequence restores F2L exactly
// regardless of what P is - so this search never has to check F2L
// preservation itself, only `target_postcondition` (the last-layer
// condition alone). That makes it dramatically cheaper than a blind wide
// search: bfs_search with all of {U,L,R,F,B} available was tried first for
// OLL and needed tens of millions of nodes without finding a 2-look edge
// orientation fix, because almost every candidate sequence permanently
// disturbs F2L instead of restoring it - exactly the failure mode real
// solving algorithms avoid by construction (they're conjugates/commutators,
// not arbitrary sequences).
template <typename Postcondition>
Result<std::vector<Move>> commutator_search(const Cube& start, const std::vector<Face>& disturbing_faces,
                                             Postcondition target_postcondition, int max_p_length) {
    if (target_postcondition(start)) {
        return std::vector<Move>{};
    }

    std::vector<Move> p;

    // `after_p` is threaded through as a parameter (the state p already
    // produces) rather than replayed from `start` via `p` on every call, and
    // `p_inverse` is only built once a node actually needs it (the
    // f2l_pieces_off_u branch) rather than on every call regardless of
    // whether it's used - replaying/rebuilding unconditionally on every one
    // of an exponential search's nodes turned an O(branching^depth) search
    // into O(branching^depth * depth); this restores the former without
    // changing which solution (if any) is found.
    std::function<Result<std::vector<Move>>(int, const Cube&)> try_from =
        [&](int remaining, const Cube& after_p) -> Result<std::vector<Move>> {
        if (f2l_pieces_off_u(after_p)) {
            std::vector<Move> p_inverse;
            p_inverse.reserve(p.size());
            for (auto it = p.rbegin(); it != p.rend(); ++it) {
                p_inverse.push_back(it->inverse());
            }
            for (int n = 1; n <= 3; ++n) {
                Cube candidate = after_p.apply(Move{Face::U, n});
                for (const Move& m : p_inverse) {
                    candidate = candidate.apply(m);
                }
                if (target_postcondition(candidate)) {
                    std::vector<Move> result = p;
                    result.push_back(Move{Face::U, n});
                    result.insert(result.end(), p_inverse.begin(), p_inverse.end());
                    return result;
                }
            }
        }

        if (remaining == 0) {
            return std::unexpected(CubeError::no_solution);
        }
        for (Face face : disturbing_faces) {
            if (!p.empty() && p.back().face == face) {
                continue;  // same-face repeats are always reducible to a single move
            }
            for (int turns = 1; turns <= 3; ++turns) {
                p.push_back(Move{face, turns});
                Result<std::vector<Move>> found = try_from(remaining - 1, after_p.apply(Move{face, turns}));
                if (found.has_value()) {
                    return found;
                }
                p.pop_back();
            }
        }
        return std::unexpected(CubeError::no_solution);
    };

    return try_from(max_p_length, start);
}

// Composes two independent commutators in sequence: [P1,U^n1,inverse(P1)]
// then [P2,U^n2,inverse(P2)] from the result. Each half is individually
// F2L-safe (same guarantee as commutator_search), so the composition is
// too - this is what actually turns out to matter in practice: a single
// commutator search big enough to reach some targets directly becomes
// computationally infeasible (branching^length grows too fast), while
// composing two much smaller commutators reaches a similar combined
// "effective length" for a fraction of the cost (product, not sum, of
// state-space growth). Tried after a single commutator_search call comes
// up empty.
template <typename Postcondition>
Result<std::vector<Move>> two_commutator_search(const Cube& start, const std::vector<Face>& disturbing_faces,
                                                 Postcondition target_postcondition, int max_length_each) {
    if (target_postcondition(start)) {
        return std::vector<Move>{};
    }

    std::vector<Move> p1;

    // See commutator_search's try_from above for why after_p1 is threaded
    // through as a parameter and p1_inverse built lazily, rather than both
    // being recomputed from `start`/`p1` on every call.
    std::function<Result<std::vector<Move>>(int, const Cube&)> try_first =
        [&](int remaining, const Cube& after_p1) -> Result<std::vector<Move>> {
        if (f2l_pieces_off_u(after_p1)) {
            std::vector<Move> p1_inverse;
            p1_inverse.reserve(p1.size());
            for (auto it = p1.rbegin(); it != p1.rend(); ++it) {
                p1_inverse.push_back(it->inverse());
            }
            for (int n1 = 1; n1 <= 3; ++n1) {
                Cube intermediate = after_p1.apply(Move{Face::U, n1});
                for (const Move& m : p1_inverse) {
                    intermediate = intermediate.apply(m);
                }

                Result<std::vector<Move>> second =
                    commutator_search(intermediate, disturbing_faces, target_postcondition, max_length_each);
                if (second.has_value()) {
                    std::vector<Move> result = p1;
                    result.push_back(Move{Face::U, n1});
                    result.insert(result.end(), p1_inverse.begin(), p1_inverse.end());
                    result.insert(result.end(), second->begin(), second->end());
                    return result;
                }
            }
        }

        if (remaining == 0) {
            return std::unexpected(CubeError::no_solution);
        }
        for (Face face : disturbing_faces) {
            if (!p1.empty() && p1.back().face == face) {
                continue;
            }
            for (int turns = 1; turns <= 3; ++turns) {
                p1.push_back(Move{face, turns});
                Result<std::vector<Move>> found = try_first(remaining - 1, after_p1.apply(Move{face, turns}));
                if (found.has_value()) {
                    return found;
                }
                p1.pop_back();
            }
        }
        return std::unexpected(CubeError::no_solution);
    };

    return try_first(max_length_each, start);
}

// As commutator_search, but for the (common) cases a single conjugate can't
// reach: P, Q, U^n, inverse(Q), inverse(P). This still provably restores
// F2L regardless of P and Q - inverse(Q) undoes Q relative to the
// "after P" state (nothing but a U turn happens in between), landing back
// at "after P"; inverse(P) then undoes P - so, like commutator_search, this
// never needs to check F2L preservation itself. Two independent disturbing
// sequences (rather than one) is what real algorithms with more than one
// "trigger" phase look like; tried after commutator_search comes up empty.
template <typename Postcondition>
Result<std::vector<Move>> nested_commutator_search(const Cube& start, const std::vector<Face>& disturbing_faces,
                                                     Postcondition target_postcondition, int max_p_length,
                                                     int max_q_length) {
    if (target_postcondition(start)) {
        return std::vector<Move>{};
    }

    std::vector<Move> p;
    std::vector<Move> q;

    std::function<Result<std::vector<Move>>(int)> try_q_from = [&](int remaining) -> Result<std::vector<Move>> {
        Cube after_p = start;
        for (const Move& m : p) {
            after_p = after_p.apply(m);
        }
        std::vector<Move> p_inverse;
        p_inverse.reserve(p.size());
        for (auto it = p.rbegin(); it != p.rend(); ++it) {
            p_inverse.push_back(it->inverse());
        }

        Cube after_pq = after_p;
        for (const Move& m : q) {
            after_pq = after_pq.apply(m);
        }
        std::vector<Move> q_inverse;
        q_inverse.reserve(q.size());
        for (auto it = q.rbegin(); it != q.rend(); ++it) {
            q_inverse.push_back(it->inverse());
        }

        if (f2l_pieces_off_u(after_pq)) {
            for (int n = 1; n <= 3; ++n) {
                Cube candidate = after_pq.apply(Move{Face::U, n});
                for (const Move& m : q_inverse) {
                    candidate = candidate.apply(m);
                }
                for (const Move& m : p_inverse) {
                    candidate = candidate.apply(m);
                }
                if (target_postcondition(candidate)) {
                    std::vector<Move> result = p;
                    result.insert(result.end(), q.begin(), q.end());
                    result.push_back(Move{Face::U, n});
                    result.insert(result.end(), q_inverse.begin(), q_inverse.end());
                    result.insert(result.end(), p_inverse.begin(), p_inverse.end());
                    return result;
                }
            }
        }

        if (remaining == 0) {
            return std::unexpected(CubeError::no_solution);
        }
        for (Face face : disturbing_faces) {
            if (!q.empty() && q.back().face == face) {
                continue;
            }
            for (int turns = 1; turns <= 3; ++turns) {
                q.push_back(Move{face, turns});
                Result<std::vector<Move>> found = try_q_from(remaining - 1);
                if (found.has_value()) {
                    return found;
                }
                q.pop_back();
            }
        }
        return std::unexpected(CubeError::no_solution);
    };

    std::function<Result<std::vector<Move>>(int)> try_p_from = [&](int remaining) -> Result<std::vector<Move>> {
        Result<std::vector<Move>> found = try_q_from(max_q_length);
        if (found.has_value()) {
            return found;
        }
        if (remaining == 0) {
            return std::unexpected(CubeError::no_solution);
        }
        for (Face face : disturbing_faces) {
            if (!p.empty() && p.back().face == face) {
                continue;
            }
            for (int turns = 1; turns <= 3; ++turns) {
                p.push_back(Move{face, turns});
                Result<std::vector<Move>> deeper = try_p_from(remaining - 1);
                if (deeper.has_value()) {
                    return deeper;
                }
                p.pop_back();
            }
        }
        return std::unexpected(CubeError::no_solution);
    };

    return try_p_from(max_p_length);
}

// A 6-bit code for "which of the 54 facelets currently shows colour `a` of
// the edge/corner piece carrying it" - since a (face, index) pair pins down
// both position and orientation, this alone is enough to distinguish every
// reachable state of that one piece.
[[nodiscard]] inline std::uint64_t piece_code_edge(const Cube& cube, Color a, Color b) noexcept {
    const Cube::EdgeLocation loc = cube.locate_edge(a, b);
    return static_cast<std::uint64_t>(loc.face_a) * 9 + static_cast<std::uint64_t>(loc.index_a);
}

[[nodiscard]] inline std::uint64_t piece_code_corner(const Cube& cube, Color a, Color b, Color c) noexcept {
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

// The 4 first-layer corners' + 4 middle edges' combined position (8 pieces,
// 48 bits - fits in a SignatureKey::rest on its own), for searches from OLL
// onward where the entire first two layers must stay intact.
[[nodiscard]] inline std::uint64_t f2l_key(const Cube& cube) noexcept {
    static constexpr std::array<std::pair<Face, Face>, 4> kCornerSides = {{
        {Face::F, Face::L},
        {Face::F, Face::R},
        {Face::B, Face::L},
        {Face::B, Face::R},
    }};
    static constexpr std::array<std::pair<Face, Face>, 4> kEdgeSides = {{
        {Face::F, Face::R},
        {Face::F, Face::L},
        {Face::B, Face::R},
        {Face::B, Face::L},
    }};
    std::uint64_t key = 0;
    for (const auto& [side_a, side_b] : kCornerSides) {
        key = push_code(key, piece_code_corner(cube, Color::White, canonical_color(side_a), canonical_color(side_b)));
    }
    for (const auto& [side_a, side_b] : kEdgeSides) {
        key = push_code(key, piece_code_edge(cube, canonical_color(side_a), canonical_color(side_b)));
    }
    return key;
}

}  // namespace cube::solver::detail
