#include <cube/vision/corner_observation.hpp>

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <numbers>
#include <optional>
#include <vector>

namespace cube::vision {

namespace {

// --- sticker-candidate detection ------------------------------------------

// Wider area band than grid_detection.cpp's: a corner view packs 27 cells
// (not 9) into the frame, so each cell gets roughly a third of the area a
// dedicated single-face capture would give it.
constexpr double kMinCellAreaFraction = 0.0005;
constexpr double kMaxCellAreaFraction = 0.05;
// Sheared side-face parallelograms and the top face's rhombi are
// legitimately elongated under minAreaRect (a 60-degree rhombus alone
// reaches ~1.7), so the shape bound is relaxed well past grid_detection's
// 1.6 - the per-face lattice fits, not per-contour squareness, carry the
// discrimination burden in this mode.
constexpr double kMaxParallelogramAspect = 3.0;
constexpr double kApproxPolyEpsilonFraction = 0.04;

// Same CLAHE + locally-adaptive brightness strategy as grid_detection.cpp
// (see the rationale there); here it is unioned with an HSV
// saturated-or-white mask so a sticker missed by one strategy can still be
// recovered by the other.
constexpr double kClaheClipLimit = 2.0;
constexpr int kClaheTileGrid = 8;
constexpr int kAdaptiveBlockDivisor = 4;
constexpr int kAdaptiveBlockMin = 7;  // adaptiveThreshold requires odd, > 1
constexpr double kAdaptiveThresholdC = 5.0;
// Same uniform-region-flood guard as grid_detection.cpp's
// kMinBrightPathValue (see the rationale there): without it, adaptive
// thresholding marks deep background as foreground and the 27 cells merge
// into the flood instead of segmenting individually.
constexpr int kMinBrightPathValue = 45;
// HSV split of "sticker-like": chromatic stickers are saturated, white
// stickers are bright but unsaturated; the union covers both without
// admitting a dark, unsaturated background.
// 40 to match face_observation.cpp: webcam exposure washes coloured tiles
// toward pastel, and the corner view needs every cell it can get.
constexpr int kMinStickerSaturation = 40;
constexpr int kMinWhiteValue = 180;

// --- corner-view plausibility ---------------------------------------------

// A genuine corner view yields most of 27 cells; a single flat face tops
// out around 9-plus-clutter. The floor alone rejects the most damaging
// false positive (a face-on cube misread as a corner view, which would
// corrupt a scan with 18 fabricated stickers).
constexpr std::size_t kMinTotalCandidates = 18;
// Defensive cap on the O(n^2) pitch estimate under background clutter;
// keeps the largest candidates, which real cells are reliably among.
constexpr std::size_t kMaxCandidatesConsidered = 60;
// Hexagon-ness proxy: a corner view's 27 centers leave ~12 cells well
// inside the convex hull (interior fraction ~0.44), while a flat 3x3 face
// leaves only its centre cell inside (~0.11) - the depth threshold makes
// the count robust to cv::convexHull's handling of collinear points.
constexpr float kInteriorDepthPitchRatio = 0.30F;
constexpr float kMinInteriorFraction = 0.30F;
// The shared corner sits at the candidate centroid, so the three cells
// touching it must appear within about half a pitch of it; a view with no
// near-central candidate is not a corner-on cube.
constexpr float kMaxCentroidGapPitchRatio = 1.25F;

// Sector boundaries for partitioning candidates into the three faces: the
// bisectors between the face-centroid directions of the documented poses
// (top face at -90 deg, lower-right at +30 deg, lower-left at +150 deg, in
// y-down screen angles measured from the candidate centroid).
constexpr float kTopSectorMin = -5.0F * std::numbers::pi_v<float> / 6.0F;
constexpr float kTopSectorMax = -std::numbers::pi_v<float> / 6.0F;
constexpr float kRightSectorMax = std::numbers::pi_v<float> / 2.0F;

// --- per-face lattice fit ---------------------------------------------------

constexpr std::size_t kMinFaceCandidates = 6;
constexpr int kMinFaceInliers = 6;  // of 9, matching grid_detection's floor
constexpr float kMaxLatticeResidualRatio = 0.35F;
// Sample only the inner 60% of each cell so gap/neighbour bleed and the
// soft contour edge never contaminate the patch median.
constexpr float kStickerSampleHalfSpan = 0.30F;
constexpr int kSampleGridDim = 5;
// Reported sticker quads cover the inner 80% of the lattice cell - the
// sticker itself, excluding the inter-sticker gap the pitch includes.
constexpr float kStickerQuadHalfSpan = 0.40F;

constexpr float kCos30 = 0.86602540F;

// On-screen lattice directions of the documented isometric corner pose, one
// basis pair per visible face (the same on-screen cell order
// corner_slot_mappings() below derives its tables for). Fixed rather than
// searched: the capture UI prompts exactly this pose, and the affine
// least-squares refit absorbs the residual hand-held tilt.
const cv::Point2f kTopColDir(kCos30, 0.5F);        // top face: down-right edge
const cv::Point2f kTopRowDir(-kCos30, 0.5F);       // top face: down-left edge
const cv::Point2f kSideColRightDir(kCos30, 0.5F);  // lower-left face: shared top edge
const cv::Point2f kSideColUpDir(kCos30, -0.5F);    // lower-right face: shared top edge
const cv::Point2f kSideRowDir(0.0F, 1.0F);         // both side faces: vertical edge

struct Candidate {
    cv::Point2f center;
    double area = 0.0;
};

[[nodiscard]] std::vector<Candidate> find_sticker_candidates(const cv::Mat& bgr_frame) {
    cv::Mat blurred;
    cv::GaussianBlur(bgr_frame, blurred, cv::Size(5, 5), 0);

    cv::Mat blurred_hsv;
    cv::cvtColor(blurred, blurred_hsv, cv::COLOR_BGR2HSV);
    std::vector<cv::Mat> blurred_channels;
    cv::split(blurred_hsv, blurred_channels);

    // The saturation/white masks come from the UNBLURRED frame: a corner
    // view's inter-sticker gaps are only ~20% of an already-small cell
    // pitch, and Gaussian bleed pushes enough sticker chroma into a dark
    // gap that the mixed pixel still reads as strongly saturated (a 20%
    // red contribution over dark background saturates at ~180 of 255) -
    // blurred masks bridge adjacent cells outright. The open pass below
    // still removes raw-frame speckle.
    cv::Mat hsv;
    cv::cvtColor(bgr_frame, hsv, cv::COLOR_BGR2HSV);
    std::vector<cv::Mat> hsv_channels;
    cv::split(hsv, hsv_channels);

    cv::Mat v_eq;
    cv::createCLAHE(kClaheClipLimit, cv::Size(kClaheTileGrid, kClaheTileGrid))->apply(blurred_channels[2], v_eq);

    int block_size = std::min(bgr_frame.cols, bgr_frame.rows) / kAdaptiveBlockDivisor;
    if (block_size % 2 == 0) {
        ++block_size;
    }
    block_size = std::max(block_size, kAdaptiveBlockMin);

    cv::Mat brightness_mask;
    cv::adaptiveThreshold(v_eq, brightness_mask, 255, cv::ADAPTIVE_THRESH_GAUSSIAN_C, cv::THRESH_BINARY, block_size,
                          kAdaptiveThresholdC);
    // Raw (pre-CLAHE, unblurred) channel: CLAHE stretches uniform
    // background toward bright, so only the raw value still distinguishes
    // it from a sticker.
    cv::Mat value_floor;
    cv::threshold(hsv_channels[2], value_floor, kMinBrightPathValue - 1, 255, cv::THRESH_BINARY);
    cv::bitwise_and(brightness_mask, value_floor, brightness_mask);

    // The inverted twin, for light-bodied cubes whose coloured tiles are
    // local brightness minima against a white lattice (see
    // grid_detection.cpp's find_dark_square_candidates). No floor needed:
    // uniform regions are never darker than their own local mean minus C.
    cv::Mat dark_mask;
    cv::adaptiveThreshold(v_eq, dark_mask, 255, cv::ADAPTIVE_THRESH_GAUSSIAN_C, cv::THRESH_BINARY_INV, block_size,
                          kAdaptiveThresholdC);

    cv::Mat saturated_mask;
    cv::threshold(hsv_channels[1], saturated_mask, kMinStickerSaturation - 1, 255, cv::THRESH_BINARY);
    cv::Mat white_mask;
    cv::threshold(hsv_channels[2], white_mask, kMinWhiteValue - 1, 255, cv::THRESH_BINARY);

    // Each mask is contoured SEPARATELY, never OR-ed together first: on a
    // light-bodied cube the white separator lattice touches every coloured
    // tile, so a union mask welds the visible faces into one giant blob -
    // while the saturated mask alone still isolates every coloured tile.
    // Duplicate detections of one sticker across masks are collapsed by the
    // caller-side dedupe below. Open only, no close: with corner-view gaps
    // this narrow, a 3x3 close re-seals exactly the separations the
    // unblurred masks preserved.
    const auto kernel = cv::getStructuringElement(cv::MORPH_RECT, cv::Size(3, 3));

    std::vector<Candidate> candidates;
    const double frame_area = static_cast<double>(bgr_frame.cols) * static_cast<double>(bgr_frame.rows);
    const double min_area = frame_area * kMinCellAreaFraction;
    const double max_area = frame_area * kMaxCellAreaFraction;

    for (cv::Mat* mask : {&brightness_mask, &dark_mask, &saturated_mask, &white_mask}) {
        cv::morphologyEx(*mask, *mask, cv::MORPH_OPEN, kernel);

        // RETR_LIST for the same reason as grid_detection.cpp: a sticker
        // swallowed as a hole inside a foreground blob is still recovered
        // as its own inner contour.
        std::vector<std::vector<cv::Point>> contours;
        cv::findContours(*mask, contours, cv::RETR_LIST, cv::CHAIN_APPROX_SIMPLE);

        for (const auto& contour : contours) {
            const double area = cv::contourArea(contour);
            if (area < min_area || area > max_area) {
                continue;
            }

            std::vector<cv::Point> approx;
            const double epsilon = kApproxPolyEpsilonFraction * cv::arcLength(contour, true);
            cv::approxPolyDP(contour, approx, epsilon, true);
            // 4-6 vertices plus a fill-ratio floor, not exactly 4: real
            // stickers' rounded corners and webcam blur routinely
            // approximate to 5-6 points (same relaxation as
            // grid_detection.cpp's candidate filter).
            if (approx.size() < 4 || approx.size() > 6 || !cv::isContourConvex(approx)) {
                continue;
            }

            const cv::RotatedRect min_rect = cv::minAreaRect(approx);
            if (min_rect.size.width <= 0.0F || min_rect.size.height <= 0.0F) {
                continue;
            }
            const float aspect = std::max(min_rect.size.width, min_rect.size.height) /
                                 std::min(min_rect.size.width, min_rect.size.height);
            if (aspect > kMaxParallelogramAspect) {
                continue;
            }
            const double rect_area = static_cast<double>(min_rect.size.width) * min_rect.size.height;
            if (rect_area <= 0.0 || area / rect_area < 0.65) {
                continue;
            }

            // Mean of the vertices approximates the centroid well for the
            // near-parallelogram shapes that survive the filters above.
            cv::Point2f center(0.0F, 0.0F);
            for (const auto& vertex : approx) {
                center += cv::Point2f(static_cast<float>(vertex.x), static_cast<float>(vertex.y));
            }
            candidates.push_back(Candidate{.center = center / static_cast<float>(approx.size()), .area = area});
        }
    }

    if (candidates.size() < 2) {
        return candidates;
    }

    // Cross-mask dedupe: the same sticker found by two masks must not read
    // as two lattice candidates. Radius from the median nearest-neighbour
    // spacing is unavailable this early, so half the median sqrt(area)
    // (~half a cell side) separates same-sticker duplicates from adjacent
    // cells.
    std::vector<double> sides;
    sides.reserve(candidates.size());
    for (const auto& candidate : candidates) {
        sides.push_back(std::sqrt(candidate.area));
    }
    const auto mid = sides.begin() + static_cast<std::ptrdiff_t>(sides.size() / 2);
    std::ranges::nth_element(sides, mid);
    const auto dedupe_radius = static_cast<float>(*mid * 0.5);

    std::ranges::sort(candidates, {}, [](const Candidate& candidate) { return -candidate.area; });
    std::vector<Candidate> unique;
    unique.reserve(candidates.size());
    for (const auto& candidate : candidates) {
        const bool duplicate = std::ranges::any_of(unique, [&](const Candidate& kept) {
            const cv::Point2f d = kept.center - candidate.center;
            return std::hypot(d.x, d.y) < dedupe_radius;
        });
        if (!duplicate) {
            unique.push_back(candidate);
        }
    }
    return unique;
}

// Median nearest-neighbour distance - the same robust pitch estimate
// grid_detection.cpp uses.
[[nodiscard]] float estimate_pitch(const std::vector<cv::Point2f>& centers) {
    if (centers.size() < 2) {
        return 0.0F;
    }
    std::vector<float> nearest_distances;
    nearest_distances.reserve(centers.size());
    for (std::size_t i = 0; i < centers.size(); ++i) {
        float best = std::numeric_limits<float>::max();
        for (std::size_t j = 0; j < centers.size(); ++j) {
            if (i == j) {
                continue;
            }
            const cv::Point2f diff = centers[i] - centers[j];
            best = std::min(best, std::hypot(diff.x, diff.y));
        }
        nearest_distances.push_back(best);
    }
    const auto mid = nearest_distances.begin() + static_cast<std::ptrdiff_t>(nearest_distances.size() / 2);
    std::ranges::nth_element(nearest_distances, mid);
    return *mid;
}

struct SlotAssignment {
    std::array<int, 9> slots{};
    int inlier_count = 0;
};

// Nearest-integer-lattice-slot assignment under (anchor, col_step,
// row_step), keeping the closer of any two candidates competing for one
// slot - the same scheme grid_detection.cpp's classify_candidates() uses.
[[nodiscard]] SlotAssignment classify_to_slots(const std::vector<cv::Point2f>& centers, cv::Point2f anchor,
                                               cv::Point2f col_step, cv::Point2f row_step) {
    SlotAssignment assignment;
    assignment.slots.fill(-1);
    std::array<float, 9> residuals{};
    residuals.fill(std::numeric_limits<float>::max());

    const double det = static_cast<double>(col_step.x) * row_step.y - static_cast<double>(col_step.y) * row_step.x;
    if (std::abs(det) < 1e-6) {
        return assignment;
    }

    for (std::size_t idx = 0; idx < centers.size(); ++idx) {
        const cv::Point2f d = centers[idx] - anchor;
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
        const auto slot = static_cast<std::size_t>((static_cast<int>(rv) + 1) * 3 + (static_cast<int>(ru) + 1));
        if (residual < residuals[slot]) {
            residuals[slot] = residual;
            assignment.slots[slot] = static_cast<int>(idx);
        }
    }
    for (const int idx : assignment.slots) {
        if (idx >= 0) {
            ++assignment.inlier_count;
        }
    }
    return assignment;
}

struct FaceFit {
    cv::Point2f anchor;    // centre-cell (row=1, col=1) position
    cv::Point2f col_step;  // +1 column basis vector
    cv::Point2f row_step;  // +1 row basis vector
    int inlier_count = 0;
};

[[nodiscard]] double det3(const std::array<double, 9>& m) {
    return m[0] * (m[4] * m[8] - m[5] * m[7]) - m[1] * (m[3] * m[8] - m[5] * m[6]) +
           m[2] * (m[3] * m[7] - m[4] * m[6]);
}

// Full 6-parameter affine least squares over the matched slots (anchor +
// both basis vectors), rather than grid_detection's anchor-only recentre:
// a hand-held corner pose shears and scales each face's lattice away from
// the fixed nominal directions, and the basis must be allowed to follow or
// cell projection/sampling drifts off the outer stickers.
[[nodiscard]] std::optional<FaceFit> refit_affine(const std::vector<cv::Point2f>& centers,
                                                  const std::array<int, 9>& slots) {
    double n = 0;
    double su = 0;
    double sv = 0;
    double suu = 0;
    double suv = 0;
    double svv = 0;
    std::array<double, 3> bx{};
    std::array<double, 3> by{};

    for (int slot = 0; slot < 9; ++slot) {
        const int idx = slots[static_cast<std::size_t>(slot)];
        if (idx < 0) {
            continue;
        }
        const double u = (slot % 3) - 1;
        const double v = (slot / 3) - 1;
        const cv::Point2f p = centers[static_cast<std::size_t>(idx)];
        n += 1.0;
        su += u;
        sv += v;
        suu += u * u;
        suv += u * v;
        svv += v * v;
        bx[0] += p.x;
        bx[1] += u * p.x;
        bx[2] += v * p.x;
        by[0] += p.y;
        by[1] += u * p.y;
        by[2] += v * p.y;
    }

    const std::array<double, 9> m = {n, su, sv, su, suu, suv, sv, suv, svv};
    const double det = det3(m);
    if (std::abs(det) < 1e-9) {
        return std::nullopt;
    }

    const auto solve = [&](const std::array<double, 3>& rhs) {
        const std::array<double, 9> m0 = {rhs[0], su, sv, rhs[1], suu, suv, rhs[2], suv, svv};
        const std::array<double, 9> m1 = {n, rhs[0], sv, su, rhs[1], suv, sv, rhs[2], svv};
        const std::array<double, 9> m2 = {n, su, rhs[0], su, suu, rhs[1], sv, suv, rhs[2]};
        return std::array<double, 3>{det3(m0) / det, det3(m1) / det, det3(m2) / det};
    };
    const auto x = solve(bx);
    const auto y = solve(by);

    return FaceFit{
        .anchor = cv::Point2f(static_cast<float>(x[0]), static_cast<float>(y[0])),
        .col_step = cv::Point2f(static_cast<float>(x[1]), static_cast<float>(y[1])),
        .row_step = cv::Point2f(static_cast<float>(x[2]), static_cast<float>(y[2])),
    };
}

// Fits one visible face's 3x3 parallelogram lattice: nominal pose-derived
// basis scaled by the sector's own pitch, provisional classification,
// affine refit, final classification.
[[nodiscard]] std::optional<FaceFit> fit_face_grid(const std::vector<cv::Point2f>& centers, cv::Point2f col_dir,
                                                   cv::Point2f row_dir) {
    if (centers.size() < kMinFaceCandidates) {
        return std::nullopt;
    }
    const float pitch = estimate_pitch(centers);
    if (pitch <= 0.0F) {
        return std::nullopt;
    }

    cv::Point2f anchor(0.0F, 0.0F);
    for (const auto& center : centers) {
        anchor += center;
    }
    anchor /= static_cast<float>(centers.size());

    const cv::Point2f col_step = pitch * col_dir;
    const cv::Point2f row_step = pitch * row_dir;
    const auto provisional = classify_to_slots(centers, anchor, col_step, row_step);
    if (provisional.inlier_count < 3) {
        return std::nullopt;
    }

    FaceFit fit{.anchor = anchor, .col_step = col_step, .row_step = row_step};
    if (auto refined = refit_affine(centers, provisional.slots)) {
        fit = *refined;
    }
    const auto final_assignment = classify_to_slots(centers, fit.anchor, fit.col_step, fit.row_step);
    if (final_assignment.inlier_count < kMinFaceInliers) {
        return std::nullopt;
    }
    fit.inlier_count = final_assignment.inlier_count;
    return fit;
}

// Patch-median float Lab over a grid of sample points inside the inner 60%
// of the cell: convert the gathered BGR pixels to CV_32FC3 in [0,1] then
// cv::COLOR_BGR2Lab (L in [0,100]) - the same float-Lab convention as
// color_classification.cpp's bgr_to_lab - and take the per-channel median,
// which shrugs off glare pixels and edge bleed a mean would average in.
[[nodiscard]] cv::Vec3f sample_cell_lab(const cv::Mat& bgr_frame, cv::Point2f center, cv::Point2f col_step,
                                        cv::Point2f row_step) {
    std::vector<cv::Vec3b> samples;
    samples.reserve(static_cast<std::size_t>(kSampleGridDim) * kSampleGridDim);
    for (int i = 0; i < kSampleGridDim; ++i) {
        for (int j = 0; j < kSampleGridDim; ++j) {
            const float fu = (static_cast<float>(i) / (kSampleGridDim - 1) - 0.5F) * 2.0F * kStickerSampleHalfSpan;
            const float fv = (static_cast<float>(j) / (kSampleGridDim - 1) - 0.5F) * 2.0F * kStickerSampleHalfSpan;
            const cv::Point2f pt = center + fu * col_step + fv * row_step;
            const int x = static_cast<int>(std::lround(pt.x));
            const int y = static_cast<int>(std::lround(pt.y));
            if (x < 0 || y < 0 || x >= bgr_frame.cols || y >= bgr_frame.rows) {
                continue;
            }
            samples.push_back(bgr_frame.at<cv::Vec3b>(y, x));
        }
    }
    if (samples.empty()) {
        return cv::Vec3f(0.0F, 0.0F, 0.0F);
    }

    cv::Mat3f bgr_float(static_cast<int>(samples.size()), 1);
    for (std::size_t i = 0; i < samples.size(); ++i) {
        bgr_float(static_cast<int>(i), 0) =
            cv::Vec3f(static_cast<float>(samples[i][0]) / 255.0F, static_cast<float>(samples[i][1]) / 255.0F,
                      static_cast<float>(samples[i][2]) / 255.0F);
    }
    cv::Mat3f lab;
    cv::cvtColor(bgr_float, lab, cv::COLOR_BGR2Lab);

    cv::Vec3f median;
    std::vector<float> channel(samples.size());
    for (int c = 0; c < 3; ++c) {
        for (std::size_t i = 0; i < samples.size(); ++i) {
            channel[i] = lab(static_cast<int>(i), 0)[c];
        }
        const auto mid = channel.begin() + static_cast<std::ptrdiff_t>(channel.size() / 2);
        std::ranges::nth_element(channel, mid);
        median[c] = *mid;
    }
    return median;
}

[[nodiscard]] CornerFaceView build_face_view(const cv::Mat& bgr_frame, const FaceFit& fit) {
    CornerFaceView view;
    const cv::Point2f half_face_col = 1.5F * fit.col_step;
    const cv::Point2f half_face_row = 1.5F * fit.row_step;
    view.quad = {fit.anchor - half_face_col - half_face_row, fit.anchor + half_face_col - half_face_row,
                 fit.anchor + half_face_col + half_face_row, fit.anchor - half_face_col + half_face_row};

    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 3; ++col) {
            const auto slot = static_cast<std::size_t>(row * 3 + col);
            const cv::Point2f center =
                fit.anchor + static_cast<float>(col - 1) * fit.col_step + static_cast<float>(row - 1) * fit.row_step;
            const cv::Point2f half_col = kStickerQuadHalfSpan * fit.col_step;
            const cv::Point2f half_row = kStickerQuadHalfSpan * fit.row_step;
            view.cells[slot].corners = {center - half_col - half_row, center + half_col - half_row,
                                        center + half_col + half_row, center - half_col + half_row};
            view.lab[slot] = sample_cell_lab(bgr_frame, center, fit.col_step, fit.row_step);
        }
    }
    return view;
}

}  // namespace

