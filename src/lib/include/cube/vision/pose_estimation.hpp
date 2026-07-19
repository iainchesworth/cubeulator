#pragma once

#include <cube/error.hpp>
#include <cube/vision/corner_observation.hpp>
#include <cube/vision/edge_observation.hpp>
#include <cube/vision/face_observation.hpp>

#include <opencv2/core.hpp>

#include <array>
#include <optional>

namespace cube::vision {

// The previous pass's accepted pose in OpenCV extrinsic form (model->
// camera Rodrigues rotation + translation), handed back into the
// projectors to bias the CURRENT pass toward temporal continuity. Two
// distinct mechanisms hang off it: (1) the generic-PnP solves initialise
// from it (useExtrinsicGuess), which keeps the iterative optimiser in the
// basin the cube actually occupied a frame ago instead of whatever local
// minimum a cold start finds; (2) the face path uses it to ARBITRATE the
// planar-square two-fold ambiguity - IPPE occasionally hands back the
// mirror solution, and only history can tell the two apart (a single
// frame cannot). Callers own staleness policy: a seed should come from
// the immediately preceding pass (or a short hold window), never from an
// old pose, because a wildly wrong seed downgrades gracefully (the seeded
// solve fails its gates and the unseeded result stands) but a subtly
// stale one biases the arbitration for nothing.
struct PoseSeed {
    cv::Vec3d rvec;
    cv::Vec3d tvec;
};

// Intrinsics approximated per the projector contracts: fx = fy = frame
// width (a typical webcam's ~53 degree horizontal FOV gives tan(hfov/2)
// ~= 0.5, i.e. f ~= width), principal point at the frame centre, no
// distortion. Good enough for an overlay to track convincingly; not
// metrology. Exposed so everything that reasons about these poses -
// seeding, reprojection checks, predicted-pose hints - shares the ONE
// camera model the projectors used, byte for byte.
[[nodiscard]] cv::Matx33d approximate_camera_matrix(cv::Size frame_size);

// Converts a solvePnP model->camera rotation (OpenCV-style axes: +y down,
// +z into the screen) into the display-frame quaternion convention
// CubePoseProjection exports ({w,x,y,z}, +y up, +z toward the viewer), and
// back. The two frames differ by conjugation with F = diag(1,-1,-1); the
// pair is exposed so trackers can round-trip display-frame predictions
// into OpenCV extrinsics (PoseSeed rvec via cv::Rodrigues) without
// re-deriving the axis reconciliation and silently disagreeing with it.
[[nodiscard]] std::array<float, 4> display_quaternion(const cv::Matx33d& opencv_rotation);
[[nodiscard]] cv::Matx33d opencv_rotation_from_display(const std::array<float, 4>& display_orientation);

// Derives an observe_corner() hint from a predicted pose: which three
// model faces the prediction says are visible, re-assigned to the
// {top, lower_left, lower_right} view roles (most up-screen face plays
// top, the other two follow by the corner's fixed chirality), and each
// role's on-screen sector / lattice-basis directions. Returns nullopt when
// the prediction is not a credible three-faces-visible corner presentation
// (a frontal or edge-on pose - the caller simply runs the detector
// unhinted, or not at all).
//
// Only the seed's ROTATION is consumed. Directions are computed under the
// weak-perspective approximation (a 3D direction's screen image is the xy
// part of its camera-frame rotation): at the distances the projectors
// model (cube several units out, f = frame width) the perspective
// correction to a DIRECTION is a fraction of a degree - far below the
// hand-tilt tolerance the detector's affine refit absorbs anyway - and
// dropping the translation sidesteps the fact that a PoseSeed's model
// ORIGIN differs between the face path (near-face centre) and the corner
// path (cube centre) while their model AXES agree, which is the only part
// a rotation-only derivation touches. This also makes the hint frame-size
// independent, so it applies unchanged on the downscaled analysis frame.
[[nodiscard]] std::optional<CornerHint> compute_corner_hint(const PoseSeed& seed);

// Derives an observe_edge() hint from a predicted pose: exactly TWO model
// faces visible (three is compute_corner_hint's territory, one is a
// frontal pose) around a near-vertical shared edge, roles assigned by
// screen side (left plays model F, right plays R - always
// chirality-consistent, see edge_observation.hpp), and each role's
// on-screen lattice basis under the same rotation-only weak-perspective
// derivation and shorter-axis scaling compute_corner_hint uses. Returns
// nullopt for any other presentation (the caller probes the canonical
// bases, or skips the edge path entirely).
[[nodiscard]] std::optional<EdgeHint> compute_edge_hint(const PoseSeed& seed);

// The cube's eight corners projected into frame pixels - what a 3D AR
// wireframe overlay draws so the user sees the cube tracked as an object
// in space rather than a flat alignment target. Vertex order: 0-3 the
// near (camera-facing) face's corners as seen on screen (tl, tr, br, bl),
// 4-7 the corresponding rear corners (same order, each directly behind its
// near counterpart). The twelve edges connect 0-1-2-3-0, 4-5-6-7-4, and
// i to i+4.
struct CubePoseProjection {
    std::array<cv::Point2f, 8> vertices{};

