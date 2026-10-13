#pragma once

// Edge/lattice pose refinement (docs/pipeline-plan.md Phase 2 item 3):
// RAPID-style 1-D measurement that polishes a tracked pose against what
// the image actually shows - the cube's outer edges (silhouette and
// ridges) plus the sticker-gap lattice lines - then re-solves the pose
// from the matched points. It REFINES and never ANCHORS: the 1-D search
// window is capped well below half a projected sticker cell, so the
// one-cell-aliasing hazard (a 3x3 grid looks the same shifted by one
// cell, and a cube silhouette looks the same rotated a quarter turn)
// is structurally out of reach, and the accepted update is capped in
// both rotation and translation - a pose that needs a bigger correction
// is a re-anchor decision, which belongs to the detector PnP path, not
// here.
//
// Why it exists (P2.7 baseline, synthetic corpus): region tracking alone
// slips quarter-turn registrations during free rotation (silhouettes
// cannot forbid it), wobbles 0.46% of extent on a steady cube against a
// 0.3% gate, and carries 0.86% bias against a 0.5% gate. The internal
// lattice lines rotate WITH the cube, giving the rotational constraint
// the silhouette lacks - continuous sub-cell correction keeps the drift
// from ever reaching a symmetry basin's edge.
//
// Geometry: the Singmaster body model of pose_from_corners.hpp - axes
// x L->R, y D->U, z B->F, half-edge 1, faces at +-1 per axis, sticker
// pitch 2/3, internal gap lines at +-1/3. Poses are model->camera in
// OpenCV's camera frame. Pure OpenCV; no renderer, no tracker types -
// callable from any stage that holds a pose.

#include <opencv2/core.hpp>

namespace cube::vision::pipeline {

struct PoseRefineParams {
    double sample_spacing_px = 8.0;  // target spacing between samples along a projected edge
    int max_samples_per_edge = 12;
    double search_cap_px = 12.0;         // hard cap on the 1-D normal search half-window
    double search_cell_fraction = 0.35;  // sub-cell window: fraction of the projected cell pitch
    double min_gradient = 12.0;          // grey-level central-difference step to accept a match
    double frontality_min = 0.25;        // face |cos| below which its lattice lines are skipped
    int min_matches = 12;
    double outlier_mad_factor = 2.5;  // trim matches beyond this many MADs of displacement
    // Measure->solve->re-measure rounds: matches made from a wrong pose
    // carry tangential correspondence error, so one solve recovers only
    // part of an offset; re-matching from the solved pose takes out the
    // rest. Caps below bound the TOTAL movement across all rounds.
    int iterations = 3;
    double max_update_deg = 6.0;           // caps: refine, never anchor
    double max_update_translation = 0.15;  // model units (half-edge = 1)
};

struct PoseRefineResult {
    bool applied = false;  // refined pose accepted (rvec/tvec valid)
    cv::Vec3d rvec{};
    cv::Vec3d tvec{};
    int samples = 0;                        // candidate points probed along visible edges
    int matches = 0;                        // correspondences fed to the solve (post-trim)
    double mean_residual_before_px = -1.0;  // mean |1-D displacement| of the matches
    double mean_residual_after_px = -1.0;   // mean reprojection error vs the matches, refined pose
    double update_deg = 0.0;
    double update_translation = 0.0;      // model units
    const char* reject_reason = nullptr;  // set when !applied ("insufficient", "degenerate", ...)
};

// One refinement pass. `image` is the frame the pose was tracked on
// (BGR or already-grey; converted internally). Never throws; failures
// come back as applied=false with a reason.
[[nodiscard]] PoseRefineResult refine_pose(const cv::Mat& image, const cv::Vec3d& rvec,
                                           const cv::Vec3d& tvec, const cv::Matx33d& camera_matrix,
                                           const PoseRefineParams& params = {});

}  // namespace cube::vision::pipeline
