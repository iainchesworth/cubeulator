#include <cube/vision/pipeline/legacy_pipeline.hpp>

#include <cube/model/face.hpp>
#include <cube/vision/capture_gates.hpp>
#include <cube/vision/color_resolution.hpp>
#include <cube/vision/corner_observation.hpp>
#include <cube/vision/edge_observation.hpp>
#include <cube/vision/face_observation.hpp>
#include <cube/vision/pose_from_corners.hpp>
#include <cube/vision/track_fusion.hpp>

#include <opencv2/calib3d.hpp>
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace cube::vision::pipeline {

namespace {

using Clock = std::chrono::steady_clock;

double elapsed_ms(Clock::time_point start) {
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

// Same fully-visible gate as the live worker (scan_session.cpp) and the
// [.track-replay] harness, at analysis scale.
bool cell_in_frame(const StickerQuad& cell, const cv::Mat& bounds) {
    cv::Point2f center(0.0F, 0.0F);
    for (const auto& corner : cell.corners) {
        center += corner;
    }
    center *= 0.25F;
    return center.x >= 0.0F && center.y >= 0.0F && center.x < static_cast<float>(bounds.cols) &&
           center.y < static_cast<float>(bounds.rows);
}

template <typename Cells>
bool all_cells_in_frame(const Cells& cells, const cv::Mat& frame) {
    return std::all_of(cells.begin(), cells.end(),
                       [&](const StickerQuad& cell) { return cell_in_frame(cell, frame); });
}

const char* state_name(::cube::vision::TrackState state) {
    switch (state) {
        case ::cube::vision::TrackState::lost:
            return "lost";
        case ::cube::vision::TrackState::acquiring:
            return "acquiring";
        case ::cube::vision::TrackState::locked:
            return "locked";
        case ::cube::vision::TrackState::coasting:
            return "coasting";
    }
    return "?";
}

const char* gate_name(TrackGate gate) {
    switch (gate) {
        case TrackGate::accepted:
            return "accepted";
        case TrackGate::anchored:
            return "anchored";
        case TrackGate::no_observation:
            return "no-obs";
        case TrackGate::basin:
            return "basin";
        case TrackGate::margin:
            return "margin";
        case TrackGate::omega_outlier:
            return "omega-outlier";
        case TrackGate::implausible:
            return "implausible";
        case TrackGate::slip_evidence:
            return "slip-evidence";
    }
    return "?";
}

cv::Rect quad_bounds(const std::array<cv::Point2f, 4>& quad) {
    return cv::boundingRect(std::vector<cv::Point2f>(quad.begin(), quad.end()));
}

}  // namespace

LegacyPipelineResult LegacyPipeline::process(const FramePacket& frame) {
    LegacyPipelineResult result;
    result.trace.seq = frame.seq;
    result.trace.t_ms = frame.t_ms;

    // Deterministic replay: RANSAC inside cv::findHomography /
    // cv::estimateAffine2D draws from the thread-local RNG; reseeding from
    // the frame seq makes every frame's fits independent of processing
    // history, so identical recordings give byte-identical traces.
    // UINT64_C, not a bare ULL: on LP64 std::uint64_t is `unsigned long`, and mixing it
    // with an `unsigned long long` literal trips GCC's -Wsign-conversion.
    cv::theRNG().state = UINT64_C(0x9E3779B97F4A7C15) ^ static_cast<std::uint64_t>(frame.seq);

    // --- A. acquire --------------------------------------------------------
    auto stage_start = Clock::now();
    StageTrace acquire;
    acquire.stage = "acquire";
    acquire.ran = true;
    double fx = frame.fx;
    if (fx <= 0.0) {
        const cv::Matx33d k = approximate_camera_matrix(frame.bgr.size());
        fx = k(0, 0);
    }
    const float motion_score = stability_.update(frame.bgr).motion_score;
    acquire.add("w", static_cast<std::int64_t>(frame.bgr.cols));
    acquire.add("h", static_cast<std::int64_t>(frame.bgr.rows));
    acquire.add("full_w", static_cast<std::int64_t>(frame.full_size.width));
    acquire.add("full_h", static_cast<std::int64_t>(frame.full_size.height));
    acquire.add("fx", fx);
    acquire.add("intrinsics_calibrated", frame.intrinsics_calibrated);
    acquire.add("motion_score", static_cast<double>(motion_score));
    acquire.duration_ms = elapsed_ms(stage_start);
    result.trace.stages.push_back(std::move(acquire));

    // --- B. detect ---------------------------------------------------------
    // Seed/hint staleness is decided BEFORE the detector stage, exactly as
    // the live pass samples its one prior at queue time.
    stage_start = Clock::now();
    StageTrace detect;
    detect.stage = "detect";
    detect.ran = true;
    if (seed_prior_ && frame.t_ms - seed_t_ms_ > gates::kOverlayHoldMs) {
        seed_prior_.reset();
    }
    const PoseSeed* seed = seed_prior_ ? &*seed_prior_ : nullptr;

    CubeDetection& detection = result.detection;
    const auto observed = observe_face(frame.bgr);
    const bool face_gated = observed.has_value() && !all_cells_in_frame(observed->cells, frame.bgr);
    if (observed.has_value() && !face_gated) {
        if (observed->pose_only) {
            detection.legacy_pose_only_face = *observed;
        } else {
            detection.legacy_face = *observed;
        }
    }

    std::optional<CornerHint> corner_hint;
    if (seed_prior_) {
        corner_hint = compute_corner_hint(*seed_prior_);
    }
    const auto observed_corner = observe_corner(frame.bgr, corner_hint ? &*corner_hint : nullptr);
    const bool corner_gated = observed_corner.has_value() &&
                              !(all_cells_in_frame(observed_corner->top.cells, frame.bgr) &&
                                all_cells_in_frame(observed_corner->lower_left.cells, frame.bgr) &&
                                all_cells_in_frame(observed_corner->lower_right.cells, frame.bgr));
    if (observed_corner.has_value() && !corner_gated) {
        detection.legacy_corner = *observed_corner;
    }

    detection.found = detection.legacy_face.has_value() || detection.legacy_corner.has_value() ||
                      detection.legacy_pose_only_face.has_value();
    float confidence = 0.0F;
    cv::Rect roi;
    if (detection.legacy_face) {
        confidence = std::max(confidence, detection.legacy_face->geometry_confidence);
        roi |= quad_bounds(detection.legacy_face->face_quad);
    }
    if (detection.legacy_pose_only_face) {
        confidence = std::max(confidence, detection.legacy_pose_only_face->geometry_confidence);
        roi |= quad_bounds(detection.legacy_pose_only_face->face_quad);
    }
    if (detection.legacy_corner) {
        confidence = std::max(confidence, detection.legacy_corner->geometry_confidence);
        roi |= quad_bounds(detection.legacy_corner->top.quad);
        roi |= quad_bounds(detection.legacy_corner->lower_left.quad);
        roi |= quad_bounds(detection.legacy_corner->lower_right.quad);
    }
    detection.confidence = confidence;
    detection.roi = roi;

    // Phase 1/2 detect+track loop (detect_track_loop.hpp - the SAME class
    // the live app runs). When a detector is attached it is the stage-B
    // PRIMARY: its verdict becomes found/confidence/roi and it fills the
    // content-agnostic corner candidates (view-canonical ids). The loop's
    // tracking half is consumed by stage C below; its cost is attributed
    // to this stage's duration (the loop is fused by design).
    bool learned_ran = false;
    bool learned_found = false;
    DetectTrackResult dt;
    if (loop_.attached()) {
        learned_ran = true;
        dt = loop_.process(frame.bgr);
        learned_found = dt.detection.found;
        detection.found = dt.detection.found;
        detection.confidence = dt.detection.confidence;
        confidence = dt.detection.confidence;
        if (dt.detection.found) {
            detection.roi =
                cv::Rect(static_cast<int>(dt.detection.box.x), static_cast<int>(dt.detection.box.y),
                         static_cast<int>(dt.detection.box.width),
                         static_cast<int>(dt.detection.box.height));
            for (int id = 0; id < 8; ++id) {
                const auto& corner = dt.detection.corners[static_cast<std::size_t>(id)];
                if (corner.confidence < OnnxCornerDetector::kDefaultKeypointFloor) {
                    continue;
                }
                CornerCandidate candidate;
                candidate.model_vertex = id;
                candidate.px = corner.px;
                candidate.confidence = corner.confidence;
                detection.corners.push_back(candidate);
            }
        } else {
            detection.roi = cv::Rect();
        }
    }

    detect.add("found", detection.found);
    detect.add("confidence", static_cast<double>(confidence));
    detect.add("learned_ran", learned_ran);
    detect.add("learned_found", learned_found);
    detect.add("learned_corners", static_cast<std::int64_t>(detection.corners.size()));
    detect.add("face_found", detection.legacy_face.has_value());
    detect.add("face_pose_only", detection.legacy_pose_only_face.has_value());
    detect.add("face_gated", face_gated);
    detect.add("corner_found", detection.legacy_corner.has_value());
    detect.add("corner_gated", corner_gated);
    detect.add("corner_hinted", corner_hint.has_value());
    detect.duration_ms = elapsed_ms(stage_start);
    result.trace.stages.push_back(std::move(detect));

    // --- C. track ----------------------------------------------------------
    // Corner-first pose exactly like the live pass; edge fallback only when
    // every richer estimator produced no pose at all. The edge DETECTOR
    // runs inside this stage (see the header's honesty note).
    stage_start = Clock::now();
    StageTrace track_trace;
    track_trace.stage = "track";
    track_trace.ran = true;

    std::optional<CubePoseProjection> pose;
    const char* pose_src = "none";
    bool face_pose = false;
    std::optional<EdgeObservation> edge;
    bool edge_ran = false;
    if (detection.legacy_corner.has_value()) {
        if (const auto projected =
                project_cube_from_corner(*detection.legacy_corner, frame.bgr.size(), seed);
            projected.has_value()) {
            pose = *projected;
            pose_src = "corner";
        }
    } else if (detection.legacy_face.has_value()) {
        if (const auto projected =
                project_cube_from_face(*detection.legacy_face, frame.bgr.size(), seed);
            projected.has_value()) {
            pose = *projected;
            pose_src = "face";
            face_pose = true;
        }
    } else if (detection.legacy_pose_only_face.has_value()) {
        if (const auto projected =
                project_cube_from_face(*detection.legacy_pose_only_face, frame.bgr.size(), seed);
            projected.has_value()) {
            pose = *projected;
            pose_src = "face-pose-only";
        }
    }
    if (!pose.has_value()) {
        edge_ran = true;
        std::optional<EdgeHint> edge_hint;
        if (seed_prior_) {
            edge_hint = compute_edge_hint(*seed_prior_);
        }
        if (const auto observed_edge = observe_edge(frame.bgr, edge_hint ? &*edge_hint : nullptr);
            observed_edge.has_value()) {
            edge = *observed_edge;
            if (const auto projected =
                    project_cube_from_edge(*observed_edge, frame.bgr.size(), seed);
                projected.has_value()) {
                pose = *projected;
                pose_src = "edge";
            }
        }
    }
    const bool corner_pose = detection.legacy_corner.has_value() && pose.has_value();
    if (pose.has_value() && pose->orientation_valid) {
        seed_prior_ = PoseSeed{pose->rvec, pose->tvec};
        seed_t_ms_ = frame.t_ms;
    }

    TrackerPassInput track_in;
    track_in.t_ms = frame.t_ms;
    track_in.motion_score = motion_score;
    if (pose.has_value() && pose->orientation_valid) {
        track_in.q_obs = pose->orientation;
        track_in.corner_view = corner_pose;
        const auto lab_entry = [](const cv::Vec3f& lab) {
            return std::array<float, 3>{lab[0], lab[1], lab[2]};
        };
        if (corner_pose && detection.legacy_corner.has_value()) {
            track_in.centre_lab_count = 3;
            track_in.centre_lab[0] =
                lab_entry(detection.legacy_corner->top.lab[cube::model::center_facelet_index]);
            track_in.centre_lab[1] = lab_entry(
                detection.legacy_corner->lower_left.lab[cube::model::center_facelet_index]);
            track_in.centre_lab[2] = lab_entry(
                detection.legacy_corner->lower_right.lab[cube::model::center_facelet_index]);
        } else if (face_pose && detection.legacy_face.has_value()) {
            track_in.centre_lab_count = 1;
            track_in.centre_lab[0] =
                lab_entry(detection.legacy_face->lab[cube::model::center_facelet_index]);
        }
    }

    // Idle-gate shadow: the live worker would skip the detectors on
    // gate-approved lost passes; this instrument analyses every frame and
    // records the verdict instead (header note).
    const bool would_idle_skip = idle_gate_.skip_next();
    const auto track_out = tracker_.on_pass(track_in);
    const int cool_hue = count_cool_hue_pixels(frame.bgr);
    idle_gate_.on_pass(cool_hue, track_out.state == ::cube::vision::TrackState::lost);

    TrackState& track = result.track;
    track.phase = track_out.state;
    track.epoch = track_out.epoch;
    track.gate = gate_name(track_out.gate);
    track.pose_source = pose_src;
    track.motion_score = motion_score;
    track.front_face = track_out.front_face;
    track.identity_certain_front =
        track_out.front_face &&
        track_out.identity_certain_face[static_cast<std::size_t>(*track_out.front_face)];
    if (pose.has_value() && pose->orientation_valid) {
        track.pose_valid = true;
        track.rvec = pose->rvec;
        track.tvec = pose->tvec;
        track.orientation = pose->orientation;
        track.projected_vertices = pose->vertices;
        track.reproj_fraction = pose->max_reproj_fraction;
    }

    track_trace.add("pose_src", std::string(pose_src));
    track_trace.add("pose_valid", track.pose_valid);
    track_trace.add("from_seed", pose.has_value() && pose->from_seed);
    track_trace.add("reproj_fraction", track.reproj_fraction);
    track_trace.add("edge_ran", edge_ran);
    track_trace.add("edge_found", edge.has_value());
    track_trace.add("state", std::string(state_name(track_out.state)));
    track_trace.add("gate", std::string(track.gate));
    track_trace.add("epoch", static_cast<std::int64_t>(track_out.epoch));
    track_trace.add("front",
                    std::string(1, track_out.front_face
                                       ? "URFDLB"[static_cast<std::size_t>(*track_out.front_face)]
                                       : '-'));
    track_trace.add("certain_front", track.identity_certain_front);
    track_trace.add("theta_deg", static_cast<double>(track_out.theta_deg));
    track_trace.add("margin_deg", static_cast<double>(track_out.margin_deg));
    track_trace.add("sigma_deg", static_cast<double>(track_out.sigma_deg));
    track_trace.add("belief_margin", static_cast<double>(track_out.belief_margin));
    track_trace.add("would_idle_skip", would_idle_skip);
    track_trace.add("cool_hue_pixels", static_cast<std::int64_t>(cool_hue));

    // Phase 2: detector-corner PnP facts from the loop - the tracker's
    // anchor and the trace's agreement measurement.
    if (detection.corners.size() >= 4) {
        if (dt.pnp.has_value()) {
            track_trace.add("detector_pnp_valid", true);
            track_trace.add("detector_pnp_corners",
                            static_cast<std::int64_t>(dt.pnp->corners_used));
            track_trace.add("detector_pnp_mean_reproj_px", dt.pnp->mean_reproj_px);
            track_trace.add("detector_pnp_max_reproj_px", dt.pnp->max_reproj_px);
            if (track.pose_valid) {
                // Angle between the two display quaternions - the
                // detector-vs-legacy orientation agreement, in degrees.
                double dot = 0.0;
                for (int i = 0; i < 4; ++i) {
                    dot += static_cast<double>(dt.pnp->orientation[static_cast<std::size_t>(i)]) *
                           static_cast<double>(track.orientation[static_cast<std::size_t>(i)]);
                }
                const double angle_deg = 2.0 * std::acos(std::clamp(std::abs(dot), 0.0, 1.0)) *
                                         180.0 / 3.14159265358979323846;
                track_trace.add("detector_pnp_vs_legacy_deg", angle_deg);
            }
        } else {
            track_trace.add("detector_pnp_valid", false);
        }
    }

    // Continuous 6-DoF tracking (Phase 2): the loop already ran the init/
    // step/health/re-init machinery in stage B; here its verdict is
    // consumed - when live and healthy the tracker OWNS TrackState's
    // pose/vertices/orientation/phase; the legacy orientation tracker's
    // verdicts remain in the trace above.
    if (loop_.has_tracker()) {
        if (dt.track_live && dt.step_valid) {
            track.pose_valid = true;
            track.rvec = dt.rvec;
            track.tvec = dt.tvec;
            cv::Matx33d rotation;
            cv::Rodrigues(dt.rvec, rotation);
            track.orientation = display_quaternion(rotation);
            track.projected_vertices = dt.projected;
            track.phase = TrackPhase::locked;
            track.pose_source = "m3t";
            track.reproj_fraction = dt.health_frac >= 0.0 ? dt.health_frac : track.reproj_fraction;
        } else if (dt.step_valid) {
            track.phase = TrackPhase::lost;  // dropped this frame by health
        }
        track_trace.add("m3t_attached", true);
        track_trace.add("m3t_live", dt.track_live);
        track_trace.add("m3t_step_valid", dt.step_valid);
        track_trace.add("m3t_health_frac", dt.health_frac);
        track_trace.add("m3t_reinits", static_cast<std::int64_t>(dt.reinits));
        result.track_faces = dt.faces;
        result.gauge_epoch = dt.gauge_epoch;
        result.stitch_candidates = dt.stitch_candidates;
        track_trace.add("derived_faces", static_cast<std::int64_t>(dt.faces.size()));
        track_trace.add("gauge_epoch", static_cast<std::int64_t>(dt.gauge_epoch));
        track_trace.add("read_grade", dt.read_grade);
        track_trace.add("stitched", dt.stitched);
        track_trace.add("stitch_snap_deg", dt.stitch_snap_deg);
        track_trace.add("gauge_slip", dt.gauge_slip);
        track_trace.add("gauge_pinned_err", dt.gauge_pinned_err);
        track_trace.add("refine_applied", dt.refine_applied);
        track_trace.add("refine_matches", static_cast<std::int64_t>(dt.refine_matches));
        track_trace.add("refine_update_deg", dt.refine_update_deg);
        track_trace.add("refine_residual_before_px", dt.refine_residual_before_px);
        track_trace.add("refine_residual_after_px", dt.refine_residual_after_px);
    }
    track_trace.duration_ms = elapsed_ms(stage_start);
    result.trace.stages.push_back(std::move(track_trace));

    // --- D. faces ----------------------------------------------------------
    // Views come from the pose-producing observation family; body_face only
    // where the legacy stack can honestly name it (frontal view of a
    // locked/coasting track). rectified stays empty until Phase 3.
    stage_start = Clock::now();
    StageTrace faces_trace;
    faces_trace.stage = "faces";
    faces_trace.ran = true;

    const bool track_has_identity =
        track.phase == TrackPhase::locked || track.phase == TrackPhase::coasting;
    // P3.4: while the continuous track is live, stage D-F geometry comes
    // from the POSE - the legacy cascade is retired as the primary
    // source (it still runs in stage B as a trace-only instrument until
    // the app's colour path moves in Phases 4-5). Frontality gates
    // capture grade: a grazing face's cells are slivers no sampler
    // should trust (threshold provisional, swept at the Phase 3 gate;
    // shared with the app's live track-mode sampler via track_fusion.hpp
    // since P6.12 - the two must agree byte-for-byte).
    using fusion::kTrackCaptureFrontality;
    const bool track_geometry = loop_.has_tracker() && dt.track_live && !dt.faces.empty();
    // Rectification homographies per derived face (identity-size with
    // dt.faces), kept for stage E's sub-pixel grid refinement.
    std::vector<cv::Mat> rectify_homographies(dt.faces.size());
    if (track_geometry) {
        for (std::size_t face_index = 0; face_index < dt.faces.size(); ++face_index) {
            const auto& derived = dt.faces[face_index];
            FaceView face_view;
            face_view.quad = derived.quad;
            face_view.frontal_deg =
                static_cast<float>(std::acos(std::clamp(derived.frontality, 0.0, 1.0)) * 180.0 /
                                   3.14159265358979323846);
            // P6.2: capture grade = geometric frontality AND the loop's
            // tracker-quality read grade (quasi-static, health-current).
            // The legacy accept-streak/gauge-margin compensations have no
            // tracked-path equivalent - this gate is their replacement.
            face_view.capture_grade =
                derived.frontality >= kTrackCaptureFrontality && dt.read_grade;
            if (face_view.capture_grade) {
                face_view.rectified =
                    rectify_face(frame.bgr, derived, 96, &rectify_homographies[face_index]);
            }
            // body_face stays empty: identity is epoch-local gauge, and
            // the assembly arbiter remains centre-colour keying (plan
            // Phase 3 item 3).
            result.faces.faces.push_back(std::move(face_view));
        }
        faces_trace.add("source", std::string("track-geometry"));
    } else if (detection.legacy_corner) {
        for (const auto* view :
             {&detection.legacy_corner->top, &detection.legacy_corner->lower_left,
              &detection.legacy_corner->lower_right}) {
            FaceView face_view;
            face_view.quad = view->quad;
            face_view.capture_grade = true;
            result.faces.faces.push_back(std::move(face_view));
        }
    } else if (detection.legacy_face) {
        FaceView face_view;
        face_view.quad = detection.legacy_face->face_quad;
        face_view.capture_grade = true;
        if (track_has_identity) {
            face_view.body_face = track.front_face;
        }
        result.faces.faces.push_back(std::move(face_view));
    } else if (detection.legacy_pose_only_face) {
        FaceView face_view;
        face_view.quad = detection.legacy_pose_only_face->face_quad;
        face_view.capture_grade = false;  // translation-ambiguous: never colour
        result.faces.faces.push_back(std::move(face_view));
    } else if (edge) {
        for (const auto* quad : {&edge->left_quad, &edge->right_quad}) {
            FaceView face_view;
            face_view.quad = *quad;
            face_view.capture_grade = false;  // pose-only by contract
            result.faces.faces.push_back(std::move(face_view));
        }
    }
    faces_trace.add("count", static_cast<std::int64_t>(result.faces.faces.size()));
    faces_trace.add("capture_grade", static_cast<std::int64_t>(std::count_if(
                                         result.faces.faces.begin(), result.faces.faces.end(),
                                         [](const FaceView& v) { return v.capture_grade; })));
    faces_trace.duration_ms = elapsed_ms(stage_start);
    result.trace.stages.push_back(std::move(faces_trace));

    // --- E. cells ----------------------------------------------------------
    // Index-aligned with FaceSet; zero-filled where the legacy source has
    // no per-cell geometry (edge views). No sub-pixel refinement until
    // Phase 4 (refined_px stays zero).
    stage_start = Clock::now();
    StageTrace cells_trace;
    cells_trace.stage = "cells";
    cells_trace.ran = true;
    int faces_with_cells = 0;
    result.cells.faces.resize(result.faces.faces.size());
    {
        std::size_t index = 0;
        const auto put_cells = [&](const std::array<StickerQuad, 9>& cells) {
            result.cells.faces[index].cells = cells;
            ++faces_with_cells;
            ++index;
        };
        if (track_geometry) {
            // P4 sub-pixel refinement: where the face was rectified, the
            // gap lines re-fit the internal boundaries (clamped +-0.2
            // pitch - polish, never re-seat) and the sticker quads map
            // back through the homography; otherwise the projected
            // model-fraction cells stand.
            int refined_faces = 0;
            for (std::size_t face_index = 0; face_index < dt.faces.size(); ++face_index) {
                const auto& derived = dt.faces[face_index];
                std::array<StickerQuad, 9> cells{};
                bool refined = false;
                if (face_index < result.faces.faces.size() &&
                    !result.faces.faces[face_index].rectified.empty() &&
                    !rectify_homographies[face_index].empty()) {
                    const RefinedGrid grid =
                        refine_cell_grid(result.faces.faces[face_index].rectified);
                    if (grid.valid) {
                        const auto refined_cells =
                            cells_from_grid(grid, rectify_homographies[face_index]);
                        for (std::size_t c = 0; c < 9; ++c) {
                            cells[c].corners = refined_cells[c];
                        }
                        refined = true;
                        ++refined_faces;
                    }
                }
                if (!refined) {
                    for (std::size_t c = 0; c < 9; ++c) {
                        cells[c].corners = derived.cells[c];
                    }
                }
                put_cells(cells);
            }
            cells_trace.add("refined_faces", static_cast<std::int64_t>(refined_faces));
        } else if (detection.legacy_corner) {
            put_cells(detection.legacy_corner->top.cells);
            put_cells(detection.legacy_corner->lower_left.cells);
            put_cells(detection.legacy_corner->lower_right.cells);
        } else if (detection.legacy_face) {
            put_cells(detection.legacy_face->cells);
        } else if (detection.legacy_pose_only_face) {
            put_cells(detection.legacy_pose_only_face->cells);
        }
        // edge views: no cells by contract; entries stay zero-filled.
    }
    cells_trace.add("faces_with_cells", static_cast<std::int64_t>(faces_with_cells));
    cells_trace.duration_ms = elapsed_ms(stage_start);
    result.trace.stages.push_back(std::move(cells_trace));

    // --- F. sample ---------------------------------------------------------
    // The legacy detectors sample Lab during detection; this stage adapts
    // those samples into the stage contract. Only capture-grade views may
    // carry colour (the live pipeline never aggregates pose-only Lab).
    stage_start = Clock::now();
    StageTrace sample_trace;
    sample_trace.stage = "sample";
    sample_trace.ran = true;
    int sampled_faces = 0;
    result.samples.faces.resize(result.faces.faces.size());
    {
        std::size_t index = 0;
        const auto put_lab = [&](const std::array<cv::Vec3f, 9>& lab,
                                 const std::array<float, 9>* purity) {
            if (result.faces.faces[index].capture_grade) {
                result.samples.faces[index].lab = lab;
                if (purity != nullptr) {
                    result.samples.faces[index].purity = *purity;
                }
                ++sampled_faces;
            }
            ++index;
        };
        if (track_geometry) {
            // P4 track-geometry sampling: the exonerated quad-masked
            // Lab-median estimator (sample_cell_patch) on each projected
            // STICKER quad, straight off the frame. This replaced a P3.4
            // placeholder that meaned 8U Lab off the rectified face -
            // wrong scale against classify_lab's float-Lab palette, a
            // convention bug this swap retires. Purity rides along; the
            // aggregators already know how to weigh it.
            for (std::size_t face_index = 0; face_index < result.faces.faces.size(); ++face_index) {
                const auto& view = result.faces.faces[face_index];
                if (!view.capture_grade) {
                    continue;
                }
                for (std::size_t c = 0; c < 9; ++c) {
                    const auto stats = sample_cell_patch(
                        frame.bgr, result.cells.faces[face_index].cells[c].corners);
                    if (stats.quad_px > 0) {
                        result.samples.faces[face_index].lab[c] = stats.lab_median;
                        result.samples.faces[face_index].purity[c] = stats.purity;
                    }
                }
                ++sampled_faces;
            }
        } else if (detection.legacy_corner) {
            put_lab(detection.legacy_corner->top.lab, nullptr);
            put_lab(detection.legacy_corner->lower_left.lab, nullptr);
            put_lab(detection.legacy_corner->lower_right.lab, nullptr);
        } else if (detection.legacy_face) {
            put_lab(detection.legacy_face->lab, &detection.legacy_face->cell_purity);
        } else if (detection.legacy_pose_only_face) {
            put_lab(detection.legacy_pose_only_face->lab,
                    &detection.legacy_pose_only_face->cell_purity);
        }
    }
    sample_trace.add("sampled_faces", static_cast<std::int64_t>(sampled_faces));
    sample_trace.add("sampled_by",
                     std::string(track_geometry ? "track-rectified" : "legacy-detector"));
    sample_trace.duration_ms = elapsed_ms(stage_start);
    result.trace.stages.push_back(std::move(sample_trace));

    // --- G. classify -------------------------------------------------------
    // Provisional classify_lab naming for sampled faces; the authoritative
    // resolve_colors stays a scan-completion concern (stage I).
    stage_start = Clock::now();
    StageTrace classify_trace;
    classify_trace.stage = "classify";
    classify_trace.ran = true;
    // P5 session palette, epoch-scoped: identity is epoch-local, so an
    // anchor or a declared gauge slip clears the observed centres (they
    // could be a different face's centre now). Centres feed BEFORE this
    // frame's cells are named, so even the first tracked frame with two
    // visible faces classifies session-relative.
    if (track_geometry) {
        if (dt.gauge_epoch != session_palette_epoch_) {
            session_palette_.reset();
            session_palette_epoch_ = dt.gauge_epoch;
        }
        for (std::size_t index = 0; index < result.faces.faces.size(); ++index) {
            if (result.faces.faces[index].capture_grade &&
                result.samples.faces[index].purity[4] > 0.0F) {
                session_palette_.observe_centre(result.samples.faces[index].lab[4]);
            }
        }
    }
    int classified_faces = 0;
    std::int64_t unknown_cells = 0;
    result.reads.faces.resize(result.faces.faces.size());
    for (std::size_t index = 0; index < result.faces.faces.size(); ++index) {
        if (!result.faces.faces[index].capture_grade) {
            continue;
        }
        // Skip faces the sample stage refused (edge views are never
        // capture-grade, so a capture-grade face always has samples).
        ++classified_faces;
        for (std::size_t cell = 0; cell < 9; ++cell) {
            const auto facelet =
                track_geometry ? session_palette_.classify(result.samples.faces[index].lab[cell])
                               : classify_lab(result.samples.faces[index].lab[cell]);
            result.reads.faces[index].facelets[cell] = facelet;
            unknown_cells += facelet.color == cube::model::FacetColor::unknown ? 1 : 0;
        }
    }
    classify_trace.add("classified_faces", static_cast<std::int64_t>(classified_faces));
    classify_trace.add("unknown_cells", unknown_cells);
    classify_trace.add("session_clusters",
                       static_cast<std::int64_t>(session_palette_.cluster_count()));
    classify_trace.duration_ms = elapsed_ms(stage_start);
    result.trace.stages.push_back(std::move(classify_trace));

    // --- H. fuse (app-owned until the routing harness lands) ---------------
    StageTrace fuse_trace;
    fuse_trace.stage = "fuse";
    fuse_trace.ran = false;
    fuse_trace.add(
        "reason",
        std::string(
            "app-owned commit path (FaceAggregator/CaptureRouter); lib-drivable after P0.6"));
    result.trace.stages.push_back(std::move(fuse_trace));

    // --- I. assemble (scan-completion stage) --------------------------------
    StageTrace assemble_trace;
    assemble_trace.stage = "assemble";
    assemble_trace.ran = false;
    assemble_trace.add(
        "reason",
        std::string(
            "runs once per completed scan (resolve_colors + assemble_scanned_faces_with_mapping)"));
    result.trace.stages.push_back(std::move(assemble_trace));

    return result;
}

void LegacyPipeline::reset() {
    stability_ = StabilityTracker{};
    tracker_.reset();
    idle_gate_ = IdleGate{};
    seed_prior_.reset();
    seed_t_ms_ = 0;
    loop_.reset();
    session_palette_.reset();
    session_palette_epoch_ = -1;
}

}  // namespace cube::vision::pipeline
