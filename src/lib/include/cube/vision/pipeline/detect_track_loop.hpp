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

#include <cube/vision/corner_detector.hpp>
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

    // One frame through detect + track. Safe to call with nothing
    // attached (returns an empty result).
    [[nodiscard]] DetectTrackResult process(const cv::Mat& bgr_frame);

    void reset();

private:
    OnnxCornerDetector* detector_ = nullptr;
    PoseTracker* tracker_ = nullptr;
    bool live_ = false;
    int health_fail_streak_ = 0;
    int reinits_ = 0;
};

}  // namespace cube::vision::pipeline
