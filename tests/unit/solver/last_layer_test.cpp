#include <catch2/catch_test_macros.hpp>

#include <array>
#include <utility>
#include <vector>

#include <cube/solver/solver.hpp>

#include "last_layer.hpp"  // private header - see tests/CMakeLists.txt for why it's reachable here

using cube::solver::Cube;
using cube::solver::Face;
using cube::solver::Move;
using cube::solver::random_scramble;
namespace detail = cube::solver::detail;

namespace {

// The 33 facelet positions the last-layer alphabet must never move: all of
// D, plus rows 1 and 2 (indices 3..8) of every side face. Everything that
// is not one of last_layer.hpp's 21 last-layer facelets.
std::vector<std::pair<Face, int>> f2l_positions() {
    std::vector<std::pair<Face, int>> positions;
    positions.reserve(9);
    for (int index = 0; index < 9; ++index) {
        positions.emplace_back(Face::D, index);
    }
    for (Face side : {Face::F, Face::R, Face::B, Face::L}) {
        for (int index = 3; index < 9; ++index) {
            positions.emplace_back(side, index);
        }
    }
    return positions;
}

}  // namespace

// This is the invariant last_layer.hpp's whole bounded-search guarantee
// rests on: every alphabet word must preserve the first two layers as a
// unit, or last_layer_key's 21-facelet visited-set dedup silently
// conflates distinct states. A move sequence is a fixed permutation of
// facelet positions, so it preserves F2L for EVERY cube iff it maps each
// F2L position to itself - and checking that the colours at those
// positions are unchanged across many random scrambles (whose F2L facelets
// are richly varied) pins that down: a word that moved an F2L position
// would have to land an identical colour there on every scramble to slip
// through. This is the test that catches a one-move typo in an alphabet
// constant at the constant itself, rather than as an opaque corpus
// failure three stages downstream.
TEST_CASE("every last-layer alphabet word fixes all 33 first-two-layer facelet positions",
          "[last_layer]") {
    const std::array<std::pair<const char*, const std::vector<Move>*>, 8> alphabet = {{
        {"U", &detail::kAdjustU},
        {"U2", &detail::kAdjustU2},
        {"U'", &detail::kAdjustUPrime},
        {"edge flip", &detail::kEdgeOrientFlip},
        {"Sune", &detail::kSune},
        {"Anti-Sune", &detail::kAntiSune},
        {"A-perm", &detail::kCornerCycle},
        {"U-perm", &detail::kEdgeCycle},
    }};
    const std::vector<std::pair<Face, int>> positions = f2l_positions();

    for (const auto& [name, moves] : alphabet) {
        CAPTURE(name);
        for (unsigned seed = 0; seed < 100; ++seed) {
            CAPTURE(seed);
            const Cube before = random_scramble(seed, 25);
            Cube after = before;
            for (const Move& move : *moves) {
                after = after.apply(move);
            }
            for (const auto& [face, index] : positions) {
                REQUIRE(after.at(face, index) == before.at(face, index));
            }
        }
    }
}
