// Camera intrinsics (Phase 2): the chessboard solve recovers a known
// synthetic camera, the file round-trips, and rescaling is linear.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cube/vision/camera_intrinsics.hpp>

#include <opencv2/calib3d.hpp>
#include <opencv2/imgproc.hpp>

#include <filesystem>
#include <fstream>
#include <vector>

using namespace cube::vision;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

namespace {

// Render a chessboard view under a known camera: project the OUTER
// square grid and fill alternating quads. 10x7 squares -> 9x6 inner
// corners.
cv::Mat render_board(const cv::Matx33d& camera, const cv::Vec3d& rvec, const cv::Vec3d& tvec,
                     cv::Size frame) {
    constexpr int kCols = 10;
    constexpr int kRows = 7;
    constexpr float kSquare = 1.0F;
    cv::Mat image(frame, CV_8UC3, cv::Scalar(200, 200, 200));
    std::vector<cv::Point3f> grid;
    for (int row = 0; row <= kRows; ++row) {
        for (int col = 0; col <= kCols; ++col) {
            grid.emplace_back(static_cast<float>(col) * kSquare, static_cast<float>(row) * kSquare,
                              0.0F);
        }
    }
    std::vector<cv::Point2f> projected;
    cv::projectPoints(grid, rvec, tvec, camera, cv::noArray(), projected);
    const auto at = [&](int row, int col) {
        return projected[static_cast<std::size_t>(row * (kCols + 1) + col)];
    };
    for (int row = 0; row < kRows; ++row) {
        for (int col = 0; col < kCols; ++col) {
            if ((row + col) % 2 == 0) {
                continue;
            }
            const std::vector<cv::Point> quad{at(row, col), at(row, col + 1), at(row + 1, col + 1),
                                              at(row + 1, col)};
            cv::fillConvexPoly(image, quad, cv::Scalar(20, 20, 20), cv::LINE_AA);
        }
    }
    return image;
}

}  // namespace

TEST_CASE("chessboard calibration recovers a known camera", "[intrinsics]") {
    const cv::Size frame(640, 480);
    const cv::Matx33d truth(700.0, 0.0, 316.0, 0.0, 705.0, 244.0, 0.0, 0.0, 1.0);

    // Ten varied views: translated and tilted, board fully in frame.
    std::vector<cv::Mat> views;
    const std::vector<std::pair<cv::Vec3d, cv::Vec3d>> poses{
        {{0.0, 0.0, 0.0}, {-5.0, -3.5, 18.0}},    {{0.3, 0.0, 0.0}, {-5.0, -3.0, 17.0}},
        {{-0.3, 0.1, 0.0}, {-4.5, -4.0, 19.0}},   {{0.0, 0.35, 0.1}, {-6.0, -3.5, 18.5}},
        {{0.1, -0.35, 0.0}, {-4.0, -3.0, 17.5}},  {{0.25, 0.2, 0.2}, {-5.5, -3.8, 20.0}},
        {{-0.2, -0.2, 0.1}, {-4.8, -2.8, 16.5}},  {{0.15, 0.3, -0.15}, {-5.2, -4.2, 21.0}},
        {{-0.3, -0.1, -0.2}, {-4.2, -3.2, 18.0}}, {{0.05, 0.15, 0.3}, {-5.8, -3.0, 19.5}},
    };
    for (const auto& [rvec, tvec] : poses) {
        views.push_back(render_board(truth, rvec, tvec, frame));
    }

    int used = 0;
    const auto result = calibrate_from_views(views, cv::Size(9, 6), 1.0F, &used);
    REQUIRE(result.has_value());
    CHECK(used >= 8);
    CHECK_THAT(result->fx, WithinRel(truth(0, 0), 0.03));
    CHECK_THAT(result->fy, WithinRel(truth(1, 1), 0.03));
    CHECK_THAT(result->cx, WithinRel(truth(0, 2), 0.03));
    CHECK_THAT(result->cy, WithinRel(truth(1, 2), 0.03));
    CHECK(result->rms_reprojection < 1.0);

    SECTION("round-trips through the file format") {
        const auto path =
            (std::filesystem::temp_directory_path() / "cubeulator-tests" / "intrinsics.txt")
                .string();
        std::filesystem::create_directories(std::filesystem::path(path).parent_path());
        REQUIRE(!write_intrinsics(path, *result).has_value());
        const auto read_back = read_intrinsics(path);
        REQUIRE(read_back.has_value());
        CHECK_THAT(read_back->fx, WithinAbs(result->fx, 1e-6));
        CHECK_THAT(read_back->cy, WithinAbs(result->cy, 1e-6));
        CHECK(read_back->calibrated_size == frame);
    }

    SECTION("rescaling is linear in pixel size") {
        const auto scaled = result->scaled_to(cv::Size(1280, 960));
        CHECK_THAT(scaled.fx, WithinRel(result->fx * 2.0, 1e-9));
        CHECK_THAT(scaled.cy, WithinRel(result->cy * 2.0, 1e-9));
    }
}

TEST_CASE("calibration refuses too few usable views", "[intrinsics]") {
    std::vector<cv::Mat> views{cv::Mat(480, 640, CV_8UC3, cv::Scalar(128, 128, 128)),
                               cv::Mat(480, 640, CV_8UC3, cv::Scalar(90, 90, 90))};
    int used = -1;
    const auto result = calibrate_from_views(views, cv::Size(9, 6), 1.0F, &used);
    CHECK_FALSE(result.has_value());
    CHECK(used == 0);
}

TEST_CASE("unreadable or malformed intrinsics files are refused", "[intrinsics]") {
    CHECK_FALSE(read_intrinsics("Z:/does/not/exist.txt").has_value());
    const auto path =
        (std::filesystem::temp_directory_path() / "cubeulator-tests" / "bad.txt").string();
    std::filesystem::create_directories(std::filesystem::path(path).parent_path());
    std::ofstream(path) << "fx -1\n";
    CHECK_FALSE(read_intrinsics(path).has_value());
}
