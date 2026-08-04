#pragma once

// Camera intrinsics: calibration storage + the chessboard solve
// (docs/pipeline-plan.md Phase 2 item 1). The f = frame-width guess in
// approximate_camera_matrix poisons every PnP-derived quantity; this
// module gives each camera a one-time measured matrix, stored as a small
// text file the recorder references from the clip manifest
// ("intrinsics": path relative to the recording dir) and FramePacket
// loads at replay. approximate_camera_matrix remains the uncalibrated
// fallback everywhere.

#include <cube/error.hpp>

#include <opencv2/core.hpp>

#include <array>
#include <optional>
#include <string>
#include <vector>

namespace cube::vision {

struct CameraIntrinsics {
    double fx = 0.0;
    double fy = 0.0;
    double cx = 0.0;
    double cy = 0.0;
    // Plumb-bob distortion, OpenCV order (k1 k2 p1 p2 k3).
    std::array<double, 5> distortion{};
    cv::Size calibrated_size{};  // the frame size the numbers are valid at
    double rms_reprojection = 0.0;

    [[nodiscard]] cv::Matx33d matrix() const {
        return cv::Matx33d(fx, 0.0, cx, 0.0, fy, cy, 0.0, 0.0, 1.0);
    }
    // Rescale to a different capture resolution (same sensor, same
    // aspect): intrinsics scale linearly with pixel size.
    [[nodiscard]] CameraIntrinsics scaled_to(cv::Size size) const;
};

// Text round-trip (a flat "key value" file - diffable, no dependency).
[[nodiscard]] std::optional<std::string> write_intrinsics(const std::string& path, const CameraIntrinsics& value);
[[nodiscard]] Result<CameraIntrinsics> read_intrinsics(const std::string& path);

// Chessboard calibration over a set of BGR views. inner_corners is the
// pattern's INNER corner grid (e.g. {9,6} for a 10x7-square board);
// square_size in any consistent unit (only ratios matter for intrinsics).
// Views where the full board is not found are skipped and reported via
// views_used; fails when fewer than 4 views survive.
[[nodiscard]] Result<CameraIntrinsics> calibrate_from_views(const std::vector<cv::Mat>& bgr_views,
                                                            cv::Size inner_corners, float square_size,
                                                            int* views_used = nullptr);

}  // namespace cube::vision
