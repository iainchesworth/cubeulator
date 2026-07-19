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
    std::array<cv::Point2f, 4> quad{};   // tl, tr, br, bl of the parallelogram
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

// Locates a cube presented corner-on (three faces visible) by its
// hexagonal silhouette and samples all 27 visible stickers. Returns
// CubeError::no_valid_cube_detected when the frame doesn't contain a
// convincing corner-on cube.
[[nodiscard]] Result<CornerObservation> observe_corner(const cv::Mat& bgr_frame);

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
