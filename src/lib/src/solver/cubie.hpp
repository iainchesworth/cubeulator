#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

#include <cube/solver/cube.hpp>

// The cubie-level model the intermediate (CFOP) and advanced (two-phase)
// solvers search over.
//
// Cube is a 54-facelet picture, which is the right shape for the UI, the
// scanner and the beginner stages' colour-based postconditions, but a poor
// one to search: every move is 20 sticker copies and every "where is this
// piece" is a scan. A CubieCube describes the same cube as 8 corners and 12
// edges - which piece sits in each position, and how it is turned - so a
// face turn is a handful of byte shuffles and a whole state is 40 bytes.
//
// The conversion is the only place the two models meet, and it goes through
// Cube's own locate_corner/locate_edge and its own apply() (the move cubies
// are *derived* from apply, not retyped), so the cubie model cannot drift
// from the facelet geometry in cube.cpp. tests/unit/solver/cubie_test.cpp
// proves the two agree on random cubes.

namespace cube::solver::detail {

// Corner positions and pieces, in the standard Kociemba order. A piece is
// named by the position it occupies on the solved cube.
inline constexpr std::size_t kCornerCount = 8;
inline constexpr std::uint8_t kURF = 0, kUFL = 1, kULB = 2, kUBR = 3;
inline constexpr std::uint8_t kDFR = 4, kDLF = 5, kDBL = 6, kDRB = 7;

// Edge positions and pieces. The first eight are the U and D layers' edges,
// the last four the middle ("slice") layer's.
inline constexpr std::size_t kEdgeCount = 12;
inline constexpr std::uint8_t kUR = 0, kUF = 1, kUL = 2, kUB = 3;
inline constexpr std::uint8_t kDR = 4, kDF = 5, kDL = 6, kDB = 7;
inline constexpr std::uint8_t kFR = 8, kFL = 9, kBL = 10, kBR = 11;

// A cube as piece placement.
//
//   cp[pos] / ep[pos]  the piece currently at that position.
//   co[pos]            0..2: how many clockwise thirds the corner at `pos`
//                      is turned from its home orientation, counted from its
//                      U/D sticker (0 when that sticker is on a U or D side).
//   eo[pos]            0 or 1: whether the edge at `pos` is flipped. An edge
//                      is unflipped when its primary sticker - the U/D one
//                      for the eight U/D-layer edges, the F/B one for the
//                      four slice edges - is on its position's primary side,
//                      i.e. when it can be solved without F/B quarter turns.
//
// Both orientation conventions are the ones is_solvable() in cube.cpp uses,
// which are the classical ones, so a U or D turn changes no orientation, an
// L or R quarter turn twists corners only, and an F or B quarter turn twists
// corners and flips edges.
struct CubieCube {
    std::array<std::uint8_t, kCornerCount> cp{0, 1, 2, 3, 4, 5, 6, 7};
    std::array<std::uint8_t, kCornerCount> co{};
    std::array<std::uint8_t, kEdgeCount> ep{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};
    std::array<std::uint8_t, kEdgeCount> eo{};

    friend bool operator==(const CubieCube&, const CubieCube&) = default;
};

// 6 faces x 3 turn counts. A move's index is face * 3 + (turns - 1), with
// the face index being Face's own enumerator value (U, D, L, R, F, B).
inline constexpr std::size_t kMoveCount = 18;

[[nodiscard]] constexpr std::size_t move_index(Move move) noexcept {
    return static_cast<std::size_t>(move.face) * 3 + static_cast<std::size_t>(move.turns - 1);
}

[[nodiscard]] constexpr Move move_from_index(std::size_t index) noexcept {
    return Move{kAllFaces[index / 3], static_cast<int>(index % 3) + 1};
}

// `state` followed by `move`: the cube you get by applying `move` to a cube
// that is in `state`. Composition is associative, so a whole sequence's
// cubie may be built once and applied as a unit.
[[nodiscard]] CubieCube multiply(const CubieCube& state, const CubieCube& move) noexcept;

// What one move does, as a CubieCube (the state a solved cube is left in by
// it). Index as above; out-of-range is a precondition violation.
[[nodiscard]] const CubieCube& move_cubie(std::size_t index) noexcept;

// The cubie description of `cube`, or nullopt if `cube` is not a reachable
// state: is_solvable() is checked first, so a state that passes is a genuine
// permutation of the 8 + 12 pieces with orientations summing correctly.
[[nodiscard]] std::optional<CubieCube> to_cubie(const Cube& cube) noexcept;

[[nodiscard]] constexpr bool is_identity(const CubieCube& cubie) noexcept {
    return cubie == CubieCube{};
}

}  // namespace cube::solver::detail
