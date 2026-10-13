#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <unordered_set>
#include <utility>
#include <vector>

#include <cube/error.hpp>
#include <cube/solver/cube.hpp>

// Fixed-algorithm solving for the last layer (2-look OLL / 2-look PLL).
//
// The last layer was originally solved by *searching* for a
// conjugate/commutator move sequence per scramble (commutator_search /
// two_commutator_search, see git history). That search was correct, but its
// cost was branching^depth with no useful upper bound in practice: a hard
// PLL corner permutation legitimately took minutes of CPU on a desktop, and
// one documented case ran 80+ minutes without resolving. This solver's
// whole purpose is to guide a person interactively on a phone while they
// hold a real scrambled cube, so a solve must complete in seconds - always,
// on every input, on mobile hardware.
//
// This header replaces that search with the approach human speedcubers use:
// a small fixed alphabet of named last-layer algorithms, each of which
// preserves the first two layers as a unit, composed by a breadth-first
// search over *whole algorithms* (plus bare U turns for alignment) rather
// than over individual face turns. Because every word in the alphabet is
// F2L-safe, every reachable state keeps F2L intact, and the entire
// reachable space is a subset of the last-layer group - 62,208 states in
// total, far fewer per stage. The search is therefore bounded by the state
// space, not by branching^depth: worst case a few thousand node
// expansions, i.e. milliseconds on any hardware.

