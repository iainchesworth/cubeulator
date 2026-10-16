#pragma once

#include <vector>

#include <cube/nxn/cube.hpp>
#include <cube/nxn/geometry.hpp>

namespace cube::nxn {

// A side of the cube as someone looking straight at it from outside sees it:
// N x N stickers row-major, top-left first. Nothing is mirrored - this is the
// picture a camera takes of that side, and what a drawing of the cube has to
// show for it to match the cube in the hand.
//
// It is cube::solver::outside_view for a cube of any size, and the same
// convention: on F, B, L and R the top row is the one against U; on U it is
// the row against B; on D the row against F. NCube's own layout is this view
// on U, F and L, its left-right mirror on R and B, and its top-bottom mirror
// on D. At size 3 the two are equal sticker for sticker.
[[nodiscard]] std::vector<Color> outside_view(const NCube& cube, Face face);

}  // namespace cube::nxn
