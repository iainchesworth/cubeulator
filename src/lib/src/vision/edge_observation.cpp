#include <cube/vision/edge_observation.hpp>

#include <cube/vision/face_grid_fit.hpp>
#include <cube/vision/face_observation.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <expected>
#include <limits>
#include <optional>
#include <utility>
#include <vector>

namespace cube::vision {

namespace {

using detail::StickerCandidate;

// Candidate floor and cap. Eight is deliberately far below the corner
// path's 18: an edge view shows 18 cells, both faces obliquely, in warm
// dim light - live1's oblique frames segmented 7-13 candidates while the
// cube was plainly there. The floor only prunes obviously-empty frames;
// the per-side/combined inlier gates below carry the real discrimination.
constexpr std::size_t kEdgeMinTotalCandidates = 8;
constexpr std::size_t kEdgeMaxCandidates = 40;

// Looser per-contour squareness bound than the frontal default (1.6): a
// 40-50-degree oblique face foreshortens its cells to ~1.4 nominal and
// past 1.6 once the shared edge's perspective magnification stretches the
// near column (measured on synthetic renders: 18 clean tiles yielded 6
// candidates under the frontal bound). 2.5 stays under the corner path's
// 3.0 - the split + paired-fit gates below carry the clutter rejection the
// squareness proxy gives up, exactly as they do there.
constexpr double kEdgeMaxCandidateAspect = 2.5;

// At most this many split boundaries are evaluated (the most balanced
// partitions with at least one side-floor of candidates per side): the
// split search is the only combinatorial dimension, and four boundaries
// around the median cover every plausible edge position at this candidate
// density while keeping the whole path at a handful of affine fits.
constexpr int kEdgeMaxSplits = 4;

// Per-side and combined inlier floors. A side floor of 4 accepts the
// foreshortened flank's sparse segmentation, and the combined floor of 9
// demands the pair jointly explain as many cells as a full frontal face
// fit would - one strong side can never drag a fantasy side along.
constexpr int kEdgeMinSideInliers = 4;
constexpr int kEdgeMinCombinedInliers = 9;

// Cross-face consistency: the two faces are the SAME cube at the same
// distance, so their row pitches (along the shared vertical edge, which
// foreshortens equally for both) must agree within this ratio...
constexpr float kEdgePitchRatioMax = 1.3F;
// ...and their inner column edges must both land on the shared cube edge.
// The tolerance is the larger of an absolute floor (8 px at 480-frame-
// width scale) and a column-pitch fraction: the fits are AFFINE while the
// projection is perspective, and the shared edge is the NEAR column where
// perspective magnifies the pitch, so both quads systematically undershoot
// it by a few-tenths of a pitch (measured ~0.4 combined on synthetic
// renders at close range). A WRONG pairing - overlapping grids, grids a
// face apart - errs by at least a full pitch, so 0.45 keeps 2x margin
// while absorbing the affine bias. These two gates are what a pair of
// unrelated grids (shirt texture plus cube, two clutter patches)
// essentially never passes together.
constexpr float kEdgeSharedTolerancePx = 8.0F;
constexpr float kEdgeSharedToleranceScaleWidth = 480.0F;
constexpr float kEdgeSharedPitchFraction = 0.45F;

// Canonical no-hint bases: an edge-view face projects its columns
// horizontally, foreshortened by its yaw, and its rows vertically at full
// pitch - so with the shorter (column) axis normalised to unit, the row
// axis carries the foreshortening ratio 1/cos(yaw). The per-sector pitch
// estimate (nearest-neighbour median) tracks the COLUMN spacing but is
// dragged by perspective, so a single sqrt(2) ratio only survives near
// the symmetric 45/45 view - measured on synthetic renders, a 40/50 split
// left the steeper face's rows outside the residual gate for every roll.
// Probing three ratios spanning the 40-50-degree yaw band (with the
// perspective drag folded in) x three rolls (the natural grip's wrist
// range) fixes that at nine cheap affine fits per side per split - still
// a bounded fixed set, NOT a 24-element pose probe. Each SIDE picks its
// own best variant (an asymmetric yaw means asymmetric ratios), and the
// pairing gates below re-impose cross-face consistency. While tracking, a
// pose-derived EdgeHint replaces the whole set with one basis per side.
constexpr std::array<float, 3> kEdgeCanonicalRowRatios = {1.25F, 1.41421356F, 1.6F};
constexpr std::array<float, 3> kEdgeCanonicalRollsDeg = {0.0F, 15.0F, -15.0F};

struct Basis {
    cv::Point2f col;
    cv::Point2f row;
};

[[nodiscard]] cv::Point2f rotated(cv::Point2f v, float deg) {
    const float rad = deg * 3.14159265358979F / 180.0F;
    const float c = std::cos(rad);
    const float s = std::sin(rad);
    return {v.x * c - v.y * s, v.x * s + v.y * c};
}

// Basis variants for one side: the hint's single pose-derived basis, or
// the canonical roll x ratio grid.
[[nodiscard]] std::vector<Basis> side_basis_sets(const EdgeHint* hint, std::size_t role) {
    std::vector<Basis> sets;
    if (hint != nullptr) {
        sets.push_back(Basis{hint->col_dir[role], hint->row_dir[role]});
        return sets;
    }
    for (const float roll : kEdgeCanonicalRollsDeg) {
        for (const float ratio : kEdgeCanonicalRowRatios) {
            sets.push_back(Basis{rotated({1.0F, 0.0F}, roll), rotated({0.0F, ratio}, roll)});
        }
    }
    return sets;
}

// Best fit of one side's centres over its basis variants: most inliers
// wins (the pairing gates re-check consistency, so a sloppier variant
// cannot smuggle a wrong grid past them on inlier count alone).
[[nodiscard]] std::optional<cube::vision::detail::FaceFit> best_side_fit(
    const std::vector<cv::Point2f>& centers, const std::vector<Basis>& sets) {
    std::optional<cube::vision::detail::FaceFit> best;
    for (const auto& basis : sets) {
        const auto fit = cube::vision::detail::fit_face_grid(
            centers, basis.col, basis.row, static_cast<std::size_t>(kEdgeMinSideInliers),
            kEdgeMinSideInliers);
        if (fit && (!best || fit->inlier_count > best->inlier_count)) {
            best = fit;
        }
    }
    return best;
}

[[nodiscard]] std::array<cv::Point2f, 4> face_quad(const detail::FaceFit& fit) {
    const cv::Point2f half_col = 1.5F * fit.col_step;
    const cv::Point2f half_row = 1.5F * fit.row_step;
    return {fit.anchor - half_col - half_row, fit.anchor + half_col - half_row,
            fit.anchor + half_col + half_row, fit.anchor - half_col + half_row};
}

}  // namespace

namespace detail {

Result<EdgeObservation> observe_edge_diagnosed(const cv::Mat& bgr_frame, const EdgeHint* hint,
                                               EdgeDiagnostics* diagnostics) {
    EdgeDiagnostics local{};
    EdgeDiagnostics& diag = diagnostics != nullptr ? *diagnostics : local;
    diag = EdgeDiagnostics{};

    if (bgr_frame.empty() || bgr_frame.type() != CV_8UC3) {
        return std::unexpected(CubeError::no_valid_cube_detected);
    }

    auto candidates = merged_face_candidates(bgr_frame, kEdgeMaxCandidateAspect);
    diag.candidate_count = candidates.size();
    if (candidates.size() < kEdgeMinTotalCandidates) {
        return std::unexpected(CubeError::no_valid_cube_detected);
    }
    if (candidates.size() > kEdgeMaxCandidates) {
        std::ranges::partial_sort(
            candidates, candidates.begin() + static_cast<std::ptrdiff_t>(kEdgeMaxCandidates), {},
            [](const StickerCandidate& candidate) { return -candidate.area; });
        candidates.resize(kEdgeMaxCandidates);
    }

    std::vector<cv::Point2f> centers;
    centers.reserve(candidates.size());
    for (const auto& candidate : candidates) {
        centers.push_back(candidate.center);
    }
    std::ranges::sort(centers, {}, [](const cv::Point2f& p) { return p.x; });
    const auto n = static_cast<int>(centers.size());

    // Split boundaries: index i puts centers [0, i) left of the line and
    // [i, n) right of it. Only boundaries leaving a side-floor's worth of
    // candidates on each side are viable, and of those the most BALANCED
    // are evaluated first - the shared edge of a real two-face view sits
    // near the middle of the candidate cloud by construction (both faces
    // segment comparably many cells).
    std::vector<int> boundaries;
    for (int i = kEdgeMinSideInliers; i <= n - kEdgeMinSideInliers; ++i) {
        boundaries.push_back(i);
    }
    std::ranges::sort(boundaries, {}, [n](int i) { return std::abs(2 * i - n); });
    if (boundaries.size() > static_cast<std::size_t>(kEdgeMaxSplits)) {
        boundaries.resize(static_cast<std::size_t>(kEdgeMaxSplits));
    }

    const auto left_sets = side_basis_sets(hint, 0);
    const auto right_sets = side_basis_sets(hint, 1);
    diag.basis_sets = static_cast<int>(left_sets.size());

    const float shared_tolerance_px = kEdgeSharedTolerancePx * static_cast<float>(bgr_frame.cols) /
                                      kEdgeSharedToleranceScaleWidth;

    std::optional<EdgeObservation> best;
    int best_combined = 0;
    float best_shared_px = std::numeric_limits<float>::max();

    for (const int boundary : boundaries) {
        ++diag.splits_evaluated;
        const float split_x = 0.5F * (centers[static_cast<std::size_t>(boundary - 1)].x +
                                      centers[static_cast<std::size_t>(boundary)].x);
        const std::vector<cv::Point2f> left_centers(centers.begin(), centers.begin() + boundary);
        const std::vector<cv::Point2f> right_centers(centers.begin() + boundary, centers.end());

        {
            const auto left_fit = best_side_fit(left_centers, left_sets);
            const auto right_fit = best_side_fit(right_centers, right_sets);
            if (!left_fit || !right_fit) {
                continue;
            }
            ++diag.paired_fits;
            const int combined = left_fit->inlier_count + right_fit->inlier_count;

            // Consistency facts, computed BEFORE the gates so the
            // diagnostics can record how far the best paired fit got and
            // which gate stopped it - the funnel's whole point.
            const auto left_quad = face_quad(*left_fit);
            const auto right_quad = face_quad(*right_fit);
            const float top_gap =
                std::hypot(left_quad[1].x - right_quad[0].x, left_quad[1].y - right_quad[0].y);
            const float bottom_gap =
                std::hypot(left_quad[2].x - right_quad[3].x, left_quad[2].y - right_quad[3].y);
            const float shared_px = std::max(top_gap, bottom_gap);
            const float left_row = std::hypot(left_fit->row_step.x, left_fit->row_step.y);
            const float right_row = std::hypot(right_fit->row_step.x, right_fit->row_step.y);
            const float pitch_ratio =
                left_row > 0.0F && right_row > 0.0F
                    ? std::max(left_row, right_row) / std::min(left_row, right_row)
                    : 0.0F;
            const float col_pitch =
                0.5F * (std::hypot(left_fit->col_step.x, left_fit->col_step.y) +
                        std::hypot(right_fit->col_step.x, right_fit->col_step.y));
            const float shared_gate_px =
                std::max(shared_tolerance_px, kEdgeSharedPitchFraction * col_pitch);
            if (combined > diag.best_left_inliers + diag.best_right_inliers) {
                diag.best_split_x = split_x;
                diag.best_left_inliers = left_fit->inlier_count;
                diag.best_right_inliers = right_fit->inlier_count;
                diag.best_pitch_ratio = pitch_ratio;
                diag.best_shared_edge_px = shared_px;
            }

            if (combined < kEdgeMinCombinedInliers) {
                continue;
            }
            // The refit may flex the basis but must not have flipped it: a
            // mirrored fit would silently swap the quads' corner labels
            // and hand the pose solve a chirally impossible cube.
            if (left_fit->col_step.x <= 0.0F || right_fit->col_step.x <= 0.0F ||
                left_fit->row_step.y <= 0.0F || right_fit->row_step.y <= 0.0F) {
                continue;
            }
            // Grids on opposite sides of the split line.
            if (left_fit->anchor.x >= split_x || right_fit->anchor.x <= split_x) {
                continue;
            }
            // Row pitches (along the shared edge, equally foreshortened
            // for both faces) must agree.
            if (pitch_ratio <= 0.0F || pitch_ratio > kEdgePitchRatioMax) {
                continue;
            }
            // Shared-edge coincidence: the left grid's right column edge
            // and the right grid's left column edge are the same physical
            // cube edge, endpoint for endpoint.
            if (shared_px > shared_gate_px) {
                continue;
            }

            if (combined > best_combined ||
                (combined == best_combined && shared_px < best_shared_px)) {
                EdgeObservation observation;
                observation.left_quad = left_quad;
                observation.right_quad = right_quad;
                observation.geometry_confidence = static_cast<float>(combined) / 18.0F;
                best = observation;
                best_combined = combined;
                best_shared_px = shared_px;
            }
        }
    }

    if (!best) {
        return std::unexpected(CubeError::no_valid_cube_detected);
    }
    diag.accepted = true;
    return *best;
}

}  // namespace detail

Result<EdgeObservation> observe_edge(const cv::Mat& bgr_frame, const EdgeHint* hint) {
    return detail::observe_edge_diagnosed(bgr_frame, hint, nullptr);
}

}  // namespace cube::vision
