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
            cv::rectangle(frame, cv::Rect(x, y, kCell, kCell), layout[static_cast<std::size_t>(index)].bgr,
                          cv::FILLED);
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
            REQUIRE(point_distance(quad_centroid(observation->cells[index]), flat_cell_center(row, col)) < 12.0F);
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
            REQUIRE(point_distance(quad_centroid(observation->cells[index]), expected_center[0]) < 12.0F);
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
