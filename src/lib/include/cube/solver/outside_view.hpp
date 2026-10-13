#pragma once

#include <cube/solver/cube.hpp>

#include <array>

namespace cube::solver {

// A side of the cube as someone looking straight at it from outside sees it:
// nine stickers row-major, top-left first. Nothing is mirrored - this is the
// picture a camera takes of that side, and what a drawing of the cube has to
// show for it to match the cube in the hand.
//
// Which edge counts as the top is a convention, and this is it: on F, B, L
// and R the top row is the one against U; on U it is the row against B; on D
// the row against F. That is the cube unfolded into the usual cross with F in
// the middle, and it is the order Cube3DView.qml's faceletColors takes.
//
// Cube's own layout (cube.hpp) is this view on U, F and L, its left-right
// mirror on R and B, and its top-bottom mirror on D.
[[nodiscard]] std::array<Color, 9> outside_view(const Cube& cube, Face face) noexcept;

[[nodiscard]] constexpr Face opposite(Face face) noexcept {
    switch (face) {
        case Face::U:
            return Face::D;
        case Face::D:
            return Face::U;
        case Face::L:
            return Face::R;
        case Face::R:
            return Face::L;
        case Face::F:
            return Face::B;
        case Face::B:
            return Face::F;
    }
    return Face::U;
}

// The side whose edge runs along the top of outside_view(face).
[[nodiscard]] constexpr Face top_of(Face face) noexcept {
    switch (face) {
        case Face::U:
            return Face::B;
        case Face::D:
            return Face::F;
        case Face::L:
        case Face::R:
        case Face::F:
        case Face::B:
            return Face::U;
    }
    return Face::U;
}

// The side whose edge runs down the right of outside_view(face).
[[nodiscard]] constexpr Face right_of(Face face) noexcept {
    switch (face) {
        case Face::U:
        case Face::D:
        case Face::F:
            return Face::R;
        case Face::B:
            return Face::L;
        case Face::L:
            return Face::F;
        case Face::R:
            return Face::B;
    }
    return Face::R;
}

}  // namespace cube::solver
