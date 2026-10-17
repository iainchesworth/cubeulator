#pragma once

#include <cube/error.hpp>
#include <cube/model/face.hpp>
#include <cube/model/facelet.hpp>
#include <cube/solver/cube.hpp>

#include <array>
#include <cstddef>
#include <vector>

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
//      centre -> D, Yellow -> U, Green -> F, Blue -> B, Red -> L,
//      Orange -> R - the inverse of canonical_color()).
//   2. Reflections cannot occur optically - but they DO occur
//      conventionally: the solver's cross-unfolded facelet layout is
//      camera-handed on U, F, L and mirror-handed on D, R, B, so an
//      outside-view capture of those three faces is a REFLECTION of the
//      solver layout that no quarter-rotation can bridge. Assembly applies that fixed,
//      centre-pinned flip itself; the remaining unknown per face is then one of four
//      quarter-rotations, and the assembly is (in practice) pinned by
//      solvability: all 4^6 = 4096 per-face rotation combinations are
//      tried exhaustively, and only those whose Cube::from_facelets()
//      construction succeeds AND whose cube.is_solvable() holds survive.
//
// Input convention: each face row-major as an OUTSIDE viewer - a camera
// pointed at that face - sees it, in any in-plane rotation, which a
// rectified camera image satisfies by construction.
//
// The search is exhaustive: the unique solvable assembly is found whenever one
// exists. It does not try every combination one by one - a combination whose
// faces turned so far put two stickers of one axis pair on an edge or a corner
// is given up there, and a face that looks the same turned is tried once - but
// what that skips is only what from_facelets() would have refused or what an
// earlier combination had already made, so the cubes found, and their order,
// are those of trying all 4,096 (tests/unit/solver/scan_assembly_test.cpp holds
// it to that). Each look at the faces counts as a node in
// cube/solver/search_work.hpp.
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
//     describe earlier pipeline stages). It is not rare: about one scrambled
//     cube in eighty puts together two ways from six square-on sides, and
//     assemble_all_scanned_faces() below is how a caller sees which. It
//     collapses to the same user remedy as
//     a misread - rescan - so it deliberately shares invalid_cube_state
//     rather than growing the enum for an unobservable distinction.
[[nodiscard]] Result<AssembledScan> assemble_scanned_faces_with_mapping(
    const std::array<std::array<model::Facelet, 9>, 6>& faces) noexcept;

// Every distinct solvable cube the six faces make, up to `limit` of them -
// the survivors assemble_scanned_faces_with_mapping() counts to decide
// between one cube, none, and ambiguity. Empty when the faces are not six
// distinct centres or make no solvable cube; more than one when they make
// several, which is when something other than the six flat faces has to
// say which (two sides seen in one view, for one).
[[nodiscard]] std::vector<Cube> assemble_all_scanned_faces(
    const std::array<std::array<model::Facelet, 9>, 6>& faces, std::size_t limit = 16) noexcept;

// As assemble_scanned_faces_with_mapping(), for callers (the solver
// pipeline) that only need the cube and not the input-index bookkeeping.
[[nodiscard]] Result<Cube> assemble_scanned_faces(
    const std::array<std::array<model::Facelet, 9>, 6>& faces) noexcept;

// The same for faces as the vision layer names them (cube::vision::resolve_colors):
// six vectors of facelets, which must be nine each. Anything else is not a 3x3's
// faces and assembles to nothing.
[[nodiscard]] Result<AssembledScan> assemble_scanned_faces_with_mapping(
    const std::array<std::vector<model::Facelet>, 6>& faces) noexcept;
[[nodiscard]] std::vector<Cube> assemble_all_scanned_faces(
    const std::array<std::vector<model::Facelet>, 6>& faces, std::size_t limit = 16) noexcept;
[[nodiscard]] Result<Cube> assemble_scanned_faces(
    const std::array<std::vector<model::Facelet>, 6>& faces) noexcept;

}  // namespace cube::solver
