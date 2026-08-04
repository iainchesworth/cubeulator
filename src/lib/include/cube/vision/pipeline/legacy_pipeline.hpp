#pragma once

// The transition adapter: drives the LEGACY detector cascade + orientation
// tracker through the pipeline's typed stage contracts (stage_types.hpp)
// with a full per-stage trace, replicating the live worker's semantics the
// way the [.track-replay] harness does — seed/hint hold for the shared
// kOverlayHoldMs window, all-cells-in-frame gates, corner-first pose,
// pose-only split, edge fallback only when every richer estimator failed,
// tracker fed the exact TrackerPassInput handleAnalysisFinished builds.
//
// Purpose (docs/pipeline-plan.md Phase 0): give the offline runner a
// stage-shaped, traced view of the CURRENT pipeline so baselines exist
// before any stage is replaced. Honesty notes:
//  - The legacy cascade does not decompose cleanly at the B/C boundary:
//    observe_corner is hinted from the PREVIOUS pose and observe_edge only
//    runs inside the pose fallback. The adapter therefore runs the edge
//    detector inside stage C and says so in the trace (field "edge_ran")
//    rather than pretending the legacy code has the target shape.
//  - Stages H (fuse) and I (assemble) live in the app's GUI-thread commit
//    path; the adapter emits ran=false records with a reason until the
//    routing harness (P0.6) makes them lib-drivable.
//  - The IDLE-gate detector skip is a live-cadence optimisation; the
//    adapter (an instrument) analyses every frame and records the gate's
//    would-skip verdict in the trace instead of skipping.
//
// Determinism: process() reseeds OpenCV's thread-local RNG from the frame
// seq, so identical recordings produce byte-identical traces regardless of
// what ran before — the property the scaffold test pins.

#include <cube/vision/corner_detector.hpp>
#include <cube/vision/idle_gate.hpp>
#include <cube/vision/orientation_tracker.hpp>
#include <cube/vision/pipeline/detect_track_loop.hpp>
#include <cube/vision/pipeline/pose_tracker.hpp>
#include <cube/vision/pipeline/stage_types.hpp>
#include <cube/vision/pipeline/trace.hpp>
#include <cube/vision/pose_estimation.hpp>
#include <cube/vision/stability.hpp>

#include <cstdint>
#include <optional>

namespace cube::vision::pipeline {

struct LegacyPipelineResult {
    CubeDetection detection;
    TrackState track;
    FaceSet faces;
    CellGrid cells;
    CellSamples samples;
    FaceReads reads;
    FrameTrace trace;
    // Pose-derived stage-D geometry while the continuous track is live
    // (P3.1, face_projection.hpp) - the tracking-first replacement for
    // the legacy FaceSet above, carried side by side until P3.4 retires
    // the cascade. Empty while lost or with no tracker attached.
    std::vector<ProjectedFace> track_faces;
};

class LegacyPipeline {
public:
    LegacyPipeline() = default;

    // Exactly one call per frame, in recording order (the tracker's clock
    // is the pass cadence). Returns every stage's output plus the complete
    // per-stage trace; validate_trace() on the result is the completeness
    // contract.
    [[nodiscard]] LegacyPipelineResult process(const FramePacket& frame);

    // Fresh scan: tracker, stability, idle gate, seed state.
    void reset();

    // Phase 1: attach the learned corner detector (not owned; may be
    // nullptr = legacy-only). When present, stage B ALSO runs it and
    // fills CubeDetection's content-agnostic fields (found/confidence/
    // roi/corners with Singmaster ids) from its result, while the legacy
    // observations keep driving stages C-G until the Phase 2 tracker
    // consumes the corners directly. found/confidence/roi become the
    // learned detector's verdict - it is the stage-B primary
    // (docs/pipeline-plan.md, classical spike verdict).
    void set_corner_detector(OnnxCornerDetector* detector) {
        corner_detector_ = detector;
        loop_.attach(corner_detector_, pose_tracker_);
    }

    // Phase 2: attach the continuous 6-DoF tracker (not owned; may be
    // nullptr). Requires a corner detector (init and health both consume
    // its corners). When attached and healthy, TrackState's pose,
    // projected vertices, orientation and phase come from the TRACKER;
    // the legacy orientation tracker keeps running for its identity
    // machinery and its verdicts stay in the trace. Health: the tracked
    // pose's projected model corners are compared against this frame's
    // detected corners, minimised over the 24 cube rotations
    // (view-canonical ids - see corner_detector.hpp); a streak of
    // failures drops the track and the next good detector PnP re-inits.
    void set_pose_tracker(PoseTracker* tracker) {
        pose_tracker_ = tracker;
        loop_.attach(corner_detector_, pose_tracker_);
    }

    // A/B switch for the edge/lattice refinement stage (default ON, the
    // live path's contract) - the runner's --no-refine measurement hook.
    void set_refine_enabled(bool enabled) { loop_.set_refine_enabled(enabled); }

    // Measured per-camera intrinsics for the loop's camera-matrix
    // consumers (P2.9); unset = the f=width approximation.
    void set_camera_intrinsics(const CameraIntrinsics& intrinsics) { loop_.set_camera_intrinsics(intrinsics); }

private:
    StabilityTracker stability_;
    OrientationTracker tracker_;
    IdleGate idle_gate_;
    std::optional<PoseSeed> seed_prior_;
    std::int64_t seed_t_ms_ = 0;
    OnnxCornerDetector* corner_detector_ = nullptr;
    PoseTracker* pose_tracker_ = nullptr;
    // The shared detect->init->track->health loop (detect_track_loop.hpp)
    // - the SAME instance class the live app runs, so offline replays and
    // the shipping path cannot drift.
    DetectTrackLoop loop_;
};

}  // namespace cube::vision::pipeline
