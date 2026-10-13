#include <catch2/catch_test_macros.hpp>

#include <cube/error.hpp>
#include <cube/model/facelet.hpp>
#include <cube/vision/color_classification.hpp>
#include <cube/vision/face_observation.hpp>

#include <opencv2/imgproc.hpp>

#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <vector>

using namespace cube::vision;
using cube::model::FacetColor;

namespace {

constexpr int kMargin = 15;
constexpr int kCell = 60;
constexpr int kGap = 10;
constexpr int kFrameSize = 230;  // 15 + 3*60 + 2*10 + 15

struct NamedColor {
    FacetColor color;
    cv::Scalar bgr;
};

// All 6 palette colours across the 9 cells, drawn with the exact BGR seed
// values default_palette() converts to Lab - so a correct patch sample's
// nearest palette entry is unambiguous.
const std::array<NamedColor, 9>& face_layout() {
    static const std::array<NamedColor, 9> layout{{
        {FacetColor::white, cv::Scalar(255, 255, 255)},
        {FacetColor::yellow, cv::Scalar(0, 213, 255)},
        {FacetColor::red, cv::Scalar(0, 0, 196)},
        {FacetColor::orange, cv::Scalar(0, 106, 255)},
        {FacetColor::green, cv::Scalar(72, 155, 0)},
        {FacetColor::blue, cv::Scalar(173, 70, 0)},
        {FacetColor::white, cv::Scalar(255, 255, 255)},
        {FacetColor::yellow, cv::Scalar(0, 213, 255)},
        {FacetColor::red, cv::Scalar(0, 0, 196)},
    }};
    return layout;
}

// A synthetic 3x3 face: 60x60 coloured stickers on a dark background with
// 10px gaps. `skip_index` (row-major) leaves that sticker undrawn - stands
// in for a sticker neither segmentation strategy could isolate.
cv::Mat synthetic_face_frame(int skip_index = -1) {
    cv::Mat frame = cv::Mat::zeros(kFrameSize, kFrameSize, CV_8UC3);
    const auto& layout = face_layout();
    int index = 0;
    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 3; ++col, ++index) {
            if (index == skip_index) {
                continue;
            }
            const int x = kMargin + col * (kCell + kGap);
            const int y = kMargin + row * (kCell + kGap);
            cv::rectangle(frame, cv::Rect(x, y, kCell, kCell),
                          layout[static_cast<std::size_t>(index)].bgr, cv::FILLED);
        }
    }
    return frame;
}

// Analytic center of cell (row, col) in the unwarped frame.
cv::Point2f flat_cell_center(int row, int col) {
    return cv::Point2f(static_cast<float>(kMargin + col * (kCell + kGap)) + kCell / 2.0F,
                       static_cast<float>(kMargin + row * (kCell + kGap)) + kCell / 2.0F);
}

FacetColor nearest_palette_color(const cv::Vec3f& lab) {
    const ColorPalette palette = default_palette();
    FacetColor best = FacetColor::unknown;
    float best_distance = std::numeric_limits<float>::max();
    for (const auto& entry : palette.entries) {
        const cv::Vec3f diff = lab - entry.lab;
        const float distance = std::sqrt(diff.dot(diff));
        if (distance < best_distance) {
            best_distance = distance;
            best = entry.color;
        }
    }
    return best;
}

cv::Point2f quad_centroid(const StickerQuad& quad) {
    cv::Point2f sum(0.0F, 0.0F);
    for (const auto& corner : quad.corners) {
        sum += corner;
    }
    return cv::Point2f(sum.x / 4.0F, sum.y / 4.0F);
}

float point_distance(cv::Point2f a, cv::Point2f b) {
    const cv::Point2f d = a - b;
    return std::hypot(d.x, d.y);
}

}  // namespace

TEST_CASE("observe_face reads all 9 sticker colours from a synthetic face", "[vision]") {
    const auto observation = observe_face(synthetic_face_frame());
    REQUIRE(observation.has_value());
    REQUIRE(observation->geometry_confidence > 0.9F);

    const auto& layout = face_layout();
    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 3; ++col) {
            const auto index = static_cast<std::size_t>(row * 3 + col);
            REQUIRE(nearest_palette_color(observation->lab[index]) == layout[index].color);
            REQUIRE(point_distance(quad_centroid(observation->cells[index]),
                                   flat_cell_center(row, col)) < 12.0F);
        }
    }
}

