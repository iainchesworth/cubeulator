#pragma once

#include <cube/model/face.hpp>

#include <array>
#include <cstddef>

namespace cube::vision {

// ---------------------------------------------------------------------------
// The 24-element orientation lattice of a cube, in the display frame.
//
// Why this exists: every per-frame pose the vision pipeline produces
// (pose_estimation.hpp) is ABSOLUTE up to one unknown - which physical face
// the detector modelled as its near face, and at which in-plane quarter
// turn it labelled the stickers. That unknown is exactly one element of the
// cube's 24-element rotation group. Tracking therefore never integrates
// deltas: each pass re-resolves the lattice element against a predicted
// orientation (resolve_alignment), so error cannot accumulate, and the
// resolved element mechanically determines how the detector's screen-order
// cells map onto the cube's own faces (kFaceObsToBody / kCornerObsToBody) -
// which is what makes cell routing immune to the detector re-keying its
// row-major labelling as the cube crosses a 45-degree in-plane boundary:
// the labelling jump and the resolved element jump cancel by construction.
//
// Frames, pinned once for the whole tracking stack:
//
//   Display frame - pose_estimation.hpp's exported convention: +x screen
//   right, +y screen up, +z out of the screen toward the viewer.
//
//   Body frame - the cube's own frame, axes named per cube.cpp's
//   position_of() ground truth: +x = R's outward normal (L->R), +y = U's
//   (D->U), +z = F's (B->F), faces labelled with model::Face. "Body face"
//   throughout means a PHYSICAL face of the cube being tracked; which
//   body face is which is fixed the moment tracking anchors (the anchoring
//   detection's model frame becomes the body frame) and identity is then
//   carried purely by orientation, never by colour.
//
//   An orientation quaternion q maps body coordinates to display
//   coordinates: v_display = q * v_body * conj(q). Identity is therefore
//   the canonical camera-facing pose - body F toward the viewer, U up,
//   R right - matching CubePoseProjection's documented identity.
//
//   The detector's model frame is the same kind of cube-adapted frame (its
//   near face plays F, screen-up plays U); a detection's q_obs is the model
//   frame's orientation, and model->body is the unknown lattice element.
//
// Everything here is a pure value table or a pure function - no state, no
// OpenCV, bounded work (a resolve is 24 quaternion products), safe on any
// thread.
// ---------------------------------------------------------------------------

// {w, x, y, z}, matching CubePoseProjection::orientation's layout so pose
// results can be consumed without conversion. Unit-norm expected everywhere
// below; both q and -q describe the same rotation and every function here
// treats them identically.
using Quat = std::array<float, 4>;

// Hamilton product: rotation composition, R(a * b) = R(a) * R(b).
[[nodiscard]] Quat quat_multiply(const Quat& a, const Quat& b) noexcept;

// Inverse for unit quaternions.
[[nodiscard]] Quat quat_conjugate(const Quat& q) noexcept;

// Rotation angle (degrees, in [0, 180]) taking orientation a to
// orientation b - the geodesic distance on the rotation group, computed
// double-cover-safely via |<a, b>|.
[[nodiscard]] float quat_angle_deg(const Quat& a, const Quat& b) noexcept;

inline constexpr std::size_t kLatticeSize = 24;

// Index convention, load-bearing for every table below: element 4*n + k is
// the orientation with body face n (model::Face order U,R,F,D,L,B) as the
// NEAR face (outward normal toward the viewer, +z) after k additional
// quarter turns of the whole cube CLOCKWISE AS SEEN ON SCREEN. The k = 0
// base orientation per near face (chosen for derivational simplicity, not
// any grip convention - k covers all grips):
//
//   U: pitch forward 90 degrees about +x (F ends at frame-bottom)
//   R: yaw 90 degrees about +y bringing R to front (F ends at frame-left)
//   F: identity
//   D: pitch back 90 degrees about +x (F ends at frame-top)
//   L: yaw -90 degrees about +y (F ends at frame-right)
//   B: 180 degrees about +y (U stays up)
//
// The identity orientation is therefore element 4*2 + 0 = 8.
inline constexpr int kIdentityLatticeIndex = 8;

// The 24 lattice orientations as display-frame quaternions, derived at
// compile time from exact integer rotation matrices (orientation_lattice.cpp)
// so the tables cannot drift from the frame conventions above. Signs are
// canonicalised (w > 0, else first nonzero of x,y,z > 0) for deterministic
// comparisons; consumers must still compare double-cover-safely.
extern const std::array<Quat, kLatticeSize> kLattice24;

// Where each body face's outward normal points under each lattice element,
// named as the DISPLAY slot it occupies (model::Face labels applied to the
// display frame: U = screen-up, R = screen-right, F = toward viewer, ...).
// kFacePermutation[i][f] == Face::F identifies element i's near face.
// Regression-locked against cube.cpp's position_of() geometry.
extern const std::array<std::array<cube::model::Face, 6>, kLatticeSize> kFacePermutation;

// How one detected face's 9 screen-row-major cells land on the physical
// cube, per resolved lattice element. `cell[obs]` is the SOLVER row-major
// facelet index (cube.hpp's per-face layout, the order Cube::from_facelets
// consumes) of screen cell `obs` on `body_face`. Note this is deliberately
// solver order, NOT the raw camera order the legacy slot store kept:
// camera row-major only matches solver order up to rotation for U/F/L -
// for R/B/D the two differ by a REFLECTION (see scan_conversion.cpp's
// camera_to_solver_permutation: kFlipHorizontal/kFlipVertical entries), so
// routing through these tables is also what makes aggregated faces
// directly assemblable.
struct FaceCellMap {
    cube::model::Face body_face{};
    std::array<int, 9> cell{};
};

// Face-path observation (FaceObservation cells, row-major as seen on
// screen, the detector's near face) -> body face + solver facelet indices,
// indexed by the element resolve_alignment returned for that observation's
// q_obs. Valid for all 24 elements; only 4 (the in-plane turns of one near
// face) arise from a genuinely frontal detection, but the table is total
// so a mis-resolved element still routes deterministically (and gets
// caught by verification downstream, never by reading out of bounds).
extern const std::array<FaceCellMap, kLatticeSize> kFaceObsToBody;

// Corner-path observation -> body, per resolved element, view order
// {top, lower_left, lower_right} exactly as CornerObservation stores them.
// The corner detector's fixed screen sectors mean its model frame ALWAYS
// calls the top face U, lower-left F and lower-right R (see
// project_cube_from_corner's kCornerModel and corner_observation.cpp's
// pose derivation); under free rotation any of the 24 gauges can therefore
// occur, and this table is how each one routes all 27 stickers correctly.
// Regression-locked against corner_slot_mappings() for the two poses that
// function documents.
extern const std::array<std::array<FaceCellMap, 3>, kLatticeSize> kCornerObsToBody;

// Nearest lattice element to an arbitrary orientation, with the residual
// angle. Any orientation is within acos(...) <= 62 degrees of some element;
// anything within 45 degrees of an element is unambiguously in its basin
// (distinct elements are at least 90 degrees apart, so the tracker's
// acceptance thresholds stay well inside half that).
struct LatticeQuantization {
    int index = 0;
    float theta_deg = 0.0F;
};
[[nodiscard]] LatticeQuantization quantize24(const Quat& q) noexcept;

// The correctness keystone: given a detection's orientation q_obs (model
// frame -> display) and a predicted body orientation q_pred, finds the
// lattice element S minimising the angle between q_obs * S and q_pred.
// That best composition IS the measured body orientation (q_obs * S maps
// body -> display because S is the inverse of the unknown model->body
// gauge), theta_deg is its distance to the prediction, and margin_deg is
// how much worse the runner-up element was (second-best theta minus best
// theta) - the gate against accepting a resolution the prediction cannot
// actually disambiguate. Distinct candidates are >= 90 degrees apart, so
// with a sane prediction the margin is large; near the midpoint between
// two elements it collapses toward zero, which is precisely when a caller
// must coast rather than guess.
struct AlignmentResolution {
    int lattice_index = 0;
    float theta_deg = 0.0F;
    float margin_deg = 0.0F;
};
[[nodiscard]] AlignmentResolution resolve_alignment(const Quat& q_obs, const Quat& q_pred) noexcept;

// How far the most camera-facing body face is from dead-on (degrees, 0 =
// its normal points straight out of the screen). The capture-gate metric:
// beyond ~35 degrees a face view foreshortens too much to trust colours.
[[nodiscard]] float frontal_deg(const Quat& q) noexcept;

// Which body face is most camera-facing under orientation q. Ties (exact
// 45-degree presentations) break toward the lowest model::Face enum value;
// callers gating on frontal_deg never see a tie matter.
[[nodiscard]] cube::model::Face nearest_face(const Quat& q) noexcept;

// How far ONE named body face is from dead-on under orientation q
// (degrees; 0 = its outward normal points straight at the viewer, 90 =
// edge-on, up to 180 facing away). frontal_deg(q) is this minimised over
// faces; the per-face form exists for the corner-view capture gates, where
// each FLANK face has its own (looser) frontal-ness bound.
[[nodiscard]] float face_frontal_deg(const Quat& q, cube::model::Face face) noexcept;

}  // namespace cube::vision
