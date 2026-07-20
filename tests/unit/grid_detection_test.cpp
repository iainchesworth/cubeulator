#include <catch2/catch_test_macros.hpp>

#include <cube/vision/grid_detection.hpp>

#include <opencv2/imgproc.hpp>

#include <cmath>
#include <vector>

using namespace cube::vision;

namespace {

constexpr int kMargin = 15;
constexpr int kCell = 60;
constexpr int kGap = 10;

// A synthetic 3x3 grid of 60x60 white squares on a black background, with a
// 15px margin and 10px gaps - stands in for a cube face's 9 stickers
// without needing a real photo.
cv::Mat synthetic_grid_frame() {
    cv::Mat frame = cv::Mat::zeros(230, 230, CV_8UC3);
    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 3; ++col) {
            const int x = kMargin + col * (kCell + kGap);
            const int y = kMargin + row * (kCell + kGap);
            cv::rectangle(frame, cv::Rect(x, y, kCell, kCell), cv::Scalar(255, 255, 255), cv::FILLED);
        }
    }
    return frame;
}

// As synthetic_grid_frame(), but the cell at `skip_index` (row-major) is
// left undrawn - stands in for a sticker that failed to threshold as its
// own contour (e.g. blended into the background).
cv::Mat synthetic_grid_frame_missing_cell(int skip_index) {
    cv::Mat frame = cv::Mat::zeros(230, 230, CV_8UC3);
    int index = 0;
    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 3; ++col, ++index) {
            if (index == skip_index) {
                continue;
            }
            const int x = kMargin + col * (kCell + kGap);
            const int y = kMargin + row * (kCell + kGap);
            cv::rectangle(frame, cv::Rect(x, y, kCell, kCell), cv::Scalar(255, 255, 255), cv::FILLED);
        }
    }
    return frame;
}

// The standard grid plus an extra square-ish blob well outside the grid's
// footprint, sized to pass the same area/aspect filters as a real cell -
// stands in for background clutter (e.g. a light patch or object edge).
cv::Mat synthetic_grid_frame_with_distractor() {
    cv::Mat frame = cv::Mat::zeros(230, 300, CV_8UC3);
    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 3; ++col) {
            const int x = kMargin + col * (kCell + kGap);
            const int y = kMargin + row * (kCell + kGap);
            cv::rectangle(frame, cv::Rect(x, y, kCell, kCell), cv::Scalar(255, 255, 255), cv::FILLED);
        }
    }
    cv::rectangle(frame, cv::Rect(250, 130, 40, 40), cv::Scalar(255, 255, 255), cv::FILLED);
    return frame;
}

// The standard grid (mid-range, non-saturated colors) under a column-wise
// brightness gradient wide enough that no single global threshold could
// separate sticker from background everywhere: leftmost background
// (60-80=-20, clamped 0) vs leftmost sticker (180-80=100) needs a cutoff
// below 100, but rightmost background (60+80=140) vs rightmost sticker
// (180+80=255) needs a cutoff above 140 - a real regression case against
// the old single-Otsu-threshold behavior.
cv::Mat synthetic_grid_frame_with_gradient() {
    cv::Mat frame(230, 230, CV_8UC3, cv::Scalar(60, 60, 60));
    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 3; ++col) {
            const int x = kMargin + col * (kCell + kGap);
            const int y = kMargin + row * (kCell + kGap);
            cv::rectangle(frame, cv::Rect(x, y, kCell, kCell), cv::Scalar(180, 180, 180), cv::FILLED);
        }
    }
    for (int x = 0; x < frame.cols; ++x) {
        const double offset = -80.0 + 160.0 * (static_cast<double>(x) / static_cast<double>(frame.cols - 1));
        cv::add(frame.col(x), cv::Scalar(offset, offset, offset), frame.col(x));
    }
    return frame;
}

