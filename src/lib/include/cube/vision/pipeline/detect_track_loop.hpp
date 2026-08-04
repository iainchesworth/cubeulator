#pragma once

// The Phase 2 detect->init->track->health loop, factored so the OFFLINE
// runner (via LegacyPipeline) and the LIVE app (ScanSession's worker) run
// the identical code path - the plan's live-path-only rule. One instance
// owns the loop state (liveness, health streak, re-init count); the
// detector and tracker are attached, not owned.
//
//   detection := detector(frame)                     (stage B)
//   if !live and PnP(detection) solid: tracker.init  (anchor)
//   if live:   tracker.step(frame)                   (stage C)
//              health := min over the 24 view-canonical rotations of
//                        mean |projected model corner - detected corner|
//              a 3-frame health-failure streak drops the track; the next
//              solid PnP re-anchors.
//
// Health is expressed as a fraction of the detection box diagonal so it
// is scale-free; kHealthFailFrac/kHealthFailStreak are the loop's policy
// constants (provisional - swept properly at the Phase 2 gate).

#include <cube/vision/camera_intrinsics.hpp>
#include <cube/vision/corner_detector.hpp>
#include <cube/vision/pipeline/face_projection.hpp>
#include <cube/vision/pipeline/pose_tracker.hpp>
#include <cube/vision/pose_from_corners.hpp>

#include <opencv2/core.hpp>

#include <array>
#include <optional>

namespace cube::vision::pipeline {

struct DetectTrackResult {
    CornerDetectionResult detection;         // stage-B verdict (found=false when nothing)
    std::optional<CornerPnpResult> pnp;      // single-frame pose when >=4 ids solved
    bool track_live = false;                 // tracker holds a healthy pose
    bool step_valid = false;                 // tracker produced a pose this frame
    cv::Vec3d rvec{};                        // tracked pose (valid when track_live)
    cv::Vec3d tvec{};
    std::array<cv::Point2f, 8> projected{};  // Singmaster-ordered model corners under the tracked pose
    double health_frac = -1.0;               // -1 = not measurable this frame
    int reinits = 0;                         // cumulative anchors this loop lifetime
    // P3.3 gauge/identity facts. The track's model frame is the anchor
    // frame's view-canonical orientation; epoch-local face identity is
    // exactly that gauge held by continuity. gauge_epoch increments on
    // every event that invalidates it - an anchor OR a detected
    // quarter-turn slip (pinned-gauge error vs detections persistently
    // failing while the best-of-24 error passes, the online version of
    // the P2.7 registration metric). gauge_slip flags a slip declared
    // this frame; gauge_pinned_err is the pinned-gauge mean corner
    // error fraction on detect frames (-1 = not measurable).
    int gauge_epoch = 0;
    bool gauge_slip = false;
    double gauge_pinned_err = -1.0;
    // Stage-D geometry derived from the OUTPUT pose while live (P3.1,
    // face_projection.hpp): visible faces, quads, sticker centres -
    // most frontal first. Empty while lost.
    std::vector<ProjectedFace> faces;
    // Edge/lattice refinement facts (pose_refiner.hpp; P2.8). When
    // applied, rvec/tvec/projected above ARE the refined pose.
    bool refine_applied = false;
    int refine_matches = 0;
    double refine_update_deg = 0.0;
    double refine_residual_before_px = -1.0;
    double refine_residual_after_px = -1.0;
};

class DetectTrackLoop {
public:
    static constexpr int kInitMinCorners = 5;
    static constexpr double kHealthFailFrac = 0.15;
    static constexpr int kHealthFailStreak = 3;

    // tracker may be nullptr: the loop then runs detection + PnP only
    // (the P1 detector-only mode) and track_live stays false.
    void attach(OnnxCornerDetector* detector, PoseTracker* tracker) {
        detector_ = detector;
        tracker_ = tracker;
    }
    [[nodiscard]] bool attached() const { return detector_ != nullptr; }
    [[nodiscard]] bool has_tracker() const { return tracker_ != nullptr; }

    // While the track is LIVE, run the (expensive) detector only every
    // n-th frame - the tracker steps every frame, health is checked on
    // detect frames. 1 (default) = detect every frame, the offline
    // metrics contract; the live app raises it to trade health cadence
    // for pass rate. While lost the detector always runs (re-anchor
    // needs it).
    void set_live_detect_interval(int n) { live_detect_interval_ = n > 1 ? n : 1; }

    // Edge/lattice refinement after each tracking step (pose_refiner.hpp;
    // P2.8): default ON - it is part of the tracking contract, the same
    // on the live path and offline. Off exists for A/B measurement.
    void set_refine_enabled(bool enabled) { refine_enabled_ = enabled; }

    // Measured per-camera intrinsics (P2.9): every camera-matrix consumer
    // in the loop - PnP anchor, tracker init, projection, refinement -
    // switches from the f=width approximation to these, rescaled to each
    // frame's size. Unset = the approximation, as before.
    void set_camera_intrinsics(const CameraIntrinsics& intrinsics) { intrinsics_ = intrinsics; }

    // The loop's one source of camera truth for a frame size.
    [[nodiscard]] cv::Matx33d camera_matrix_for(cv::Size size) const;

    // One frame through detect + track. Safe to call with nothing
    // attached (returns an empty result).
    [[nodiscard]] DetectTrackResult process(const cv::Mat& bgr_frame);

    void reset();

private:
    OnnxCornerDetector* detector_ = nullptr;
    PoseTracker* tracker_ = nullptr;
    std::optional<CameraIntrinsics> intrinsics_;
    bool live_ = false;
    bool refine_enabled_ = true;
    int health_fail_streak_ = 0;
    int reinits_ = 0;
    int live_detect_interval_ = 1;
    int frames_since_detect_ = 0;
    // P3.3 gauge state: the pinned view->body alignment (index into
    // singmaster_rotation_permutations(), -1 = pin on the next
    // measurable detect frame) and the slip-failure streak.
    // gauge_slip_holdoff_ (P6.1): one physical drift transits several
    // basins and used to fire a declaration per basin - 11 epochs for 3
    // real slips on the synthetic free-rotation clip, fragmenting
    // fusion's identity windows for nothing (the FIRST declaration
    // already invalidated pre-drift identity). While the hold-off runs,
    // continued slips re-pin SILENTLY: same gauge correctness, one
    // epoch per drift.
    static constexpr int kGaugeSlipHoldoff = 15;  // detect-frame measurements
    int gauge_pinned_perm_ = -1;
    int gauge_epoch_ = 0;
    int gauge_fail_streak_ = 0;
    int gauge_slip_holdoff_ = 0;
};

}  // namespace cube::vision::pipeline
