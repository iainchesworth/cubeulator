#pragma once

#include <cube/error.hpp>

#include <array>
#include <cstdint>

namespace cube::solver {

// One of the six faces of the cube. Values double as an array index (see
// Cube's storage) so their order must stay stable.
enum class Face : std::uint8_t { U, D, L, R, F, B };

inline constexpr std::array<Face, 6> kAllFaces = {Face::U, Face::D, Face::L,
                                                  Face::R, Face::F, Face::B};

// A sticker colour. Deliberately named after the colour, not the face it
// starts on: Theme.qml's faceU/faceD/etc. constants are a palette keyed by
// "which colour", not a claim about which physical face currently shows it
// (confirmed by the pre-existing scrambled demo data mixing colours freely
// within one physical face). Cube::solved() is the only place that pins a
// colour to a starting face.
enum class Color : std::uint8_t { White, Yellow, Green, Blue, Orange, Red };

// The colour Cube::solved() fixes to `face`'s centre - and therefore, since
// centres never move, the colour that face must show everywhere once it's
// solved. Cross-solving deliberately targets D (White) so the beginner
// method's first two stages build on the hidden bottom face, leaving U
// (Yellow) for the last-layer stages where the arrow overlay matters most.
[[nodiscard]] constexpr Color canonical_color(Face face) noexcept {
    switch (face) {
        case Face::U:
            return Color::Yellow;
        case Face::D:
            return Color::White;
        case Face::L:
            return Color::Orange;
        case Face::R:
            return Color::Red;
        case Face::F:
            return Color::Green;
        case Face::B:
            return Color::Blue;
    }
    return Color::White;
}

// A single face turn: `turns` quarter-turns clockwise as viewed from outside
// that face (the standard cubing convention every face letter uses). 1 = the
// plain letter (e.g. "R"), 2 = the letter+2 ("R2"), 3 = the primed letter
// ("R'"). No slice (M/E/S) or whole-cube (x/y/z) moves - this solver never
// needs them (see docs/architecture.md).
struct Move {
    Face face;
    int turns = 1;

    [[nodiscard]] constexpr Move inverse() const noexcept { return Move{face, 4 - turns}; }

    friend constexpr bool operator==(const Move&, const Move&) = default;
};

// A 3x3x3 cube's 54 stickers. No whole-cube reorientation is ever applied
// (see Move), so every face's centre sticker - and therefore that face's
// identity in Cube::solved()'s colour scheme - never moves for the lifetime
// of a Cube.
//
// Facelet layout (shared with CubeView.qml's projection, see cube.cpp for
// the derivation): each face is indexed row-major 0..8 over a local (row,
// col) grid. U and D use (row=z+1, col=x+1); R and L use (row=1-y, col=z+1);
// F and B use (row=1-y, col=x+1), where x/y/z are the sticker's position in
// [-1,0,1] on the L->R / D->U / B->F axes respectively. Index 4 is always
// the centre.
class Cube {
public:
    [[nodiscard]] static Cube solved() noexcept;

    // Builds a Cube from raw per-face sticker data, row-major per the
    // layout documented above (the caller is responsible for correct
    // facelet ordering - see cube::solver::cube_from_scan(), which derives
    // it from a camera-oriented scan). Fails with
    // CubeError::invalid_cube_state if the result doesn't pass
    // is_physically_valid() - the guard against a malformed vision-pipeline
    // read this class's own is_physically_valid() doc comment anticipated.
    [[nodiscard]] static Result<Cube> from_facelets(
        const std::array<std::array<Color, 9>, 6>& facelets) noexcept;

    // row, col in [0,3).
    [[nodiscard]] Color at(Face face, int row, int col) const noexcept {
        return at(face, row * 3 + col);
    }
    [[nodiscard]] Color at(Face face, int index) const noexcept;

    // The face's 9 stickers, row-major - a direct match for CubeView.qml's
    // topColors/rightColors/leftColors props when face is U/R/F.
    [[nodiscard]] std::array<Color, 9> face(Face face) const noexcept;

    [[nodiscard]] Cube apply(Move move) const noexcept;

    // True iff every sticker on every face matches that face's (fixed)
    // centre sticker.
    [[nodiscard]] bool is_solved() const noexcept;

    // A necessary (not sufficient) check that the facelet arrangement could
    // come from a real cube: every colour appears exactly 9 times, every
    // corner/edge cubie carries one sticker from each axis colour-pair it
    // touches (never two from the same pair), and the corner twist sum is
    // parity-consistent. Guards against a grossly malformed read (e.g. from
    // a future vision pipeline); every Cube built via solved()+apply() in
    // this codebase already satisfies it by construction.
    [[nodiscard]] bool is_physically_valid() const noexcept;

    // The full reachability test: true iff this facelet state can be
    // produced from solved() by some sequence of face turns. Like solved()
    // itself, this is defined w.r.t. the fixed centre scheme
    // (canonical_color): a physically valid cube always carries one centre
    // of each colour but possibly permuted vs canonical - exactly what a
    // free-form scan produces when its six faces are assembled into the
    // wrong slots - and since face turns never move centres, such a state
    // is unreachable and reported as not solvable (checked first). On top
    // of is_physically_valid() (checked next; false -> false) this
    // enforces the three classical invariants: corner twist sum = 0
    // (mod 3), edge flip sum = 0 (mod 2), and equal corner/edge
    // permutation parities - together necessary AND sufficient for
    // reachability once every real cubie is present exactly once (see
    // cube.cpp for the conventions and their derivations).
    [[nodiscard]] bool is_solvable() const noexcept;

    // Where the edge cubie carrying colours `a` and `b` (in either order)
    // currently sits: face_a/index_a is wherever `a` landed, face_b/index_b
    // wherever `b` landed. Every solving stage after the cross needs to find
    // specific pieces by colour rather than by fixed facelet index, since a
    // piece's position and orientation are exactly what's being solved for.
    // Undefined (all-U/index-0) if the cube doesn't actually carry both
    // colours on one edge, which never happens for a physically valid cube.
    struct EdgeLocation {
        Face face_a{};
        int index_a = 0;
        Face face_b{};
        int index_b = 0;
    };
    [[nodiscard]] EdgeLocation locate_edge(Color a, Color b) const noexcept;

    // As EdgeLocation, for the corner cubie carrying colours `a`, `b`, `c`.
    struct CornerLocation {
        Face face_a{};
        int index_a = 0;
        Face face_b{};
        int index_b = 0;
        Face face_c{};
        int index_c = 0;
    };
    [[nodiscard]] CornerLocation locate_corner(Color a, Color b, Color c) const noexcept;

    friend bool operator==(const Cube&, const Cube&) = default;

private:
    void set(Face face, int index, Color color) noexcept {
        facelets_[static_cast<std::size_t>(face)][static_cast<std::size_t>(index)] = color;
    }

    std::array<std::array<Color, 9>, 6> facelets_{};
};

}  // namespace cube::solver
