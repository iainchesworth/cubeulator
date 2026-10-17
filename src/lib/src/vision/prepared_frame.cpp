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

// A cube of more stickers across the same span has less of the frame to a
// sticker, so the frame is worked at more width for it: the stickers of a 5x5
// held as a 3x3 is are three fifths the size, and brought down to 640 they are
// as small as a 3x3's would be at 380 (measured on drawn cubes, docs/nxn-app-plan.md
// gate S0: the nearest a cube may be to unreadable halves in a warm lamp's light
// at 1280). It costs the pixels it adds: a frame wider than the working width
// is read at the width asked for, and one narrower is read as it is.
[[nodiscard]] int working_width_for(int size) {
    if (size <= 3) {
        return kWorkingWidth;
    }
    return size <= 5 ? kWorkingWidth * 3 / 2 : kWorkingWidth * 2;
}

// A pixel is bright if it is within kThresholdOffset of the mean of a block
// this fraction of the frame's height across; see narrow_contours for why the
// second look's is narrower.
constexpr double kBlockFraction = 0.12;
constexpr double kNarrowBlockFraction = 0.06;
constexpr double kThresholdOffset = 8.0;

// How much smaller than a 3x3's the stickers of a cube of `size` are, when the
// cube spans as much of the frame: 3 / N for N above 3.
[[nodiscard]] double block_scale(int size) {
    return size > 3 ? 3.0 / static_cast<double>(size) : 1.0;
}

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

std::optional<PreparedFrame> prepare_frame(const cv::Mat& bgr, int size) {
    CUBE_ZONE_SCOPED_N("vision.prepare_frame");
    if (bgr.empty() || bgr.type() != CV_8UC3) {
        return std::nullopt;
    }

    PreparedFrame prepared;
    prepared.working = bgr;
    const int working_width = working_width_for(size);
    if (bgr.cols > working_width) {
        prepared.scale = static_cast<float>(bgr.cols) / static_cast<float>(working_width);
        const int height =
            static_cast<int>(std::lround(static_cast<float>(bgr.rows) / prepared.scale));
        cv::resize(bgr, prepared.working, cv::Size(working_width, height), 0.0, 0.0,
                   cv::INTER_AREA);
    }
    const cv::Mat& working = prepared.working;

    // Value (the brightest channel) is used so a saturated blue or red
    // sticker still stands clear of black plastic.
    std::array<cv::Mat, 3> channels;
    cv::split(working, channels.data());
    prepared.brightness = cv::max(cv::max(channels.at(0), channels.at(1)), channels.at(2));
    cv::GaussianBlur(prepared.brightness, prepared.brightness, cv::Size(5, 5), 0.0);

    prepared.contours = outlines(prepared.brightness, kBlockFraction * block_scale(size));
    return prepared;
}

std::vector<std::vector<cv::Point>> narrow_contours(const cv::Mat& brightness, int size) {
    CUBE_ZONE_SCOPED_N("vision.narrow_contours");
    if (brightness.empty() || brightness.type() != CV_8UC1) {
        return {};
    }
    return outlines(brightness, kNarrowBlockFraction * block_scale(size));
}

}  // namespace cube::vision
