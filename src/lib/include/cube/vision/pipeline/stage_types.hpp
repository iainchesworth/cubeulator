#pragma once

// The typed stage contracts of the tracking-first pipeline
// (docs/pipeline-plan.md, "The pipeline"): every stage consumes exactly its
// predecessor's struct and produces exactly its own. No stage reaches into
// another's internals; anything a stage wants downstream must be a field
// here. The structs deliberately describe the TARGET architecture — during
// the transition the legacy detectors are adapted into these shapes by
// legacy_pipeline.hpp, and fields the legacy path cannot honestly fill are
// optional and documented as such rather than fabricated.
//
//   A. Acquire   -> FramePacket
//   B. Detect    -> CubeDetection
//   C. Track     -> TrackState
//   D. Faces     -> FaceSet
//   E. Cells     -> CellGrid
//   F. Sample    -> CellSamples
//   G. Classify  -> FaceReads
//   H. Fuse      -> ScanState        (commit path; app-owned until P0.6/P6)
//   I. Assemble  -> solver handoff   (resolve_colors + scan_assembly)

#include <cube/model/face.hpp>
#include <cube/model/facelet.hpp>
#include <cube/vision/corner_observation.hpp>
#include <cube/vision/face_observation.hpp>
#include <cube/vision/orientation_tracker.hpp>
#include <cube/vision/pose_estimation.hpp>

