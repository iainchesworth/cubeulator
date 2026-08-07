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
// Health is expressed as a fraction of the detection box diagonal so
// it is scale-free. The level/streak pair separates two populations IN
// TIME, not level (both were measured on the six-face sweep): a track
// settling after a deliberate quarter-turn OSCILLATES around 0.15 for
// a dozen frames (0.145-0.178 - a 3-streak dropped that essentially-
// correct track twice per sweep), while a wrong-basin pose SUSTAINS
// 0.36+ indefinitely. Widening the level to 0.22 was tried first and
// let wrong basins park (sweep locked-correct fell to 53.5%); the
// 20-frame streak at the tight level forgives oscillating settle (any
// dip below resets it) while a parked wrong basin still drops within a
// second. Re-swept on the real corpus.

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
    // P6.2 commit-grade: the tracker-quality gate for feeding reads
    // downstream (sampling, classification, palette, fusion) - live,
    // health CURRENT and passing, and quasi-static. Replaces the legacy
    // accept-streak/gauge-margin compensations on the tracked path; the
    // photometric gates (aggregator convergence, fail-closed unknowns)
    // remain downstream.
    bool read_grade = false;
    // P6.2b: this anchor was STITCHED to the previous epoch - the
    // model-frame change between the dropped track's last pose and the
    // fresh anchor snapped to one of the 24 cube rotations, so face ids
    // are relabelled into the old epoch's frame and identity survives
    // the re-anchor. Only benign drops stitch (marginal health, tiny
    // gap); slips and absences still open a real epoch.
    bool stitched = false;
    double stitch_snap_deg = -1.0;  // snap angle at the last anchor attempt; -1 = no attempt
    // On an UNSTITCHED anchor whose drop was benign: the geometric
    // shortlist of plausible relabels (face perms, new-frame id -> old
    // epoch id) that the ambiguous snap could not choose between.
    // Content registration downstream picks among these with as little
    // as one shared face - geometry narrows, content decides.
    std::vector<std::array<int, 6>> stitch_candidates;
    double turn_deg = -1.0;  // rotation vs the previous tracked frame; -1 = no previous
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
    // Anchor confidence bar (P6.13): a track may only be BORN on a
    // detection whose cube score clears this. The live-demo recording
    // measured two cleanly separated anchor populations: with no cube in
    // frame the detector still fires on face/hands/background at scores
    // 0.25-0.77 (83 hallucinated anchors, median 0.42, max 0.773 - one
    // outlier, next 0.669), and every one of them seeded a junk track
    // that health could NOT catch (M3T anchors ON the hallucination, so
    // the same hallucinated corners it is judged against agree with it -
    // the fist window passed health at 0.13 for 119 frames and fed 95
    // dark-grey windows into fusion). Every real-cube anchor across the
    // live session and the seven-clip synthetic corpus scores 0.826+
    // (live min 0.902; synthetic min 0.826, an exit-reentry
    // re-acquisition). 0.80 splits the measured [0.773, 0.826] gap -
    // thin, and honestly thin: 0.027 to the strongest hallucination,
    // 0.026 to the weakest real re-anchor. Health keeps consuming
    // detections BELOW this bar unchanged - an existing real track may
    // be judged (and dropped) by marginal detections; it may not be
    // seeded by one.
    static constexpr float kAnchorMinConfidence = 0.80F;
    static constexpr double kHealthFailFrac = 0.15;
    // The streak is AGE-DEPENDENT because the two failure populations
    // need opposite responses (both measured on the synthetic corpus):
    // a BAD RE-ANCHOR fails health from its first frames and must drop
    // fast or re-acquisition stalls (a flat 20-streak pushed exit-
    // reentry reacq from 264 ms to 1122 ms with a failure), while a
    // MATURE track failing after a deliberate turn is usually settle
    // oscillation that a 3-streak wrongly dropped. Young tracks get the
    // fast trigger; proven ones get patience.
    static constexpr int kHealthFailStreakYoung = 3;
    static constexpr int kHealthFailStreak = 20;
    static constexpr int kTrackMatureFrames = 30;
    // A health-MEASUREMENT gap voids maturity: a zombie track that
    // survived the cube's absence unmeasurable resurfaces with mature
    // patience it never earned (measured: exit-reentry reacq 264 ms ->
    // 1122 ms under the flat age rule). After this many live frames
    // without a health measurement, the track re-proves like a fresh
    // anchor. Settle sequences measure every detect frame and never
    // trip this.
    static constexpr int kHealthGapFrames = 10;
    // Stitching bounds (P6.2b): a drop whose last health sat in the
    // marginal band (below wrong-basin ~0.36, measured) and whose
    // re-anchor arrives within the gap is a benign re-anchor - the old
    // registration was approximately right, so the old->new model-frame
    // rotation snaps safely to the nearest of the 24 (drift is far
    // under the 45-degree basin half-width). An in-place re-seed from
    // the refined pose was tried first and CANNOT work: at these
    // errors (~30 px) the refiner is out of reach by its own
    // never-re-seat design, so refine_applied is false exactly when the
    // seed would be needed.
    static constexpr double kStitchableHealthFrac = 0.30;
    static constexpr int kStitchMaxGapFrames = 5;
    static constexpr double kStitchMaxSnapDeg = 30.0;
    static constexpr double kStitchShortlistDeg = 65.0;  // export candidates within this

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
    // A gauge slip is only DECIDABLE when the pose is quasi-static: mid
    // -turn the tracked pose lags the physical cube and a neighbouring
    // gauge briefly fits the lagged state - the synthetic six-face sweep
    // measured 5 declarations against 0 real slips, all during its
    // quarter-turn transitions. Above this per-frame rotation rate the
    // slip evaluation (and fresh pinning) is suspended; real slips
    // persist after the turn settles and are caught then.
    static constexpr double kGaugeEvalMaxTurnDeg = 2.5;  // per tracked frame
    // ...and for a window AFTER the turn settles: the tracked pose
    // converges back over several frames, and - worse - the rate gate is
    // keyed on TRACKED pose motion, which under-reads exactly when M3T
    // lags hardest mid-turn (the pose barely moves, then snaps). The
    // settle window must cover the snap-back; 6 frames left 2 of the
    // sweep's 5 false positives alive, 12 covers the measured tail.
    static constexpr int kGaugeSettleFrames = 12;
    static constexpr int kGaugeSlipHoldoff = 15;  // detect-frame measurements
    int gauge_pinned_perm_ = -1;
    int gauge_epoch_ = 0;
    int gauge_fail_streak_ = 0;
    int gauge_slip_holdoff_ = 0;
    bool prev_track_rvec_valid_ = false;
    cv::Vec3d prev_track_rvec_{};
    int gauge_settle_ = 0;
    int frames_since_anchor_ = 0;
    int frames_since_health_ = 0;
    bool last_health_pass_ = false;
    // Stitch state: the dropped track's last pose (for the model-frame
    // snap), its recency, and the cumulative face-id relabel from the
    // CURRENT model frame back to the epoch-original one.
    bool drop_pose_valid_ = false;
    cv::Vec3d drop_rvec_{};
    int frames_since_drop_ = 0;
    std::array<int, 6> stitch_relabel_{0, 1, 2, 3, 4, 5};
};

}  // namespace cube::vision::pipeline