TEST_CASE("observe_face stays correct under a perspective warp", "[vision]") {
    const cv::Mat flat = synthetic_face_frame();

    // ~20-25 degree tilt about a mostly-vertical axis: the right edge is
    // foreshortened to ~86% of the left edge's height.
    const std::array<cv::Point2f, 4> source_corners{{
        cv::Point2f(0.0F, 0.0F),
        cv::Point2f(static_cast<float>(kFrameSize), 0.0F),
        cv::Point2f(static_cast<float>(kFrameSize), static_cast<float>(kFrameSize)),
        cv::Point2f(0.0F, static_cast<float>(kFrameSize)),
    }};
    const std::array<cv::Point2f, 4> warped_corners{{
        cv::Point2f(40.0F, 50.0F),
        cv::Point2f(290.0F, 62.0F),
        cv::Point2f(288.0F, 270.0F),
        cv::Point2f(42.0F, 292.0F),
    }};
    const cv::Mat warp = cv::getPerspectiveTransform(source_corners.data(), warped_corners.data());
    cv::Mat warped;
    cv::warpPerspective(flat, warped, warp, cv::Size(340, 340));

    const auto observation = observe_face(warped);
    REQUIRE(observation.has_value());

    const auto& layout = face_layout();
    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 3; ++col) {
            const auto index = static_cast<std::size_t>(row * 3 + col);
            REQUIRE(nearest_palette_color(observation->lab[index]) == layout[index].color);

            const std::vector<cv::Point2f> flat_center{flat_cell_center(row, col)};
            std::vector<cv::Point2f> expected_center;
            cv::perspectiveTransform(flat_center, expected_center, warp);
            REQUIRE(point_distance(quad_centroid(observation->cells[index]), expected_center[0]) <
                    12.0F);
        }
    }
}

TEST_CASE("observe_face extrapolates a missing sticker's quad", "[vision]") {
    const auto observation = observe_face(synthetic_face_frame(4));
    REQUIRE(observation.has_value());
    // The undrawn center sticker cannot have matched a contour.
    REQUIRE(observation->geometry_confidence < 1.0F);
    REQUIRE(observation->geometry_confidence >= 6.0F / 9.0F);

    REQUIRE(point_distance(quad_centroid(observation->cells[4]), flat_cell_center(1, 1)) < 12.0F);
}

TEST_CASE("a two-row-span fit yields a pose-only observation, never capture-grade", "[vision]") {
    // Only the top two rows drawn: six genuine anchors spanning two rows
    // fit a clean lattice whose TRANSLATION is ambiguous (the third row
    // could extend either way) and the completion scorer rejects every
    // placement (whichever way the missing row extends it lands on black
    // background and fails the cell-lightness gate). The canvas is
    // enlarged well past the face so the outline fallback cannot fire
    // either: a lone 60px tile then sits under the outline's 2%-of-frame
    // area floor (on a tight canvas it passes, and the outline happily
    // returns one uniform tile as a 'face' - gap and cell chroma are equal
    // there, so the phase check cannot object). The plane itself is real,
    // so the observation must come back pose_only - orientation for the
    // tracker, nothing for capture.
    constexpr int kWideFrameSize = 430;
    cv::Mat frame = cv::Mat::zeros(kWideFrameSize, kWideFrameSize, CV_8UC3);
    const auto& layout = face_layout();
    for (int row = 0; row < 2; ++row) {
        for (int col = 0; col < 3; ++col) {
            const int x = kMargin + col * (kCell + kGap);
            const int y = kMargin + row * (kCell + kGap);
            cv::rectangle(frame, cv::Rect(x, y, kCell, kCell),
                          layout[static_cast<std::size_t>(row * 3 + col)].bgr, cv::FILLED);
        }
    }

    const auto observation = observe_face(frame);
    REQUIRE(observation.has_value());
    CHECK(observation->pose_only);

    // And the flag defaults false on a full capture-grade read - the
    // pose-only fallback must never relabel the ordinary path.
    const auto full = observe_face(synthetic_face_frame());
    REQUIRE(full.has_value());
    CHECK_FALSE(full->pose_only);
}

TEST_CASE("observe_face reports no_valid_cube_detected for a blank frame", "[vision]") {
    const auto blank = observe_face(cv::Mat::zeros(200, 200, CV_8UC3));
    REQUIRE_FALSE(blank.has_value());
    REQUIRE(blank.error() == cube::CubeError::no_valid_cube_detected);

    const auto empty = observe_face(cv::Mat());
    REQUIRE_FALSE(empty.has_value());
    REQUIRE(empty.error() == cube::CubeError::no_valid_cube_detected);
}

