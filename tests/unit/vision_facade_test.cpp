#include <catch2/catch_test_macros.hpp>

#include <cube/model/scan_progress.hpp>
#include <cube/vision/vision.hpp>

#include <opencv2/imgproc.hpp>

#include <array>

using namespace cube::vision;
using namespace cube::model;

namespace {

cv::Mat synthetic_face_frame(const std::array<cv::Vec3b, 9>& cell_colors) {
    cv::Mat frame = cv::Mat::zeros(230, 230, CV_8UC3);
    constexpr int margin = 15;
    constexpr int cell = 60;
    constexpr int gap = 10;
    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 3; ++col) {
            const int x = margin + col * (cell + gap);
            const int y = margin + row * (cell + gap);
            const auto& color = cell_colors[static_cast<std::size_t>(row * 3 + col)];
            cv::rectangle(frame, cv::Rect(x, y, cell, cell), cv::Scalar(color[0], color[1], color[2]),
                          cv::FILLED);
        }
    }
    return frame;
}

}  // namespace

TEST_CASE("analyze_frame classifies each cell of a synthetic face", "[vision]") {
    const std::array<cv::Vec3b, 9> colors{
        cv::Vec3b(255, 255, 255), cv::Vec3b(0, 213, 255), cv::Vec3b(0, 0, 196),
        cv::Vec3b(0, 106, 255),   cv::Vec3b(72, 155, 0),  cv::Vec3b(173, 70, 0),
        cv::Vec3b(255, 255, 255), cv::Vec3b(0, 213, 255), cv::Vec3b(0, 0, 196),
    };
    const auto reading = analyze_frame(synthetic_face_frame(colors));

    REQUIRE(reading.has_value());
    REQUIRE(reading->facelets[0].color == FacetColor::white);
    REQUIRE(reading->facelets[4].color == FacetColor::green);
    REQUIRE(reading->facelets[8].color == FacetColor::red);
    REQUIRE(reading->overall_confidence > 0.9F);
}

TEST_CASE("analyze_frame reports no_valid_cube_detected when no grid is present", "[vision]") {
    const cv::Mat blank = cv::Mat::zeros(200, 200, CV_8UC3);
    const auto reading = analyze_frame(blank);

    REQUIRE_FALSE(reading.has_value());
    REQUIRE(reading.error() == cube::CubeError::no_valid_cube_detected);
}

TEST_CASE("confirm_face writes a reading into ScanProgress", "[vision]") {
    FaceReading reading;
    reading.facelets.fill(Facelet{.color = FacetColor::white, .confidence = 0.95F});
    reading.overall_confidence = 0.95F;

    ScanProgress progress;
    confirm_face(Face::U, reading, progress);

    REQUIRE(progress.is_face_confirmed(Face::U));
    REQUIRE(progress.face_scan(Face::U).facelets[0].color == FacetColor::white);
}
