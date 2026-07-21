#pragma once

#include <cube/error.hpp>
#include <cube/vision/grid_detection.hpp>

#include <opencv2/core.hpp>

#include <array>
#include <cstdint>
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
    // Per-cell sampling reliability: the fraction of the pixels inside the
    // cell's projected sample quad that agreed with the colour returned in
    // `lab` (see patch_stats_for_corners). 1.0 is a patch of one material;
    // a low value means the sample straddled two, so `lab` names only the
    // larger of them and the cell should be weighed accordingly.
    //
    // MEASUREMENT-ONLY FOR NOW - nothing downstream reads it and no gate
    // branches on it. It is carried on the observation rather than left in
    // the detector because the aggregator and the router are where a
    // reliability weight would eventually have to be applied, and the
    // signal has to be measurable there before any policy can be written
    // against it.
    std::array<float, 9> cell_purity{};
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

// One cell's sample-patch statistics, re-derived from a stored cell quad
// rather than from the (discarded) lattice transform. Exposed ONLY so the
// measurement harness can ask whether intra-patch dispersion predicts
// cross-reading colour instability; no production path reads it.
struct CellPatchSample {
    cv::Vec3f median;                // same statistic as FaceObservation::lab
    float weighted_mad = 0.0F;       // chroma-weighted intra-patch MAD
    float cell_quad_area_px = 0.0F;  // stored cell quad's screen area
    float purity = 0.0F;             // FaceObservation::cell_purity for this cell
    int quad_px = 0;                 // pixels genuinely inside the projected sample quad
    // The sample square production actually reads (the cell quad shrunk
    // about its centroid), in frame pixels. Emitted so the sampling-purity
    // harness can measure how much of the sampler's footprint falls outside
    // this quad without re-deriving the shrink and drifting from it.
    std::array<cv::Point2f, 4> sample_quad{};
};

// Recomputes the sample-patch statistics for `cell` straight off the
// frame. The sample square is the cell quad shrunk about its centroid by
// kSampleHalfExtent/kCellQuadHalfExtent - exact under an affine lattice
// fit, and within a fraction of a pixel of project_quad's homography at
// these cell sizes. The statistics themselves are NOT re-derived: this
// calls the same body sample_patch_stats calls.
[[nodiscard]] CellPatchSample cell_patch_sample(const cv::Mat& bgr_frame, const StickerQuad& cell);

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

// Which geometric model placed the cells of the most recent observe_face()
// call, and how many real contour centres backed it. PURELY DIAGNOSTIC:
// no production path reads it and nothing branches on it - it exists so
// the measurement harness can split readings by transform model without
// recomputing a lookalike transform that might not match the one the
// detector actually used.
enum class TransformModel : std::uint8_t {
    none,          // no observation produced
    homography,    // >= 5 matched centres, cv::findHomography
    ls_affine,     // exactly 4 matched centres, cv::estimateAffine2D
    basis_affine,  // lattice's own anchor/col_step/row_step frame
};

enum class PlacementPath : std::uint8_t {
    none,
    lattice_fit,  // translation-pinned direct fit
    completion,   // photometrically scored partial-lattice placement
    outline,      // face-outline quad fallback
    pose_only,    // un-pinned fit, orientation only
};

struct PlacementDiagnostic {
    TransformModel model = TransformModel::none;
    PlacementPath path = PlacementPath::none;
    int matched_cells = 0;  // contour centres feeding the transform estimate

    // Counterfactual placement. When the lattice fit had enough matched
    // centres to use a homography, the SAME frame is also sampled with the
    // lattice's own basis affine, so the harness can compare the two models
    // on identical pixels and identical detections. Nothing in the detector
    // reads these; they exist purely to separate the model's contribution
    // from the frame quality that selects which model gets used.
    bool has_counterfactual = false;
    std::array<cv::Vec3f, cells_per_grid> counterfactual_lab{};
};

// The diagnostic for the calling thread's most recent observe_face().
// Valid only immediately after that call returns.
[[nodiscard]] PlacementDiagnostic last_placement_diagnostic();

}  // namespace detail

}  // namespace cube::vision
