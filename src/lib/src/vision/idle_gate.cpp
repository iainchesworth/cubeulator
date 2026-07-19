#include <cube/vision/idle_gate.hpp>

#include <opencv2/imgproc.hpp>

namespace cube::vision {

int count_cool_hue_pixels(const cv::Mat& bgr_frame) {
    if (bgr_frame.empty()) {
        return 0;
    }
    // Fixed 160x120 statistic resolution: the count's calibrated floor
    // (IdleGate::kIdlePixelFloor) must mean the same thing whatever the
    // camera delivers, and INTER_AREA keeps thin tile slivers contributing
    // fractional intensity instead of aliasing away.
    cv::Mat small;
    cv::resize(bgr_frame, small, cv::Size(160, 120), 0.0, 0.0, cv::INTER_AREA);
    cv::Mat hsv;
    cv::cvtColor(small, hsv, cv::COLOR_BGR2HSV);
    // Green through blue at STRONG saturation and moderate brightness:
    // tiles a matte cube always shows some of, and the warm/unsaturated
    // clutter that produced live1's false candidates (skin, forehead,
    // curtain, desk) never does. S >= 100 / V >= 60, not the softer cut
    // first drafted: measured over the full live1 recording (the
    // [.cool-hue-scan] harness), dim bluish shadow pixels kept a softer
    // band at 39-169 on cube-absent frames - above any usable floor -
    // while this band is cleanly bimodal: cube-absent p90 = 6, max 6 away
    // from span boundaries, vs cube-present p05 = 103. See
    // idle_gate.hpp's floor.
    cv::Mat mask;
    cv::inRange(hsv, cv::Scalar(35, 100, 60), cv::Scalar(135, 255, 255), mask);
    return cv::countNonZero(mask);
}

}  // namespace cube::vision
