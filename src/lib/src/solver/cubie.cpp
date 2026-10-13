#include "cubie.hpp"

#include <algorithm>
#include <array>
#include <iterator>

namespace cube::solver::detail {

namespace {

// The faces each corner position touches, in clockwise order as seen from
// outside the cube, starting with its U or D face. A corner's orientation is
// the index in this list of the face its U/D-coloured sticker is on.
constexpr std::array<std::array<Face, 3>, kCornerCount> kCornerFaces = {{
    {Face::U, Face::R, Face::F},  // URF
    {Face::U, Face::F, Face::L},  // UFL
    {Face::U, Face::L, Face::B},  // ULB
    {Face::U, Face::B, Face::R},  // UBR
    {Face::D, Face::F, Face::R},  // DFR
    {Face::D, Face::L, Face::F},  // DLF
    {Face::D, Face::B, Face::L},  // DBL
    {Face::D, Face::R, Face::B},  // DRB
}};

// The faces each edge position touches, primary face first (U/D for the
// eight U/D-layer edges, F/B for the four slice edges).
constexpr std::array<std::array<Face, 2>, kEdgeCount> kEdgeFaces = {{
    {Face::U, Face::R},  // UR
    {Face::U, Face::F},  // UF
    {Face::U, Face::L},  // UL
    {Face::U, Face::B},  // UB
    {Face::D, Face::R},  // DR
    {Face::D, Face::F},  // DF
    {Face::D, Face::L},  // DL
    {Face::D, Face::B},  // DB
    {Face::F, Face::R},  // FR
    {Face::F, Face::L},  // FL
    {Face::B, Face::L},  // BL
    {Face::B, Face::R},  // BR
}};

// The position whose three faces are exactly {a, b, c}, or -1.
int corner_position_of(Face a, Face b, Face c) noexcept {
    for (std::size_t pos = 0; pos < kCornerCount; ++pos) {
        const auto& faces = kCornerFaces[pos];
        auto has = [&](Face f) { return std::ranges::find(faces, f) != faces.end(); };
        if (a != b && b != c && a != c && has(a) && has(b) && has(c)) {
            return static_cast<int>(pos);
        }
    }
    return -1;
}

int edge_position_of(Face a, Face b) noexcept {
    for (std::size_t pos = 0; pos < kEdgeCount; ++pos) {
        const auto& faces = kEdgeFaces[pos];
        if (a != b && ((faces[0] == a && faces[1] == b) || (faces[0] == b && faces[1] == a))) {
            return static_cast<int>(pos);
        }
    }
    return -1;
}

std::array<CubieCube, kMoveCount> build_move_cubies() noexcept {
    std::array<CubieCube, kMoveCount> moves{};
    for (std::size_t i = 0; i < kMoveCount; ++i) {
        // Deliberately derived from Cube::apply on a solved cube, not typed
        // in: this is what ties the cubie model to the facelet geometry.
        const std::optional<CubieCube> cubie = to_cubie(Cube::solved().apply(move_from_index(i)));
        moves[i] = cubie.value_or(CubieCube{});
    }
    return moves;
}

}  // namespace

CubieCube multiply(const CubieCube& state, const CubieCube& move) noexcept {
    CubieCube result;
    for (std::size_t pos = 0; pos < kCornerCount; ++pos) {
        const std::size_t from = move.cp[pos];
        result.cp[pos] = state.cp[from];
        result.co[pos] = static_cast<std::uint8_t>((state.co[from] + move.co[pos]) % 3);
    }
    for (std::size_t pos = 0; pos < kEdgeCount; ++pos) {
        const std::size_t from = move.ep[pos];
        result.ep[pos] = state.ep[from];
        result.eo[pos] = static_cast<std::uint8_t>((state.eo[from] + move.eo[pos]) % 2);
    }
    return result;
}

const CubieCube& move_cubie(std::size_t index) noexcept {
    // Function-local static: initialised once, thread-safely, on first use.
    static const std::array<CubieCube, kMoveCount> kMoves = build_move_cubies();
    return kMoves[index];
}

std::optional<CubieCube> to_cubie(const Cube& cube) noexcept {
    // Piece identity is read through canonical_color's fixed centre scheme,
    // and the three classical invariants are what make the result a state
    // the searches' coordinates can represent, so both are checked up front.
    if (!cube.is_solvable()) {
        return std::nullopt;
    }

    CubieCube result;
    std::array<bool, kCornerCount> corner_seen{};
    std::array<bool, kEdgeCount> edge_seen{};

    for (std::size_t piece = 0; piece < kCornerCount; ++piece) {
        // The piece is the one whose solved stickers are these colours, so
        // finding those colours finds the piece wherever it has been moved.
        const auto& home = kCornerFaces[piece];
        const Color c0 = canonical_color(home[0]);  // the U/D sticker
        const Color c1 = canonical_color(home[1]);
        const Color c2 = canonical_color(home[2]);
        const Cube::CornerLocation loc = cube.locate_corner(c0, c1, c2);
        const int pos = corner_position_of(loc.face_a, loc.face_b, loc.face_c);
        if (pos < 0 || corner_seen[static_cast<std::size_t>(pos)]) {
            return std::nullopt;
        }
        corner_seen[static_cast<std::size_t>(pos)] = true;

        const auto& faces = kCornerFaces[static_cast<std::size_t>(pos)];
        const auto turned = std::ranges::find(faces, loc.face_a);  // where the U/D sticker is
        result.cp[static_cast<std::size_t>(pos)] = static_cast<std::uint8_t>(piece);
        result.co[static_cast<std::size_t>(pos)] =
            static_cast<std::uint8_t>(std::distance(faces.begin(), turned));
    }

    for (std::size_t piece = 0; piece < kEdgeCount; ++piece) {
        const auto& home = kEdgeFaces[piece];
        const Color primary = canonical_color(home[0]);
        const Color secondary = canonical_color(home[1]);
        const Cube::EdgeLocation loc = cube.locate_edge(primary, secondary);
        const int pos = edge_position_of(loc.face_a, loc.face_b);
        if (pos < 0 || edge_seen[static_cast<std::size_t>(pos)]) {
            return std::nullopt;
        }
        edge_seen[static_cast<std::size_t>(pos)] = true;

        result.ep[static_cast<std::size_t>(pos)] = static_cast<std::uint8_t>(piece);
        // face_a is where the piece's primary sticker landed.
        result.eo[static_cast<std::size_t>(pos)] =
            loc.face_a == kEdgeFaces[static_cast<std::size_t>(pos)][0] ? 0 : 1;
    }

    return result;
}

}  // namespace cube::solver::detail
