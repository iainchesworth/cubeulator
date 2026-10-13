#pragma once

#include <opencv2/core.hpp>

#include <array>
#include <cstddef>
#include <optional>
#include <vector>

namespace cube::vision::detail {

// ---------------------------------------------------------------------------
// Shared per-face 3x3 lattice fitting over candidate sticker centres.
//
// Hoisted verbatim from corner_observation.cpp's anonymous namespace when
// the two-face edge-view detector (edge_observation.hpp) arrived: both
// detectors partition candidates into per-face sectors and then need the
// identical pitch-estimate -> provisional classification -> affine refit
// -> final classification pipeline per face, and duplicating a
// least-squares fitter is how the two paths drift apart. The corner
// detector's behaviour is bit-identical through the hoist: the default
// floors below are exactly the constants it always used.
//
// Not a stable API for anything outside the vision module or its tests.
// ---------------------------------------------------------------------------

// A candidate must land within this fraction of one cell pitch of an exact
// integer lattice point to count as an inlier for that slot - the same
// tolerance grid_detection.cpp gates its lattice fit with.
inline constexpr float kMaxLatticeResidualRatio = 0.35F;

// Historical corner-path floors, the defaults for fit_face_grid: a corner
// view demands most of a face's cells before trusting a fit. The edge path
// passes its own (lower) floors - an obliquely foreshortened face
// legitimately segments fewer cells, and the edge detector compensates
// with cross-face consistency gates the corner path doesn't need.
inline constexpr std::size_t kDefaultMinFaceCandidates = 6;
inline constexpr int kDefaultMinFaceInliers = 6;

// Median nearest-neighbour distance between centres - a robust
// (outlier-tolerant, unlike a mean) estimate of the grid's cell pitch.
[[nodiscard]] float estimate_face_pitch(const std::vector<cv::Point2f>& centers);

struct SlotAssignment {
    std::array<int, 9> slots{};  // candidate index per slot, -1 unmatched
    int inlier_count = 0;
};

// Nearest-integer-lattice-slot assignment under (anchor, col_step,
// row_step), keeping the closer of any two candidates competing for one
// slot - the same scheme grid_detection.cpp's classify_candidates() uses.
[[nodiscard]] SlotAssignment classify_to_slots(const std::vector<cv::Point2f>& centers,
                                               cv::Point2f anchor, cv::Point2f col_step,
                                               cv::Point2f row_step);

struct FaceFit {
    cv::Point2f anchor;    // centre-cell (row=1, col=1) position
    cv::Point2f col_step;  // +1 column basis vector
    cv::Point2f row_step;  // +1 row basis vector
    int inlier_count = 0;
};

// Full 6-parameter affine least squares over the matched slots (anchor +
// both basis vectors), rather than grid_detection's anchor-only recentre:
// a hand-held oblique pose shears and scales each face's lattice away from
// the nominal directions, and the basis must be allowed to follow or cell
// projection/sampling drifts off the outer stickers.
[[nodiscard]] std::optional<FaceFit> refit_affine(const std::vector<cv::Point2f>& centers,
                                                  const std::array<int, 9>& slots);

// Fits one visible face's 3x3 parallelogram lattice: nominal pose-derived
// basis scaled by the sector's own pitch, provisional classification,
// affine refit, final classification. Floors default to the corner path's
// historical values (see above).
[[nodiscard]] std::optional<FaceFit> fit_face_grid(
    const std::vector<cv::Point2f>& centers, cv::Point2f col_dir, cv::Point2f row_dir,
    std::size_t min_candidates = kDefaultMinFaceCandidates,
    int min_inliers = kDefaultMinFaceInliers);

}  // namespace cube::vision::detail
