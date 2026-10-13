#pragma once

#include <cube/error.hpp>
#include <cube/model/face.hpp>
#include <cube/vision/face_observation.hpp>

#include <opencv2/core.hpp>

#include <array>
#include <cstdint>

namespace cube::vision {

// The two prompted corner views that together show all six faces:
//
//   first_corner:  the cube held with U on top, F toward the viewer's
//                  lower-left and R toward the viewer's lower-right - the
//                  natural "show me a corner" pose from the standard
//                  U-up/F-front grip.
//   second_corner: after flipping the cube upside down and giving it a
//                  quarter turn: D on top, L toward the viewer's
//                  lower-left and B toward the viewer's lower-right.
//
// Face identity is assigned by prompt sequence (like the guided per-face
// flow), not by colour inference - the first corner scanned is always
// treated as U/F/R, the second as D/L/B.
enum class CornerViewId : std::uint8_t { first_corner, second_corner };

// One visible face within a corner view. `cells` and `lab` are ordered
// row-major in that face's own on-screen parallelogram (top-left of the
// drawn face first), NOT yet in the scan convention cube_from_scan()
// expects - corner_slot_mappings() provides that remapping.
struct CornerFaceView {
    std::array<cv::Point2f, 4> quad{};  // tl, tr, br, bl of the parallelogram
    std::array<StickerQuad, 9> cells{};
    std::array<cv::Vec3f, 9> lab{};
};

// One frame's reading of a three-faces-at-once corner view.
struct CornerObservation {
    CornerFaceView top;
    CornerFaceView lower_left;
    CornerFaceView lower_right;
    float geometry_confidence = 0.0F;
};

// Predicted-pose hint generalising observe_corner beyond the one prompted
// isometric pose: under free rotation the three visible faces can sit at
// ANY screen arrangement, so the fixed screen sectors and fixed lattice
// basis directions the prompted pose justifies stop matching the frame.
// The hint replaces exactly those two fixed pieces with directions derived
// from a predicted pose (compute_corner_hint, pose_estimation.hpp);
// everything else - candidate detection, plausibility checks, per-face
// affine refit, sampling - is geometry-generic and identical either way.
//
// All arrays are in view-role order {top, lower_left, lower_right},
// matching CornerObservation. The ROLES themselves are re-assigned by the
// hint (the most up-screen visible face plays "top", the other two follow
// by chirality), so a hinted observation's model frame is always the same
// U/F/R arrangement project_cube_from_corner and the orientation lattice's
// corner tables expect - which physical face actually plays which role is
// the lattice gauge the tracker resolves, exactly as at the fixed pose.
struct CornerHint {
    // Unit on-screen direction (image coords, y down) from the visible-
    // corner centroid toward each role face's centre: the sector centres
    // candidates are partitioned by (nearest direction wins - at the
    // prompted pose these are the bisector sectors the fixed path uses).
    std::array<cv::Point2f, 3> face_dir{};
    // On-screen lattice basis per role face: +1 column / +1 row of that
    // face's 3x3 cell grid, in the SAME cell-order convention the fixed
    // bases define (so the quad-corner coincidences the corner pose
    // derivation pins still hold - see project_cube_from_corner). Scaled
    // so the SHORTER of a face's pair is unit length and the longer keeps
    // their true foreshortening ratio: the per-sector pitch estimate is
    // the spacing along the more foreshortened axis (nearest-neighbour
    // median), so pitch * dir reproduces both steps of an obliquely viewed
    // face where equal-length bases would misclassify the outer cells.
    std::array<cv::Point2f, 3> col_dir{};
    std::array<cv::Point2f, 3> row_dir{};
};

// Locates a cube presented corner-on (three faces visible) by its
// hexagonal silhouette and samples all 27 visible stickers. Returns
// CubeError::no_valid_cube_detected when the frame doesn't contain a
// convincing corner-on cube.
//
// With a null hint the behaviour is EXACTLY the historical fixed-pose
// detector (the prompted isometric arrangement). A hint substitutes the
// predicted sectors/bases per CornerHint above; a wrong hint degrades to a
// failed detection or a low-inlier fit, never to differently-labelled
// stickers passing silently - the per-face lattice fits still demand the
// same inlier floors, and the pose/gauge layers downstream re-verify.
[[nodiscard]] Result<CornerObservation> observe_corner(const cv::Mat& bgr_frame,
                                                       const CornerHint* hint = nullptr);

// How each corner-view slot maps into the guided-scan convention: which
// model::Face the slot is, and the index permutation taking that slot's
// on-screen row-major cell order to the order a guided single-face scan of
// the same face would have produced (i.e. the input convention
// cube::solver::cube_from_scan() already expects). Derived from the
// solver's facelet-position ground truth (cube.cpp's position_of()) for
// the two poses documented on CornerViewId; verified by round-trip test.
struct CornerSlotMapping {
    cube::model::Face face;
    std::array<int, 9> to_scan_order{};
};

// Mappings for {top, lower_left, lower_right}, in that order.
[[nodiscard]] std::array<CornerSlotMapping, 3> corner_slot_mappings(CornerViewId view) noexcept;

}  // namespace cube::vision
