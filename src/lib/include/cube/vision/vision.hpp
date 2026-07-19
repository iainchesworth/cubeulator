#pragma once

#include <cube/error.hpp>
#include <cube/model/face.hpp>
#include <cube/model/facelet.hpp>
#include <cube/model/scan_progress.hpp>

#include <opencv2/core.hpp>

#include <array>

namespace cube::vision {

struct FaceReading {
    std::array<cube::model::Facelet, cube::model::facelets_per_face> facelets{};
    float overall_confidence = 0.0F;
};

// Locates a face's 3x3 grid in a frame and classifies each cell's color -
// the per-frame preview a live scan reads confidence/coverage from. Fails
// with CubeError::no_valid_cube_detected when no grid is found.
[[nodiscard]] Result<FaceReading> analyze_frame(const cv::Mat& bgr_frame);

// Records a reading as the confirmed scan of `face`. Manual per-face
// confirmation in the UI (or the auto-advance gate) is what decides a
// reading is good enough to confirm - this just writes it in.
void confirm_face(cube::model::Face face, const FaceReading& reading,
                   cube::model::ScanProgress& progress);

}  // namespace cube::vision
