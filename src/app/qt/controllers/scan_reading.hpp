#pragma once

#include "controllers/cube_edit.hpp"

#include <cube/solver/cube.hpp>
#include <cube/vision/direct_scan.hpp>

#include <optional>

// What the camera read, as the review screen takes it (CubeEdit's Reading): the
// stickers named, and the ones the reader was not sure of marked.
//
// Qt-free, and the only place the scan's own types meet the review. The scan reads
// 3x3 cubes only today; Reading carries a size and the numbering of any cube, so
// when the scan reads other sizes this is the one function to widen.

namespace cube::app {

// A sticker the colour naming placed in its cluster with less than this much
// to spare over the next-nearest (ColorResolution: 1 is far from any rival, 0 is a
// rival as close) is marked as doubted. A first guess, not a measured edge: it is
// to be set from recorded misreads.
inline constexpr float kDoubtBelow = 0.3F;

// A scan that made a cube. The stickers are the cube's; the doubts are the naming's
// own confidences, laid over the cube's sides wherever the naming and the cube agree
// on what a side shows (an assembly that exchanged two names leaves a side unmarked
// rather than mark the wrong stickers). `naming` is ScanDiagnosis::naming, or none.
[[nodiscard]] Reading reading_from_cube(
    const cube::solver::Cube& cube,
    const std::optional<cube::vision::ColorResolution>& naming = std::nullopt);

// A scan that did not make a cube: the sides it named, each placed on the side of the
// cube its middle colour belongs to, as the camera had it (Reading::upright is
// false - the assembly could not turn them to fit). A colour named for no side is
// left unpainted, as are the stickers of a side never captured. Empty when the
// scan has not named its sides (fewer than six captured, or naming failed).
[[nodiscard]] std::optional<Reading> reading_from_named_sides(
    const cube::vision::ColorResolution& naming);

}  // namespace cube::app
