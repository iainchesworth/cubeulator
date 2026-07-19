#include <cube/vision/grid_detection.hpp>

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <utility>
#include <vector>

namespace cube::vision {

namespace {

using detail::StickerCandidate;

// 0.05% of frame area (~12x12px cells at 640x480): a hand-held cube at a
// natural distance produces cells well under the 25px the previous 0.2%
// floor demanded - the floor's job is only to reject speckle, and the
// lattice fit rejects unstructured noise regardless of count.
constexpr double kMinCellAreaFraction = 0.0005;
constexpr double kMaxCellAreaFraction = 0.20;  // 20% of frame area
// Rotation-invariant squareness bound (max/min side of the contour's
// minAreaRect, always >= 1) - replaces the old axis-aligned width/height
// aspect check, which only tolerated a held cube facing the camera
// dead-on.
constexpr double kMaxSquareAspectRatio = 1.6;
constexpr double kApproxPolyEpsilonFraction = 0.03;

// CLAHE (local contrast normalization) + a locally-adaptive threshold
// stand in for the old single global Otsu cutoff on HSV's V channel, which
// a lighting gradient across the frame (e.g. a shadowed corner) can defeat
// outright - no single global cutoff can separate a dim sticker in shadow
// from a bright gap lit directly, even though both are locally
// distinguishable from their own neighborhood.
constexpr double kClaheClipLimit = 2.0;
constexpr int kClaheTileGrid = 8;
// Adaptive-threshold block sizes at THREE scales (frame/4, /8, /16): the
// right block is ~one cell pitch, but the cube's on-screen size varies with
// how far the user holds it - a single frame/4 block suits a frame-filling
// cube and, for a smaller one, averages in so much unrelated scene (hands,
// clothing) that cell-vs-lattice contrast disappears into the regional
// mean (diagnosed from a real webcam frame where only 4 of 9 cells
// survived the single-scale threshold). Candidates from every scale pool
// into one set; duplicates collapse in dedupe/slot assignment.
constexpr std::array<int, 3> kAdaptiveBlockDivisors = {4, 8, 16};
constexpr int kAdaptiveBlockMin = 7;  // adaptiveThreshold requires odd, > 1
constexpr double kAdaptiveThresholdC = 5.0;
// adaptiveThreshold's local-mean comparison degenerates inside uniform
// regions: any flat area trivially satisfies src > mean - C, so deep
// background floods the mask as foreground - connecting stickers to the
// background blob through incomplete boundary rings, and minting phantom
// candidates inside uniform dark pockets (e.g. the gap where a sticker
// went undetected). A low absolute floor on the RAW V channel removes the
// flood without reintroducing the global-threshold fragility CLAHE exists
// to avoid: the floor sits far below any plausibly-lit sticker, and dark
// chromatic stickers are independently covered by the saturation mask in
// face_observation.cpp's colour path.
constexpr int kMinBrightPathValue = 45;

// Candidate-count/grid-shape tolerance: replaces the old "exactly 9
// contours forming exactly 3 even rows of 3" all-or-nothing check with a
// scored lattice fit over whatever candidates are found - see
// find_best_lattice() below.
constexpr float kMinPitchRatio = 0.7F;
constexpr float kMaxPitchRatio = 1.4F;
// A candidate must land within this fraction of one cell-pitch of an exact
// integer lattice point to count as an inlier for that slot - small enough
// that a spurious background contour essentially never lands this close to
// an unrelated grid's lattice points by chance, large enough to tolerate
// real photo noise.
constexpr float kMaxLatticeResidualRatio = 0.35F;
// Acceptance floor of 4 (of 9): a shrink-wrapped light-lattice cube under
// webcam wash segments only ~4-5 unique cells per frame (whites vanish
// into the white lattice, sheen guts saturation, hands weld the colour
// mask - measured on real frames where detection flickered between 4
// candidates rejected and 5 accepted). Four residual-gated hits still
// rarely align with a random lattice, and four further safety layers stand
// behind this gate: area-weighted hypothesis scoring (fragments lose to
// true cells), temporal convergence (a fluke lattice never repeats
// consistently), census-guarded slot routing, and the final
// solvability-filtered assembly.
constexpr int kMinInlierCells = 4;
// Defensive cap bounding the worst-case O(n^3) hypothesis search under
// pathological background clutter; keeps the largest-area candidates,
// which real grid cells are reliably among.
constexpr std::size_t kMaxCandidatesForLatticeSearch = 40;

// A hypothesized 3x3 lattice: `anchor` is the world position of the center
// slot (row=1, col=1); `col_step`/`row_step` are the grid's basis vectors
// for +1 column / +1 row. `slot_candidate[row*3+col]` holds the matched
// candidate's index, or -1 if no real candidate landed close enough to that
// slot (see kMaxLatticeResidualRatio).
struct LatticeHypothesis {
    cv::Point2f anchor;
    cv::Point2f col_step;
    cv::Point2f row_step;
    std::array<int, cells_per_grid> slot_candidate{};
    int inlier_count = 0;
    float residual_sum = 0.0F;
    // Sum of matched candidates' contour areas - the discriminator between
    // the true cell grid and a smaller offset lattice fitted to fragment
    // blobs (multiscale threshold pieces of one cell, white cross-pieces of
    // a light-lattice cube). Observed live: with several true cells washed
    // invisible, a tight 5-inlier fragment lattice at ~80% of the true
    // pitch out-scored the real grid on residual alone and sampled border
    // regions as garbage colours. True cells are the LARGE blobs, so at
    // equal inlier counts the greater matched area identifies the real
    // grid.
    double matched_area = 0.0;
};

// Median nearest-neighbor distance between candidate centers - a robust
// (outlier-tolerant, unlike a mean) estimate of the grid's cell pitch, used
// to bound which candidate pairs plausibly represent adjacent cells.
[[nodiscard]] float estimate_pitch(const std::vector<StickerCandidate>& candidates) {
    if (candidates.size() < 2) {
        return 0.0F;
    }
    std::vector<float> nearest_distances;
    nearest_distances.reserve(candidates.size());
    for (std::size_t i = 0; i < candidates.size(); ++i) {
        float best = std::numeric_limits<float>::max();
        for (std::size_t j = 0; j < candidates.size(); ++j) {
            if (i == j) {
                continue;
            }
            const cv::Point2f diff = candidates[i].center - candidates[j].center;
            best = std::min(best, std::hypot(diff.x, diff.y));
        }
        nearest_distances.push_back(best);
    }
    const auto mid = nearest_distances.begin() + static_cast<std::ptrdiff_t>(nearest_distances.size() / 2);
    std::ranges::nth_element(nearest_distances, mid);
    return *mid;
}

// Assigns each candidate to its nearest integer lattice slot under the
// hypothesis anchor/basis, keeping only the closer of any two candidates
// that round to the same slot. Returns the per-slot candidate index (-1 if
// unmatched) and per-slot residual (lattice-unit distance to the exact
// integer point, meaningless where unmatched).
[[nodiscard]] std::pair<std::array<int, cells_per_grid>, std::array<float, cells_per_grid>> classify_candidates(
    const std::vector<StickerCandidate>& candidates, cv::Point2f anchor, cv::Point2f col_step, cv::Point2f row_step,
    double det) {
    std::array<int, cells_per_grid> slots{};
    slots.fill(-1);
    std::array<float, cells_per_grid> residuals{};
    residuals.fill(std::numeric_limits<float>::max());

    for (std::size_t idx = 0; idx < candidates.size(); ++idx) {
        const cv::Point2f d = candidates[idx].center - anchor;
        // Solve d = u*col_step + v*row_step via Cramer's rule; u/v are
        // already in lattice-unit space (u=1 means "one col_step away")
        // since col_step/row_step carry the real measured pitch.
        const double u = (static_cast<double>(d.x) * row_step.y - static_cast<double>(d.y) * row_step.x) / det;
        const double v = (static_cast<double>(col_step.x) * d.y - static_cast<double>(col_step.y) * d.x) / det;
        const double ru = std::round(u);
        const double rv = std::round(v);
        if (ru < -1.0 || ru > 1.0 || rv < -1.0 || rv > 1.0) {
            continue;
        }
        const auto residual = static_cast<float>(std::hypot(u - ru, v - rv));
        if (residual > kMaxLatticeResidualRatio) {
            continue;
        }
        const auto row = static_cast<int>(rv) + 1;
        const auto col = static_cast<int>(ru) + 1;
        const auto slot = static_cast<std::size_t>(row * grid_dimension + col);
        if (residual < residuals[slot]) {
            residuals[slot] = residual;
            slots[slot] = static_cast<int>(idx);
        }
    }
    return {slots, residuals};
}

// Scores one (col_step, row_step) basis hypothesis: classifies candidates
// against a provisional anchor (median candidate center), re-centers the
// anchor via least-squares over the resulting inliers, then reclassifies
// once more against the refined anchor.
[[nodiscard]] LatticeHypothesis fit_lattice_hypothesis(const std::vector<StickerCandidate>& candidates,
                                                        cv::Point2f col_step, cv::Point2f row_step) {
    LatticeHypothesis hyp{.anchor = {}, .col_step = col_step, .row_step = row_step};
    hyp.slot_candidate.fill(-1);

    const double det =
        static_cast<double>(col_step.x) * row_step.y - static_cast<double>(col_step.y) * row_step.x;
    if (std::abs(det) < 1e-6) {
        return hyp;
    }

    std::vector<float> xs;
    std::vector<float> ys;
    xs.reserve(candidates.size());
    ys.reserve(candidates.size());
    for (const auto& candidate : candidates) {
        xs.push_back(candidate.center.x);
        ys.push_back(candidate.center.y);
    }
    const auto mid_x = xs.begin() + static_cast<std::ptrdiff_t>(xs.size() / 2);
    const auto mid_y = ys.begin() + static_cast<std::ptrdiff_t>(ys.size() / 2);
    std::ranges::nth_element(xs, mid_x);
    std::ranges::nth_element(ys, mid_y);
    cv::Point2f anchor(*mid_x, *mid_y);

    auto [provisional_slots, provisional_residuals] = classify_candidates(candidates, anchor, col_step, row_step, det);

    cv::Point2f offset_sum(0.0F, 0.0F);
    int matched = 0;
    for (int row = 0; row < grid_dimension; ++row) {
        for (int col = 0; col < grid_dimension; ++col) {
            const auto slot = static_cast<std::size_t>(row * grid_dimension + col);
            if (provisional_slots[slot] < 0) {
                continue;
            }
            const cv::Point2f expected_offset =
                static_cast<float>(col - 1) * col_step + static_cast<float>(row - 1) * row_step;
            offset_sum += candidates[static_cast<std::size_t>(provisional_slots[slot])].center - expected_offset;
            ++matched;
        }
    }
    if (matched > 0) {
        anchor = cv::Point2f(offset_sum.x / static_cast<float>(matched), offset_sum.y / static_cast<float>(matched));
    }

    auto [slots, residuals] = classify_candidates(candidates, anchor, col_step, row_step, det);

    hyp.anchor = anchor;
    hyp.slot_candidate = slots;
    for (int slot = 0; slot < cells_per_grid; ++slot) {
        const int idx = slots[static_cast<std::size_t>(slot)];
        if (idx >= 0) {
            ++hyp.inlier_count;
            hyp.residual_sum += residuals[static_cast<std::size_t>(slot)];
            hyp.matched_area += candidates[static_cast<std::size_t>(idx)].area;
        }
    }
    return hyp;
}

// Searches candidate pairs for the best-scoring 3x3 lattice hypothesis.
// Seed pairs are restricted to "more horizontal than vertical" separation,
// which fixes which axis is the column axis by construction (a pair's
// perpendicular is then always the more-vertical row axis) and bounds
// rotation tolerance to under ~45 degrees - comfortably past what a
// hand-held cube facing the camera needs.
[[nodiscard]] std::optional<LatticeHypothesis> find_best_lattice(const std::vector<StickerCandidate>& candidates) {
    const float pitch = estimate_pitch(candidates);
    if (pitch <= 0.0F) {
        return std::nullopt;
    }
    const float min_pitch = pitch * kMinPitchRatio;
    const float max_pitch = pitch * kMaxPitchRatio;

    std::optional<LatticeHypothesis> best;
    for (std::size_t i = 0; i < candidates.size(); ++i) {
        for (std::size_t j = i + 1; j < candidates.size(); ++j) {
            cv::Point2f d = candidates[j].center - candidates[i].center;
            if (std::abs(d.x) < std::abs(d.y)) {
                continue;
            }
            const float len = std::hypot(d.x, d.y);
            if (len < min_pitch || len > max_pitch) {
                continue;
            }
            if (d.x < 0) {
                d = -d;
            }
            const cv::Point2f row_step(-d.y, d.x);
            auto hypothesis = fit_lattice_hypothesis(candidates, d, row_step);
            // Ordering: most inliers, then greatest matched area (see
            // LatticeHypothesis::matched_area), then tightest residual.
            const bool better =
                !best || hypothesis.inlier_count > best->inlier_count ||
                (hypothesis.inlier_count == best->inlier_count &&
                 (hypothesis.matched_area > best->matched_area ||
                  (hypothesis.matched_area == best->matched_area && hypothesis.residual_sum < best->residual_sum)));
            if (better) {
                best = std::move(hypothesis);
            }
        }
    }
    if (!best || best->inlier_count < kMinInlierCells) {
        return std::nullopt;
    }
    return best;
}

// Median matched-cell width/height, used to size slots that had no matched
// candidate (see make_extrapolated_bounds()).
[[nodiscard]] cv::Size2f median_matched_size(const std::vector<StickerCandidate>& candidates,
                                              const LatticeHypothesis& hypothesis) {
    std::vector<float> widths;
    std::vector<float> heights;
    for (int slot = 0; slot < cells_per_grid; ++slot) {
        const int idx = hypothesis.slot_candidate[static_cast<std::size_t>(slot)];
        if (idx < 0) {
            continue;
        }
        widths.push_back(static_cast<float>(candidates[static_cast<std::size_t>(idx)].bounds.width));
        heights.push_back(static_cast<float>(candidates[static_cast<std::size_t>(idx)].bounds.height));
    }
    const auto mid_w = widths.begin() + static_cast<std::ptrdiff_t>(widths.size() / 2);
    const auto mid_h = heights.begin() + static_cast<std::ptrdiff_t>(heights.size() / 2);
    std::ranges::nth_element(widths, mid_w);
    std::ranges::nth_element(heights, mid_h);
    return cv::Size2f(*mid_w, *mid_h);
}

// Builds a cell region for a lattice slot with no matched candidate contour
// (a missed/merged/occluded sticker), centered on the hypothesis's own
// prediction for that slot. The center is clamped strictly inside the
// frame - vision.cpp's sample_cell_color() does an unchecked pixel access
// at exactly this region's reconstructed center, so an out-of-frame
// extrapolation would crash it.
[[nodiscard]] cv::Rect make_extrapolated_bounds(cv::Point2f center, cv::Size2f size, cv::Size frame_size) {
    const int width = std::max(1, static_cast<int>(size.width));
    const int height = std::max(1, static_cast<int>(size.height));
    const int safe_x = std::clamp(static_cast<int>(std::lround(center.x)), 0, std::max(0, frame_size.width - 1));
    const int safe_y = std::clamp(static_cast<int>(std::lround(center.y)), 0, std::max(0, frame_size.height - 1));
    return cv::Rect(safe_x - width / 2, safe_y - height / 2, width, height);
}

}  // namespace

namespace detail {

std::vector<StickerCandidate> candidates_from_binary_mask(const cv::Mat& binary_mask) {
    std::vector<std::vector<cv::Point>> contours;
    // RETR_LIST, not RETR_EXTERNAL: if a lighting gradient makes the
    // background register as foreground and swallows a sticker (the
    // sticker becomes a hole inside a large background blob), RETR_EXTERNAL
    // would drop that sticker's contour entirely; RETR_LIST recovers it as
    // its own inner contour. The enclosing background blob itself is
    // filtered out by the area cap below.
    cv::findContours(binary_mask, contours, cv::RETR_LIST, cv::CHAIN_APPROX_SIMPLE);

    const double frame_area = static_cast<double>(binary_mask.cols) * static_cast<double>(binary_mask.rows);
    const double min_area = frame_area * kMinCellAreaFraction;
    const double max_area = frame_area * kMaxCellAreaFraction;

    std::vector<StickerCandidate> candidates;
    for (const auto& contour : contours) {
        const double area = cv::contourArea(contour);
        if (area < min_area || area > max_area) {
            continue;
        }

        std::vector<cv::Point> approx;
        const double epsilon = kApproxPolyEpsilonFraction * cv::arcLength(contour, true);
        cv::approxPolyDP(contour, approx, epsilon, true);
        // 4-6 vertices, not exactly 4: real stickers have rounded corners
        // and webcam blur, so their contours routinely approximate to 5 or
        // 6 points - demanding a perfect quadrilateral was a major reason
        // detection only worked at one exact distance. The fill-ratio check
        // below carries the "actually box-like" discrimination instead.
        if (approx.size() < 4 || approx.size() > 6 || !cv::isContourConvex(approx)) {
            continue;
        }

        const cv::RotatedRect min_rect = cv::minAreaRect(approx);
        if (min_rect.size.width <= 0.0F || min_rect.size.height <= 0.0F) {
            continue;
        }
        const float squareness = std::max(min_rect.size.width, min_rect.size.height) /
                                  std::min(min_rect.size.width, min_rect.size.height);
        if (squareness > kMaxSquareAspectRatio) {
            continue;
        }
        // A convex blob that fills most of its own minAreaRect is box-like;
        // crescents/wedges that sneak through the relaxed vertex bound do
        // not.
        const double rect_area = static_cast<double>(min_rect.size.width) * min_rect.size.height;
        if (rect_area <= 0.0 || area / rect_area < 0.65) {
            continue;
        }

        const cv::Rect bounds = cv::boundingRect(approx);
        candidates.push_back(StickerCandidate{
            .bounds = bounds,
            .center = cv::Point2f(static_cast<float>(bounds.x) + static_cast<float>(bounds.width) / 2.0F,
                                   static_cast<float>(bounds.y) + static_cast<float>(bounds.height) / 2.0F),
            .area = area,
        });
    }
    return candidates;
}

// Isolates the 9 bright stickers from the darker gaps/background between
// them via a locally-adaptive threshold (tracks lighting changes across the
// frame, not just between frames) on HSV's V channel - not plain grayscale
// luma, which weighs blue so low (0.114) that a fully-saturated blue
// sticker reads as dark as the background despite being just as reflective
// as any other sticker; V (max of B/G/R) doesn't have that hue bias.
namespace {

// Shared implementation of the forward and inverted brightness paths: CLAHE
// once, then an adaptive threshold per block scale (see
// kAdaptiveBlockDivisors), all candidates pooled. `threshold_type` selects
// brighter-than-local-mean (standard dark-lattice cubes) or
// darker-than-local-mean (light-lattice cubes, THRESH_BINARY_INV). Both
// directions AND a raw-value floor: forward to kill the uniform-region
// flood (a flat area trivially passes src > mean - C; CLAHE stretches
// uniform background toward bright so only the raw channel still tells it
// from a lit sticker), inverted so a truly black pocket (an undetected
// sticker's gap, deep shadow) keeps reading as "missing" for lattice
// extrapolation while a real-but-dark coloured tile still passes.
[[nodiscard]] std::vector<StickerCandidate> find_brightness_candidates(const cv::Mat& bgr_frame,
                                                                        cv::ThresholdTypes threshold_type) {
    cv::Mat blurred;
    cv::GaussianBlur(bgr_frame, blurred, cv::Size(5, 5), 0);

    cv::Mat hsv;
    cv::cvtColor(blurred, hsv, cv::COLOR_BGR2HSV);
    std::vector<cv::Mat> hsv_channels;
    cv::split(hsv, hsv_channels);

    cv::Mat v_eq;
    cv::createCLAHE(kClaheClipLimit, cv::Size(kClaheTileGrid, kClaheTileGrid))->apply(hsv_channels[2], v_eq);

    cv::Mat value_floor;
    cv::threshold(hsv_channels[2], value_floor, kMinBrightPathValue - 1, 255, cv::THRESH_BINARY);

    const auto kernel = cv::getStructuringElement(cv::MORPH_RECT, cv::Size(3, 3));

    std::vector<StickerCandidate> candidates;
    for (const int divisor : kAdaptiveBlockDivisors) {
        int block_size = std::min(bgr_frame.cols, bgr_frame.rows) / divisor;
        if (block_size % 2 == 0) {
            ++block_size;
        }
        block_size = std::max(block_size, kAdaptiveBlockMin);

        cv::Mat binary;
        cv::adaptiveThreshold(v_eq, binary, 255, cv::ADAPTIVE_THRESH_GAUSSIAN_C, threshold_type, block_size,
                               kAdaptiveThresholdC);
        cv::bitwise_and(binary, value_floor, binary);

        // Close first to bridge small internal gaps (glare/seam noise on a
        // single sticker), then open to remove speckle and cut any thin
        // bridge left between adjacent stickers.
        cv::morphologyEx(binary, binary, cv::MORPH_CLOSE, kernel);
        cv::morphologyEx(binary, binary, cv::MORPH_OPEN, kernel);

        auto scale_candidates = candidates_from_binary_mask(binary);
        candidates.insert(candidates.end(), scale_candidates.begin(), scale_candidates.end());
    }
    return candidates;
}

}  // namespace

std::vector<StickerCandidate> find_square_candidates(const cv::Mat& bgr_frame) {
    return find_brightness_candidates(bgr_frame, cv::THRESH_BINARY);
}

std::vector<StickerCandidate> find_dark_square_candidates(const cv::Mat& bgr_frame) {
    // The mirror image of find_square_candidates(), for light-bodied cubes:
    // a white/light plastic lattice between tiles makes every coloured cell
    // a local brightness MINIMUM, invisible to the brighter-than-local-mean
    // path (diagnosed from a real webcam frame where the bright path could
    // only ever find the lightest 4 of 9 cells).
    return find_brightness_candidates(bgr_frame, cv::THRESH_BINARY_INV);
}

// Fits the best 3x3 lattice explaining `candidates` (relaxed from the old
// "exactly 9 candidates in exactly 3 even rows" requirement), extrapolating
// bounds for any slot without a matched candidate.
std::optional<GridFitDetail> fit_lattice_detailed(std::vector<StickerCandidate> candidates, cv::Size frame_size) {
    // Near-coincident duplicates (the same sticker found at several
    // threshold scales, or by several strategies) must collapse before the
    // pitch estimate: each duplicate's nearest neighbour is its own twin a
    // couple of pixels away, so the median nearest-neighbour "pitch"
    // collapses to noise and no seed pair survives the pitch window.
    // Radius from median blob side (~half a cell) separates same-sticker
    // twins from genuinely adjacent cells; larger area wins a collapse.
    if (candidates.size() >= 2) {
        std::vector<double> sides;
        sides.reserve(candidates.size());
        for (const auto& candidate : candidates) {
            sides.push_back(std::sqrt(candidate.area));
        }
        const auto mid = sides.begin() + static_cast<std::ptrdiff_t>(sides.size() / 2);
        std::ranges::nth_element(sides, mid);
        const auto dedupe_radius = static_cast<float>(*mid * 0.5);

        std::ranges::sort(candidates, {}, [](const StickerCandidate& candidate) { return -candidate.area; });
        std::vector<StickerCandidate> unique;
        unique.reserve(candidates.size());
        for (const auto& candidate : candidates) {
            const bool duplicate = std::ranges::any_of(unique, [&](const StickerCandidate& kept) {
                const cv::Point2f d = kept.center - candidate.center;
                return std::hypot(d.x, d.y) < dedupe_radius;
            });
            if (!duplicate) {
                unique.push_back(candidate);
            }
        }
        candidates = std::move(unique);
    }

    if (candidates.size() > kMaxCandidatesForLatticeSearch) {
        std::ranges::partial_sort(
            candidates, candidates.begin() + static_cast<std::ptrdiff_t>(kMaxCandidatesForLatticeSearch), {},
            [](const StickerCandidate& candidate) { return -candidate.area; });
        candidates.resize(kMaxCandidatesForLatticeSearch);
    }

    const auto hypothesis = find_best_lattice(candidates);
    if (!hypothesis) {
        return std::nullopt;
    }

    const cv::Size2f fallback_size = median_matched_size(candidates, *hypothesis);
    GridFitDetail fit{.anchor = hypothesis->anchor, .col_step = hypothesis->col_step,
                      .row_step = hypothesis->row_step};
    fit.inlier_count = hypothesis->inlier_count;
    for (int row = 0; row < grid_dimension; ++row) {
        for (int col = 0; col < grid_dimension; ++col) {
            const auto slot = static_cast<std::size_t>(row * grid_dimension + col);
            const int idx = hypothesis->slot_candidate[slot];
            if (idx >= 0) {
                const auto& candidate = candidates[static_cast<std::size_t>(idx)];
                fit.matched_centers[slot] = candidate.center;
                fit.cell_bounds[slot] = candidate.bounds;
            } else {
                const cv::Point2f center = hypothesis->anchor +
                                            static_cast<float>(col - 1) * hypothesis->col_step +
                                            static_cast<float>(row - 1) * hypothesis->row_step;
                fit.cell_bounds[slot] = make_extrapolated_bounds(center, fallback_size, frame_size);
            }
        }
    }
    return fit;
}

}  // namespace detail

std::optional<GridFitDetail> detect_cube_grid_detailed(const cv::Mat& bgr_frame) {
    if (bgr_frame.empty()) {
        return std::nullopt;
    }
    return detail::fit_lattice_detailed(detail::find_square_candidates(bgr_frame), bgr_frame.size());
}

std::optional<GridDetection> detect_cube_grid(const cv::Mat& bgr_frame) {
    const auto fit = detect_cube_grid_detailed(bgr_frame);
    if (!fit) {
        return std::nullopt;
    }

    std::array<CellRegion, cells_per_grid> cells;
    for (int slot = 0; slot < cells_per_grid; ++slot) {
        cells[static_cast<std::size_t>(slot)] = CellRegion{.bounds = fit->cell_bounds[static_cast<std::size_t>(slot)]};
    }

    cv::Rect union_bounds = cells[0].bounds;
    for (const auto& cell : cells) {
        union_bounds |= cell.bounds;
    }
    const double frame_area = static_cast<double>(bgr_frame.cols) * static_cast<double>(bgr_frame.rows);
    const double coverage = static_cast<double>(union_bounds.area()) / frame_area;

    return GridDetection{.cells = cells, .coverage_ratio = static_cast<float>(coverage)};
}

}  // namespace cube::vision