namespace cube::solver::detail {

// ---------------------------------------------------------------------------
// The algorithm alphabet, in this codebase's Move terms (turns: 1 =
// clockwise, 2 = half turn, 3 = counter-clockwise). Every algorithm here
// must preserve the first two layers exactly (it may permute/reorient
// last-layer pieces only); tests/unit/solver/last_layer_test.cpp verifies
// that property for each word directly (it fixes all 33 F2L facelet
// positions across many scrambles), and every corpus test verifies it
// end-to-end via Cube::is_solved().
// ---------------------------------------------------------------------------

// Bare U-layer turns: never touch F2L by construction, and let the search
// align a case to wherever the fixed algorithms expect it (the "AUF" -
// adjust U face - between algorithms in human solving).
inline const std::vector<Move> kAdjustU = {Move{Face::U, 1}};
inline const std::vector<Move> kAdjustU2 = {Move{Face::U, 2}};
inline const std::vector<Move> kAdjustUPrime = {Move{Face::U, 3}};

// F R U R' U' F' - the standard 2-look OLL edge-orientation algorithm.
// Applied from the right AUF it advances dot -> L-shape -> line -> cross.
inline const std::vector<Move> kEdgeOrientFlip = {Move{Face::F, 1}, Move{Face::R, 1},
                                                  Move{Face::U, 1}, Move{Face::R, 3},
                                                  Move{Face::U, 3}, Move{Face::F, 3}};

// R U R' U R U2 R' (Sune) and R U2 R' U' R U' R' (Anti-Sune) - the
// standard 2-look OLL corner-orientation pair. Together with U turns they
// reach every zero-sum corner-twist state (all 7 OLL corner cases) in at
// most two applications. That is an existence bound, not a description of
// algorithm_search's output: the search minimises word count, where an
// AUF costs the same as a Sune, so it sometimes emits a third application
// in place of interleaved U turns.
inline const std::vector<Move> kSune = {Move{Face::R, 1}, Move{Face::U, 1}, Move{Face::R, 3},
                                        Move{Face::U, 1}, Move{Face::R, 1}, Move{Face::U, 2},
                                        Move{Face::R, 3}};
inline const std::vector<Move> kAntiSune = {Move{Face::R, 1}, Move{Face::U, 2}, Move{Face::R, 3},
                                            Move{Face::U, 3}, Move{Face::R, 1}, Move{Face::U, 3},
                                            Move{Face::R, 3}};

// R' F R' B2 R F' R' B2 R2 (A-perm) - cycles three U-layer corners without
// touching orientation. With U turns it reaches every last-layer corner
// permutation: even permutations directly via 3-cycles, odd ones because a
// bare U turn is itself an odd corner permutation that the following
// cycles then absorb (the edges it also moves are the next stage's
// problem, not this one's).
inline const std::vector<Move> kCornerCycle = {
    Move{Face::R, 3}, Move{Face::F, 1}, Move{Face::R, 3}, Move{Face::B, 2}, Move{Face::R, 1},
    Move{Face::F, 3}, Move{Face::R, 3}, Move{Face::B, 2}, Move{Face::R, 2}};

// R U' R U R U R U' R' U' R2 (U-perm) - cycles three U-layer edges without
// touching corners or orientation. Once the corners are permuted, the
// remaining edge permutation is always even (a legal cube's corner and
// edge permutation parities match), so 3-cycles plus U alignment always
// finish the solve.
inline const std::vector<Move> kEdgeCycle = {Move{Face::R, 1}, Move{Face::U, 3}, Move{Face::R, 1},
                                             Move{Face::U, 1}, Move{Face::R, 1}, Move{Face::U, 1},
                                             Move{Face::R, 1}, Move{Face::U, 3}, Move{Face::R, 3},
                                             Move{Face::U, 3}, Move{Face::R, 2}};

// ---------------------------------------------------------------------------
// Search
// ---------------------------------------------------------------------------

// The 21 last-layer facelets (all 9 of U, plus each side face's U-adjacent
// row 0), 3 bits each - 63 bits, one word. Complete for this search's
// purposes: every alphabet word preserves the other 33 facelets, so all
// states reachable from one start agree on them and can only differ inside
// these 21.
[[nodiscard]] inline std::uint64_t last_layer_key(const Cube& cube) noexcept {
    std::uint64_t key = 0;
    for (int index = 0; index < 9; ++index) {
        key = (key << 3) | static_cast<std::uint64_t>(cube.at(Face::U, index));
    }
    for (Face side : {Face::F, Face::R, Face::B, Face::L}) {
        for (int index = 0; index < 3; ++index) {
            key = (key << 3) | static_cast<std::uint64_t>(cube.at(side, index));
        }
    }
    return key;
}

// Backstop only: the reachable state space is at most the last-layer
// group's 62,208 states, so a search that grows past this is a bug (an
// alphabet word that fails to preserve F2L, silently expanding the space)
// surfacing as bounded, diagnosable misbehaviour rather than unbounded
// growth - same philosophy as bfs_search's kMaxVisitedNodes. A broken
// word can never produce a *wrong* stage answer, only a missed/longer
// solution or no_solution: every stage postcondition implies F2L intact
// (oll.cpp gates both of its stages on f2l_solved for exactly this
// reason; pll.cpp's postconditions include oll_solved / is_solved), and
// the postcondition is checked on the genuine final state of the emitted
// move list.
inline constexpr std::size_t kMaxLastLayerStates = 100'000;

// Breadth-first search over sequences of whole alphabet words driving
// `start` to a state satisfying `postcondition`. Same arena/parent-chain
// design as bfs_search (see its comment); a node's `word` indexes into
// `alphabet`, and the returned move list is the concatenation of the
// winning sequence's words. `max_words` caps the sequence length in words
// (not moves); the visited-set bound above is what actually guarantees
// termination, so the cap can be generous.
template <typename Postcondition>
Result<std::vector<Move>> algorithm_search(const Cube& start,
                                           const std::vector<std::vector<Move>>& alphabet,
                                           Postcondition postcondition, int max_words) {
    if (postcondition(start)) {
        return std::vector<Move>{};
    }

    struct Node {
        Cube cube;
        std::int32_t parent;
        std::int32_t word;
        std::int32_t depth;
    };

    std::vector<Node> nodes;
    std::unordered_set<std::uint64_t> visited;
    nodes.push_back(Node{start, -1, -1, 0});
    visited.insert(last_layer_key(start));

    auto reconstruct = [&](std::size_t leaf, std::size_t last_word) {
        std::vector<std::int32_t> words;
        for (std::int32_t i = static_cast<std::int32_t>(leaf); i > 0;
             i = nodes[static_cast<std::size_t>(i)].parent) {
            words.push_back(nodes[static_cast<std::size_t>(i)].word);
        }
        std::reverse(words.begin(), words.end());
        words.push_back(static_cast<std::int32_t>(last_word));

        std::vector<Move> solution;
        for (std::int32_t word : words) {
            const std::vector<Move>& moves = alphabet[static_cast<std::size_t>(word)];
            solution.insert(solution.end(), moves.begin(), moves.end());
        }
        return solution;
    };

    for (std::size_t head = 0; head < nodes.size(); ++head) {
        if (nodes[head].depth >= max_words) {
            continue;
        }
        // Copies, not references: the push_back below may reallocate the
        // arena and invalidate anything pointing into it.
        const Cube current = nodes[head].cube;
        const std::int32_t depth = nodes[head].depth;

        for (std::size_t word = 0; word < alphabet.size(); ++word) {
            Cube next = current;
            for (const Move& move : alphabet[word]) {
                next = next.apply(move);
            }
            if (postcondition(next)) {
                return reconstruct(head, word);
            }
            if (visited.size() >= kMaxLastLayerStates) {
                return std::unexpected(CubeError::no_solution);
            }
            if (visited.insert(last_layer_key(next)).second) {
                nodes.push_back(Node{std::move(next), static_cast<std::int32_t>(head),
                                     static_cast<std::int32_t>(word), depth + 1});
            }
        }
    }

    return std::unexpected(CubeError::no_solution);
}

}  // namespace cube::solver::detail
