#include <cube/vision/stability.hpp>

#include <cube/profiling.hpp>

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cstddef>

namespace cube::vision {

namespace {

const cv::Size kAnalysisSize{160, 120};
constexpr std::size_t kRollingWindowSize = 5;
constexpr float kSteadyMotionThreshold = 0.02F;

[[nodiscard]] cv::Mat downscale_grayscale(const cv::Mat& bgr_frame) {
    cv::Mat gray;
    cv::cvtColor(bgr_frame, gray, cv::COLOR_BGR2GRAY);
    cv::Mat small;
    cv::resize(gray, small, kAnalysisSize, 0, 0, cv::INTER_AREA);
    return small;
}

}  // namespace

StabilityReading StabilityTracker::update(const cv::Mat& bgr_frame) {
    CUBE_ZONE_SCOPED_N("vision.stability.update");
    const cv::Mat current = downscale_grayscale(bgr_frame);

    if (previous_frame_.empty()) {
        previous_frame_ = current;
        return StabilityReading{.motion_score = 0.0F, .is_steady = false};
    }

    cv::Mat diff;
    cv::absdiff(current, previous_frame_, diff);
    const float motion_score = static_cast<float>(cv::mean(diff)[0]) / 255.0F;
    previous_frame_ = current;

    recent_scores_.push_back(motion_score);
    while (recent_scores_.size() > kRollingWindowSize) {
        recent_scores_.pop_front();
    }

    const bool window_full = recent_scores_.size() == kRollingWindowSize;
    const bool all_below_threshold =
        std::ranges::all_of(recent_scores_, [](float s) { return s < kSteadyMotionThreshold; });

    return StabilityReading{.motion_score = motion_score,
                            .is_steady = window_full && all_below_threshold};
}

}  // namespace cube::vision