    // The underlying 3D orientation as a unit quaternion {w, x, y, z}.
    // Convention, exactly: the rotation taking the CANONICAL CAMERA-FACING
    // MODEL FRAME - model +x -> screen right, +y -> screen up, +z -> out of
    // the screen toward the viewer, near face toward the camera - to the
    // observed pose. A cube presented flat-on and unrotated therefore
    // yields (near) identity; turning the physical cube turns the
    // quaternion identically, which is what lets a Quick3D scene rotate a
    // rendered cube in lockstep with the real one. Note this is a
    // graphics-style y-UP right-handed frame, NOT OpenCV's camera frame
    // (+y down, +z into the screen) - the implementation reconciles the
    // axes so this convention holds regardless of which estimator produced
    // it. Identity whenever orientation_valid is false.
    std::array<float, 4> orientation{1.0F, 0.0F, 0.0F, 0.0F};

    // True when orientation was robustly estimated. The face path always
    // sets it (a successful solvePnP pose IS the orientation); the corner
    // path re-derives a pose from its seven directly-pinned projected
    // corners and can fail that derivation even when the wireframe vertices
    // themselves are fine - see project_cube_from_corner. Consumers treat
    // false as "no tracking this reading" (the UI falls back to its idle
    // rotation); vertices remain valid either way.
    bool orientation_valid = true;

    // The accepted solve's raw OpenCV extrinsics - exactly what the next
    // pass feeds back as a PoseSeed. Deliberately the UNRECONCILED
    // model->camera form (not the display quaternion): seeding re-enters
    // solvePnP, which speaks this frame, and converting out and back in
    // would only launder float error through two basis changes. Left at
    // zero whenever orientation_valid is false (the corner path can return
    // drawable vertices without ever completing a solve) - a zero tvec
    // puts the model AT the camera, so consumers must gate seeding on
    // orientation_valid, never on the fields themselves.
    cv::Vec3d rvec{};
    cv::Vec3d tvec{};

    // Worst single-corner reprojection error of the accepted solve, as a
    // fraction of the observation's apparent (on-screen) radius - the same
    // scale-free residual the corner path has always gated on at 0.15.
    // Now measured on BOTH paths and exported as pose-quality evidence:
    // near zero for a self-consistent detection, creeping up as the quads
    // stop describing any true cube projection. 0 when orientation_valid
    // is false.
    double max_reproj_fraction = 0.0;