#include <opencv2/core.hpp>

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace cube::vision::pipeline {

// --- A. Acquire ------------------------------------------------------------

// One frame as the pipeline consumes it. `bgr` is the ANALYSIS-SCALE
// stream — the same downscale the live app analyses — in live and replay
// alike, so offline gates measure the shipping path (full-resolution frames
// are recorded alongside for labelling, never silently substituted here).
struct FramePacket {
    cv::Mat bgr;
    std::int64_t t_ms = 0;  // steady-clock mailbox timestamp (deltas only)
    int seq = 0;            // pass number; the recording's index key
    cv::Size full_size{};   // original camera frame size (geometry scale-back)
    // Pinhole intrinsics AT ANALYSIS SCALE. Until per-camera calibration
    // lands (Phase 2) these carry the shared approximation
    // (approximate_camera_matrix: f = width, principal point at centre).
    double fx = 0.0;
    double fy = 0.0;
    double cx = 0.0;
    double cy = 0.0;
    bool intrinsics_calibrated = false;
};

// --- B. Detect -------------------------------------------------------------

// A candidate cube outer corner in frame pixels. model_vertex is the
// SINGMASTER corner id (0 URF, 1 UFL, 2 ULB, 3 UBR, 4 DFR, 5 DLF, 6 DBL,
// 7 DRB) - the ground-truth sidecar convention, so detection metrics
// match by id instead of by nearest-point. -1 = detector saw a corner
// but cannot name it. (CubePoseProjection's screen-relative 0-7 vertex
// order is a DIFFERENT convention; stage C owns that translation.)
struct CornerCandidate {
    int model_vertex = -1;
    cv::Point2f px{};
    float confidence = 0.0F;
};

// Where the cube body is in THIS frame, content-agnostically. A Phase 1
// detector fills silhouette/corners/roi; the legacy shim can honestly fill
// only `found`/`confidence`/`roi` plus the legacy observation payloads it
// adapted (which stage C still needs to solve a pose the legacy way).
struct CubeDetection {
    bool found = false;
    float confidence = 0.0F;         // best contributing observation's confidence
    cv::Rect roi{};                  // bounding box of everything detected
    std::vector<cv::Point2f> silhouette;   // convex outline; empty until Phase 1
    std::vector<CornerCandidate> corners;  // empty until Phase 1

    // Legacy carriers (transition only; empty under a Phase 1 detector).
    // pose_only faces are orientation-grade, never capture-grade — the
    // distinction the legacy detectors already enforce.
    std::optional<FaceObservation> legacy_face;
    std::optional<FaceObservation> legacy_pose_only_face;
    std::optional<CornerObservation> legacy_corner;
};

// --- C. Track --------------------------------------------------------------

using TrackPhase = ::cube::vision::TrackState;  // lost/acquiring/locked/coasting

// The pipeline's pose belief after this frame. Under the legacy shim the
// continuous part comes from the per-frame pose solve (seeded by the
// previous accepted pose) and the identity/phase part from
// OrientationTracker; a Phase 2 tracker owns both.
struct TrackState {
    TrackPhase phase = TrackPhase::lost;
    int epoch = 0;

    // Pose of THIS frame when one was accepted. rvec/tvec are OpenCV
    // extrinsics (the PoseSeed frame); orientation is the display-frame
    // quaternion (see CubePoseProjection::orientation for the exact
    // convention). pose_valid gates all three.
    bool pose_valid = false;
    cv::Vec3d rvec{};
    cv::Vec3d tvec{};
    std::array<float, 4> orientation{1.0F, 0.0F, 0.0F, 0.0F};
    std::array<cv::Point2f, 8> projected_vertices{};

    // Quality evidence. reproj_fraction is the solve's worst-corner
    // residual (scale-free, CubePoseProjection::max_reproj_fraction);
    // gate names the tracker's verdict for this pass ("accepted",
    // "basin", ... — TrackGate spelling, for the trace and overlays).
    double reproj_fraction = 0.0;
    std::string gate;
    std::string pose_source;  // "corner"/"face"/"face-pose-only"/"edge"/"none"

    // Identity evidence carried through from the tracker (legacy) — which
    // body face fronts the camera, and whether identity is commit-grade
    // for it. Absent while lost/acquiring.
    std::optional<cube::model::Face> front_face;
    bool identity_certain_front = false;
    float motion_score = 0.0F;
};

// --- D. Faces --------------------------------------------------------------

// One visible face derived for sampling. Under tracking-first the quad is
// a projection of the tracked pose and body_face is authoritative within
// an epoch; the legacy shim carries the detectors' fitted quads instead
// and can name body_face only for the frontal view of a locked track.
struct FaceView {
    std::optional<cube::model::Face> body_face;
    std::array<cv::Point2f, 4> quad{};  // tl, tr, br, bl in frame pixels
    float frontal_deg = 0.0F;           // 0 = unknown under the legacy shim
    bool capture_grade = false;         // may feed colour (vs pose-only)
    cv::Mat rectified;                  // canonical square; empty until Phase 3
};

struct FaceSet {
    std::vector<FaceView> faces;
};

// --- E. Cells --------------------------------------------------------------

// Nine sample regions per face in FaceSet order. refined_px is the
// sub-pixel refinement displacement Phase 4 applies (zero until then) —
// bounded by design to ±0.2 cell pitch so refinement can never re-seat a
// cell on a neighbouring sticker.
struct FaceCells {
    std::array<StickerQuad, 9> cells{};  // row-major as seen in the frame
    std::array<cv::Point2f, 9> refined_px{};
};

struct CellGrid {
    std::vector<FaceCells> faces;  // index-aligned with FaceSet::faces
};

// --- F. Sample -------------------------------------------------------------

struct FaceSamples {
    std::array<cv::Vec3f, 9> lab{};     // float Lab, L in [0,100]
    std::array<float, 9> purity{};      // 0 = not measured (see FaceObservation)
};

struct CellSamples {
    std::vector<FaceSamples> faces;  // index-aligned with FaceSet::faces
};

// --- G. Classify -----------------------------------------------------------

// Provisional per-cell naming (classify_lab): live UI evidence and fusion
// input. The AUTHORITATIVE naming remains resolve_colors at scan
// completion (stage I) — that split is load-bearing and documented in
// color_resolution.hpp.
struct FaceRead {
    std::array<cube::model::Facelet, 9> facelets{};
};

struct FaceReads {
    std::vector<FaceRead> faces;  // index-aligned with FaceSet::faces
};

// --- H. Fuse ---------------------------------------------------------------

// Commit-path summary. The real fusion/routing state machine lives in the
// app's GUI-thread commit path (FaceAggregator + CaptureRouter); the
// pipeline exposes its observable state here as it becomes lib-driven
// (routing replay harness P0.6, re-derived gates P6). Until then the
// legacy shim leaves this stage marked not-run in the trace rather than
// pretending.
struct ScanState {
    int slots_filled = 0;
    std::array<bool, 6> slot_filled{};
};

}  // namespace cube::vision::pipeline
