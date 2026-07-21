#include <cube/vision/face_observation.hpp>

#include <cube/vision/grid_detection.hpp>

#include <opencv2/calib3d.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <optional>
#include <utility>
#include <vector>

namespace cube::vision {

namespace {

using detail::StickerCandidate;

// HSV colour-mask segmentation thresholds. Any coloured sticker clears the
// saturation floor regardless of brightness; a white sticker fails it, so a
// second bright-AND-unsaturated clause admits white without also admitting
// the dark unsaturated background and inter-sticker gaps. This path exists
// because the brightness-threshold path (find_square_candidates) can lose a
// dim saturated sticker to its local neighborhood, while saturation is
// nearly lighting-invariant - and vice versa for white stickers, which only
// brightness can separate. The union covers both failure modes.
// 40, not 60: real webcam exposure washes strongly-coloured tiles toward
// pastel (an overexposed red reads salmon at S ~ 45-70), and a too-high
// floor loses exactly the cells the brightness paths also struggle with.
// The extra low-saturation clutter this admits (skin, wood) is filtered by
// the shape gates and the lattice residual, not the mask.
constexpr int kMinStickerSaturation = 40;
constexpr int kMinWhiteValue = 150;

// Cell quads span 0.40 lattice units from the cell center (one lattice unit
// = one cell pitch, sticker + gap), so adjacent quads leave the gap between
// stickers uncovered instead of overlapping across it.
constexpr float kCellQuadHalfExtent = 0.40F;
// 3 cells of half-a-pitch each side of the center: the face's outer edge.
constexpr float kFaceHalfExtent = 1.5F;
// Sampled patch = the central ~40% of a cell quad (0.40 * 0.40 lattice
// units): enough pixels for a meaningful median, far enough inside the
// sticker that a few pixels of fit error can't bleed gap/background colour
// into the sample.
constexpr float kSampleHalfExtent = kCellQuadHalfExtent * 0.40F;
// Two strategies routinely find the same sticker with centers a few px
// apart; half a median cell side separates "same sticker seen twice" from
// "adjacent stickers" with margin on both sides.
constexpr float kDedupeRadiusFraction = 0.5F;

// A visibly-off contour center (a merged blob, a partly-occluded sticker)
// must not drag the whole face's geometry; RANSAC needs a reprojection
// tolerance looser than OpenCV's 3px default because matched centers are
// bounding-rect midpoints, which sit a few px off the true projected cell
// center under perspective - they are noisy inliers, not outliers.
constexpr double kHomographyReprojThreshold = 8.0;

// Photometric gates for the partial-lattice completion fallback (see
// score_placements below). Deviations are chroma-weighted Lab distances
// from the placement's own lattice reference (the colour sampled between
// its anchor tiles). The gap deviation is an ABSURDITY cap only, not the
// placement discriminator: on a flush-tile cube the inter-tile lattice is
// only a few pixels wide at webcam scale, so gap samples blend the
// neighbouring tiles - measured on real frames, CORRECT placements ran
// gap means of 17-31 while the metrics that actually separated correct
// from shifted placements were cell lightness and cell-patch structure.
constexpr float kCompletionMaxMeanGapDeviation = 45.0F;
// The anchors themselves must contrast with the lattice reference: real
// tiles segmented as local minima/chromatic blobs always do, while a
// false "lattice" seeded on uniform clutter (curtain speckle, skin
// texture) samples the same colour at its anchors as between them - the
// cheapest possible uniform-region rejector.
constexpr float kCompletionMinAnchorContrast = 10.0F;
// No cell of a real face reads much darker than the face's median cell:
// tiles and lattice alike are lit cube material. A placement translated
// off the face drops a row/column onto whatever is behind the cube, and
// against any normally-lit scene that is far darker (a shirt, a shadowed
// room) or at least boundary-riddled (caught by the structure ranking).
constexpr float kCompletionMinCellLightnessRatio = 0.35F;
// Every side of a real face must show inside-vs-outside contrast at its
// edge - the cube ends there. A translated placement's trailing edge sits
// INSIDE the real cube where both sides of the "edge" read tile material
// (measured: correct placements 24+ on every real frame, translated ones
// 0.8-13, including one whose extra row sat on the user's forehead and
// passed every cell-based gate - skin is bright AND uniform, but it does
// not end where the placement claims the cube does).
constexpr float kCompletionMinBoundaryContrast = 10.0F;

// Inlier floor for the pose-only fallback (see observe_face's tail): four
// residual-gated anchors match the lattice fitter's own acceptance floor,
// and fewer could not pin the plane's basis vectors against noise. Kept as
// a named constant (rather than reusing the fitter's) because the two
// gates protect different outputs - the fitter's floor guards capture
// candidacy, this one guards a pose - and the calibration sweeps may want
// to move them independently.
constexpr int kPoseOnlyMinInliers = 4;

// Degenerate-transform guard bounds (see transform_grid_sane): adjacent
// projected cell centers closer than this are a collapsed fit (observed
// live: a 4-point homography through 3 true tiles plus one noise blob
// mapped the whole lattice to a single pixel, and the phase check passed
// trivially because cells and gaps sampled the same point); further apart
// than half the frame's short side cannot be a real cell pitch (three
// cells must fit the frame).
constexpr float kMinProjectedCellSpacing = 6.0F;
// A real (near-square, mildly-perspective) face never shears its basis
// vectors close to parallel; sin of the minimum angle between them.
constexpr float kMinBasisAngleSine = 0.34F;  // ~20 degrees

// The saturated and white masks are contoured SEPARATELY, never OR-ed into
// one mask: on a light-bodied cube (white plastic lattice between tiles -
// diagnosed from a real webcam frame), the white separator grid touches
// every coloured sticker, so a union mask welds the whole face into one
// blob the area cap rejects - while the saturated mask ALONE segments every
// coloured sticker cleanly (the lattice has no saturation). White cells
// that can't be told apart from a white lattice are recovered downstream by
// lattice extrapolation, which samples their true colour anyway. Masks come
// from the UNBLURRED frame (Gaussian bleed pushes enough sticker chroma
// into narrow gaps to bridge adjacent cells - a ~20% colour mix over a dark
// gap still reads strongly saturated), and only open (no close) runs so
// preserved separations aren't re-sealed.
[[nodiscard]] std::vector<StickerCandidate> candidates_from_condition_mask(cv::Mat mask, double max_aspect) {
    const auto kernel = cv::getStructuringElement(cv::MORPH_RECT, cv::Size(3, 3));
    cv::morphologyEx(mask, mask, cv::MORPH_OPEN, kernel);
    return detail::candidates_from_binary_mask(mask, max_aspect);
}

[[nodiscard]] std::vector<StickerCandidate> find_saturation_candidates(const std::vector<cv::Mat>& hsv_channels,
                                                                       double max_aspect) {
    return candidates_from_condition_mask(hsv_channels[1] >= kMinStickerSaturation, max_aspect);
}

// A THIRD saturation cut at a PERMISSIVE floor, contoured independently
// (live1 F4): warm dim indoor light washes matte tiles to S 20-35, under
// the standard 40 floor, and the frontal-but-failed census class was
// starving right here - sharp frontal frames with 0-6 candidates while
// the cube filled the view. The extra low-saturation clutter this admits
// (more skin, more wood) is filtered where it always was: the shape
// gates, the lattice residual and the completion/phase gates - acceptance
// thresholds are unchanged, only candidate SUPPLY grows.
constexpr int kPermissiveStickerSaturation = 25;

[[nodiscard]] std::vector<StickerCandidate> find_permissive_saturation_candidates(
    const std::vector<cv::Mat>& hsv_channels, double max_aspect) {
    return candidates_from_condition_mask(hsv_channels[1] >= kPermissiveStickerSaturation, max_aspect);
}

// A FOURTH cut at a HIGH floor (live1 F4, frame 448 class): under warm
// dim light the cube's WHITE tiles pick up enough colour cast to clear
// even the strict floor (measured S 57-73 on a real frame), so every
// saturation mask below this welds the whole face - tiles, lattice and
// white cells - into one blob the shape gates reject, and the face
// produces ZERO candidates while filling the view sharply. At 100 the
// colour-cast whites and the washed lattice drop out and the six-plus
// genuinely chromatic tiles (S 103-189 on the same frame) segment
// individually. Purely additive supply, like every other source.
constexpr int kVeryStrictStickerSaturation = 100;

[[nodiscard]] std::vector<StickerCandidate> find_very_strict_saturation_candidates(
    const std::vector<cv::Mat>& hsv_channels, double max_aspect) {
    return candidates_from_condition_mask(hsv_channels[1] >= kVeryStrictStickerSaturation, max_aspect);
}

// A SECOND saturation cut at a stricter floor, contoured independently:
// the permissive floor exists to catch washed-out pastel tiles, but on a
// sheen-heavy frame it can also grow a tile's blob until it merges with
// its neighbour or the surrounding wash and fails the shape filters -
// while the stricter cut still isolates that tile's saturated core
// (observed on a real frame: S>=60 segmented a teal tile cleanly that
// S>=40 dissolved). Both cuts feed the same dedupe, so the extra source
// only ever ADDS candidates the permissive one lost.
constexpr int kStrictStickerSaturation = 60;

[[nodiscard]] std::vector<StickerCandidate> find_strict_saturation_candidates(
    const std::vector<cv::Mat>& hsv_channels, double max_aspect) {
    return candidates_from_condition_mask(hsv_channels[1] >= kStrictStickerSaturation, max_aspect);
}

[[nodiscard]] std::vector<StickerCandidate> find_white_candidates(const std::vector<cv::Mat>& hsv_channels,
                                                                  double max_aspect) {
    cv::Mat white_mask;
    cv::bitwise_and(hsv_channels[2] >= kMinWhiteValue, hsv_channels[1] < kMinStickerSaturation, white_mask);
    return candidates_from_condition_mask(std::move(white_mask), max_aspect);
}

// Unions the strategies' candidates, collapsing near-coincident pairs (the
// same physical sticker found by more than one source) so the lattice fit
// doesn't see phantom double-density. Larger area wins a collapse - the
// fuller of two detections of one sticker bounds it better.
[[nodiscard]] std::vector<StickerCandidate> merge_candidates(std::vector<std::vector<StickerCandidate>> sources) {
    std::vector<StickerCandidate> merged;
    for (auto& source : sources) {
        merged.insert(merged.end(), source.begin(), source.end());
    }
    if (merged.size() < 2) {
        return merged;
    }

    std::vector<float> sides;
    sides.reserve(merged.size());
    for (const auto& candidate : merged) {
        sides.push_back(static_cast<float>(std::max(candidate.bounds.width, candidate.bounds.height)));
    }
    const auto mid = sides.begin() + static_cast<std::ptrdiff_t>(sides.size() / 2);
    std::ranges::nth_element(sides, mid);
    const float dedupe_radius = *mid * kDedupeRadiusFraction;

    std::ranges::sort(merged, {}, [](const StickerCandidate& candidate) { return -candidate.area; });

    std::vector<StickerCandidate> unique;
    unique.reserve(merged.size());
    for (const auto& candidate : merged) {
        const bool duplicate = std::ranges::any_of(unique, [&](const StickerCandidate& kept) {
            const cv::Point2f d = kept.center - candidate.center;
            return std::hypot(d.x, d.y) < dedupe_radius;
        });
        if (!duplicate) {
            unique.push_back(candidate);
        }
    }
    return unique;
}

[[nodiscard]] cv::Mat basis_affine(cv::Point2f anchor, cv::Point2f col_step, cv::Point2f row_step) {
    cv::Mat affine = cv::Mat::eye(3, 3, CV_64F);
    affine.at<double>(0, 0) = static_cast<double>(col_step.x);
    affine.at<double>(0, 1) = static_cast<double>(row_step.x);
    affine.at<double>(0, 2) = static_cast<double>(anchor.x);
    affine.at<double>(1, 0) = static_cast<double>(col_step.y);
    affine.at<double>(1, 1) = static_cast<double>(row_step.y);
    affine.at<double>(1, 2) = static_cast<double>(anchor.y);
    return affine;
}

// Maps lattice coordinates (col-1, row-1 in [-1,1], cell centers at
// integers) to frame pixels. With >= 5 matched centers a full homography is
// estimable with at least one point of redundancy, which the affine
// anchor/basis model cannot express: under perspective the far row's pitch
// is genuinely shorter than the near row's, and only a homography places
// extrapolated cells correctly there. Exactly 4 matches get a least-squares
// affine instead - a 4-point homography is an EXACT solve through noisy
// bounding-rect midpoints with no averaging, and one bad center (a merged
// blob, a clutter contour the residual gate let through) shears or outright
// collapses the whole lattice (observed live: all nine cells projected to
// one pixel). Below 4 (or if estimation degenerates) the lattice's own
// affine frame is the best available model.
// Diagnostic-only record of how the current thread's last observation was
// placed. Written on every path, read only by the measurement harness via
// detail::last_placement_diagnostic(); nothing branches on it.
thread_local detail::PlacementDiagnostic g_placement_diagnostic{};

[[nodiscard]] cv::Mat lattice_to_frame_transform(const GridFitDetail& fit) {
    std::vector<cv::Point2f> lattice_points;
    std::vector<cv::Point2f> frame_points;
    for (int row = 0; row < grid_dimension; ++row) {
        for (int col = 0; col < grid_dimension; ++col) {
            const auto slot = static_cast<std::size_t>(row * grid_dimension + col);
            if (!fit.matched_centers[slot]) {
                continue;
            }
            lattice_points.emplace_back(static_cast<float>(col - 1), static_cast<float>(row - 1));
            frame_points.push_back(*fit.matched_centers[slot]);
        }
    }

    g_placement_diagnostic.matched_cells = static_cast<int>(lattice_points.size());

    if (lattice_points.size() >= 5) {
        cv::Mat homography = cv::findHomography(lattice_points, frame_points, cv::RANSAC, kHomographyReprojThreshold);
        if (!homography.empty()) {
            homography.convertTo(homography, CV_64F);
            g_placement_diagnostic.model = detail::TransformModel::homography;
            return homography;
        }
    }

    if (lattice_points.size() >= 4) {
        cv::Mat affine2x3 =
            cv::estimateAffine2D(lattice_points, frame_points, cv::noArray(), cv::RANSAC, kHomographyReprojThreshold);
        if (!affine2x3.empty()) {
            cv::Mat affine = cv::Mat::eye(3, 3, CV_64F);
            affine2x3.convertTo(affine2x3, CV_64F);
            affine2x3.copyTo(affine(cv::Rect(0, 0, 3, 2)));
            g_placement_diagnostic.model = detail::TransformModel::ls_affine;
            return affine;
        }
    }

    g_placement_diagnostic.model = detail::TransformModel::basis_affine;
    return basis_affine(fit.anchor, fit.col_step, fit.row_step);
}

[[nodiscard]] cv::Point2f project_lattice_point(const cv::Mat& transform, float u, float v) {
    const auto ud = static_cast<double>(u);
    const auto vd = static_cast<double>(v);
    const double x = transform.at<double>(0, 0) * ud + transform.at<double>(0, 1) * vd + transform.at<double>(0, 2);
    const double y = transform.at<double>(1, 0) * ud + transform.at<double>(1, 1) * vd + transform.at<double>(1, 2);
    const double w = transform.at<double>(2, 0) * ud + transform.at<double>(2, 1) * vd + transform.at<double>(2, 2);
    // w -> 0 only for a degenerate estimate placing the point at infinity;
    // clamping keeps the arithmetic finite rather than emitting NaN corners.
    const double safe_w = std::abs(w) > 1e-9 ? w : 1e-9;
    return cv::Point2f(static_cast<float>(x / safe_w), static_cast<float>(y / safe_w));
}

// Corners ordered top-left, top-right, bottom-right, bottom-left in lattice
// space; the homography preserves that winding for any physically-plausible
// (non-mirrored) view, matching StickerQuad's documented order.
[[nodiscard]] std::array<cv::Point2f, 4> project_quad(const cv::Mat& transform, cv::Point2f center_uv,
                                                       float half_extent) {
    return {
        project_lattice_point(transform, center_uv.x - half_extent, center_uv.y - half_extent),
        project_lattice_point(transform, center_uv.x + half_extent, center_uv.y - half_extent),
        project_lattice_point(transform, center_uv.x + half_extent, center_uv.y + half_extent),
        project_lattice_point(transform, center_uv.x - half_extent, center_uv.y + half_extent),
    };
}

// Rejects transforms whose projected grid cannot be a real cube face: any
// adjacent pair of cell centers collapsed together or blown past a
// plausible pitch, a non-finite projection, or basis directions sheared
// near-parallel. Cheap, purely geometric, and applied to EVERY candidate
// transform regardless of origin (lattice fit, completion, outline) -
// the photometric checks downstream all assume cells and gaps sample
// DIFFERENT pixels, which is exactly what a degenerate transform breaks.
[[nodiscard]] bool transform_grid_sane(const cv::Mat& transform, cv::Size frame_size) {
    std::array<cv::Point2f, cells_per_grid> centers;
    for (int row = 0; row < grid_dimension; ++row) {
        for (int col = 0; col < grid_dimension; ++col) {
            const auto center =
                project_lattice_point(transform, static_cast<float>(col - 1), static_cast<float>(row - 1));
            if (!std::isfinite(center.x) || !std::isfinite(center.y)) {
                return false;
            }
            centers[static_cast<std::size_t>(row * grid_dimension + col)] = center;
        }
    }

    const float max_spacing = static_cast<float>(std::min(frame_size.width, frame_size.height)) * 0.5F;
    const auto spacing_sane = [&](const cv::Point2f& a, const cv::Point2f& b) {
        const float spacing = std::hypot(a.x - b.x, a.y - b.y);
        return spacing >= kMinProjectedCellSpacing && spacing <= max_spacing;
    };
    for (int row = 0; row < grid_dimension; ++row) {
        for (int col = 0; col < grid_dimension; ++col) {
            const auto slot = static_cast<std::size_t>(row * grid_dimension + col);
            if (col + 1 < grid_dimension && !spacing_sane(centers[slot], centers[slot + 1])) {
                return false;
            }
            if (row + 1 < grid_dimension &&
                !spacing_sane(centers[slot], centers[slot + static_cast<std::size_t>(grid_dimension)])) {
                return false;
            }
        }
    }

    const cv::Point2f col_dir = centers[5] - centers[3];  // row 1: col 2 - col 0
    const cv::Point2f row_dir = centers[7] - centers[1];  // col 1: row 2 - row 0
    const float cross = col_dir.x * row_dir.y - col_dir.y * row_dir.x;
    const float lengths = std::hypot(col_dir.x, col_dir.y) * std::hypot(row_dir.x, row_dir.y);
    return lengths > 0.0F && std::abs(cross) >= lengths * kMinBasisAngleSine;
}

[[nodiscard]] float channel_median(std::vector<float>& values) {
    const auto mid = values.begin() + static_cast<std::ptrdiff_t>(values.size() / 2);
    std::ranges::nth_element(values, mid);
    return *mid;
}

// Chroma-weighted Lab distance: a and b at full weight, L at half. The
// lattice's own lightness varies by 20+ across one face under directional
// room light (shading, sheen), while its CHROMA is nearly constant - and
// chroma is precisely what separates lattice from skin (b* +15..25), from
// tiles (either axis, hugely), and from shadowed background. Hoisted above
// the patch sampler because the same argument holds INSIDE one tile: the
// dominant-mode test below must not call a shaded corner of a tile a
// different material, and the patch MAD already weights L this way.
[[nodiscard]] float weighted_lab_distance(const cv::Vec3f& a, const cv::Vec3f& b) {
    const float dl = (a[0] - b[0]) * 0.5F;
    const float da = a[1] - b[1];
    const float db = a[2] - b[2];
    return std::sqrt(dl * dl + da * da + db * db);
}

// Agreement radius for the dominant-mode refinement below, in the same
// chroma-weighted units. Measured over 8883 real cell samples from the
// runA-D recordings: intra-patch MAD is 1.22 at the median and 2.0 at p90,
// so a genuinely single-material patch has essentially all of its pixels
// within 6 of its own median (purity ~1) - while the contrast that a
// contaminant brings is an order of magnitude larger (a tile against the
// dark background behind the cube measures 30+). The radius therefore has
// to separate 2 from 30, and anywhere in the middle works; 6 is 3x the p90
// dispersion of a clean patch, the loosest value that still excludes a
// second material outright.
constexpr float kModeAgreementRadius = 6.0F;
// Below this many in-quad pixels the mask is not a usable sample - a
// projected cell can be only a few pixels across on a distant or heavily
// foreshortened face - so the sampler falls back to the whole bounding
// rect rather than returning a statistic drawn from two or three pixels.
// A sampler that rejects such cells would trade colour purity for
// coverage, and coverage is the thing the scan cannot do without.
constexpr int kMinQuadPixels = 8;
// Likewise for the mode refinement: it may only ever REFINE a median that
// a majority already supports. If fewer than this fraction of the in-quad
// pixels agree with the current estimate, the patch has no dominant mode
// and the plain masked median is kept (and the low purity says so).
constexpr float kMinModeShare = 0.25F;
// Mode-seek iteration bounds. Four rounds is a safety cap: on the corpus
// the walk settles in one or two (patches are single-material - intra-patch
// MAD is 1.22 at the median - so the very first window already holds
// everything and the estimate does not move), and only a genuinely split
// patch takes more. The settle epsilon is well under the 1.22 median
// dispersion, so stopping on it cannot stop short of a real move.
constexpr int kMaxModeRounds = 4;
constexpr float kModeSettledEpsilon = 0.05F;

// WHAT A PATCH IS BEING SAMPLED FOR. The two uses want opposite things and
// conflating them is what cost runA two faces in the first version of this
// change (measured: URFDLB -> UFDL, filled slots 6 -> 4).
//
//   kCellColour  - "what colour is this sticker". Wants the quad mask and
//                  the dominant-mode walk: every pixel that is not the
//                  sticker is an error to be excluded.
//   kMaterialMix - the completion scorer's structural probes (inter-tile
//                  gaps, the anchor/lattice contrast, the inside-vs-outside
//                  boundary strips) and the intra-patch MAD. These are
//                  DELIBERATELY aimed at multi-material locations; their
//                  whole signal is how much foreign material a patch
//                  contains. Masking to the quad and then walking onto the
//                  dominant mode reports the majority tile instead of the
//                  mixture - it does not sharpen these probes, it deletes
//                  what they measure. Bisected in a clean worktree: mask
//                  alone -> runA 4, refinement alone -> runA 4, both off ->
//                  runA 6, and the loss is NOT generic noise sensitivity (a
//                  uniform L+1.0 and a per-cell jitter of the same 0.23
//                  weighted-Lab mean magnitude as the mask both leave runA
//                  at 6). It is these probes being fed a different kind of
//                  statistic than they were calibrated on.
//
// So kMaterialMix reproduces the shipped rect-median exactly and placement
// scoring is bit-identical to before this round; only the emitted cell
// colour changes. Re-pointing the scorer at masked samples may well be
// right, but it is a downstream-policy change and out of this round's scope.
enum class PatchIntent : std::uint8_t { kCellColour, kMaterialMix };

// Robust per-patch statistics: channel medians plus a chroma-weighted
// median absolute deviation. The MAD is the completion scorer's structure
// probe - a sample patch lying INSIDE one tile is uniform (small MAD)
// while a patch straddling a tile boundary, the face's outer edge, or a
// finger contour is not, which is exactly what separates a correctly
// placed grid from one translated by a cell pitch. The two statistics
// deliberately have DIFFERENT footprints: `median` is masked to the quad
// because it is a colour, `weighted_mad` spans the bounding rect because
// the foreign material at the patch's corners is the very evidence it
// exists to detect.
struct PatchStats {
    cv::Vec3f median;
    float weighted_mad = 0.0F;
    // Fraction of the pixels behind `median` that agree with it, i.e. lie
    // within kModeAgreementRadius of the colour returned. 1.0 is a
    // single-material patch; well below 1 means the sample straddles two
    // materials and the colour returned names only the larger. Read it with
    // quad_px: when quad_px < kMinQuadPixels the sample fell back to the
    // bounding rect and this is rect agreement, not in-quad agreement.
    //
    // Defaults to 0 ("unknown/unusable"), NOT 1: a reliability signal whose
    // default means "maximally clean" fails open, which is the bug review
    // found in the first version of this field.
    float purity = 0.0F;
    int quad_px = 0;  // pixels genuinely inside the projected quad
};

// Patch-median Lab statistics for the cell centered at `cell_uv`.
//
// Median, not mean: a glare highlight or a few gap pixels leaking into the
// patch shift a mean sample toward white/black but leave the median
// untouched until they exceed half the patch.
//
// MASKED to the projected quad, not medianed over its bounding rect. The
// rect is what the frame can be indexed by; it is NOT the sample. For a
// quad rotated in-plane by theta the rect's area grows as (cos + sin)^2 -
// 2x at 45 degrees - and the excess sits at the CORNERS, where under a
// cube face the inter-tile lattice, the neighbouring stickers and (on an
// outer cell) the scene behind the cube live. Measured over the runA-D
// recordings the rect carries 16.9% off-quad pixels on average, 55.7% in
// the 40-45 degree rotation bucket, so under free rotation the shipped
// statistic was drawn from a majority of pixels it never intended to read.
// It survived only because kSampleHalfExtent shrinks the sample square to
// 0.16 lattice units, whose bounding rect at 45 degrees still has a
// half-diagonal of 0.226 - inside the 0.40 cell quad - so the stray pixels
// happened to land on the same sticker. That is an accident of one
// constant, not a property of the sampler: 7.2% of the pixels read in the
// 40-45 degree bucket DID leave the cell quad. Masking removes the
// dependency.
//
// ...but ONLY for `intent == kCellColour`. See PatchIntent: the structural
// probes must keep reading the mixture, so they take the rect median.
[[nodiscard]] PatchStats patch_stats_for_corners(const cv::Mat& bgr_frame,
                                                 const std::array<cv::Point2f, 4>& corners,
                                                 PatchIntent intent) {
    float min_x = corners[0].x;
    float max_x = corners[0].x;
    float min_y = corners[0].y;
    float max_y = corners[0].y;
    for (const auto& corner : corners) {
        min_x = std::min(min_x, corner.x);
        max_x = std::max(max_x, corner.x);
        min_y = std::min(min_y, corner.y);
        max_y = std::max(max_y, corner.y);
    }

    const int x0 = std::clamp(static_cast<int>(std::floor(min_x)), 0, bgr_frame.cols - 1);
    const int y0 = std::clamp(static_cast<int>(std::floor(min_y)), 0, bgr_frame.rows - 1);
    const int x1 = std::clamp(static_cast<int>(std::ceil(max_x)), x0 + 1, bgr_frame.cols);
    const int y1 = std::clamp(static_cast<int>(std::ceil(max_y)), y0 + 1, bgr_frame.rows);
    const cv::Mat patch = bgr_frame(cv::Rect(x0, y0, x1 - x0, y1 - y0));

    // Float Lab via the library-wide convention (see color_classification's
    // bgr_to_lab): scale to [0,1] before conversion so L lands in [0,100].
    cv::Mat patch_float;
    patch.convertTo(patch_float, CV_32FC3, 1.0 / 255.0);
    cv::Mat lab;
    cv::cvtColor(patch_float, lab, cv::COLOR_BGR2Lab);

    // Point-in-convex-quad by edge sign. A homography maps a square to a
    // convex quad with its winding preserved, so a pixel is inside exactly
    // when it lies on the same side of all four directed edges - the same
    // predicate cv::fillConvexPoly rasterises, evaluated in place rather
    // than through a mask Mat: a mask would have to be rasterised and then
    // scanned a second time to gather the pixels it selected, and that
    // gather is the same loop this predicate already runs.
    //
    // The earlier "an allocation would cost more than the test" rationale
    // did not survive review, and is withdrawn rather than patched up: the
    // function allocated its vectors on every call regardless. When I
    // hoisted them into thread_local scratch to make the claim true, the
    // cost did not move (15.2 vs 15.1 us per cell, three runs each), so the
    // allocations were never the cost - the extra passes over the pixels
    // are. The vectors are plain locals and the claim is gone.
    //
    // The winding is read off the quad's own signed area
    // instead of assumed, so a mirrored transform cannot silently invert
    // the test and mask away the entire patch.
    float signed_area = 0.0F;
    for (std::size_t k = 0; k < 4; ++k) {
        const cv::Point2f& current = corners[k];
        const cv::Point2f& next = corners[(k + 1) % 4];
        signed_area += current.x * next.y - next.x * current.y;
    }
    const float winding = signed_area >= 0.0F ? 1.0F : -1.0F;
    const auto inside_quad = [&](float px, float py) {
        for (std::size_t k = 0; k < 4; ++k) {
            const cv::Point2f& current = corners[k];
            const cv::Point2f& next = corners[(k + 1) % 4];
            const float cross = (next.x - current.x) * (py - current.y) - (next.y - current.y) * (px - current.x);
            if (cross * winding < 0.0F) {
                return false;
            }
        }
        return true;
    };

    const auto pixel_count = static_cast<std::size_t>(lab.rows) * static_cast<std::size_t>(lab.cols);
    const bool mask_to_quad = intent == PatchIntent::kCellColour;

    // THE RECT PASS, run for both intents because the MAD needs it either
    // way. This is verbatim the pre-fix function: one walk of the rect into
    // three channel vectors, median, MAD. A kMaterialMix patch does this and
    // NOTHING else, so the structural probes cost exactly what they always
    // did - which matters, because they outnumber the cell colours by an
    // order of magnitude per frame (12 gap + up to 9 anchor + 9 cell + 24
    // boundary probes per SCORED PLACEMENT, against 9 cell colours per
    // observation). An earlier revision of this change routed them through
    // the masked path as well and put 58% on whole-clip replay to compute a
    // statistic those callers then discarded.
    std::vector<float> l_values;
    std::vector<float> a_values;
    std::vector<float> b_values;
    l_values.reserve(pixel_count);
    a_values.reserve(pixel_count);
    b_values.reserve(pixel_count);
    for (int y = 0; y < lab.rows; ++y) {
        for (int x = 0; x < lab.cols; ++x) {
            const auto& pixel = lab.at<cv::Vec3f>(y, x);
            l_values.push_back(pixel[0]);
            a_values.push_back(pixel[1]);
            b_values.push_back(pixel[2]);
        }
    }
    // channel_median permutes its argument, and the MAD below needs only the
    // multiset, so taking the rect median here costs nothing extra.
    const cv::Vec3f rect_median(channel_median(l_values), channel_median(a_values), channel_median(b_values));

    PatchStats stats;
    stats.median = rect_median;

    // Everything from here down to the MAD is kCellColour-only.
    std::vector<cv::Vec3f> masked;
    std::vector<cv::Vec3f> mode_pixels;
    std::vector<float> scratch_l;
    std::vector<float> scratch_a;
    std::vector<float> scratch_b;
    const auto medians_of = [&](const std::vector<cv::Vec3f>& pixels) {
        scratch_l.clear();
        scratch_a.clear();
        scratch_b.clear();
        for (const auto& pixel : pixels) {
            scratch_l.push_back(pixel[0]);
            scratch_a.push_back(pixel[1]);
            scratch_b.push_back(pixel[2]);
        }
        return cv::Vec3f(channel_median(scratch_l), channel_median(scratch_a), channel_median(scratch_b));
    };

    if (mask_to_quad) {
        masked.reserve(pixel_count);
        mode_pixels.reserve(pixel_count);
        scratch_l.reserve(pixel_count);
        scratch_a.reserve(pixel_count);
        scratch_b.reserve(pixel_count);
        for (int y = 0; y < lab.rows; ++y) {
            const float py = static_cast<float>(y0 + y) + 0.5F;
            for (int x = 0; x < lab.cols; ++x) {
                if (!inside_quad(static_cast<float>(x0 + x) + 0.5F, py)) {
                    continue;
                }
                masked.push_back(lab.at<cv::Vec3f>(y, x));
            }
        }
        stats.quad_px = static_cast<int>(masked.size());
        // YIELD GUARD: a sub-pixel or degenerate quad must still produce a
        // reading. Keeping the rect median already computed above reproduces
        // the pre-mask behaviour for those cells exactly rather than dropping
        // them; `masked` is emptied so the refinement and the purity figure
        // below both stand down, leaving purity 0 - which for this cell means
        // "not measurable", not "filthy". Read it together with quad_px.
        if (stats.quad_px < kMinQuadPixels) {
            masked.clear();
        } else {
            stats.median = medians_of(masked);
        }
    }

    // DOMINANT-MODE REFINEMENT. Masking bounds the GEOMETRIC contamination
    // but not the photometric kind - a cast shadow edge, a fingertip, a
    // mis-registered grid landing the patch across a tile boundary - and a
    // plain median is unbiased only while the minority is small enough to
    // leave the median inside the tile's own distribution. At a 45-50%
    // contaminant the median survives in the right material but sits at its
    // extreme edge, which is a colour the tile never actually is.
    //
    // So the estimate is walked onto the dominant cluster: re-median over
    // the pixels within kModeAgreementRadius of the current estimate, and
    // repeat. ONE step is not enough - starting at the cluster's edge, a
    // single re-median only reaches the middle of the half of the cluster
    // the first window happened to admit (measured on the synthetic
    // 48%-contaminant case in face_observation_test: 41 -> 47, against a
    // true tile centre of 60). The iteration is a median-of-window fixed
    // point: it moves monotonically toward the dense region and stops, so
    // the round cap is a safety bound rather than the usual exit.
    const auto gather_mode = [&](const cv::Vec3f& centre) {
        mode_pixels.clear();
        for (const auto& pixel : masked) {
            if (weighted_lab_distance(pixel, centre) <= kModeAgreementRadius) {
                mode_pixels.push_back(pixel);
            }
        }
        return masked.empty() ? 0.0F
                              : static_cast<float>(mode_pixels.size()) / static_cast<float>(masked.size());
    };

    for (int round = 0; !masked.empty() && round < kMaxModeRounds; ++round) {
        const float share = gather_mode(stats.median);
        // A window holding almost nothing is not a mode; keep the plain
        // masked median rather than chase a handful of pixels. Note this
        // leaves `purity` to the recomputation below, which will report the
        // genuinely low share - the previous code broke out of the loop
        // without ever assigning it, so the patches with NO dominant mode
        // (exactly the ones the signal exists to flag) reported 1.0.
        if (mode_pixels.empty() || share < kMinModeShare) {
            break;
        }
        const cv::Vec3f refined = medians_of(mode_pixels);
        const bool settled = weighted_lab_distance(refined, stats.median) < kModeSettledEpsilon;
        stats.median = refined;
        if (settled) {
            break;
        }
    }

    // PER-CELL RELIABILITY: the share of in-quad pixels agreeing with the
    // colour finally returned. 1.0 is a patch of one material; well below 1
    // means the sample straddled two and `median` names only the larger.
    // Deliberately NOT an absolute-lightness rule - a genuinely dark tile is
    // uniform and scores high, while a shadow boundary across a bright tile
    // scores low, which is exactly the distinction an L threshold cannot
    // make and the one a rejection policy would need.
    //
    // Recomputed HERE against the final median rather than carried out of
    // the loop, so it describes the colour actually returned in all three
    // exits: converged, no-dominant-mode, and the kMaxModeRounds cap (where
    // the loop's last computed share belonged to the PREVIOUS estimate).
    // When the quad was sub-pixel and the rect fallback fired
    // (quad_px < kMinQuadPixels) this is agreement within the RECT, not
    // within the quad - the caller must read it together with quad_px.
    // A kMaterialMix patch reports purity 0: not "filthy", but "not asked".
    // Its median is a rect statistic by design, so an in-quad agreement
    // figure would be a category error dressed as a measurement. Likewise
    // the sub-pixel fallback above.
    stats.purity = masked.empty() ? 0.0F : gather_mode(stats.median);

    // Dispersion stays a property of the whole BOUNDING RECT, deliberately
    // NOT of the masked quad, for BOTH intents. It is not a colour estimate;
    // it is the completion scorer's mis-registration probe, and what exposes
    // a grid translated by a cell pitch is precisely the foreign material at
    // the patch's corners - the pixels the mask exists to remove. Masking it
    // measurably blunted the probe on the corpus (runA mean MAD 1.554 ->
    // 1.461, sd 1.950 -> 1.593: the discriminating tail compressed 18% while
    // the median barely moved), and cell_mad is the sort key that breaks
    // ties between equally-acceptable placements.
    const auto channel_mad = [](std::vector<float>& values, float median) {
        for (auto& value : values) {
            value = std::abs(value - median);
        }
        return channel_median(values);
    };
    // Centred on the RECT's own median, not on the masked/refined colour, so
    // the probe is end-to-end identical to the shipped one and this round
    // moves it by exactly zero.
    const float mad_l = channel_mad(l_values, rect_median[0]);
    const float mad_a = channel_mad(a_values, rect_median[1]);
    const float mad_b = channel_mad(b_values, rect_median[2]);
    stats.weighted_mad = std::sqrt(0.25F * mad_l * mad_l + mad_a * mad_a + mad_b * mad_b);
    return stats;
}

[[nodiscard]] PatchStats sample_patch_stats(const cv::Mat& bgr_frame, const cv::Mat& transform, cv::Point2f cell_uv,
                                            PatchIntent intent) {
    return patch_stats_for_corners(bgr_frame, project_quad(transform, cell_uv, kSampleHalfExtent), intent);
}

// Every caller of this overload is a STRUCTURAL probe - gap material, the
// lattice reference, the inside/outside boundary strips - so it is fixed at
// kMaterialMix rather than parameterised. A cell colour goes through
// sample_patch_stats directly, where the intent has to be spelled out.
[[nodiscard]] cv::Vec3f sample_patch_lab(const cv::Mat& bgr_frame, const cv::Mat& transform, cv::Point2f cell_uv) {
    return sample_patch_stats(bgr_frame, transform, cell_uv, PatchIntent::kMaterialMix).median;
}

}  // namespace

namespace {

[[nodiscard]] FaceObservation build_observation(const cv::Mat& bgr_frame, const cv::Mat& transform,
                                                 float geometry_confidence) {
    FaceObservation observation;
    for (int row = 0; row < grid_dimension; ++row) {
        for (int col = 0; col < grid_dimension; ++col) {
            const auto slot = static_cast<std::size_t>(row * grid_dimension + col);
            const cv::Point2f cell_uv(static_cast<float>(col - 1), static_cast<float>(row - 1));
            observation.cells[slot].corners = project_quad(transform, cell_uv, kCellQuadHalfExtent);
            const auto stats = sample_patch_stats(bgr_frame, transform, cell_uv, PatchIntent::kCellColour);
            observation.lab[slot] = stats.median;
            observation.cell_purity[slot] = stats.purity;
        }
    }
    observation.face_quad = project_quad(transform, cv::Point2f(0.0F, 0.0F), kFaceHalfExtent);
    observation.geometry_confidence = geometry_confidence;
    return observation;
}

// Grid-phase validation: on any real cube the midpoints BETWEEN adjacent
// cells are lattice/gap material - they must never read MORE chromatic than
// the cells themselves. A half-pitch-offset fit inverts that exactly (its
// "cells" sample the white lattice, its "gaps" sample the tiles), and such
// a fit is stable frame to frame, so temporal convergence alone can NOT
// reject it - offset captures were landing with phantom white rows and
// mis-keyed centres (observed live: an orange-centred face slotted as
// blue). Equality is fine (a white-heavy face reads white both places);
// only the inversion is disqualifying.
// Lattice-space midpoints of the 12 gaps between 4-adjacent cells.
[[nodiscard]] const std::array<cv::Point2f, 12>& gap_midpoints() {
    // static const, not constexpr: cv::Point2f's constructors are not
    // constexpr in this OpenCV build.
    static const std::array<cv::Point2f, 12> points = {{
        {-0.5F, -1.0F}, {0.5F, -1.0F}, {-0.5F, 0.0F}, {0.5F, 0.0F}, {-0.5F, 1.0F}, {0.5F, 1.0F},
        {-1.0F, -0.5F}, {0.0F, -0.5F}, {1.0F, -0.5F}, {-1.0F, 0.5F}, {0.0F, 0.5F}, {1.0F, 0.5F},
    }};
    return points;
}

[[nodiscard]] bool grid_phase_plausible(const cv::Mat& bgr_frame, const cv::Mat& transform,
                                         const FaceObservation& observation) {
    constexpr float kInversionMargin = 8.0F;

    float cell_chroma = 0.0F;
    for (const auto& lab : observation.lab) {
        cell_chroma += std::hypot(lab[1], lab[2]);
    }
    cell_chroma /= static_cast<float>(observation.lab.size());

    float gap_chroma = 0.0F;
    for (const auto& midpoint : gap_midpoints()) {
        const auto lab = sample_patch_lab(bgr_frame, transform, midpoint);
        gap_chroma += std::hypot(lab[1], lab[2]);
    }
    gap_chroma /= static_cast<float>(gap_midpoints().size());

    return gap_chroma <= cell_chroma + kInversionMargin;
}

[[nodiscard]] std::vector<StickerCandidate> merged_face_candidate_union(
    const cv::Mat& bgr_frame, const std::vector<cv::Mat>& hsv_channels,
    double max_aspect = detail::kDefaultMaxCandidateAspect) {
    return merge_candidates({detail::find_square_candidates(bgr_frame, max_aspect),
                             detail::find_dark_square_candidates(bgr_frame, max_aspect),
                             find_saturation_candidates(hsv_channels, max_aspect),
                             find_strict_saturation_candidates(hsv_channels, max_aspect),
                             find_very_strict_saturation_candidates(hsv_channels, max_aspect),
                             find_permissive_saturation_candidates(hsv_channels, max_aspect),
                             find_white_candidates(hsv_channels, max_aspect)});
}

// Partial-lattice completion, the fallback for frames where too few cells
// segment for a unique lattice fit (a white-heavy face on a white-lattice
// cube yields 2-3 coloured tiles): every plausible placement of a partial
// lattice (grid_detection's partial_lattice_placements) is scored
// photometrically and gated. The key idea is SELF-CALIBRATION: the colour
// midway between two adjacent anchor tiles is, by construction, the
// cube's own lattice/gap material as lit in THIS frame - so a placement is
// real exactly when all 12 of its gap midpoints read as that reference.
// Every wrong placement fails somewhere: shifted one pitch, some "gap"
// lands on a tile (chromatic); slid off-cube, gaps land on shirt (dark) or
// skin (warm-chromatic); seeded on uniform clutter, the anchors
// themselves fail to contrast with the reference.
[[nodiscard]] std::vector<detail::ScoredPlacement> score_placements(const cv::Mat& bgr_frame,
                                                                    const std::vector<StickerCandidate>& candidates) {
    std::vector<detail::ScoredPlacement> scored;
    for (const auto& placement : detail::partial_lattice_placements(candidates, bgr_frame.size())) {
        cv::Mat transform = basis_affine(placement.anchor, placement.col_step, placement.row_step);

        std::vector<std::pair<int, int>> matched;  // (row, col)
        for (int row = 0; row < grid_dimension; ++row) {
            for (int col = 0; col < grid_dimension; ++col) {
                if (placement.matched_centers[static_cast<std::size_t>(row * grid_dimension + col)]) {
                    matched.emplace_back(row, col);
                }
            }
        }

        std::vector<cv::Vec3f> reference_samples;
        for (std::size_t i = 0; i < matched.size(); ++i) {
            for (std::size_t j = i + 1; j < matched.size(); ++j) {
                const int row_gap = std::abs(matched[i].first - matched[j].first);
                const int col_gap = std::abs(matched[i].second - matched[j].second);
                if (row_gap + col_gap != 1) {
                    continue;
                }
                const cv::Point2f midpoint(
                    0.5F * static_cast<float>(matched[i].second + matched[j].second) - 1.0F,
                    0.5F * static_cast<float>(matched[i].first + matched[j].first) - 1.0F);
                reference_samples.push_back(sample_patch_lab(bgr_frame, transform, midpoint));
            }
        }
        // No two anchors adjacent means no trustworthy lattice reference -
        // nothing this placement claims can be validated, so it is never
        // accepted.
        if (reference_samples.empty()) {
            continue;
        }
        cv::Vec3f reference;
        for (int channel = 0; channel < 3; ++channel) {
            std::vector<float> values;
            values.reserve(reference_samples.size());
            for (const auto& sample : reference_samples) {
                values.push_back(sample[channel]);
            }
            reference[channel] = channel_median(values);
        }

        detail::ScoredPlacement entry;
        entry.transform = std::move(transform);
        entry.inlier_count = placement.inlier_count;

        float deviation_sum = 0.0F;
        for (const auto& midpoint : gap_midpoints()) {
            const float deviation =
                weighted_lab_distance(sample_patch_lab(bgr_frame, entry.transform, midpoint), reference);
            deviation_sum += deviation;
            entry.peak_gap_deviation = std::max(entry.peak_gap_deviation, deviation);
        }
        entry.mean_gap_deviation = deviation_sum / static_cast<float>(gap_midpoints().size());

        std::vector<float> contrasts;
        contrasts.reserve(matched.size());
        for (const auto& [row, col] : matched) {
            const cv::Point2f cell_uv(static_cast<float>(col - 1), static_cast<float>(row - 1));
            contrasts.push_back(
                weighted_lab_distance(sample_patch_lab(bgr_frame, entry.transform, cell_uv), reference));
        }
        entry.anchor_contrast = channel_median(contrasts);

        float mad_sum = 0.0F;
        std::vector<float> cell_lightness;
        cell_lightness.reserve(cells_per_grid);
        for (int row = 0; row < grid_dimension; ++row) {
            for (int col = 0; col < grid_dimension; ++col) {
                const cv::Point2f cell_uv(static_cast<float>(col - 1), static_cast<float>(row - 1));
                // kMaterialMix: this is placement scoring, not colour
                // reporting. cell_mad and min_cell_lightness are the
                // discriminators between a correct placement and one
                // translated by a cell pitch, and they were calibrated on
                // the rect statistic.
                const auto stats =
                    sample_patch_stats(bgr_frame, entry.transform, cell_uv, PatchIntent::kMaterialMix);
                mad_sum += stats.weighted_mad;
                cell_lightness.push_back(stats.median[0]);
            }
        }
        entry.cell_mad = mad_sum / static_cast<float>(cells_per_grid);
        const float min_lightness = *std::ranges::min_element(cell_lightness);
        const float median_lightness = channel_median(cell_lightness);
        entry.min_cell_lightness = median_lightness > 0.0F ? min_lightness / median_lightness : 0.0F;

        // Boundary contrast: the face ends at +-1.5 lattice units, so a
        // strip just inside each edge and one just outside it should read
        // DIFFERENT material (cube vs shirt/hand/room) on all four sides.
        // A placement translated by one pitch keeps its trailing edge
        // inside the real cube, where inside and outside read the same
        // tile material - the lowest-contrast side exposes it.
        entry.boundary_contrast = std::numeric_limits<float>::max();
        for (int side = 0; side < 4; ++side) {
            std::vector<float> pair_contrasts;
            pair_contrasts.reserve(3);
            for (int lane = -1; lane <= 1; ++lane) {
                const auto lane_f = static_cast<float>(lane);
                cv::Point2f inside_uv;
                cv::Point2f outside_uv;
                switch (side) {
                    case 0: inside_uv = {lane_f, -1.2F}; outside_uv = {lane_f, -1.8F}; break;
                    case 1: inside_uv = {lane_f, 1.2F}; outside_uv = {lane_f, 1.8F}; break;
                    case 2: inside_uv = {-1.2F, lane_f}; outside_uv = {-1.8F, lane_f}; break;
                    default: inside_uv = {1.2F, lane_f}; outside_uv = {1.8F, lane_f}; break;
                }
                pair_contrasts.push_back(
                    weighted_lab_distance(sample_patch_lab(bgr_frame, entry.transform, inside_uv),
                                          sample_patch_lab(bgr_frame, entry.transform, outside_uv)));
            }
            entry.boundary_contrast = std::min(entry.boundary_contrast, channel_median(pair_contrasts));
        }

        entry.accepted = entry.mean_gap_deviation <= kCompletionMaxMeanGapDeviation &&
                         entry.anchor_contrast >= kCompletionMinAnchorContrast &&
                         entry.min_cell_lightness >= kCompletionMinCellLightnessRatio &&
                         entry.boundary_contrast >= kCompletionMinBoundaryContrast;
        scored.push_back(std::move(entry));
    }

    // Among acceptable placements the discriminator is STRUCTURE, not the
    // gap match: on a flush-tile cube the inter-tile lattice is 1-3px at
    // webcam scale, so gap samples mostly read the adjacent tiles and the
    // correct and one-pitch-shifted placements tie on gap deviation. Cell
    // patch uniformity does separate them - a translated grid's cells
    // straddle tile boundaries and the face's outer edge.
    std::ranges::sort(scored, [](const detail::ScoredPlacement& a, const detail::ScoredPlacement& b) {
        if (a.accepted != b.accepted) {
            return a.accepted;
        }
        if (a.inlier_count != b.inlier_count) {
            return a.inlier_count > b.inlier_count;
        }
        return a.cell_mad < b.cell_mad;
    });
    return scored;
}

// Outline fallback: when too few interior cells segment (a white-heavy
// face on a white lattice, sheen-gutted saturation), the cube's whole
// silhouette against the room is usually still crisp. The largest
// plausibly-square blob of "cube material" (chromatic OR bright) gives the
// face boundary; thirds of its minAreaRect give the grid. The result faces
// exactly the same phase validation as a lattice fit, so a silhouette
// polluted by gripping fingers fails the gap check instead of capturing
// garbage.
[[nodiscard]] std::optional<cv::Mat> outline_transform(const cv::Mat& bgr_frame,
                                                        const std::vector<cv::Mat>& hsv_channels) {
    cv::Mat mask;
    cv::bitwise_or(hsv_channels[1] >= 25, hsv_channels[2] >= 150, mask);
    const auto kernel = cv::getStructuringElement(cv::MORPH_RECT, cv::Size(5, 5));
    cv::morphologyEx(mask, mask, cv::MORPH_OPEN, kernel);
    cv::morphologyEx(mask, mask, cv::MORPH_CLOSE, kernel);

    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(mask, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);

    const double frame_area = static_cast<double>(bgr_frame.cols) * bgr_frame.rows;
    const double min_area = frame_area * 0.02;
    const double max_area = frame_area * 0.45;

    const std::vector<cv::Point>* best = nullptr;
    double best_area = 0.0;
    for (const auto& contour : contours) {
        const double area = cv::contourArea(contour);
        if (area < min_area || area > max_area || area <= best_area) {
            continue;
        }
        const cv::RotatedRect rect = cv::minAreaRect(contour);
        if (rect.size.width <= 0.0F || rect.size.height <= 0.0F) {
            continue;
        }
        const float squareness =
            std::max(rect.size.width, rect.size.height) / std::min(rect.size.width, rect.size.height);
        // Tighter than the per-cell bound: a whole cube face reads as a
        // near-square even under moderate tilt, while a hand-plus-cube
        // blob is elongated - the cheapest finger filter available here.
        if (squareness > 1.35F) {
            continue;
        }
        // The blob must FILL its rect like a solid face, not a hull around
        // scattered content.
        const double rect_area = static_cast<double>(rect.size.width) * rect.size.height;
        if (area / rect_area < 0.80) {
            continue;
        }
        best = &contour;
        best_area = area;
    }
    if (best == nullptr) {
        return std::nullopt;
    }

    const cv::RotatedRect rect = cv::minAreaRect(*best);
    std::array<cv::Point2f, 4> pts;
    rect.points(pts.data());
    // rect.points orders bl, tl, tr, br; the affine below only needs a
    // consistent pair of edge directions and the centre.
    const cv::Point2f col_step = (pts[2] - pts[1]) / 3.0F;
    const cv::Point2f row_step = (pts[0] - pts[1]) / 3.0F;

    cv::Mat affine = cv::Mat::eye(3, 3, CV_64F);
    affine.at<double>(0, 0) = static_cast<double>(col_step.x);
    affine.at<double>(0, 1) = static_cast<double>(row_step.x);
    affine.at<double>(0, 2) = static_cast<double>(rect.center.x);
    affine.at<double>(1, 0) = static_cast<double>(col_step.y);
    affine.at<double>(1, 1) = static_cast<double>(row_step.y);
    affine.at<double>(1, 2) = static_cast<double>(rect.center.y);
    return affine;
}

}  // namespace

Result<FaceObservation> observe_face(const cv::Mat& bgr_frame) {
    g_placement_diagnostic = detail::PlacementDiagnostic{};
    if (bgr_frame.empty()) {
        return std::unexpected(CubeError::no_valid_cube_detected);
    }

    cv::Mat hsv;
    cv::cvtColor(bgr_frame, hsv, cv::COLOR_BGR2HSV);
    std::vector<cv::Mat> hsv_channels;
    cv::split(hsv, hsv_channels);

    const auto candidates = merged_face_candidate_union(bgr_frame, hsv_channels);
    const auto fit = detail::fit_lattice_detailed(candidates, bgr_frame.size());
    // The fit alone earns a direct accept only when its matched slots pin
    // the TRANSLATION: anchors in the first and last row AND the first and
    // last column. A fit whose anchors span only two rows (or columns) is
    // geometrically consistent in more than one placement - observed live
    // on real frames: five genuine anchors covering two rows fitted as the
    // grid's top two rows, extrapolating the third onto the hand below the
    // cube. Un-pinned fits fall through to the completion path, which
    // scores every placement photometrically instead of trusting the one
    // the fitter happened to anchor.
    bool translation_pinned = false;
    if (fit) {
        int min_row = grid_dimension;
        int max_row = -1;
        int min_col = grid_dimension;
        int max_col = -1;
        for (int row = 0; row < grid_dimension; ++row) {
            for (int col = 0; col < grid_dimension; ++col) {
                if (!fit->matched_centers[static_cast<std::size_t>(row * grid_dimension + col)]) {
                    continue;
                }
                min_row = std::min(min_row, row);
                max_row = std::max(max_row, row);
                min_col = std::min(min_col, col);
                max_col = std::max(max_col, col);
            }
        }
        translation_pinned = min_row == 0 && max_row == grid_dimension - 1 && min_col == 0 &&
                             max_col == grid_dimension - 1;
    }
    if (fit && translation_pinned) {
        const cv::Mat transform = lattice_to_frame_transform(*fit);
        if (transform_grid_sane(transform, bgr_frame.size())) {
            auto observation = build_observation(
                bgr_frame, transform, static_cast<float>(fit->inlier_count) / static_cast<float>(cells_per_grid));
            if (grid_phase_plausible(bgr_frame, transform, observation)) {
                g_placement_diagnostic.path = detail::PlacementPath::lattice_fit;
                // Counterfactual: re-sample this frame with the lattice's own
                // affine frame, so the harness can compare both models on
                // identical pixels and identical detections. Diagnostic only -
                // the returned observation is untouched and still carries the
                // transform the detector actually chose.
                if (g_placement_diagnostic.model == detail::TransformModel::homography) {
                    const cv::Mat affine = basis_affine(fit->anchor, fit->col_step, fit->row_step);
                    for (int row = 0; row < grid_dimension; ++row) {
                        for (int col = 0; col < grid_dimension; ++col) {
                            const auto slot = static_cast<std::size_t>(row * grid_dimension + col);
                            const cv::Point2f cell_uv(static_cast<float>(col - 1), static_cast<float>(row - 1));
                            g_placement_diagnostic.counterfactual_lab[slot] =
                                sample_patch_lab(bgr_frame, affine, cell_uv);
                        }
                    }
                    g_placement_diagnostic.has_counterfactual = true;
                }
                return observation;
            }
        }
        // A phase-inverted or degenerate lattice fit is worse than no fit:
        // fall through to the completion/outline paths rather than
        // capturing lattice material (or one repeated pixel) as sticker
        // colours.
    }

    // Too few cells for a unique fit: try every plausible placement of a
    // partial lattice, best photometric score first. The scoring gates do
    // the heavy validation; the phase check stays as the final authority
    // exactly as for the other paths.
    for (const auto& scored : score_placements(bgr_frame, candidates)) {
        if (!scored.accepted) {
            break;  // sorted accepted-first; nothing acceptable remains
        }
        if (!transform_grid_sane(scored.transform, bgr_frame.size())) {
            continue;
        }
        auto observation =
            build_observation(bgr_frame, scored.transform,
                              static_cast<float>(scored.inlier_count) / static_cast<float>(cells_per_grid));
        if (grid_phase_plausible(bgr_frame, scored.transform, observation)) {
            // score_placements builds every placement with basis_affine.
            g_placement_diagnostic.model = detail::TransformModel::basis_affine;
            g_placement_diagnostic.path = detail::PlacementPath::completion;
            g_placement_diagnostic.matched_cells = scored.inlier_count;
            return observation;
        }
    }

    if (const auto transform = outline_transform(bgr_frame, hsv_channels)) {
        if (transform_grid_sane(*transform, bgr_frame.size())) {
            auto observation = build_observation(bgr_frame, *transform, 0.4F);
            if (grid_phase_plausible(bgr_frame, *transform, observation)) {
                // outline_transform builds an anchor/col_step/row_step
                // affine off the outline's minAreaRect.
                g_placement_diagnostic.model = detail::TransformModel::basis_affine;
                g_placement_diagnostic.path = detail::PlacementPath::outline;
                g_placement_diagnostic.matched_cells = 0;
                return observation;
            }
        }
    }

    // Last resort, ORIENTATION-ONLY: a real lattice fit whose anchors span
    // only two rows (or columns) failed the translation pin above, and the
    // photometric completion/outline paths could not rescue a capture-grade
    // placement either - but the fit's PLANE is still good. Its basis
    // vectors are measured from real tile centres, so the face quad's
    // orientation is right even if the whole grid sits one pitch off, and
    // orientation is all the tracker needs (see FaceObservation::pose_only:
    // these observations are barred from aggregation and capture by
    // contract). Held to the same sanity + phase gates as every accepted
    // transform - a degenerate or phase-inverted fit describes no real
    // plane and must not emit a pose either.
    if (fit && !translation_pinned && fit->inlier_count >= kPoseOnlyMinInliers) {
        const cv::Mat transform = lattice_to_frame_transform(*fit);
        if (transform_grid_sane(transform, bgr_frame.size())) {
            auto observation = build_observation(
                bgr_frame, transform, static_cast<float>(fit->inlier_count) / static_cast<float>(cells_per_grid));
            if (grid_phase_plausible(bgr_frame, transform, observation)) {
                observation.pose_only = true;
                g_placement_diagnostic.path = detail::PlacementPath::pose_only;
                return observation;
            }
        }
    }

    return std::unexpected(CubeError::no_valid_cube_detected);
}

namespace detail {

PlacementDiagnostic last_placement_diagnostic() { return g_placement_diagnostic; }

std::vector<StickerCandidate> merged_face_candidates(const cv::Mat& bgr_frame, double max_aspect) {
    if (bgr_frame.empty()) {
        return {};
    }
    cv::Mat hsv;
    cv::cvtColor(bgr_frame, hsv, cv::COLOR_BGR2HSV);
    std::vector<cv::Mat> hsv_channels;
    cv::split(hsv, hsv_channels);
    return merged_face_candidate_union(bgr_frame, hsv_channels, max_aspect);
}

std::vector<ScoredPlacement> score_partial_placements(const cv::Mat& bgr_frame) {
    if (bgr_frame.empty()) {
        return {};
    }
    cv::Mat hsv;
    cv::cvtColor(bgr_frame, hsv, cv::COLOR_BGR2HSV);
    std::vector<cv::Mat> hsv_channels;
    cv::split(hsv, hsv_channels);
    return score_placements(bgr_frame, merged_face_candidate_union(bgr_frame, hsv_channels));
}

CellPatchSample cell_patch_sample(const cv::Mat& bgr_frame, const StickerQuad& cell) {
    CellPatchSample sample;
    if (bgr_frame.empty()) {
        return sample;
    }

    cv::Point2f centroid(0.0F, 0.0F);
    for (const auto& corner : cell.corners) {
        centroid += corner;
    }
    centroid *= 0.25F;

    constexpr float kShrink = kSampleHalfExtent / kCellQuadHalfExtent;
    std::array<cv::Point2f, 4> sample_corners{};
    for (std::size_t k = 0; k < 4; ++k) {
        sample_corners[k] = centroid + (cell.corners[k] - centroid) * kShrink;
    }

    const auto stats = patch_stats_for_corners(bgr_frame, sample_corners, PatchIntent::kCellColour);
    sample.median = stats.median;
    sample.weighted_mad = stats.weighted_mad;
    sample.purity = stats.purity;
    sample.quad_px = stats.quad_px;
    sample.cell_quad_area_px = static_cast<float>(std::abs(cv::contourArea(
        std::vector<cv::Point2f>(cell.corners.begin(), cell.corners.end()))));
    sample.sample_quad = sample_corners;
    return sample;
}

}  // namespace detail

}  // namespace cube::vision
