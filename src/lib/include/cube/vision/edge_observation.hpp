#pragma once

#include <cube/error.hpp>

#include <opencv2/core.hpp>

#include <array>
#include <cstddef>

namespace cube::vision {

// ---------------------------------------------------------------------------
// Two-face edge-view detection: a cube held with a VERTICAL edge toward
// the camera, two faces visible at roughly +-45 degrees and the top face
// barely showing - the user's natural free-rotation grip, and live1's
// single largest cube-present failure class (~136 frames). Candidates ARE
// present on most of those frames (7-13); what was missing is a fitter
// that models two sheared grids sharing a vertical edge instead of one
// planar lattice that fragments across the fold.
//
// POSE-ONLY by design: an edge view's cells foreshorten too hard to trust
// colours, and the false-positive budget of a two-grid model is spent
// entirely on orientation - this observation never carries lab samples,
// never routes to aggregation or capture, and its output feeds
// project_cube_from_edge (pose_estimation.hpp) and through it the
// orientation tracker, nothing else.
//
// Model-frame convention, mirroring the corner path's fixed roles: the
// grid LEFT of the split plays model F, the grid RIGHT of it plays model
// R, rows run down-screen along the shared (F/R) edge. Which PHYSICAL
// faces those are is the lattice gauge the tracker resolves - and the
// screen-derived assignment is always chirality-consistent, because for a
// real cube seen around a near-vertical edge the left face's outward
// normal crossed with the right's always points up along that edge
// (n_left x n_right = up is exactly F x R = U).
// ---------------------------------------------------------------------------

// Pose-derived basis hint while tracking (compute_edge_hint,
// pose_estimation.hpp): on-screen lattice directions per role
// {left, right}, in the same shorter-axis-unit scaling CornerHint uses.
// With a null hint observe_edge probes a small fixed set of canonical
// edge-view bases instead (symmetric +-45-degree yaw, roll 0/+-15).
struct EdgeHint {
    std::array<cv::Point2f, 2> col_dir{};
    std::array<cv::Point2f, 2> row_dir{};
};

// The two fitted face quads, corners tl, tr, br, bl on screen. The left
// quad's RIGHT column edge and the right quad's LEFT column edge are the
// shared cube edge (gated to coincide within a few pixels).
struct EdgeObservation {
    std::array<cv::Point2f, 4> left_quad{};
    std::array<cv::Point2f, 4> right_quad{};
    float geometry_confidence = 0.0F;  // combined inliers / 18
};

// Locates a two-face edge presentation. Returns no_valid_cube_detected
// when the frame doesn't contain a convincing pair of sheared grids
// meeting at a near-vertical shared edge - the consistency gates (per-side
// and combined inlier floors, matched row pitches, coincident inner
// edges, grids on opposite sides of the split) are the false-positive
// budget, backed downstream by the pose solve's reprojection gate and the
// tracker's acquisition streak.
[[nodiscard]] Result<EdgeObservation> observe_edge(const cv::Mat& bgr_frame, const EdgeHint* hint = nullptr);

// Diagnostics seam for the offline frame-replay harness ([.frame-debug]).
// Not a stable API for anything outside the vision module or its tests.
namespace detail {

struct EdgeDiagnostics {
    std::size_t candidate_count = 0;
    int splits_evaluated = 0;
    int basis_sets = 0;
    int paired_fits = 0;  // split x basis combos where BOTH sides fitted
    // Best surviving (or nearly surviving) combination's facts; zeros when
    // nothing got as far as a paired fit.
    float best_split_x = 0.0F;
    int best_left_inliers = 0;
    int best_right_inliers = 0;
    float best_pitch_ratio = 0.0F;
    float best_shared_edge_px = 0.0F;
    bool accepted = false;
};

// observe_edge with the funnel exposed; observe_edge() itself is a thin
// wrapper over this with a null diagnostics pointer.
[[nodiscard]] Result<EdgeObservation> observe_edge_diagnosed(const cv::Mat& bgr_frame, const EdgeHint* hint,
                                                             EdgeDiagnostics* diagnostics);

}  // namespace detail

}  // namespace cube::vision
