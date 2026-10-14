#pragma once

#include <opencv2/core.hpp>

#include <optional>
#include <vector>

namespace cube::vision {

// A camera frame made ready to be read.
//
// Every reader begins the same way: bring the frame down to the width the
// readers are tuned for, take its brightest channel, threshold it against its
// surroundings and trace the outlines. That is most of what a read costs
// (resize, blur, threshold and contours are 1.7 ms of read_side's 2.0 at the
// working width), and a frame in which no side is square to the camera is
// handed to a second reader, which used to do all of it again. Made once per
// frame, it is shared by read_side (direct_scan.hpp) and read_corner
// (corner_view.hpp).
struct PreparedFrame {
    // The frame at the working width, or the frame itself when it is already
    // no wider.
    cv::Mat working;
    // Input pixels per working pixel: what turns a position found in `working`
    // back into the frame's own.
    float scale = 1.0F;
    // Outlines of the bright regions of `working` against their surroundings:
    // the stickers, and anything else bright and bounded by something darker.
    std::vector<std::vector<cv::Point>> contours;
};

// Nothing for a frame that is empty or is not 8-bit BGR.
[[nodiscard]] std::optional<PreparedFrame> prepare_frame(const cv::Mat& bgr);

}  // namespace cube::vision