// --- Per-cell sampling purity -------------------------------------------
//
// These exercise detail::cell_patch_sample, which runs the SAME
// patch_stats_for_corners body observe_face's per-cell sampler runs, off a
// caller-supplied quad - so a synthetic quad can put the sampler in
// geometries a synthetic 3x3 face cannot reach (strong shear, a
// contaminant filling most of the patch) and assert on what it returns.
namespace {

// The shrink cell_patch_sample applies to get from a CELL quad to the
// SAMPLE quad (kSampleHalfExtent / kCellQuadHalfExtent). Transcribed, not
// derived: if production's ratio moves, these tests must be re-measured
// rather than silently following it.
constexpr float kSampleShrink = 0.40F;

// The cell quad whose shrunken sample quad is exactly `sample_corners`.
StickerQuad cell_quad_for_sample(const std::array<cv::Point2f, 4>& sample_corners) {
    cv::Point2f centroid(0.0F, 0.0F);
    for (const auto& corner : sample_corners) {
        centroid += corner;
    }
    centroid *= 0.25F;
    StickerQuad cell;
    for (std::size_t k = 0; k < 4; ++k) {
        cell.corners[k] = centroid + (sample_corners[k] - centroid) / kSampleShrink;
    }
    return cell;
}

void fill_quad(cv::Mat& frame, const std::array<cv::Point2f, 4>& corners, const cv::Scalar& bgr) {
    std::array<cv::Point, 4> points{};
    for (std::size_t k = 0; k < 4; ++k) {
        points[k] = cv::Point(cvRound(corners[k].x), cvRound(corners[k].y));
    }
    cv::fillConvexPoly(frame, points.data(), 4, bgr, cv::LINE_8);
}

}  // namespace

// THE BOUNDING-RECT DEFECT. A sheared cell quad - what an obliquely viewed
// face projects to - has a bounding rect far larger than itself: the
// parallelogram below spans 16 px of base over a 20 px shear, so its
// 48x20 bounding rect is 3x its own 320 px area and two thirds of every
// pixel in that rect belongs to the NEIGHBOUR, not the cell. The sampler
// used to median the whole rect, so it returned the neighbour's colour by
// a 2:1 majority. Verified to fail before the mask: the pre-mask build
// returns the background's Lab here (L ~ 88 for the light neighbour, vs
// the cell's L ~ 32), so the first CHECK below fails by ~56 L.
TEST_CASE("cell sampling reads the cell, not its bounding rect", "[vision][patch-sampling]") {
    cv::Mat frame(80, 100, CV_8UC3, cv::Scalar(230, 230, 230));  // light neighbour material
    const std::array<cv::Point2f, 4> sample{
        {{10.0F, 10.0F}, {26.0F, 10.0F}, {58.0F, 30.0F}, {42.0F, 30.0F}}};
    fill_quad(frame, sample, cv::Scalar(40, 40, 40));  // the cell itself: dark

    const auto stats = detail::cell_patch_sample(frame, cell_quad_for_sample(sample));

    // The dark cell, not the light neighbour that fills most of the rect.
    CHECK(stats.median[0] < 45.0F);
    // And the mask really is the quad: the in-quad pixel count must be a
    // small fraction of the 48x20 = 960 px rect the sampler indexes.
    CHECK(stats.quad_px > 200);
    CHECK(stats.quad_px < 420);
    // A single-material patch reads as pure.
    CHECK(stats.purity > 0.9F);
}

// THE DOMINANT-MODE REFINEMENT. A plain median is unbiased only while the
// contaminant is a minority small enough to leave the median inside the
// tile's own distribution. Here 47.5% of the patch is a far-away dark
// colour and the tile's 52.5% carries the lightness ramp any real tile has
// under directional light (L 50.5 at one edge, 65.9 at the other, centre
// 58.2). The median INDEX then lands 2.5% into the tile's block - the right
// material, but at the very bottom of its spread, a colour the tile is
// nowhere. Walking the estimate onto the dense region fixes that.
//
// Verified to fail before the refinement, and the two stages really are
// separately covered - the measured values on this quad are:
//   full pre-fix build (no mask, no refinement) ... L 6.43, fails by 48.6
//   mask on, refinement compiled out ............. L 51.04, fails by 4.0
//   with both ..................................... inside the band below
// An earlier version of this comment claimed the mask was "nearly a no-op
// on this axis-aligned quad, so that build reads L 51.0 too". That is
// wrong: the quad is axis-aligned but the sample quad is the SHRUNKEN one,
// whose bounding rect reaches well into the black frame margin, so the
// pre-mask build lands at L 6.4 - the mask is decisive here as well. The
// isolation claim still holds via the middle row: with the mask on and the
// refinement off this test fails on its own.
TEST_CASE("cell sampling recentres on the dominant mode, not the split median",
          "[vision][patch-sampling]") {
    cv::Mat frame(80, 100, CV_8UC3, cv::Scalar(0, 0, 0));
    const std::array<cv::Point2f, 4> sample{
        {{20.0F, 20.0F}, {60.0F, 20.0F}, {60.0F, 60.0F}, {20.0F, 60.0F}}};
    // Rows 20..38 (19 of the patch's 40 rows) are a dark contaminant; rows
    // 39..59 are the tile, ramping across its own shading.
    fill_quad(frame, sample, cv::Scalar(20, 20, 20));
    for (int y = 39; y < 60; ++y) {
        const int level = 120 + 2 * (y - 39);  // BGR 120..160, i.e. L 50.5..65.9
        cv::rectangle(frame, cv::Rect(20, y, 41, 1), cv::Scalar(level, level, level), cv::FILLED);
    }

    const auto stats = detail::cell_patch_sample(frame, cell_quad_for_sample(sample));

    // The tile's centre, not the bottom edge of its ramp.
    CHECK(stats.median[0] > 55.0F);
    CHECK(stats.median[0] < 62.0F);
    // And the contamination is reported rather than hidden: a patch that is
    // 47.5% foreign must not claim to be pure.
    CHECK(stats.purity < 0.70F);
    CHECK(stats.purity > 0.40F);
}

