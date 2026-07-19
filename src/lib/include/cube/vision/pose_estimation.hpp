#pragma once

#include <cube/error.hpp>
#include <cube/vision/corner_observation.hpp>
#include <cube/vision/face_observation.hpp>

#include <opencv2/core.hpp>

#include <array>

namespace cube::vision {

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
};

// Estimates the cube's 3D pose from a single detected face and projects
// the full cube. Camera intrinsics are approximated (principal point at
// the frame centre, focal length ~= frame width - adequate for an overlay,
// not for metrology) and the face is modelled as the unit square of a
// cube extending away from the camera. Fails (no_valid_cube_detected) when
// the pose estimate degenerates.
[[nodiscard]] Result<CubePoseProjection> project_cube_from_face(const FaceObservation& observation,
                                                                 cv::Size frame_size);

// As above from a corner view, where the three visible face quads pin
// seven of the eight corners directly; the hidden eighth is completed by
// parallelepiped symmetry. The near face slot for vertex ordering is the
// TOP face's quad.
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
[[nodiscard]] Result<CubePoseProjection> project_cube_from_corner(const CornerObservation& observation,
                                                                   cv::Size frame_size);

}  // namespace cube::vision