Result<CornerObservation> observe_corner(const cv::Mat& bgr_frame) {
    if (bgr_frame.empty() || bgr_frame.type() != CV_8UC3) {
        return std::unexpected(CubeError::no_valid_cube_detected);
    }

    auto candidates = find_sticker_candidates(bgr_frame);
    if (candidates.size() < kMinTotalCandidates) {
        return std::unexpected(CubeError::no_valid_cube_detected);
    }
    if (candidates.size() > kMaxCandidatesConsidered) {
        std::ranges::partial_sort(candidates,
                                  candidates.begin() + static_cast<std::ptrdiff_t>(kMaxCandidatesConsidered), {},
                                  [](const Candidate& candidate) { return -candidate.area; });
        candidates.resize(kMaxCandidatesConsidered);
    }

    std::vector<cv::Point2f> centers;
    centers.reserve(candidates.size());
    for (const auto& candidate : candidates) {
        centers.push_back(candidate.center);
    }

    const float pitch = estimate_pitch(centers);
    if (pitch <= 0.0F) {
        return std::unexpected(CubeError::no_valid_cube_detected);
    }

    // Hexagonal-silhouette check: a corner view's cell centers wrap around
    // the shared corner, leaving a large interior; a flat face's centers
    // form a shallow ring with only its centre cell inside the hull.
    std::vector<cv::Point2f> hull;
    cv::convexHull(centers, hull);
    if (hull.size() < 3) {
        return std::unexpected(CubeError::no_valid_cube_detected);
    }
    std::size_t interior = 0;
    for (const auto& center : centers) {
        if (cv::pointPolygonTest(hull, center, true) > kInteriorDepthPitchRatio * pitch) {
            ++interior;
        }
    }
    if (static_cast<float>(interior) < kMinInteriorFraction * static_cast<float>(centers.size())) {
        return std::unexpected(CubeError::no_valid_cube_detected);
    }

    cv::Point2f centroid(0.0F, 0.0F);
    for (const auto& center : centers) {
        centroid += center;
    }
    centroid /= static_cast<float>(centers.size());

    float nearest_to_centroid = std::numeric_limits<float>::max();
    for (const auto& center : centers) {
        const cv::Point2f diff = center - centroid;
        nearest_to_centroid = std::min(nearest_to_centroid, std::hypot(diff.x, diff.y));
    }
    if (nearest_to_centroid > kMaxCentroidGapPitchRatio * pitch) {
        return std::unexpected(CubeError::no_valid_cube_detected);
    }

    std::vector<cv::Point2f> top_centers;
    std::vector<cv::Point2f> lower_left_centers;
    std::vector<cv::Point2f> lower_right_centers;
    for (const auto& center : centers) {
        const cv::Point2f rel = center - centroid;
        const float angle = std::atan2(rel.y, rel.x);
        if (angle >= kTopSectorMin && angle < kTopSectorMax) {
            top_centers.push_back(center);
        } else if (angle >= kTopSectorMax && angle < kRightSectorMax) {
            lower_right_centers.push_back(center);
        } else {
            lower_left_centers.push_back(center);
        }
    }
    if (top_centers.size() < kMinFaceCandidates || lower_left_centers.size() < kMinFaceCandidates ||
        lower_right_centers.size() < kMinFaceCandidates) {
        return std::unexpected(CubeError::no_valid_cube_detected);
    }

    const auto top_fit = fit_face_grid(top_centers, kTopColDir, kTopRowDir);
    const auto lower_left_fit = fit_face_grid(lower_left_centers, kSideColRightDir, kSideRowDir);
    const auto lower_right_fit = fit_face_grid(lower_right_centers, kSideColUpDir, kSideRowDir);
    if (!top_fit || !lower_left_fit || !lower_right_fit) {
        return std::unexpected(CubeError::no_valid_cube_detected);
    }

    CornerObservation observation;
    observation.top = build_face_view(bgr_frame, *top_fit);
    observation.lower_left = build_face_view(bgr_frame, *lower_left_fit);
    observation.lower_right = build_face_view(bgr_frame, *lower_right_fit);
    observation.geometry_confidence =
        static_cast<float>(top_fit->inlier_count + lower_left_fit->inlier_count + lower_right_fit->inlier_count) /
        27.0F;
    return observation;
}