// THE PURITY SIGNAL MUST FAIL CLOSED. The reliability field exists to flag
// a patch whose pixels do NOT agree on a colour, so the one case it must
// never get wrong is a patch with no dominant mode at all. Five equal-area
// materials, each 20% of the patch and each more than kModeAgreementRadius
// from its neighbours (BGR 40/80/120/160/210 -> L 16.1/34.0/50.0/65.9/84.2,
// adjacent gaps 16-18 L against the radius's 12 L equivalent), so no window
// reaches kMinModeShare and the refinement declines to move.
//
// VERIFIED TO FAIL PRE-FIX: the previous code initialised mode_share = 1.0
// and took the `share < kMinModeShare` break WITHOUT assigning it, so this
// patch reported purity = 1.000 - maximally clean, on the most contaminated
// input in the suite. Reproduced against the built pre-fix binary
// (purity=1.000, quad_px=1600) before the fix; it now reports 0.20, the
// share genuinely behind the returned median.
TEST_CASE("cell sampling reports low purity when no mode dominates", "[vision][patch-sampling]") {
    cv::Mat frame(80, 100, CV_8UC3, cv::Scalar(0, 0, 0));
    const std::array<cv::Point2f, 4> sample{
        {{20.0F, 20.0F}, {60.0F, 20.0F}, {60.0F, 60.0F}, {20.0F, 60.0F}}};
    constexpr std::array<int, 5> kLevels{{40, 80, 120, 160, 210}};
    for (std::size_t band = 0; band < kLevels.size(); ++band) {
        const int top = 20 + static_cast<int>(band) * 8;
        const auto level = static_cast<double>(kLevels[band]);
        cv::rectangle(frame, cv::Rect(20, top, 40, 8), cv::Scalar(level, level, level), cv::FILLED);
    }

    const auto stats = detail::cell_patch_sample(frame, cell_quad_for_sample(sample));

    // The whole quad is read - this is not a coverage failure being reported
    // as impurity.
    CHECK(stats.quad_px > 1500);
    // No material holds even a quarter of the patch, and the signal says so.
    CHECK(stats.purity < 0.5F);
    CHECK(stats.purity > 0.1F);
    // And the colour returned is still the middle band, not a fabrication.
    CHECK(stats.median[0] > 44.0F);
    CHECK(stats.median[0] < 56.0F);
}

// YIELD GUARD. A cell projected only a few pixels across - a distant or
// steeply foreshortened face - has no usable in-quad pixel set, and a
// sampler that refused those cells would buy purity with coverage. The
// fallback must still return the material under the quad.
TEST_CASE("cell sampling still reads a sub-pixel quad", "[vision][patch-sampling]") {
    cv::Mat frame(40, 40, CV_8UC3, cv::Scalar(30, 30, 30));
    cv::rectangle(frame, cv::Rect(18, 18, 3, 3), cv::Scalar(220, 220, 220), cv::FILLED);
    const std::array<cv::Point2f, 4> sample{
        {{19.0F, 19.0F}, {20.0F, 19.0F}, {20.0F, 20.0F}, {19.0F, 20.0F}}};

    const auto stats = detail::cell_patch_sample(frame, cell_quad_for_sample(sample));

    CHECK(stats.quad_px < 8);        // the mask alone would have yielded nothing usable
    CHECK(stats.median[0] > 70.0F);  // yet the bright patch under it still reads
}

// The purity signal has to be carried on the observation, not just
// available in the diagnostics seam, or no downstream stage can ever weigh
// a contaminated cell. On a clean synthetic face every cell is one
// material, so every cell must report near-total agreement.
TEST_CASE("observe_face reports per-cell sampling purity", "[vision][patch-sampling]") {
    const auto observation = observe_face(synthetic_face_frame());
    REQUIRE(observation.has_value());
    for (const float purity : observation->cell_purity) {
        CHECK(purity > 0.9F);
    }
}
