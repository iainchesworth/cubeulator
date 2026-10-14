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

// A pixel is bright if it is within kThresholdOffset of the mean of a block
// this fraction of the frame's height across; see narrow_contours for why the
// second look's is narrower.
constexpr double kBlockFraction = 0.12;
constexpr double kNarrowBlockFraction = 0.06;
constexpr double kThresholdOffset = 8.0;

[[nodiscard]] std::vector<std::vector<cv::Point>> outlines(const cv::Mat& brightness,
                                                           double block_fraction) {
    const int block = static_cast<int>(
        static_cast<unsigned>(static_cast<double>(std::min(brightness.rows, brightness.cols)) *
                              block_fraction) |
        1U);
    cv::Mat binary;
    cv::adaptiveThreshold(brightness, binary, 255.0, cv::ADAPTIVE_THRESH_MEAN_C, cv::THRESH_BINARY,
                          std::max(block, 3), kThresholdOffset);
    cv::morphologyEx(binary, binary, cv::MORPH_OPEN, cv::Mat::ones(3, 3, CV_8U));

    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(binary, contours, cv::RETR_LIST, cv::CHAIN_APPROX_SIMPLE);
    return contours;
}

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
    prepared.brightness = cv::max(cv::max(channels.at(0), channels.at(1)), channels.at(2));
    cv::GaussianBlur(prepared.brightness, prepared.brightness, cv::Size(5, 5), 0.0);

    prepared.contours = outlines(prepared.brightness, kBlockFraction);
    return prepared;
}

std::vector<std::vector<cv::Point>> narrow_contours(const cv::Mat& brightness) {
    CUBE_ZONE_SCOPED_N("vision.narrow_contours");
    if (brightness.empty() || brightness.type() != CV_8UC1) {
        return {};
    }
    return outlines(brightness, kNarrowBlockFraction);
}

}  // namespace cube::vision
