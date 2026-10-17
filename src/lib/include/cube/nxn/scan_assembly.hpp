#pragma once

#include <cube/error.hpp>
#include <cube/nxn/cube.hpp>
#include <cube/nxn/geometry.hpp>

#include <array>
#include <cstddef>
#include <vector>

// Putting six scanned sides together into a cube of any size.
//
// The 3x3 does this in cube::solver::assemble_scanned_faces: each side is
// pinned to a place on the cube by its centre sticker, and the one thing left
// unknown is which way up each side was held, four quarter-turns of six sides,
// 4^6 = 4096 cubes of which solvability keeps the real one. An N x N cube has
// the same structure and the same count. What it lacks is a solvability test
// written for 3x3s, and cube::nxn::validate is the replacement: the same
// question for any size, asked of the stickers alone.
//
// A side with a middle sticker (odd N) is pinned by it, as at 3x3. A side
// without one (even N) is not pinned by anything on it, and is put on the cube
// by trying every face it could go on: the first on the top, the other five on
// any face that is free, each in four turns, which is 5! x 4^5 at the most and a
// good deal fewer once the pieces are looked at (a corner or a wing that shows a
// colour twice is no piece). The cube comes back with the first side on the top,
// in the colours the sides have, whichever way up the cube was held.

namespace cube::nxn {

// One side as a camera pointed at it sees it: N x N stickers, row-major from the
// top-left, in any quarter-turn about the camera axis. Six of them, in any
// order.
using ScannedSides = std::array<std::vector<Color>, 6>;

// Every distinct cube of `size` the six sides make, up to `limit` of them, each
// in the layout NCube holds (and with the sides in the order of the six given,
// as the centres name them). Empty when the sides are not six distinct centres
// of a cube of this size, or no turn of them is a cube a person could hold;
// more than one when they make several, which is when something other than six
// flat views has to say which.
[[nodiscard]] std::vector<NCube> assemble_all_scanned_sides(int size, const ScannedSides& sides,
                                                            std::size_t limit = 16);

// The one cube the sides make: invalid_cube_state when they make none or
// several (the same code as the 3x3, for the same reasons: see
// cube/solver/scan_assembly.hpp).
[[nodiscard]] Result<NCube> assemble_scanned_sides(int size, const ScannedSides& sides);

}  // namespace cube::nxn
