// Pose-from-corners (Phase 2 init): synthetic round-trips pin the model
// geometry, the solve, cheirality, and the reprojection accounting.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cube/vision/pose_estimation.hpp>
#include <cube/vision/pose_from_corners.hpp>

#include <opencv2/calib3d.hpp>

#include <array>
#include <cmath>
#include <vector>

using namespace cube::vision;
using Catch::Matchers::WithinAbs;

namespace {

// Project the Singmaster model corners under a known pose and dress them
// up as a full-confidence detection.
CornerDetectionResult detection_for(const cv::Vec3d& rvec, const cv::Vec3d& tvec, cv::Size frame) {
    std::vector<cv::Point3f> model(kSingmasterCornerModel.begin(), kSingmasterCornerModel.end());
    std::vector<cv::Point2f> projected;
    cv::projectPoints(model, rvec, tvec, approximate_camera_matrix(frame), cv::noArray(), projected);
    CornerDetectionResult detection;
    detection.found = true;
    detection.confidence = 1.0F;
    for (int id = 0; id < 8; ++id) {
        detection.corners[static_cast<std::size_t>(id)] = {projected[static_cast<std::size_t>(id)], 1.0F};
    }
    return detection;
}

double rotation_error_deg(const cv::Vec3d& rvec_a, const cv::Vec3d& rvec_b) {
    cv::Matx33d a;
    cv::Matx33d b;
    cv::Rodrigues(rvec_a, a);
    cv::Rodrigues(rvec_b, b);
    const cv::Matx33d delta = a.t() * b;
    const double trace = delta(0, 0) + delta(1, 1) + delta(2, 2);
    return std::acos(std::clamp((trace - 1.0) / 2.0, -1.0, 1.0)) * 180.0 / CV_PI;
}

}  // namespace

TEST_CASE("recovers a known pose from all eight identified corners", "[pose_from_corners]") {
    const cv::Size frame(640, 480);
    const cv::Vec3d true_rvec(0.3, -0.4, 0.15);
    const cv::Vec3d true_tvec(0.4, -0.2, 9.0);

    const auto detection = detection_for(true_rvec, true_tvec, frame);
    const auto result = estimate_pose_from_corners(detection, frame);
    REQUIRE(result.has_value());
    CHECK(result->corners_used == 8);
    CHECK(result->max_reproj_px < 0.5);
    CHECK(rotation_error_deg(result->rvec, true_rvec) < 0.5);
    CHECK_THAT(result->tvec[2], WithinAbs(true_tvec[2], 0.05));
}

TEST_CASE("recovers pose from the four-corner minimum and refuses three", "[pose_from_corners]") {
    const cv::Size frame(640, 480);
    const cv::Vec3d true_rvec(-0.2, 0.5, 0.0);
    const cv::Vec3d true_tvec(0.0, 0.0, 8.0);
    auto detection = detection_for(true_rvec, true_tvec, frame);
    // Keep only URF, ULB, DFR, DBL (a non-degenerate tetrad: two U, two
    // D, spanning both x and z signs).
    for (const int id : {1, 3, 5, 7}) {
        detection.corners[static_cast<std::size_t>(id)].confidence = 0.0F;
    }
    const auto four = estimate_pose_from_corners(detection, frame);
    REQUIRE(four.has_value());
    CHECK(four->corners_used == 4);
    CHECK(four->max_reproj_px < 1.0);
    CHECK(rotation_error_deg(four->rvec, true_rvec) < 1.0);

    detection.corners[0].confidence = 0.0F;  // down to three
    const auto three = estimate_pose_from_corners(detection, frame);
    CHECK_FALSE(three.has_value());
}

TEST_CASE("noisy corners degrade reprojection but not the solve", "[pose_from_corners]") {
    const cv::Size frame(640, 480);
    const cv::Vec3d true_rvec(0.1, 0.9, -0.3);
    const cv::Vec3d true_tvec(-0.5, 0.3, 10.0);
    auto detection = detection_for(true_rvec, true_tvec, frame);
    // Deterministic +-1.5px perturbation.
    for (int id = 0; id < 8; ++id) {
        auto& corner = detection.corners[static_cast<std::size_t>(id)];
        corner.px.x += (id % 2 == 0) ? 1.5F : -1.5F;
        corner.px.y += (id % 3 == 0) ? -1.5F : 1.5F;
    }
    const auto result = estimate_pose_from_corners(detection, frame);
    REQUIRE(result.has_value());
    CHECK(result->mean_reproj_px < 3.0);
    CHECK(rotation_error_deg(result->rvec, true_rvec) < 5.0);
}

TEST_CASE("a behind-the-camera solution is refused", "[pose_from_corners]") {
    const cv::Size frame(640, 480);
    // Corners that no in-front pose explains: all eight collapsed onto
    // one point cannot produce a cheirality-positive cube of half-edge 1.
    CornerDetectionResult detection;
    detection.found = true;
    for (auto& corner : detection.corners) {
        corner = {cv::Point2f(320.0F, 240.0F), 1.0F};
    }
    const auto result = estimate_pose_from_corners(detection, frame);
    CHECK_FALSE(result.has_value());
}
