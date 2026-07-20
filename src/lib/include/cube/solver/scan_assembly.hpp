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

// Externally-known per-face rotation expectations for the hinted overload
// below. `quarter_turns[input_index]` is the number of CLOCKWISE quarter
// turns (0-3, the same screen-plane direction the assembler's own search
// steps in - see kClockwiseSource in scan_assembly.cpp) the caller expects
// the search to apply to the captured face at that INPUT index to reach
// its assembled orientation; kNoHint means no expectation for that face.
// Indexed by input position rather than cube face because that is what a
// caller actually has before assembly runs: rotation knowledge attaches to
// each captured face as it was captured, while which cube position it
// lands on is exactly what assembly is about to decide.
//
// The intended producer is the orientation tracker: a tracked scan knows
// the in-plane quarter turn every capture was taken at, and those six
// digits are the one extra bit of evidence that can separate the
// near-solved assemblies that are genuinely distinct cubes yet all pass
// solvability. Out-of-range values are treated as kNoHint (defensive - a
// hint is advice, malformed advice must degrade to no advice, never to UB).
struct RotationHint {
    static constexpr int kNoHint = -1;
    std::array<int, 6> quarter_turns{kNoHint, kNoHint, kNoHint, kNoHint, kNoHint, kNoHint};
};

// As assemble_scanned_faces_with_mapping(), plus an OPTIONAL rotation hint
// consulted only to break ties among MULTIPLE already-passing survivors.
//
// The contract, deliberately conservative so the hint cannot weaken the
// acceptance oracle:
//   - The search itself is unchanged: all 4096 combinations, every
//     survivor still has to pass Cube::from_facelets AND is_solvable. The
//     hint filters NOTHING before that bar.
//   - A UNIQUE survivor is returned regardless of the hint - even one the
//     hint contradicts. Geometry outvotes advice: a wrong hint must never
//     turn a perfectly determined scan into a failure.
//   - With several distinct solvable survivors (the near-solved ambiguity
//     the unhinted call rejects), each survivor is scored by how many
//     hinted faces' rotation digits match the hint (a survivor reachable
//     through several combinations takes its best-scoring one, since
//     rotationally symmetric faces make digits non-unique per cube). A
//     STRICTLY best-scoring survivor is returned; a tie - including the
//     no-usable-hint degenerate case - fails with invalid_cube_state
//     exactly as the unhinted call does, because the ambiguity genuinely
//     stands and the remedy (rescan) is unchanged.
//   - hint == nullptr (or a hint with no 0-3 entries) is bit-identical to
//     the unhinted overload, early-exit behaviour included.
[[nodiscard]] Result<AssembledScan> assemble_scanned_faces_with_mapping(
    const std::array<std::array<model::Facelet, 9>, 6>& faces, const RotationHint* hint) noexcept;

// As assemble_scanned_faces_with_mapping(), for callers (the solver
// pipeline) that only need the cube and not the input-index bookkeeping.
[[nodiscard]] Result<Cube> assemble_scanned_faces(
    const std::array<std::array<model::Facelet, 9>, 6>& faces) noexcept;

}  // namespace cube::solver
