#include <catch2/catch_test_macros.hpp>

#include <cube/vision/stability.hpp>

using namespace cube::vision;

TEST_CASE("StabilityTracker reads identical frames as steady after enough samples", "[vision]") {
    StabilityTracker tracker;
    const cv::Mat frame(120, 160, CV_8UC3, cv::Scalar(60, 60, 60));

    StabilityReading reading;
    for (int i = 0; i < 6; ++i) {
        reading = tracker.update(frame);
    }

    REQUIRE(reading.motion_score == 0.0F);
    REQUIRE(reading.is_steady);
}

TEST_CASE("StabilityTracker reads alternating frames as not steady", "[vision]") {
    StabilityTracker tracker;
    const cv::Mat dark(120, 160, CV_8UC3, cv::Scalar(10, 10, 10));
    const cv::Mat bright(120, 160, CV_8UC3, cv::Scalar(240, 240, 240));

    StabilityReading reading;
    for (int i = 0; i < 6; ++i) {
        reading = tracker.update(i % 2 == 0 ? dark : bright);
    }

    REQUIRE(reading.motion_score > 0.0F);
    REQUIRE_FALSE(reading.is_steady);
}

TEST_CASE("StabilityTracker is not steady before the rolling window fills", "[vision]") {
    StabilityTracker tracker;
    const cv::Mat frame(120, 160, CV_8UC3, cv::Scalar(60, 60, 60));

    const auto first = tracker.update(frame);
    REQUIRE_FALSE(first.is_steady);

    const auto second = tracker.update(frame);
    REQUIRE_FALSE(second.is_steady);
}