// The standard grid pasted into a larger canvas and rotated ~12 degrees
// about its own center - stands in for a hand-held cube not held dead-on to
// the camera. `paste_offset`/`rotation_center` are shared with the test
// that computes expected post-rotation cell positions.
constexpr int kRotationCanvasSize = 320;
constexpr int kPasteOffset = 45;
constexpr double kRotationDegrees = 12.0;

cv::Mat synthetic_grid_frame_rotated() {
    cv::Mat canvas = cv::Mat::zeros(kRotationCanvasSize, kRotationCanvasSize, CV_8UC3);
    const cv::Mat grid = synthetic_grid_frame();
    grid.copyTo(canvas(cv::Rect(kPasteOffset, kPasteOffset, grid.cols, grid.rows)));

    const cv::Point2f center(kRotationCanvasSize / 2.0F, kRotationCanvasSize / 2.0F);
    const cv::Mat rotation = cv::getRotationMatrix2D(center, kRotationDegrees, 1.0);
    cv::Mat rotated;
    cv::warpAffine(canvas, rotated, rotation, canvas.size());
    return rotated;
}

// Only 3 cells of the standard grid - below the inlier floor
// detect_cube_grid() requires to accept a lattice at all (the floor has
// been tuned down as the safety layers behind it grew; this test guards
// whatever its current value is from degrading into "always accepts").
cv::Mat synthetic_grid_frame_sparse() {
    cv::Mat frame = cv::Mat::zeros(230, 230, CV_8UC3);
    for (int index : {0, 2, 6}) {
        const int row = index / 3;
        const int col = index % 3;
        const int x = kMargin + col * (kCell + kGap);
        const int y = kMargin + row * (kCell + kGap);
        cv::rectangle(frame, cv::Rect(x, y, kCell, kCell), cv::Scalar(255, 255, 255), cv::FILLED);
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

TEST_CASE("detect_cube_grid extrapolates a missing cell", "[vision]") {
    const auto detection = detect_cube_grid(synthetic_grid_frame_missing_cell(4));
    REQUIRE(detection.has_value());

    const auto& cell = detection->cells[4];
    REQUIRE(cell.bounds.width > 0);
    REQUIRE(cell.bounds.height > 0);

    // Analytically known true center: margin + cell + gap + cell/2 = 115.
    const cv::Point center(cell.bounds.x + cell.bounds.width / 2, cell.bounds.y + cell.bounds.height / 2);
    REQUIRE(std::abs(center.x - 115) < 15);
    REQUIRE(std::abs(center.y - 115) < 15);

    // Row-major ordering still holds around the extrapolated cell.
    REQUIRE(detection->cells[3].bounds.x < cell.bounds.x);
    REQUIRE(cell.bounds.x < detection->cells[5].bounds.x);
    REQUIRE(detection->cells[1].bounds.y < cell.bounds.y);
    REQUIRE(cell.bounds.y < detection->cells[7].bounds.y);
}

TEST_CASE("detect_cube_grid ignores a distractor contour outside the grid", "[vision]") {
    const auto detection = detect_cube_grid(synthetic_grid_frame_with_distractor());
    REQUIRE(detection.has_value());

    for (const auto& cell : detection->cells) {
        REQUIRE(cell.bounds.x < 230);
    }
    REQUIRE(detection->cells[0].bounds.x < detection->cells[1].bounds.x);
    REQUIRE(detection->cells[1].bounds.x < detection->cells[2].bounds.x);
    REQUIRE(detection->cells[0].bounds.y < detection->cells[3].bounds.y);
    REQUIRE(detection->cells[3].bounds.y < detection->cells[6].bounds.y);
}

TEST_CASE("detect_cube_grid handles an uneven-lighting gradient a global threshold could not", "[vision]") {
    const auto detection = detect_cube_grid(synthetic_grid_frame_with_gradient());
    REQUIRE(detection.has_value());

    const auto& cells = detection->cells;
    REQUIRE(cells[0].bounds.x < cells[1].bounds.x);
    REQUIRE(cells[1].bounds.x < cells[2].bounds.x);
    REQUIRE(cells[0].bounds.y < cells[3].bounds.y);
    REQUIRE(cells[3].bounds.y < cells[6].bounds.y);
}

TEST_CASE("detect_cube_grid tolerates a rotated grid", "[vision]") {
    const auto detection = detect_cube_grid(synthetic_grid_frame_rotated());
    REQUIRE(detection.has_value());

    const cv::Point2f rotation_center(kRotationCanvasSize / 2.0F, kRotationCanvasSize / 2.0F);
    const cv::Mat rotation = cv::getRotationMatrix2D(rotation_center, kRotationDegrees, 1.0);

    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 3; ++col) {
            const double x = kPasteOffset + kMargin + col * (kCell + kGap) + kCell / 2.0;
            const double y = kPasteOffset + kMargin + row * (kCell + kGap) + kCell / 2.0;
            const double expected_x = rotation.at<double>(0, 0) * x + rotation.at<double>(0, 1) * y +
                                       rotation.at<double>(0, 2);
            const double expected_y = rotation.at<double>(1, 0) * x + rotation.at<double>(1, 1) * y +
                                       rotation.at<double>(1, 2);

            const auto& cell = detection->cells[row * 3 + col];
            const double actual_x = cell.bounds.x + cell.bounds.width / 2.0;
            const double actual_y = cell.bounds.y + cell.bounds.height / 2.0;

            REQUIRE(std::abs(actual_x - expected_x) < 20.0);
            REQUIRE(std::abs(actual_y - expected_y) < 20.0);
        }
    }
}

