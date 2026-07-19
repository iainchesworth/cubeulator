#include <cube/vision/vision.hpp>

#include <cube/vision/color_classification.hpp>
#include <cube/vision/grid_detection.hpp>

#include <cstddef>

namespace cube::vision {

namespace {

// Samples a cell's color at its center pixel rather than averaging the
// whole region: cheap, and avoids blending in the black plastic gap that
// borders each sticker.
[[nodiscard]] cv::Vec3b sample_cell_color(const cv::Mat& bgr_frame, const CellRegion& cell) {
    const cv::Point center(cell.bounds.x + cell.bounds.width / 2, cell.bounds.y + cell.bounds.height / 2);
    return bgr_frame.at<cv::Vec3b>(center);
}

}  // namespace

Result<FaceReading> analyze_frame(const cv::Mat& bgr_frame) {
    const auto grid = detect_cube_grid(bgr_frame);
    if (!grid) {
        return std::unexpected(CubeError::no_valid_cube_detected);
    }

    const auto palette = default_palette();
    FaceReading reading;
    float confidence_sum = 0.0F;
    for (std::size_t i = 0; i < grid->cells.size(); ++i) {
        const auto facelet = classify_color(sample_cell_color(bgr_frame, grid->cells[i]), palette);
        reading.facelets[i] = facelet;
        confidence_sum += facelet.confidence;
    }
    reading.overall_confidence = confidence_sum / static_cast<float>(grid->cells.size());

    return reading;
}

void confirm_face(cube::model::Face face, const FaceReading& reading, cube::model::ScanProgress& progress) {
    progress.set_face(face, reading.facelets);
}

}  // namespace cube::vision
