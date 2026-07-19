#include <catch2/catch_test_macros.hpp>

#include <cube/vision/grid_detection.hpp>

#include <opencv2/imgproc.hpp>

using namespace cube::vision;

namespace {

// A synthetic 3x3 grid of 60x60 white squares on a black background, with a
// 15px margin and 10px gaps - stands in for a cube face's 9 stickers
// without needing a real photo.
cv::Mat synthetic_grid_frame() {
    cv::Mat frame = cv::Mat::zeros(230, 230, CV_8UC3);
    constexpr int margin = 15;
    constexpr int cell = 60;
    constexpr int gap = 10;
    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 3; ++col) {
            const int x = margin + col * (cell + gap);
            const int y = margin + row * (cell + gap);
            cv::rectangle(frame, cv::Rect(x, y, cell, cell), cv::Scalar(255, 255, 255), cv::FILLED);
        }
    }
    return frame;
}

}  // namespace

TEST_CASE("detect_cube_grid finds a synthetic 3x3 grid", "[vision]") {
    const auto detection = detect_cube_grid(synthetic_grid_frame());

    REQUIRE(detection.has_value());
    REQUIRE(detection->coverage_ratio > 0.5F);
    REQUIRE(detection->coverage_ratio <= 1.0F);
}

TEST_CASE("detect_cube_grid arranges cells in row-major order", "[vision]") {
    const auto detection = detect_cube_grid(synthetic_grid_frame());
    REQUIRE(detection.has_value());

    const auto& cells = detection->cells;
    // Top-left cell (index 0) starts near the margin; bottom-right (index 8)
    // starts near the last row/column; the center cell (index 4) sits
    // between them on both axes.
    REQUIRE(cells[0].bounds.x < cells[1].bounds.x);
    REQUIRE(cells[1].bounds.x < cells[2].bounds.x);
    REQUIRE(cells[0].bounds.y < cells[3].bounds.y);
    REQUIRE(cells[3].bounds.y < cells[6].bounds.y);
    REQUIRE(cells[4].bounds.x > cells[0].bounds.x);
    REQUIRE(cells[4].bounds.y > cells[0].bounds.y);
}

TEST_CASE("detect_cube_grid returns nullopt for an empty frame", "[vision]") {
    REQUIRE_FALSE(detect_cube_grid(cv::Mat()).has_value());
}

TEST_CASE("detect_cube_grid returns nullopt when no grid is present", "[vision]") {
    const cv::Mat blank = cv::Mat::zeros(200, 200, CV_8UC3);
    REQUIRE_FALSE(detect_cube_grid(blank).has_value());
}
