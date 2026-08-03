#pragma once

// Phase 2 stage-C initialisation (docs/pipeline-plan.md): a full 6-DoF
// pose from the learned detector's Singmaster-identified corners. Because
// every corner carries its id, the 2D-3D correspondences are direct - no
// lattice fitting, no candidate assignment, no colour - and PnP over the
// cube's eight known corners recovers rotation AND translation in one
// step. This is the tracker's init/re-init path; it is also wired
// TRACE-ONLY into the pipeline first so its agreement with the legacy
// pose can be measured on the corpora before anything steers by it
// (ground rule: measure before you fix).

#include <cube/error.hpp>
#include <cube/vision/corner_detector.hpp>

#include <opencv2/core.hpp>

#include <array>

namespace cube::vision {

// Singmaster corner id -> model coordinates. Body axes per the
// ground-truth spec (docs/ground-truth-format.md): x L->R, y D->U,
// z B->F, half-edge 1. Index = Singmaster id (0 URF ... 7 DRB).
// (inline const, not constexpr: cv::Point3f's constructors are not
// constexpr.)
inline const std::array<cv::Point3f, 8> kSingmasterCornerModel{{
    {+1.0F, +1.0F, +1.0F},  // 0 URF
    {-1.0F, +1.0F, +1.0F},  // 1 UFL
    {-1.0F, +1.0F, -1.0F},  // 2 ULB
    {+1.0F, +1.0F, -1.0F},  // 3 UBR
    {+1.0F, -1.0F, +1.0F},  // 4 DFR
    {-1.0F, -1.0F, +1.0F},  // 5 DLF
    {-1.0F, -1.0F, -1.0F},  // 6 DBL
    {+1.0F, -1.0F, -1.0F},  // 7 DRB
}};

struct CornerPnpResult {
    // Model->camera extrinsics (OpenCV frame), model = the Singmaster
    // cube above. NOTE this model frame is the BODY frame, not
    // pose_estimation's face-anchored kCubeModel - the display
    // quaternion below is comparable across both (same screen
    // convention); the raw extrinsics are not interchangeable as seeds
    // without a frame shift. Stage C owns that translation when the
    // tracker consumes this.
    cv::Vec3d rvec{};
    cv::Vec3d tvec{};
    // Display-frame quaternion, CubePoseProjection's exact convention.
    std::array<float, 4> orientation{1.0F, 0.0F, 0.0F, 0.0F};
    // Reprojection of the USED corners through the recovered pose.
    double mean_reproj_px = 0.0;
    double max_reproj_px = 0.0;
    int corners_used = 0;
};

// PnP over the detected corners with confidence >= min_confidence.
// Requires at least 4; fails with no_valid_cube_detected otherwise or
// when the solve degenerates. Intrinsics are the shared approximation
// (approximate_camera_matrix) until per-camera calibration lands.
[[nodiscard]] Result<CornerPnpResult> estimate_pose_from_corners(
    const CornerDetectionResult& detection, cv::Size frame_size,
    float min_confidence = OnnxCornerDetector::kDefaultKeypointFloor);

}  // namespace cube::vision