TEST_CASE("detect_cube_grid rejects a grid with too few cells", "[vision]") {
    REQUIRE_FALSE(detect_cube_grid(synthetic_grid_frame_sparse()).has_value());
}

TEST_CASE("dim-blur rounded cells survive the relaxed candidate poly gate", "[vision]") {
    // Octagonal cells stand in for tiles whose corners dim webcam blur has
    // rounded away: approxPolyDP reads them as 7-8 vertices with fill
    // ~0.83, which the historical 4-6-vertex / 0.65-fill gate accepted or
    // rejected on luck. All nine must now come back as candidates.
    cv::Mat mask = cv::Mat::zeros(230, 230, CV_8UC1);
    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 3; ++col) {
            const int cx = kMargin + col * (kCell + kGap) + kCell / 2;
            const int cy = kMargin + row * (kCell + kGap) + kCell / 2;
            std::vector<cv::Point> octagon;
            for (int vertex = 0; vertex < 8; ++vertex) {
                const double angle = (static_cast<double>(vertex) + 0.5) * 2.0 * 3.14159265358979 / 8.0;
                octagon.emplace_back(cx + static_cast<int>(std::lround(28.0 * std::cos(angle))),
                                     cy + static_cast<int>(std::lround(28.0 * std::sin(angle))));
            }
            cv::fillConvexPoly(mask, octagon, cv::Scalar(255));
        }
    }
    const auto candidates = detail::candidates_from_binary_mask(mask);
    REQUIRE(candidates.size() >= 9);
}

TEST_CASE("a starved low-contrast frame recovers candidates via the permissive-C retry", "[vision]") {
    // Tiles a whisker brighter than their surroundings: the standard
    // adaptive-threshold C wipes most of them out (warm dim light on a
    // matte cube - live1's frontal-but-failed class), and only the
    // conditional low-C retry can supply the lattice fitter. The frame
    // must yield at least the fitter's own acceptance floor of candidates.
    cv::Mat frame(230, 230, CV_8UC3, cv::Scalar(58, 58, 58));
    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 3; ++col) {
            const int x = kMargin + col * (kCell + kGap);
            const int y = kMargin + row * (kCell + kGap);
            cv::rectangle(frame, cv::Rect(x, y, kCell, kCell), cv::Scalar(66, 66, 66), cv::FILLED);
        }
    }
    const auto candidates = detail::find_square_candidates(frame);
    REQUIRE(candidates.size() >= 6);
}
