#include <cube/vision/grid_detection.hpp>

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cstddef>
#include <vector>

namespace cube::vision {

namespace {

constexpr double kMinCellAreaFraction = 0.002;  // 0.2% of frame area
constexpr double kMaxCellAreaFraction = 0.20;   // 20% of frame area
constexpr double kMinCellAspectRatio = 0.6;
constexpr double kMaxCellAspectRatio = 1.6;
constexpr double kApproxPolyEpsilonFraction = 0.03;
constexpr float kRowClusterFraction = 0.6F;  // fraction of average cell height treated as "same row"

struct Candidate {
    cv::Rect bounds;
    cv::Point2f center;
};

// Isolates the 9 bright stickers from the darker gaps/background between
// them via Otsu thresholding (adaptive per-frame, so it tracks lighting
// changes) on HSV's V channel - not plain grayscale luma, which weighs blue
// so low (0.114) that a fully-saturated blue sticker reads as dark as the
// background despite being just as reflective as any other sticker; V
// (max of B/G/R) doesn't have that hue bias. Known limitation, left for
// empirical tuning against a real cube/webcam: a single global brightness
// threshold can still misclassify stickers under uneven lighting (e.g. a
// shadowed corner of the frame).
[[nodiscard]] std::vector<Candidate> find_square_candidates(const cv::Mat& bgr_frame) {
    cv::Mat blurred;
    cv::GaussianBlur(bgr_frame, blurred, cv::Size(5, 5), 0);

    cv::Mat hsv;
    cv::cvtColor(blurred, hsv, cv::COLOR_BGR2HSV);
    std::vector<cv::Mat> hsv_channels;
    cv::split(hsv, hsv_channels);

    cv::Mat binary;
    cv::threshold(hsv_channels[2], binary, 0, 255, cv::THRESH_BINARY | cv::THRESH_OTSU);
    cv::morphologyEx(binary, binary, cv::MORPH_OPEN,
                      cv::getStructuringElement(cv::MORPH_RECT, cv::Size(3, 3)));

    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(binary, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);

    const double frame_area = static_cast<double>(bgr_frame.cols) * static_cast<double>(bgr_frame.rows);
    const double min_area = frame_area * kMinCellAreaFraction;
    const double max_area = frame_area * kMaxCellAreaFraction;

    std::vector<Candidate> candidates;
    for (const auto& contour : contours) {
        const double area = cv::contourArea(contour);
        if (area < min_area || area > max_area) {
            continue;
        }

        std::vector<cv::Point> approx;
        const double epsilon = kApproxPolyEpsilonFraction * cv::arcLength(contour, true);
        cv::approxPolyDP(contour, approx, epsilon, true);
        if (approx.size() != 4 || !cv::isContourConvex(approx)) {
            continue;
        }

        const cv::Rect bounds = cv::boundingRect(approx);
        const double aspect = static_cast<double>(bounds.width) / static_cast<double>(bounds.height);
        if (aspect < kMinCellAspectRatio || aspect > kMaxCellAspectRatio) {
            continue;
        }

        candidates.push_back(Candidate{
            .bounds = bounds,
            .center = cv::Point2f(static_cast<float>(bounds.x) + static_cast<float>(bounds.width) / 2.0F,
                                   static_cast<float>(bounds.y) + static_cast<float>(bounds.height) / 2.0F),
        });
    }
    return candidates;
}

// Arranges exactly 9 square candidates into a row-major 3x3 layout, or
// returns nullopt if they don't cluster into 3 even rows of 3.
[[nodiscard]] std::optional<std::array<CellRegion, cells_per_grid>> arrange_grid(
    std::vector<Candidate> candidates) {
    if (candidates.size() != static_cast<std::size_t>(cells_per_grid)) {
        return std::nullopt;
    }

    std::ranges::sort(candidates, {}, [](const Candidate& c) { return c.center.y; });

    float total_height = 0.0F;
    for (const auto& candidate : candidates) {
        total_height += static_cast<float>(candidate.bounds.height);
    }
    const float row_threshold =
        (total_height / static_cast<float>(candidates.size())) * kRowClusterFraction;

    std::array<std::vector<Candidate>, grid_dimension> rows;
    int row_index = 0;
    float row_anchor_y = candidates.front().center.y;
    for (const auto& candidate : candidates) {
        if (candidate.center.y - row_anchor_y > row_threshold && row_index < grid_dimension - 1) {
            ++row_index;
            row_anchor_y = candidate.center.y;
        }
        rows[static_cast<std::size_t>(row_index)].push_back(candidate);
    }

    std::array<CellRegion, cells_per_grid> cells;
    std::size_t out_index = 0;
    for (auto& row : rows) {
        if (row.size() != static_cast<std::size_t>(grid_dimension)) {
            return std::nullopt;
        }
        std::ranges::sort(row, {}, [](const Candidate& c) { return c.center.x; });
        for (const auto& candidate : row) {
            cells[out_index++] = CellRegion{.bounds = candidate.bounds};
        }
    }
    return cells;
}

}  // namespace

std::optional<GridDetection> detect_cube_grid(const cv::Mat& bgr_frame) {
    if (bgr_frame.empty()) {
        return std::nullopt;
    }

    auto cells = arrange_grid(find_square_candidates(bgr_frame));
    if (!cells) {
        return std::nullopt;
    }

    cv::Rect union_bounds = (*cells)[0].bounds;
    for (const auto& cell : *cells) {
        union_bounds |= cell.bounds;
    }
    const double frame_area = static_cast<double>(bgr_frame.cols) * static_cast<double>(bgr_frame.rows);
    const double coverage = static_cast<double>(union_bounds.area()) / frame_area;

    return GridDetection{.cells = *cells, .coverage_ratio = static_cast<float>(coverage)};
}

}  // namespace cube::vision
