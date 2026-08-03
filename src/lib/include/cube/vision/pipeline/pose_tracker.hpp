#pragma once

// Stage C's tracker seam (docs/pipeline-plan.md Phase 2): a continuous
// 6-DoF pose tracker initialised from detection and stepped once per
// frame. The interface is GL-free and lives in the pipeline module so
// cube_lib never links a renderer; the M3T implementation (cube::track_m3t,
// desktop-only) plugs in behind it.
//
// Units and frames, stated once: poses are model->camera in OpenCV's
// camera frame (x right, y down, z forward) over the Singmaster cube
// model of pose_from_corners.hpp (half-edge = 1). Implementations own any
// internal metric conversion. The model frame is whatever frame the INIT
// pose was expressed in - with a view-canonical detector that is the init
// frame's canonical orientation, and it stays fixed for the lifetime of
// the track (that fixity is what resolves the per-frame canonical flips:
// the tracker's continuity IS the gauge).

#include <opencv2/core.hpp>

namespace cube::vision::pipeline {

struct TrackedPose {
    bool valid = false;
    cv::Vec3d rvec{};
    cv::Vec3d tvec{};  // Singmaster model units (half-edge = 1)
};

class PoseTracker {
public:
    virtual ~PoseTracker() = default;

    // (Re)initialise the track at the given pose. May be called at any
    // time - a health-check failure re-enters here with a fresh detector
    // pose. Returns false when the tracker cannot start (bad pose,
    // renderer failure); the caller stays in the lost state.
    virtual bool init(const cv::Vec3d& rvec, const cv::Vec3d& tvec, cv::Size frame_size,
                      const cv::Matx33d& camera_matrix) = 0;

    // One tracking step on the next frame. Only meaningful after a
    // successful init; implementations return valid=false when the track
    // has degenerated.
    virtual TrackedPose step(const cv::Mat& bgr_frame) = 0;

    virtual void reset() = 0;
    [[nodiscard]] virtual bool initialized() const = 0;
};

}  // namespace cube::vision::pipeline
