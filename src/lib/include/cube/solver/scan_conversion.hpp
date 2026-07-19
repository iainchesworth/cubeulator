#pragma once

#include <cube/error.hpp>
#include <cube/model/scan_progress.hpp>
#include <cube/solver/cube.hpp>

namespace cube::solver {

// Converts a completed 6-face scan into a solver Cube.
//
// model::FaceScan::facelets[9] is row-major as the camera saw it while that
// face was being scanned; Cube's own row/col meaning per face is pinned to
// a fixed global coordinate frame (see cube.hpp/cube.cpp's position_of()).
// Reconciling the two requires a fixed convention for how the cube is held
// during a scan: for each face being scanned, that face points at the
// camera with a reference face toward the top of frame -
//
//   U -> F toward top   D -> F toward top   F -> U toward top
//   B -> U toward top   L -> U toward top   R -> U toward top
//
// (F/L are the natural starting hold; U/D need a forward/back tilt; R/B/L
// only need a turn about the vertical axis, all keeping U up.) Every
// resulting per-face facelet permutation is a pure rotation/reflection
// about that face's own center - see scan_conversion.cpp.
//
// Fails with CubeError::face_capture_incomplete if `scan` isn't complete,
// or CubeError::invalid_cube_state if a facelet has an unrecognized colour
// (should be unreachable for a completed scan - kept as a defensive check)
// or the resulting arrangement fails Cube::is_physically_valid() (a
// realistic outcome of a genuine colour misclassification).
[[nodiscard]] Result<Cube> cube_from_scan(const model::ScanProgress& scan) noexcept;

}  // namespace cube::solver
