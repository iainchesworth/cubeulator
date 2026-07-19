#pragma once

#include <opencv2/core.hpp>

#include <deque>

namespace cube::vision {

struct StabilityReading {
    float motion_score = 0.0F;
    bool is_steady = false;
};

// Frame-to-frame absolute-difference variance, tracked over a short rolling
// window so a single lucky-still frame right after a fast pan doesn't
// immediately read as steady. Not optical flow (a flat cube face lacks the
// trackable corner features flow needs) and not a sharpness/blur measure
// (that's a focus signal, not a motion one).
class StabilityTracker {
public:
    [[nodiscard]] StabilityReading update(const cv::Mat& bgr_frame);

private:
    cv::Mat previous_frame_;
    std::deque<float> recent_scores_;
};

}  // namespace cube::vision
