#include <cube/vision/simple_face_pipeline.hpp>

#include <cube/vision/color_resolution.hpp>

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <numbers>
#include <numeric>
#include <vector>

namespace cube::vision::simple {

namespace {

using cube::model::FacetColor;

// --- geometry helpers ------------------------------------------------------

[[nodiscard]] float dist(const cv::Point2f& a, const cv::Point2f& b) {
    return static_cast<float>(cv::norm(a - b));
}

[[nodiscard]] float corner_angle_deg(const cv::Point2f& prev, const cv::Point2f& at,
                                     const cv::Point2f& next) {
    const cv::Point2f u = prev - at;
    const cv::Point2f v = next - at;
    const float len = static_cast<float>(cv::norm(u)) * static_cast<float>(cv::norm(v));
    if (len <= 0.0F) {
        return 0.0F;
    }
    const float cosine = std::clamp((u.x * v.x + u.y * v.y) / len, -1.0F, 1.0F);
    return std::acos(cosine) * 180.0F / std::numbers::pi_v<float>;
}

// Clockwise from the corner nearest the image origin. Any consistent
// convention would do (scan_assembly searches all four in-plane rotations
// regardless), but it must be a FUNCTION of the quad alone: two frames of
// the same face have to hand the sampler the same corner ordering, or the
// nine cells permute between readings and the stability metric measures the
// ordering rather than the sampler.
[[nodiscard]] std::array<cv::Point2f, 4> canonical_order(std::array<cv::Point2f, 4> corners) {
    cv::Point2f centroid{};
    for (const auto& corner : corners) {
        centroid += corner;
    }
    centroid *= 0.25F;

    // Image coordinates are y-down, so an increasing atan2 sweeps clockwise
    // on screen - sorting ascending IS clockwise here.
    std::sort(corners.begin(), corners.end(),
              [&centroid](const cv::Point2f& a, const cv::Point2f& b) {
                  return std::atan2(a.y - centroid.y, a.x - centroid.x) <
                         std::atan2(b.y - centroid.y, b.x - centroid.x);
              });

    std::size_t first = 0;
    float best = std::numeric_limits<float>::max();
    for (std::size_t i = 0; i < corners.size(); ++i) {
        const float score = corners[i].x + corners[i].y;
        if (score < best) {
            best = score;
            first = i;
        }
    }
    std::array<cv::Point2f, 4> ordered{};
    for (std::size_t i = 0; i < corners.size(); ++i) {
        ordered[i] = corners[(first + i) % corners.size()];
    }
    return ordered;
}

// Shape sanity only - see PipelineParams. Returns nullopt for anything that
// cannot be a cube face in a hand-held frame.
[[nodiscard]] std::optional<FaceQuad> accept_quad(std::array<cv::Point2f, 4> corners,
                                                  QuadSource source, const cv::Size& frame,
                                                  const PipelineParams& params) {
    const std::array<cv::Point2f, 4> ordered = canonical_order(corners);

    std::array<float, 4> sides{};
    for (std::size_t i = 0; i < ordered.size(); ++i) {
        sides[i] = dist(ordered[i], ordered[(i + 1) % ordered.size()]);
    }
    const auto [min_side, max_side] = std::minmax_element(sides.begin(), sides.end());
    if (*min_side <= 1.0F) {
        return std::nullopt;
    }
    const float side_ratio = *max_side / *min_side;
    if (side_ratio > params.max_side_ratio) {
        return std::nullopt;
    }

    for (std::size_t i = 0; i < ordered.size(); ++i) {
        const float angle = corner_angle_deg(ordered[(i + 3) % ordered.size()], ordered[i],
                                             ordered[(i + 1) % ordered.size()]);
        if (angle < params.min_corner_deg || angle > params.max_corner_deg) {
            return std::nullopt;
        }
    }

    const std::vector<cv::Point2f> poly(ordered.begin(), ordered.end());
    const auto area = static_cast<float>(std::abs(cv::contourArea(poly)));
    const float frame_area = static_cast<float>(frame.width) * static_cast<float>(frame.height);
    if (frame_area <= 0.0F) {
        return std::nullopt;
    }
    const float area_fraction = area / frame_area;
    if (area_fraction < params.min_area_fraction || area_fraction > params.max_area_fraction) {
        return std::nullopt;
    }

    return FaceQuad{.corners = ordered,
                    .source = source,
                    .area_fraction = area_fraction,
                    .side_ratio = side_ratio};
}

// Both quad routes reduce to "find the largest acceptable convex 4-gon in a
// binary edge mask", so they share this step outright. Keeping one
// implementation is what makes the LSD-vs-Canny comparison a comparison of
// the EDGE EVIDENCE and nothing else.
//
// A dilation closes the one-pixel breaks every edge operator leaves at a
// cube's rounded corners; without it approxPolyDP sees an open arc and the
// contour wraps back on itself into an 8-gon.
[[nodiscard]] QuadCandidates quads_from_edge_mask(const cv::Mat& mask, QuadSource source,
                                                 const PipelineParams& params) {
    QuadCandidates found;
    cv::Mat closed;
    const cv::Mat kernel = cv::getStructuringElement(cv::MORPH_RECT, cv::Size(3, 3));
    cv::dilate(mask, closed, kernel, cv::Point(-1, -1), 2);
    found.edge_mask = closed;

    std::vector<std::vector<cv::Point>> contours;
    // RETR_LIST, not RETR_EXTERNAL: with a hand and a busy background in
    // frame the cube's outline is frequently an INNER contour of some larger
    // blob, and RETR_EXTERNAL would discard exactly the one we want.
    cv::findContours(closed, contours, cv::RETR_LIST, cv::CHAIN_APPROX_SIMPLE);

    // A SWEEP of approxPolyDP tolerances, not the single 0.02 the reference
    // designs use. Measured reason: at a fixed 0.02*perimeter the face
    // outline of runD never reduced to four vertices - a hand-held cube's
    // rounded corners and the head/hand edges that touch its silhouette
    // leave the contour with five or six significant turns - while every
    // individual STICKER did, so the largest surviving candidate was always
    // a sticker and all six stored faces came out uniform. Coarser
    // tolerances collapse the extra turns; the shape gates below, not the
    // tolerance, decide what is admissible.
    constexpr std::array<double, 5> kApproxTolerances{0.02, 0.03, 0.05, 0.08, 0.12};

    for (const auto& contour : contours) {
        if (contour.size() < 4) {
            continue;
        }
        const double perimeter = cv::arcLength(contour, true);
        for (const double tolerance : kApproxTolerances) {
            std::vector<cv::Point> approx;
            cv::approxPolyDP(contour, approx, tolerance * perimeter, true);
            if (approx.size() != 4 || !cv::isContourConvex(approx)) {
                continue;
            }
            std::array<cv::Point2f, 4> corners{};
            for (std::size_t i = 0; i < corners.size(); ++i) {
                corners[i] =
                    cv::Point2f(static_cast<float>(approx[i].x), static_cast<float>(approx[i].y));
            }
            if (auto candidate = accept_quad(corners, source, mask.size(), params)) {
                found.quads.push_back(*candidate);
                break;  // one quad per contour: the finest tolerance that
                        // yields an admissible 4-gon is the best fit to it
            }
        }
    }
    return found;
}

[[nodiscard]] cv::Mat to_gray(const cv::Mat& bgr) {
    if (bgr.channels() == 1) {
        return bgr;
    }
    cv::Mat gray;
    cv::cvtColor(bgr, gray, cv::COLOR_BGR2GRAY);
    return gray;
}

// --- colour ----------------------------------------------------------------

// White decided on the SATURATION/VALUE axis before any hue or chroma
// comparison. Motivated by the corpus measurement that killed the chroma
// route: the camera's AWB under warm light crushes the cool channels, so
// yellow/red/orange arrive at chroma 48-74 while green sits at 26-31, blue
// at 18-24, and a warm-cast WHITE at 15.7-21.0 - white and blue overlap
// outright on chroma magnitude, and no threshold on that axis can separate
// them. Saturation does not have that problem: the whites' residual warm
// cast is a small tint on a bright pixel (low S, high V) while a crushed
// blue is still a definite hue on a dark one (higher S, much lower V).
//
// Hue bands then name the rest, in OpenCV's 0-179 half-degree units.
[[nodiscard]] FacetColor classify_hsv_pixel(const cv::Vec3b& hsv) {
    const float h = static_cast<float>(hsv[0]);
    const float s = static_cast<float>(hsv[1]);
    const float v = static_cast<float>(hsv[2]);

    // A pixel too dark to be any sticker at all: a shadowed inter-tile gap,
    // or the dark shirt behind the cube. Measured motivation (M2): two of
    // the production stack's 54 samples arrived at L=9.1/12.9. Naming those
    // is worse than abstaining, so they vote for nothing nameable.
    constexpr float kMinValue = 45.0F;
    if (v < kMinValue) {
        return FacetColor::unknown;
    }

    constexpr float kWhiteMaxSaturation = 90.0F;   // of 255
    constexpr float kWhiteMinValue = 130.0F;       // of 255
    if (s < kWhiteMaxSaturation && v > kWhiteMinValue) {
        return FacetColor::white;
    }

    if (h < 8.0F || h >= 172.0F) {
        return FacetColor::red;
    }
    if (h < 20.0F) {
        return FacetColor::orange;
    }
    if (h < 38.0F) {
        return FacetColor::yellow;
    }
    if (h < 88.0F) {
        return FacetColor::green;
    }
    if (h < 135.0F) {
        return FacetColor::blue;
    }
    return FacetColor::red;  // magenta side of the wrap, nearest red
}

[[nodiscard]] cv::Vec3f bgr_to_lab(const cv::Vec3b& bgr) {
    const cv::Mat3f sample(1, 1,
                           cv::Vec3f(static_cast<float>(bgr[0]) / 255.0F,
                                     static_cast<float>(bgr[1]) / 255.0F,
                                     static_cast<float>(bgr[2]) / 255.0F));
    cv::Mat3f lab;
    cv::cvtColor(sample, lab, cv::COLOR_BGR2Lab);
    return lab.at<cv::Vec3f>(0, 0);
}

[[nodiscard]] float median_of(std::vector<float>& values) {
    if (values.empty()) {
        return 0.0F;
    }
    const std::size_t mid = values.size() / 2;
    std::nth_element(values.begin(), values.begin() + static_cast<std::ptrdiff_t>(mid),
                     values.end());
    return values[mid];
}

// Per-pixel vote across the patch, then the majority gate (M3). The vote is
// per-pixel rather than "classify the patch median" on purpose: a median is
// a single number that cannot tell a clean sticker from a half-sticker
// half-gap patch whose average happens to land on a plausible colour, and
// distinguishing those two is the entire job of the abstention rule.
struct PatchVerdict {
    FacetColor color = FacetColor::unknown;
    float purity = 0.0F;
    cv::Vec3f lab{};
    int pixels = 0;
};

[[nodiscard]] PatchVerdict classify_patch(const std::vector<cv::Vec3b>& bgr_pixels,
                                          const PipelineParams& params) {
    PatchVerdict verdict;
    verdict.pixels = static_cast<int>(bgr_pixels.size());
    if (bgr_pixels.empty()) {
        return verdict;
    }

    std::vector<float> l_values;
    std::vector<float> a_values;
    std::vector<float> b_values;
    l_values.reserve(bgr_pixels.size());
    a_values.reserve(bgr_pixels.size());
    b_values.reserve(bgr_pixels.size());

    std::array<int, 7> votes{};  // indexed by FacetColor, unknown at 0
    cv::Mat hsv_input;
    if (params.classifier == ClassifierMode::hsv_white_axis) {
        cv::Mat bgr_row(1, static_cast<int>(bgr_pixels.size()), CV_8UC3,
                        const_cast<cv::Vec3b*>(bgr_pixels.data()));
        cv::cvtColor(bgr_row, hsv_input, cv::COLOR_BGR2HSV);
    }

    for (std::size_t i = 0; i < bgr_pixels.size(); ++i) {
        const cv::Vec3f lab = bgr_to_lab(bgr_pixels[i]);
        l_values.push_back(lab[0]);
        a_values.push_back(lab[1]);
        b_values.push_back(lab[2]);

        FacetColor vote = FacetColor::unknown;
        if (params.classifier == ClassifierMode::hsv_white_axis) {
            vote = classify_hsv_pixel(hsv_input.at<cv::Vec3b>(0, static_cast<int>(i)));
        } else {
            vote = classify_lab(lab).color;
        }
        ++votes[static_cast<std::size_t>(vote)];
    }

    verdict.lab = cv::Vec3f(median_of(l_values), median_of(a_values), median_of(b_values));

    std::size_t best = 0;
    for (std::size_t c = 1; c < votes.size(); ++c) {
        if (votes[c] > votes[best]) {
            best = c;
        }
    }
    const float total = static_cast<float>(bgr_pixels.size());
    // Purity is measured against ALL sampled pixels, including the ones that
    // voted unknown - a patch half of which is unnameable is exactly the
    // contaminated patch the gate exists to reject, and dividing by the
    // nameable pixels only would hide that.
    verdict.purity = static_cast<float>(votes[best]) / total;
    if (best == static_cast<std::size_t>(FacetColor::unknown) ||
        verdict.purity < params.min_purity) {
        verdict.color = FacetColor::unknown;
        return verdict;
    }
    verdict.color = static_cast<FacetColor>(best);
    return verdict;
}

// Bilinear point on the quad at (u, v) in [0,1]^2, u along corner 0->1.
[[nodiscard]] cv::Point2f quad_point(const std::array<cv::Point2f, 4>& c, float u, float v) {
    const cv::Point2f top = c[0] + (c[1] - c[0]) * u;
    const cv::Point2f bottom = c[3] + (c[2] - c[3]) * u;
    return top + (bottom - top) * v;
}

}  // namespace

std::string_view to_string(QuadSource source) {
    switch (source) {
        case QuadSource::line_segments:
            return "lsd";
        case QuadSource::canny:
            return "canny";
        case QuadSource::sticker_hull:
            return "hull";
        case QuadSource::none:
            break;
    }
    return "none";
}

std::string_view to_string(ClassifierMode mode) {
    switch (mode) {
        case ClassifierMode::hsv_white_axis:
            return "hsv";
        case ClassifierMode::lab_hue_bands:
            break;
    }
    return "lab";
}

QuadCandidates quad_candidates_line_segments(const cv::Mat& bgr, const PipelineParams& params) {
    if (bgr.empty()) {
        return {};
    }
    const cv::Mat gray = to_gray(bgr);

    // LSD_REFINE_STD per the reference design: standard refinement breaks
    // the arcs a rounded cube corner produces into straight pieces, which is
    // exactly the failure mode that would otherwise merge two face edges
    // into one curve and cost the quad a vertex.
    auto detector = cv::createLineSegmentDetector(cv::LSD_REFINE_STD);
    std::vector<cv::Vec4f> segments;
    detector->detect(gray, segments);

    // Short segments are texture - the shirt's weave, the fence behind the
    // window, the JPEG ringing along a highlight. A cube face edge in this
    // corpus is ~200 px; a sticker edge ~70. The floor is set relative to
    // the frame so it does not encode this clip's framing.
    const float min_length = 0.06F * static_cast<float>(std::min(gray.cols, gray.rows));

    cv::Mat mask = cv::Mat::zeros(gray.size(), CV_8UC1);
    for (const auto& segment : segments) {
        const cv::Point2f a(segment[0], segment[1]);
        const cv::Point2f b(segment[2], segment[3]);
        if (dist(a, b) < min_length) {
            continue;
        }
        cv::line(mask, cv::Point(cvRound(a.x), cvRound(a.y)), cv::Point(cvRound(b.x), cvRound(b.y)),
                 cv::Scalar(255), 2);
    }
    return quads_from_edge_mask(mask, QuadSource::line_segments, params);
}

QuadCandidates quad_candidates_canny(const cv::Mat& bgr, const PipelineParams& params) {
    if (bgr.empty()) {
        return {};
    }
    cv::Mat gray = to_gray(bgr);
    cv::Mat blurred;
    cv::GaussianBlur(gray, blurred, cv::Size(5, 5), 0.0);
    cv::Mat edges;
    cv::Canny(blurred, edges, 50, 150);
    return quads_from_edge_mask(edges, QuadSource::canny, params);
}

// THE choice: the largest admissible quad. A face contains its stickers, so
// on evidence that offers both, the face wins on area by roughly 9x. This is
// only sound because the candidate list is now complete - when the face
// outline was absent from it (the fixed-tolerance bug above) "largest" chose
// confidently and wrongly, and reported full confidence while doing it.
namespace {

[[nodiscard]] std::optional<FaceQuad> largest_of(const QuadCandidates& candidates) {
    if (candidates.quads.empty()) {
        return std::nullopt;
    }
    return *std::max_element(candidates.quads.begin(), candidates.quads.end(),
                             [](const FaceQuad& a, const FaceQuad& b) {
                                 return a.area_fraction < b.area_fraction;
                             });
}

}  // namespace

std::optional<FaceQuad> detect_quad_sticker_hull(const cv::Mat& bgr, const PipelineParams& params) {
    if (bgr.empty()) {
        return std::nullopt;
    }
    const QuadCandidates candidates = quad_candidates_line_segments(bgr, params);
    if (static_cast<int>(candidates.quads.size()) < params.min_hull_stickers) {
        return std::nullopt;
    }

    // Sticker scale is taken from the MEDIAN candidate area, not the mean:
    // one window pane or shirt panel among the candidates is many times a
    // sticker's area and would drag a mean out of the sticker range
    // entirely, taking the scale band with it.
    std::vector<float> areas;
    areas.reserve(candidates.quads.size());
    for (const auto& quad : candidates.quads) {
        areas.push_back(quad.area_fraction);
    }
    std::vector<float> sorted = areas;
    const float median = median_of(sorted);
    if (median <= 0.0F) {
        return std::nullopt;
    }

    std::vector<cv::Point2f> points;
    int kept = 0;
    for (const auto& quad : candidates.quads) {
        if (quad.area_fraction > median * params.sticker_area_tolerance ||
            quad.area_fraction * params.sticker_area_tolerance < median) {
            continue;
        }
        ++kept;
        points.insert(points.end(), quad.corners.begin(), quad.corners.end());
    }
    if (kept < params.min_hull_stickers) {
        return std::nullopt;
    }

    std::vector<cv::Point2f> hull;
    cv::convexHull(points, hull);
    if (hull.size() < 4) {
        return std::nullopt;
    }
    const double perimeter = cv::arcLength(hull, true);

    constexpr std::array<double, 6> kApproxTolerances{0.01, 0.02, 0.03, 0.05, 0.08, 0.12};
    for (const double tolerance : kApproxTolerances) {
        std::vector<cv::Point2f> approx;
        cv::approxPolyDP(hull, approx, tolerance * perimeter, true);
        if (approx.size() != 4) {
            continue;
        }
        std::array<cv::Point2f, 4> corners{};
        std::copy(approx.begin(), approx.end(), corners.begin());
        auto quad = accept_quad(corners, QuadSource::sticker_hull, bgr.size(), params);
        if (!quad) {
            continue;
        }
        // THE self-validating test: nine stickers tile a face, so the
        // enclosure must measure about nine of them. This is what refuses a
        // hull over four stickers huddled in one corner - the exact sparse,
        // lopsided evidence the production stack extrapolated confidently
        // from - rather than emitting a well-formed quad in the wrong place.
        const float ratio = quad->area_fraction / median;
        if (ratio < params.min_hull_area_ratio || ratio > params.max_hull_area_ratio) {
            return std::nullopt;
        }
        return quad;
    }
    return std::nullopt;
}

std::optional<FaceQuad> detect_quad_line_segments(const cv::Mat& bgr,
                                                  const PipelineParams& params) {
    return largest_of(quad_candidates_line_segments(bgr, params));
}

std::optional<FaceQuad> detect_quad_canny(const cv::Mat& bgr, const PipelineParams& params) {
    return largest_of(quad_candidates_canny(bgr, params));
}

FaceReading read_face(const cv::Mat& bgr, const PipelineParams& params) {
    FaceReading reading;
    if (bgr.empty()) {
        return reading;
    }

    // ONLY the sticker-hull route, with NO fallback to the outline routes.
    //
    // The fallback was there first and it had to go, on measurement. On runD
    // the outline routes return a 4-gon on 87.9% of frames and it is a
    // single sticker every time, so falling back to them raised the quad
    // rate from 32.9% to 88.2% while filling the store with faces read
    // entirely inside one tile - five of the six came out uniform
    // (YYYYYYYYY, OOOOOOOOO, ...) and were indistinguishable, from the
    // outside, from six good captures. Coverage bought that way is negative
    // value: FaceStore keeps the first complete reading, so one sticker-quad
    // frame arriving before a good one permanently poisons that face.
    //
    // A frame the hull route declines is simply not read. That is the same
    // trade the abstention rule makes per cell (M3), applied per frame.
    const auto quad = detect_quad_sticker_hull(bgr, params);
    if (!quad) {
        return reading;
    }
    reading.quad = quad;

    const auto& corners = quad->corners;
    // Cell pitch in pixels, from the mean of the four sides - the patch has
    // to stay inside its sticker on the far side of a perspective-shortened
    // quad too, so the smaller estimate is the safe one to size against.
    float min_side = std::numeric_limits<float>::max();
    for (std::size_t i = 0; i < corners.size(); ++i) {
        min_side = std::min(min_side, dist(corners[i], corners[(i + 1) % corners.size()]));
    }
    const float cell_pitch = min_side / 3.0F;
    const int radius = std::max(1, static_cast<int>(params.patch_radius_cells * cell_pitch));

    constexpr std::array<float, 3> kFractions{1.0F / 6.0F, 0.5F, 5.0F / 6.0F};
    std::vector<cv::Vec3b> patch;
    reading.unknown_count = 0;
    for (std::size_t row = 0; row < kFractions.size(); ++row) {
        for (std::size_t col = 0; col < kFractions.size(); ++col) {
            const cv::Point2f centre = quad_point(corners, kFractions[col], kFractions[row]);
            patch.clear();
            const int cx = cvRound(centre.x);
            const int cy = cvRound(centre.y);
            for (int dy = -radius; dy <= radius; ++dy) {
                for (int dx = -radius; dx <= radius; ++dx) {
                    if (dx * dx + dy * dy > radius * radius) {
                        continue;  // disc, not square - corners of a square
                                   // patch reach furthest toward the gap
                    }
                    const int x = cx + dx;
                    const int y = cy + dy;
                    if (x < 0 || y < 0 || x >= bgr.cols || y >= bgr.rows) {
                        continue;
                    }
                    patch.push_back(bgr.at<cv::Vec3b>(y, x));
                }
            }
            const PatchVerdict verdict = classify_patch(patch, params);
            const std::size_t index = row * kFractions.size() + col;
            reading.cells[index] = CellReading{.color = verdict.color,
                                               .purity = verdict.purity,
                                               .lab = verdict.lab,
                                               .centre = centre,
                                               .pixels = verdict.pixels};
            if (verdict.color == FacetColor::unknown) {
                ++reading.unknown_count;
            }
        }
    }
    return reading;
}

// --- face store ------------------------------------------------------------

std::size_t FaceStore::slot_index(FacetColor centre) {
    // FacetColor is unknown, white, yellow, red, orange, green, blue - so
    // the six nameable colours are 1..6 and map to 0..5 here. as_faces()
    // relies on this being that same order.
    const auto raw = static_cast<std::size_t>(centre);
    return raw == 0 ? kSlotCount : raw - 1;
}

bool FaceStore::offer(const FaceReading& reading) {
    if (!reading.complete()) {
        return false;
    }
    const FacetColor centre = reading.centre_color();
    const std::size_t index = slot_index(centre);
    if (index >= kSlotCount) {
        return false;
    }
    Slot& slot = slots_[index];

    std::array<FacetColor, kCellCount> cells{};
    for (std::size_t i = 0; i < kCellCount; ++i) {
        cells[i] = reading.cells[i].color;
    }

    ++slot.readings;
    if (!slot.filled) {
        slot.filled = true;
        slot.cells = cells;
        return true;
    }
    // A conflict is not an error to swallow: two readings of the same
    // physical face disagreeing is the single most useful number this store
    // produces about the sampler, so it is counted and the first reading is
    // kept unchanged (see the header - no voting, deliberately).
    if (slot.cells != cells) {
        ++slot.conflicts;
    }
    return false;
}

const FaceStore::Slot& FaceStore::slot(FacetColor centre) const {
    const std::size_t index = slot_index(centre);
    return index < kSlotCount ? slots_[index] : empty_;
}

std::size_t FaceStore::filled_count() const {
    return static_cast<std::size_t>(
        std::count_if(slots_.begin(), slots_.end(), [](const Slot& s) { return s.filled; }));
}

std::optional<std::array<std::array<cube::model::Facelet, kCellCount>, 6>> FaceStore::as_faces()
    const {
    if (filled_count() != kSlotCount) {
        return std::nullopt;
    }
    std::array<std::array<cube::model::Facelet, kCellCount>, 6> faces{};
    for (std::size_t s = 0; s < kSlotCount; ++s) {
        for (std::size_t i = 0; i < kCellCount; ++i) {
            faces[s][i] = cube::model::Facelet{.color = slots_[s].cells[i], .confidence = 1.0F};
        }
    }
    return faces;
}

}  // namespace cube::vision::simple
