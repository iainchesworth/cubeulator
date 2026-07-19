#pragma once

#include <cube/error.hpp>
#include <cube/model/facelet.hpp>
#include <cube/solver/cube.hpp>

#include <array>
#include <cstddef>

namespace cube::solver {

// The outcome of assembling a free-form scan (see assemble_scanned_faces
// below): the unique solvable Cube, plus which input face landed on which
// cube position. `input_index_of_face[static_cast<std::size_t>(face)]` is
// the index into the caller's six-face input array of the captured face
// that became solver `face`. The mapping is determined purely by centre
// colours (rotation never moves a centre), so the progress UI can light up
// "face captured" slots without caring which rotation the search settled on.
struct AssembledScan {
    Cube cube;
    std::array<std::size_t, 6> input_index_of_face{};
};

// Turns six free-form captured faces into the unique solvable Cube.
//
// Free-form means the user showed the six faces in ANY order with ANY
// rotation about the camera axis - there is no prompt sequence and no
// holding convention. The input is the output of
// cube::vision::resolve_colors(): six faces of resolved facelet colours,
// each face's 9 facelets row-major exactly as the camera happened to see
// it. Two facts anchor the reconstruction:
//
//   1. Centres never move, so each face's centre colour alone identifies
//      its cube position under Cube::solved()'s colour scheme (White
//      centre -> D, Yellow -> U, Green -> F, Blue -> B, Orange -> L,
//      Red -> R - the inverse of canonical_color()).
//   2. Reflections cannot occur optically, so the only remaining unknown
//      per face is one of four quarter-rotations - and a cube's rotation
//      assembly is (in practice) pinned by solvability: all 4^6 = 4096
//      per-face rotation combinations are tried exhaustively, and only
//      those whose Cube::from_facelets() construction succeeds AND whose
//      cube.is_solvable() holds survive.
//
// 4096 iterations of (from_facelets + is_solvable) is sub-millisecond
// bit-twiddling on any target, so no pruning is attempted - exhaustiveness
// is the point, since it guarantees the unique solvable assembly is found
// whenever one exists.
//
// Duplicate rotation combinations that build the SAME cube (a face whose
// sticker pattern is rotationally symmetric - trivially, every face of a
// solved cube - makes several combinations coincide) are deduplicated and
// count as one survivor, so a solved cube assembles fine: its uniform faces
// make the rotations degenerate, not ambiguous.
//
// Fails with CubeError::invalid_cube_state when:
//   - a facelet colour is FacetColor::unknown (defensive; unreachable for
//     resolve_colors() output),
//   - the six centres are not six distinct colours (a physically
//     impossible scan),
//   - NO rotation combination is solvable (a colour was misread somewhere;
//     the caller's remedy is a rescan), or
//   - MORE THAN ONE distinct solvable cube exists. No existing CubeError
//     code names ambiguity (unsolvable_state would be a lie - the state is
//     solvable, twice over - and face_capture_incomplete / the vision codes
//     describe earlier pipeline stages), and ambiguity is only theoretically
//     possible for cubes within a couple of moves of solved, where several
//     near-symmetric faces line up. It collapses to the same user remedy as
//     a misread - rescan - so it deliberately shares invalid_cube_state
//     rather than growing the enum for an unobservable distinction.
[[nodiscard]] Result<AssembledScan> assemble_scanned_faces_with_mapping(
    const std::array<std::array<model::Facelet, 9>, 6>& faces) noexcept;

// As assemble_scanned_faces_with_mapping(), for callers (the solver
// pipeline) that only need the cube and not the input-index bookkeeping.
[[nodiscard]] Result<Cube> assemble_scanned_faces(
    const std::array<std::array<model::Facelet, 9>, 6>& faces) noexcept;

}  // namespace cube::solver
