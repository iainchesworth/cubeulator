#pragma once

// The M3T-backed PoseTracker (docs/pipeline-plan.md Phase 2): region-based
// model tracking over the vendored third_party/m3t, behind the GL-free
// pipeline::PoseTracker seam. Desktop-only (cube::track_m3t links GLEW/
// GLFW through cube::m3t); consumers must not include this header on
// mobile targets.
//
// Frames and units: init/step speak the seam's convention (OpenCV camera
// frame, Singmaster model, half-edge = 1). Internally the cube is a
// 57 mm CAD model in metres - the classic puzzle size; because the
// intrinsics are approximate the absolute scale is nominal, but it must
// be CONSISTENT between the model and the translation, which the
// half-edge scale factor guarantees. M3T's world frame is exactly
// OpenCV's camera frame (verified by the vendored smoke test's direct
// u = f*x/z + c projection), so rotation transfers unchanged and
// translation transfers by the metric scale alone.
//
// The region-model cache is generated once per process into a scratch
// directory with fixed parameters (use_random_seed = false) - the
// determinism contract the smoke test pins extends through this wrapper.

#include <cube/vision/pipeline/pose_tracker.hpp>

#include <memory>

namespace cube::track_m3t {

class M3tPoseTracker final : public cube::vision::pipeline::PoseTracker {
public:
    M3tPoseTracker();
    ~M3tPoseTracker() override;
    M3tPoseTracker(const M3tPoseTracker&) = delete;
    M3tPoseTracker& operator=(const M3tPoseTracker&) = delete;

    bool init(const cv::Vec3d& rvec, const cv::Vec3d& tvec, cv::Size frame_size,
              const cv::Matx33d& camera_matrix) override;
    cube::vision::pipeline::TrackedPose step(const cv::Mat& bgr_frame) override;
    void reset() override;
    [[nodiscard]] bool initialized() const override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace cube::track_m3t