    // True when the exported pose came out of a seed-initialised solve
    // (the face path's mirror arbitration chose the seeded solution over
    // IPPE's, the IPPE-failure fallback started from the seed, or the
    // corner path's seeded solve passed its gates). Telemetry for tuning
    // the tracker - how often continuity actually overrode or rescued the
    // single-frame estimate - not a quality judgement: both sources pass
    // identical acceptance gates.
    bool from_seed = false;
};

// Estimates the cube's 3D pose from a single detected face and projects
// the full cube. Camera intrinsics are approximated (principal point at
// the frame centre, focal length ~= frame width - adequate for an overlay,
// not for metrology) and the face is modelled as the unit square of a
// cube extending away from the camera. Fails (no_valid_cube_detected) when
// the pose estimate degenerates.
//
// With a null seed the behaviour is EXACTLY the historical single-frame
// estimate (IPPE, generic-iterative fallback, cheirality gate). A seed
// adds continuity in two ways, both bounded at one extra solvePnP call:
// the fallback initialises from it instead of cold-starting, and when
// IPPE's answer sits more than 90 degrees from the seed - the signature
// of the planar two-fold ambiguity resolving to the mirror, since a cube
// cannot half-turn between consecutive passes - a seed-initialised
// iterative solve is arbitrated in IF it passes the same cheirality gate
// plus the corner path's reprojection bar. A seed can therefore rescue or
// override a mirror flip but never degrade a healthy estimate: when the
// seeded solve cannot explain the observed quad, the unseeded result
// stands untouched.
[[nodiscard]] Result<CubePoseProjection> project_cube_from_face(const FaceObservation& observation,
                                                                 cv::Size frame_size,
                                                                 const PoseSeed* seed = nullptr);

// As above from a corner view, where the three visible face quads pin
// seven of the eight corners directly; the hidden eighth is completed by
// parallelepiped symmetry. The near face slot for vertex ordering is the
// TOP face's quad. The vertex-sharing identities this construction rests
// on are defined by the detector's QUAD-CORNER LABELS, not by screen
// positions, so they hold for any corner presentation - fixed-pose or
// hint-rotated - as long as the observation's cell bases follow the
// corner topology (which observe_corner guarantees on both paths; see
// CornerHint).
//
// Orientation: the seven pinned corners are only IMAGE-PLANE points - the
// geometric construction never recovers depth - so the orientation is
// re-derived by solving PnP over those seven (non-coplanar in the model)
// corners under the same approximate camera model as the face path, with
// the model oriented U-up/F-toward-viewer in the canonical frame. When
// that solve fails or reprojects the corners implausibly far from where
// the detector pinned them (the constructed quads need not be consistent
// with any true cube projection), orientation_valid is false and
// orientation is identity - honestly reported rather than guessed, and
// the vertices are still returned for the 2D wireframe.
//
// With a null seed the behaviour is EXACTLY the historical estimate. A
// seed makes the orientation solve TRY a seed-initialised iterative fit
// first (the seven-point problem has local minima the cold start can land
// in when the constructed quads are noisy); it is held to the identical
// cheirality + reprojection gates, and on any failure the unseeded solve
// runs as before - so a seed can only convert an invalid-orientation
// reading into a valid one or steer between gate-passing solutions, never
// weaken the gates.
[[nodiscard]] Result<CubePoseProjection> project_cube_from_corner(const CornerObservation& observation,
                                                                   cv::Size frame_size,
                                                                   const PoseSeed* seed = nullptr);

// As above from a two-face edge view: the two fitted quads pin six unique
// cube vertices (the shared edge's two endpoints coincide by the
// detector's gates and are averaged), a non-coplanar set that makes a
// well-posed generic PnP problem under the same approximate intrinsics.
// The model frame is the centred unit cube with the left quad playing F
// and the right playing R, so a successful solve lands near the cube
// yawed ~45 degrees (shared edge toward camera); which physical faces
// those are is the lattice gauge resolve_alignment absorbs like every
// other path's.
//
// Solve order, each attempt held to the identical cheirality +
// reprojection gates: seed-initialised iterative first (temporal
// continuity), then an iterative solve initialised at the canonical
// symmetric edge pose (the basin every genuine edge detection is in by
// construction - the detector only accepts left/right column grids), then
// a cold start. Unlike the corner path there is no independent vertex
// construction to fall back on, so a solve that passes no gate is an
// outright failure (no_valid_cube_detected), never an invalid-orientation
// projection.
//
// POSE-ONLY path end to end: callers feed the orientation to the tracker
// and nothing else - EdgeObservation carries no colour and this function
// must never grow a sampling side-channel.
[[nodiscard]] Result<CubePoseProjection> project_cube_from_edge(const EdgeObservation& observation,
                                                                 cv::Size frame_size,
                                                                 const PoseSeed* seed = nullptr);

}  // namespace cube::vision
