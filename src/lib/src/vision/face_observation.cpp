#include <cube/vision/face_observation.hpp>

#include <cube/vision/grid_detection.hpp>

#include <opencv2/calib3d.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <expected>
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
[[nodiscard]] std::vector<StickerCandidate> candidates_from_condition_mask(cv::Mat mask) {
    const auto kernel = cv::getStructuringElement(cv::MORPH_RECT, cv::Size(3, 3));
    cv::morphologyEx(mask, mask, cv::MORPH_OPEN, kernel);
    return detail::candidates_from_binary_mask(mask);
}

[[nodiscard]] std::vector<StickerCandidate> find_saturation_candidates(const std::vector<cv::Mat>& hsv_channels) {
    return candidates_from_condition_mask(hsv_channels[1] >= kMinStickerSaturation);
}

[[nodiscard]] std::vector<StickerCandidate> find_white_candidates(const std::vector<cv::Mat>& hsv_channels) {
    cv::Mat white_mask;
    cv::bitwise_and(hsv_channels[2] >= kMinWhiteValue, hsv_channels[1] < kMinStickerSaturation, white_mask);
    return candidates_from_condition_mask(std::move(white_mask));
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

// Maps lattice coordinates (col-1, row-1 in [-1,1], cell centers at
// integers) to frame pixels. With >= 4 matched centers a full homography is
// estimable, which the affine anchor/basis model cannot express: under
// perspective the far row's pitch is genuinely shorter than the near row's,
// and only a homography places extrapolated cells correctly there. Below 4
// matches (or if estimation degenerates) the lattice's own affine frame is
// the best available model.
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

    if (lattice_points.size() >= 4) {
        cv::Mat homography = cv::findHomography(lattice_points, frame_points, cv::RANSAC, kHomographyReprojThreshold);
        if (!homography.empty()) {
            homography.convertTo(homography, CV_64F);
            return homography;
        }
    }

    cv::Mat affine = cv::Mat::eye(3, 3, CV_64F);
    affine.at<double>(0, 0) = static_cast<double>(fit.col_step.x);
    affine.at<double>(0, 1) = static_cast<double>(fit.row_step.x);
    affine.at<double>(0, 2) = static_cast<double>(fit.anchor.x);
    affine.at<double>(1, 0) = static_cast<double>(fit.col_step.y);
    affine.at<double>(1, 1) = static_cast<double>(fit.row_step.y);
    affine.at<double>(1, 2) = static_cast<double>(fit.anchor.y);
    return affine;
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

[[nodiscard]] float channel_median(std::vector<float>& values) {
    const auto mid = values.begin() + static_cast<std::ptrdiff_t>(values.size() / 2);
    std::ranges::nth_element(values, mid);
    return *mid;
}

// Patch-median Lab sample for the cell centered at `cell_uv`. Median, not
// mean: a glare highlight or a few gap pixels leaking into the patch shift
// a mean sample toward white/black but leave the median untouched until
// they exceed half the patch. The patch bound is the projected sample
// square's bounding rect clamped inside the frame (extrapolated cells can
// project partially outside it), never smaller than one pixel.
[[nodiscard]] cv::Vec3f sample_patch_lab(const cv::Mat& bgr_frame, const cv::Mat& transform, cv::Point2f cell_uv) {
    const auto corners = project_quad(transform, cell_uv, kSampleHalfExtent);
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

    const auto pixel_count = static_cast<std::size_t>(lab.rows) * static_cast<std::size_t>(lab.cols);
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
    return cv::Vec3f(channel_median(l_values), channel_median(a_values), channel_median(b_values));
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
            observation.lab[slot] = sample_patch_lab(bgr_frame, transform, cell_uv);
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
[[nodiscard]] bool grid_phase_plausible(const cv::Mat& bgr_frame, const cv::Mat& transform,
                                         const FaceObservation& observation) {
    // static const, not constexpr: cv::Point2f's constructors are not
    // constexpr in this OpenCV build.
    static const std::array<cv::Point2f, 12> kGapMidpoints = {{
        {-0.5F, -1.0F}, {0.5F, -1.0F}, {-0.5F, 0.0F}, {0.5F, 0.0F}, {-0.5F, 1.0F}, {0.5F, 1.0F},
        {-1.0F, -0.5F}, {0.0F, -0.5F}, {1.0F, -0.5F}, {-1.0F, 0.5F}, {0.0F, 0.5F}, {1.0F, 0.5F},
    }};
    constexpr float kInversionMargin = 8.0F;

    float cell_chroma = 0.0F;
    for (const auto& lab : observation.lab) {
        cell_chroma += std::hypot(lab[1], lab[2]);
    }
    cell_chroma /= static_cast<float>(observation.lab.size());

    float gap_chroma = 0.0F;
    for (const auto& midpoint : kGapMidpoints) {
        const auto lab = sample_patch_lab(bgr_frame, transform, midpoint);
        gap_chroma += std::hypot(lab[1], lab[2]);
    }
    gap_chroma /= static_cast<float>(kGapMidpoints.size());

    return gap_chroma <= cell_chroma + kInversionMargin;
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
    if (bgr_frame.empty()) {
        return std::unexpected(CubeError::no_valid_cube_detected);
    }

    cv::Mat hsv;
    cv::cvtColor(bgr_frame, hsv, cv::COLOR_BGR2HSV);
    std::vector<cv::Mat> hsv_channels;
    cv::split(hsv, hsv_channels);

    auto candidates = merge_candidates({detail::find_square_candidates(bgr_frame),
                                        detail::find_dark_square_candidates(bgr_frame),
                                        find_saturation_candidates(hsv_channels),
                                        find_white_candidates(hsv_channels)});
    const auto fit = detail::fit_lattice_detailed(std::move(candidates), bgr_frame.size());
    if (fit) {
        const cv::Mat transform = lattice_to_frame_transform(*fit);
        auto observation = build_observation(
            bgr_frame, transform, static_cast<float>(fit->inlier_count) / static_cast<float>(cells_per_grid));
        if (grid_phase_plausible(bgr_frame, transform, observation)) {
            return observation;
        }
        // A phase-inverted lattice fit is worse than no fit: fall through
        // to the outline path rather than capturing lattice material as
        // sticker colours.
    }

    if (const auto transform = outline_transform(bgr_frame, hsv_channels)) {
        auto observation = build_observation(bgr_frame, *transform, 0.4F);
        if (grid_phase_plausible(bgr_frame, *transform, observation)) {
            return observation;
        }
    }

    return std::unexpected(CubeError::no_valid_cube_detected);
}

}  // namespace cube::vision
