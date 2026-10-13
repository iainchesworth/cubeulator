#pragma once

#include <cstddef>
#include <cstdint>

#include "cubie.hpp"

// How a last-layer case is numbered, shared by the table generator
// (tools/cfop_tables) and the solver that reads its output, so the two can
// only ever disagree by being out of date - which the exhaustive test in
// tests/unit/solver/cfop_test.cpp catches.
//
// Both numberings read only the four U-layer corners and edges (positions 0
// to 3), which is everything the last layer's two steps depend on once the
// first two layers are solved.

namespace cube::solver::detail {

// OLL (orient the last layer) depends on which U-layer corners are twisted
// and which U-layer edges are flipped, by position - not on where any piece
// is. The fourth corner's twist and the fourth edge's flip follow from the
// other three (twists sum to 0 mod 3, flips to 0 mod 2), so a case is
// 3 * 3 * 3 * 2 * 2 * 2 = 216 values.
inline constexpr std::size_t kOllCaseCount = 216;

[[nodiscard]] constexpr std::size_t oll_case(const CubieCube& cube) noexcept {
    const std::size_t corners =
        (static_cast<std::size_t>(cube.co[0]) * 3 + cube.co[1]) * 3 + cube.co[2];
    const std::size_t edges =
        (static_cast<std::size_t>(cube.eo[0]) * 2 + cube.eo[1]) * 2 + cube.eo[2];
    return corners * 8 + edges;
}

// PLL (permute the last layer) depends on how the four U-layer corners and
// the four U-layer edges are arranged, once everything is oriented: 4! each,
// and a real cube's two parities match, so 288 of the 576 slots occur.
inline constexpr std::size_t kPllSlotCount = 576;

// Rank of a permutation of 0..3, identity = 0.
[[nodiscard]] constexpr std::size_t rank_of_four(const std::uint8_t* p) noexcept {
    std::size_t rank = 0;
    for (std::size_t i = 0; i < 4; ++i) {
        std::size_t smaller = 0;
        for (std::size_t j = i + 1; j < 4; ++j) {
            smaller += p[j] < p[i] ? 1 : 0;
        }
        rank = rank * (4 - i) + smaller;
    }
    return rank;
}

// 0 for an even permutation of 0..3, 1 for an odd one.
[[nodiscard]] constexpr std::size_t parity_of_four(const std::uint8_t* p) noexcept {
    std::size_t inversions = 0;
    for (std::size_t i = 0; i < 4; ++i) {
        for (std::size_t j = i + 1; j < 4; ++j) {
            inversions += p[i] > p[j] ? 1 : 0;
        }
    }
    return inversions % 2;
}

[[nodiscard]] constexpr std::size_t pll_slot(const CubieCube& cube) noexcept {
    return rank_of_four(cube.cp.data()) * 24 + rank_of_four(cube.ep.data());
}

}  // namespace cube::solver::detail
