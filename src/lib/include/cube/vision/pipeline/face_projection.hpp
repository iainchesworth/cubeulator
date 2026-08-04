#pragma once

// Phase 3 stage D geometry (docs/pipeline-plan.md, Phase 3 item 1):
// which faces are visible and where, DERIVED from the tracked pose -
// no per-frame face hunting, no screen-sector heuristics. A face quad
// is a projection of the model under the pose, so its accuracy is the
// pose's accuracy (the Phase 3 gate scores exactly that); the sticker
// centres come from the same 3x3 grid the edge/lattice refiner already
// measures against.
//
// Geometry: the Singmaster body model of pose_from_corners.hpp - axes
// x L->R, y D->U, z B->F, half-edge 1, faces at +-1 per axis, sticker
// pitch 2/3. Face ids 0..5 = +x,-x,+y,-y,+z,-z (R,L,U,D,F,B in the
// MODEL frame - which, for a live track, is the init frame's
// view-canonical orientation, NOT body identity; naming faces is the
// identity layer's job, P3.3).
//
// Face-local frame, declared once (the canonical orientation Phases
// 4-5 rectify into): for face axis a, u = axis (a+1)%3 and
// v = axis (a+2)%3, both in model +direction. quad = tl,tr,br,bl =
// (u,v) at (-1,-1),(+1,-1),(+1,+1),(-1,+1); centres are row-major over
// v then u at -2/3, 0, +2/3. One convention, stated here, absorbed
// downstream by the assembler's roll search (plan Phase 3 item 4).

#include <opencv2/core.hpp>

#include <array>
#include <vector>

namespace cube::vision::pipeline {

struct ProjectedFace {
    int face = 0;              // model-frame face id, 0..5 = +x,-x,+y,-y,+z,-z
    double frontality = 0.0;   // facing cosine toward the camera (1 = square-on)
    std::array<cv::Point2f, 4> quad{};      // tl,tr,br,bl per the face-local frame above
    std::array<cv::Point2f, 9> centres{};   // row-major sticker centres, same frame
    // Per-cell quads (tl,tr,br,bl each, row-major cells) - stage E's
    // geometry under tracking-first: a cell is a projection too.
    std::array<std::array<cv::Point2f, 4>, 9> cells{};
};

// All front-facing faces of the posed model, most frontal first.
// min_frontality trims grazing faces whose projections are degenerate
// slivers; pass 0 to keep every mathematically front-facing face.
[[nodiscard]] std::vector<ProjectedFace> derive_faces(const cv::Vec3d& rvec, const cv::Vec3d& tvec,
                                                      const cv::Matx33d& camera_matrix,
                                                      double min_frontality = 0.15);

// P3.2: rectify one derived face to the canonical square - the sampling
// substrate for Phases 4-5. The homography maps quad tl,tr,br,bl onto
// the square's corners in the declared face-local frame above, so cell
// (row, col) occupies the block [col,col+1)x[row,row+1) of the output's
// thirds and cell centres sit at ((col+0.5)/3, (row+0.5)/3) of the
// side. `size` is the square's side in pixels; empty Mat on an empty
// frame or a degenerate quad.
[[nodiscard]] cv::Mat rectify_face(const cv::Mat& bgr_frame, const ProjectedFace& face, int size = 96);

}  // namespace cube::vision::pipeline
