#pragma once

#include <opencv2/core.hpp>

#include <array>
#include <optional>

namespace cube::vision {

inline constexpr int grid_dimension = 3;
inline constexpr int cells_per_grid = grid_dimension * grid_dimension;

struct CellRegion {
    cv::Rect bounds;
};

struct GridDetection {
    std::array<CellRegion, cells_per_grid> cells{};
    float coverage_ratio = 0.0F;
};

// Locates a cube face's 3x3 sticker grid in a BGR frame: thresholds the
// frame to isolate the 9 bright stickers from the darker gaps/background
// between them, keeps near-square contours in a plausible size range, and
// arranges them into a row-major 3x3 layout. Returns nullopt when the frame
// doesn't yield exactly 9 such contours forming a regular grid.
[[nodiscard]] std::optional<GridDetection> detect_cube_grid(const cv::Mat& bgr_frame);

}  // namespace cube::vision
