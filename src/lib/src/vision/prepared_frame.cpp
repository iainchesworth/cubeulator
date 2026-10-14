#include <cube/vision/prepared_frame.hpp>

#include <cube/profiling.hpp>

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <array>
#include <cmath>

namespace cube::vision {

namespace {

// The readers work at this width: blob filters and the blur are sized for
// it, and a side read at a quarter of this still resolves (measured on
// recorded scans down to 320 wide).
constexpr int kWorkingWidth = 640;

}  // namespace

std::optional<PreparedFrame> prepare_frame(const cv::Mat& bgr) {
    CUBE_ZONE_SCOPED_N("vision.prepare_frame");
    if (bgr.empty() || bgr.type() != CV_8UC3) {
        return std::nullopt;
    }

    PreparedFrame prepared;
    prepared.working = bgr;
    if (bgr.cols > kWorkingWidth) {
        prepared.scale = static_cast<float>(bgr.cols) / static_cast<float>(kWorkingWidth);
        const int height =
            static_cast<int>(std::lround(static_cast<float>(bgr.rows) / prepared.scale));
        cv::resize(bgr, prepared.working, cv::Size(kWorkingWidth, height), 0.0, 0.0,
                   cv::INTER_AREA);
    }
    const cv::Mat& working = prepared.working;

    // Value (the brightest channel) is used so a saturated blue or red
    // sticker still stands clear of black plastic.
    std::array<cv::Mat, 3> channels;
    cv::split(working, channels.data());
    cv::Mat value = cv::max(cv::max(channels.at(0), channels.at(1)), channels.at(2));
    cv::GaussianBlur(value, value, cv::Size(5, 5), 0.0);

    const int block = static_cast<int>(std::min(working.rows, working.cols) * 0.12) | 1;
    cv::Mat binary;
    cv::adaptiveThreshold(value, binary, 255.0, cv::ADAPTIVE_THRESH_MEAN_C, cv::THRESH_BINARY,
                          std::max(block, 3), 8.0);
    cv::morphologyEx(binary, binary, cv::MORPH_OPEN, cv::Mat::ones(3, 3, CV_8U));

    cv::findContours(binary, prepared.contours, cv::RETR_LIST, cv::CHAIN_APPROX_SIMPLE);
    return prepared;
}

}  // namespace cube::vision
