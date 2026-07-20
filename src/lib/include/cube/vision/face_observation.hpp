#pragma once

#include <cube/error.hpp>
#include <cube/vision/grid_detection.hpp>

#include <opencv2/core.hpp>

#include <array>
#include <vector>

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
    // ORIENTATION-ONLY observation: the lattice fit was geometrically sane
    // and phase-plausible but its anchors spanned fewer than three rows or
    // columns, so the grid's TRANSLATION is ambiguous - the cells (and
    // therefore the lab samples) may sit one pitch off the true stickers.
    // Translation ambiguity is irrelevant to the pose's ORIENTATION (a
    // pitch-shifted quad of the same planar grid solves to the same
    // rotation up to perspective noise), which is exactly what the
    // orientation tracker consumes - so these observations feed
    // project_cube_from_face and the tracker, and MUST NOT feed colour
    // aggregation, facelet display or capture. Live1 measured ~25-35
    // frames per session dying solely on the translation pin while the
    // tracker starved for poses; this flag recovers them without touching
    // capture-grade acceptance.
    bool pose_only = false;
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

// Diagnostics seam for the offline frame-replay harness. Not a stable API
// for anything outside the vision module or its tests.
namespace detail {

// The full deduped union of every candidate source observe_face runs
// (both brightness polarities at three scales, the saturation cuts, the
// white mask). Exposed for the edge-view detector (edge_observation.cpp),
// which partitions the same candidates across a two-face split instead of
// fitting one planar lattice - running a second, divergent extraction
// stack there is how the two paths would rot apart. max_aspect loosens
// only the per-contour squareness gate (see kDefaultMaxCandidateAspect):
// the edge path's obliquely foreshortened cells legitimately exceed the
// frontal bound, while observe_face itself always uses the default.
[[nodiscard]] std::vector<StickerCandidate> merged_face_candidates(
    const cv::Mat& bgr_frame, double max_aspect = kDefaultMaxCandidateAspect);

// One photometrically-scored placement of a partial lattice (see
// grid_detection.hpp's partial_lattice_placements). Deviations are
// chroma-weighted Lab distances from the placement's own lattice
// reference - the colour sampled BETWEEN its matched anchor tiles.
struct ScoredPlacement {
    cv::Mat transform;                // 3x3 lattice->frame affine
    int inlier_count = 0;             // anchors backing the underlying lattice
    float mean_gap_deviation = 0.0F;  // mean over the 12 inter-cell gap midpoints
    float peak_gap_deviation = 0.0F;  // worst single gap midpoint
    float anchor_contrast = 0.0F;     // median anchor-tile deviation from the lattice reference
    float cell_mad = 0.0F;            // mean intra-patch deviation over the 9 cell samples
    float min_cell_lightness = 0.0F;  // darkest cell's L relative to the median cell's L
    float boundary_contrast = 0.0F;   // weakest per-side inside-vs-outside face-edge contrast
    bool accepted = false;            // passed every photometric gate
};

// Scores every plausible partial-lattice placement for the frame, ordered
// the way observe_face()'s completion fallback consumes them (accepted
// first, then most anchors, then tightest gap deviation). Exposed so the
// frame-debug harness can print the exact scores the production path saw.
[[nodiscard]] std::vector<ScoredPlacement> score_partial_placements(const cv::Mat& bgr_frame);

}  // namespace detail

}  // namespace cube::vision
