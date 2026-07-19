#pragma once

#include <cube/error.hpp>

#include <opencv2/core.hpp>

#include <array>

namespace cube::vision {

// Perspective-accurate quad for one sticker cell, in source-frame pixel
// coordinates, corners ordered top-left, top-right, bottom-right,
// bottom-left as seen in the frame.
struct StickerQuad {
    std::array<cv::Point2f, 4> corners{};
};

// One frame's reading of a single cube face: perspective-corrected
// geometry (for the AR overlay) plus a robust patch-median Lab colour
// sample per cell. Raw Lab rather than a classified FacetColor because
// classification happens downstream (color_resolution.hpp), where
// cross-frame and cross-face context is available - a single frame can't
// distinguish "orange under warm light" from "red under neutral light",
// but 54 stickers that must split exactly 9-per-colour can.
struct FaceObservation {
    std::array<StickerQuad, 9> cells{};      // row-major as seen in the frame
    std::array<cv::Vec3f, 9> lab{};          // OpenCV float Lab (L in [0,100])
    std::array<cv::Point2f, 4> face_quad{};  // outer face boundary, same corner order
    float geometry_confidence = 0.0F;        // fraction of cells matched by a real contour
};

// Locates a cube face's 3x3 sticker grid and samples its colours.
// Candidate stickers come from two independent strategies (locally-adaptive
// brightness thresholding and HSV colour-mask segmentation) so a sticker
// one strategy misses can still be found by the other; the candidates are
// arranged by the same RANSAC-style lattice fit detect_cube_grid() uses,
// then refined with a homography so cell geometry and sampling stay
// correct under perspective, not just for a dead-on view. Returns
// CubeError::no_valid_cube_detected when no acceptable lattice exists.
[[nodiscard]] Result<FaceObservation> observe_face(const cv::Mat& bgr_frame);

}  // namespace cube::vision