namespace {

// Corner-view cell order -> guided-scan camera order, derived from
// cube.cpp's position_of() ground truth (x: L->R, y: D->U, z: B->F) the
// same way scan_conversion.cpp derives its guided tables.
//
// On-screen cell order of a corner view (matching the lattice directions
// observe_corner() fits, see kTopColDir etc. above):
//   - top face: a rhombus; rows recede away from the viewer, i.e. row 0 is
//     the far row at the drawn face's top vertex and row 2 touches the
//     shared corner; columns run left-to-right on screen.
//   - side faces: sheared parallelograms; rows run down-screen from the
//     face's top edge (the edge shared with the top face's rhombus);
//     columns run left-to-right on screen.
//
// first_corner (U top / F lower-left / R lower-right): the viewer looks
// down the +x+y+z diagonal at the URF corner. Screen x grows with (x - z),
// screen y (downward) with (x + z)/2 - y. Per face:
//   U: drawn tl is ULB; rows advance along +z (toward the viewer), columns
//      along +x, so cell (r,c) holds the sticker at (x,z) = (c-1, r-1),
//      i.e. solver (row,col) = (z+1, x+1) = (r,c) - the on-screen order IS
//      solver row-major. A guided U scan (F toward frame-top) sees solver
//      order rotated 180 degrees (scan_conversion.cpp), so to_scan_order
//      is kRotate180.
//   F: drawn tl is UFL; rows along -y (down-screen), columns along +x, so
//      solver (row,col) = (1-y, x+1) = (r,c). The guided F permutation is
//      identity, so to_scan_order is kIdentity.
//   R: drawn tl is URF; rows along -y, columns along -z (left-to-right on
//      screen), so solver (row,col) = (r, 2-c) - a horizontal mirror. The
//      guided R permutation is itself kFlipHorizontal, and the two mirrors
//      cancel: to_scan_order is kIdentity.
//
// second_corner (D top / L lower-left / B lower-right): the viewer looks
// down the -x-y-z diagonal at the DLB corner with D up-screen. Screen x
// still grows with (x - z); screen y (downward) now grows with 2y - x - z.
//   D: drawn tl is DFR (the far top vertex); rows advance along -x,
//      columns along -z, so cell (r,c) holds (x,z) = (1-r, 1-c), i.e.
//      solver (row,col) = (z+1, x+1) = (2-c, 2-r) - the anti-transpose
//      {8,5,2,7,4,1,6,3,0}. Composing with the guided D permutation
//      (kFlipVertical) gives (r,c) -> (c, 2-r): the quarter-turn table
//      kRotateQuarter below - exactly the quarter turn the second pose's
//      grip change introduces.
//   L: drawn tl is DLF; rows along +y (U side is down-screen in this
//      upside-down pose), columns along -z, so solver (row,col) =
//      (1-y, z+1) = (2-r, 2-c) - a 180-degree rotation. The guided L
//      permutation is identity, so to_scan_order is kRotate180.
//   B: drawn tl is DLB (the shared corner); rows along +y, columns along
//      +x, so solver (row,col) = (1-y, x+1) = (2-r, c) - a vertical flip.
//      Composing with the guided B permutation (kFlipHorizontal) gives
//      to_scan_order = kRotate180.
//
// Every table maps centre index 4 to 4 (all are rotations/reflections
// about the face centre); corner_observation_test.cpp's round-trip test
// verifies the whole composition against cube_from_scan().
using Permutation = std::array<int, 9>;
constexpr Permutation kIdentity = {0, 1, 2, 3, 4, 5, 6, 7, 8};
constexpr Permutation kRotate180 = {8, 7, 6, 5, 4, 3, 2, 1, 0};
constexpr Permutation kRotateQuarter = {2, 5, 8, 1, 4, 7, 0, 3, 6};

}  // namespace

std::array<CornerSlotMapping, 3> corner_slot_mappings(CornerViewId view) noexcept {
    using cube::model::Face;
    switch (view) {
        case CornerViewId::first_corner:
            return {{
                {.face = Face::U, .to_scan_order = kRotate180},
                {.face = Face::F, .to_scan_order = kIdentity},
                {.face = Face::R, .to_scan_order = kIdentity},
            }};
        case CornerViewId::second_corner:
            return {{
                {.face = Face::D, .to_scan_order = kRotateQuarter},
                {.face = Face::L, .to_scan_order = kRotate180},
                {.face = Face::B, .to_scan_order = kRotate180},
            }};
    }
    return {{
        {.face = Face::U, .to_scan_order = kIdentity},
        {.face = Face::F, .to_scan_order = kIdentity},
        {.face = Face::R, .to_scan_order = kIdentity},
    }};
}

}  // namespace cube::vision
