#include <cube/vision/pipeline/detect_track_loop.hpp>

#include <cube/vision/pipeline/pose_refiner.hpp>
#include <cube/vision/pose_estimation.hpp>

#include <opencv2/calib3d.hpp>

#include <cmath>
#include <vector>

namespace cube::vision::pipeline {

namespace {

// The cube's 24 rotation matrices (closure over quarter-turn
// generators) - the snap targets for gauge stitching.
const std::vector<cv::Matx33d>& cube_rotations() {
    static const std::vector<cv::Matx33d> rotations = [] {
        const cv::Matx33d rot_y(0, 0, 1, 0, 1, 0, -1, 0, 0);
        const cv::Matx33d rot_z(0, -1, 0, 1, 0, 0, 0, 0, 1);
        std::vector<cv::Matx33d> out{cv::Matx33d::eye()};
        for (std::size_t i = 0; i < out.size(); ++i) {
            for (const auto& generator : {rot_y, rot_z}) {
                const cv::Matx33d candidate = generator * out[i];
                const bool known = std::any_of(out.begin(), out.end(), [&](const cv::Matx33d& r) {
                    return cv::norm(r - candidate) < 0.1;
                });
                if (!known) {
                    out.push_back(candidate);
                }
            }
        }
        return out;
    }();
    return rotations;
}

// Face id (0..5 = +x,-x,+y,-y,+z,-z) of the axis a unit vector points
// along; -1 if it is not axis-aligned within tolerance.
int face_id_of_axis(const cv::Vec3d& v) {
    for (int axis = 0; axis < 3; ++axis) {
        if (std::abs(v[axis]) > 0.9) {
            return axis * 2 + (v[axis] > 0 ? 0 : 1);
        }
    }
    return -1;
}

cv::Vec3d face_axis(int face) {
    cv::Vec3d v(0, 0, 0);
    v[face / 2] = (face % 2 == 0) ? 1.0 : -1.0;
    return v;
}

}  // namespace

cv::Matx33d DetectTrackLoop::camera_matrix_for(cv::Size size) const {
    return intrinsics_.has_value() ? intrinsics_->scaled_to(size).matrix()
                                   : approximate_camera_matrix(size);
}

DetectTrackResult DetectTrackLoop::process(const cv::Mat& bgr_frame) {
    DetectTrackResult result;
    result.reinits = reinits_;
    if (!attached() || bgr_frame.empty()) {
        return result;
    }

    // Detector decimation applies only while live (a lost loop needs the
    // detector every frame to re-anchor).
    const bool run_detector =
        !live_ || live_detect_interval_ <= 1 || ++frames_since_detect_ >= live_detect_interval_;
    if (run_detector) {
        frames_since_detect_ = 0;
        if (auto detected = detector_->detect(bgr_frame); detected.has_value()) {
            result.detection = *detected;
        }
    }

    // Single-frame PnP whenever enough identified corners exist - the
    // anchor pose and the trace's agreement measurement.
    int confident_corners = 0;
    for (const auto& corner : result.detection.corners) {
        confident_corners += corner.confidence >= OnnxCornerDetector::kDefaultKeypointFloor ? 1 : 0;
    }
    if (result.detection.found && confident_corners >= 4) {
        if (auto solved = estimate_pose_from_corners(result.detection, bgr_frame.size(),
                                                     OnnxCornerDetector::kDefaultKeypointFloor,
                                                     camera_matrix_for(bgr_frame.size()));
            solved.has_value()) {
            result.pnp = *solved;
        }
    }

    if (tracker_ == nullptr) {
        return result;  // detector-only mode
    }
    if (!live_ && drop_pose_valid_) {
        ++frames_since_drop_;
    }

    // Anchor gate (P6.13): geometry (corner count) AND detection
    // confidence - a hallucinated detection can be PnP-consistent, and a
    // track anchored on one is then health-checked against the same
    // hallucination (see the header's measurement).
    if (!live_ && result.pnp.has_value() && result.pnp->corners_used >= kInitMinCorners &&
        result.detection.confidence >= kAnchorMinConfidence) {
        if (tracker_->init(result.pnp->rvec, result.pnp->tvec, bgr_frame.size(),
                           camera_matrix_for(bgr_frame.size()))) {
            live_ = true;
            health_fail_streak_ = 0;
            result.reinits = ++reinits_;
            // Gauge stitch (P6.2b): a benign drop's registration was
            // approximately right, so the old->new model-frame rotation
            // snaps to one of the 24 and face ids relabel into the OLD
            // epoch's frame - identity survives the re-anchor.
            bool stitched = false;
            if (drop_pose_valid_ && frames_since_drop_ <= kStitchMaxGapFrames) {
                cv::Matx33d rot_old;
                cv::Matx33d rot_new;
                cv::Rodrigues(drop_rvec_, rot_old);
                cv::Rodrigues(result.pnp->rvec, rot_new);
                const cv::Matx33d frame_change = rot_new.t() * rot_old;  // model_old -> model_new
                double best_angle = 1e9;
                cv::Matx33d best_rotation = cv::Matx33d::eye();
                for (const auto& candidate : cube_rotations()) {
                    cv::Vec3d delta;
                    cv::Rodrigues(candidate.t() * frame_change, delta);
                    const double angle = cv::norm(delta) * 180.0 / CV_PI;
                    if (angle < best_angle) {
                        best_angle = angle;
                        best_rotation = candidate;
                    }
                }
                result.stitch_snap_deg = best_angle;
                // Export the geometric shortlist regardless of the snap
                // outcome: content registration downstream restricts its
                // 24 candidates to these.
                for (const auto& candidate : cube_rotations()) {
                    cv::Vec3d delta;
                    cv::Rodrigues(candidate.t() * frame_change, delta);
                    if (cv::norm(delta) * 180.0 / CV_PI > kStitchShortlistDeg) {
                        continue;
                    }
                    std::array<int, 6> perm{};
                    bool sane = true;
                    for (int face = 0; face < 6; ++face) {
                        const int old_face = face_id_of_axis(candidate.t() * face_axis(face));
                        if (old_face < 0) {
                            sane = false;
                            break;
                        }
                        perm[static_cast<std::size_t>(face)] =
                            stitch_relabel_[static_cast<std::size_t>(old_face)];
                    }
                    if (sane) {
                        result.stitch_candidates.push_back(perm);
                    }
                }
                if (best_angle <= kStitchMaxSnapDeg) {
                    // Compose: new-frame face id -> old-frame id -> the
                    // epoch-original id the old relabel already spoke.
                    std::array<int, 6> relabel{};
                    bool sane = true;
                    for (int face = 0; face < 6; ++face) {
                        const cv::Vec3d in_old = best_rotation.t() * face_axis(face);
                        const int old_face = face_id_of_axis(in_old);
                        if (old_face < 0) {
                            sane = false;
                            break;
                        }
                        relabel[static_cast<std::size_t>(face)] =
                            stitch_relabel_[static_cast<std::size_t>(old_face)];
                    }
                    if (sane) {
                        stitch_relabel_ = relabel;
                        stitched = true;
                        result.stitched = true;
                    }
                }
            }
            if (!stitched) {
                // A fresh anchor is a fresh identity epoch: the model
                // frame is this frame's view-canonical orientation
                // (P3.3), and the relabel resets to identity.
                ++gauge_epoch_;
                stitch_relabel_ = {0, 1, 2, 3, 4, 5};
            }
            drop_pose_valid_ = false;
            gauge_pinned_perm_ = -1;
            gauge_fail_streak_ = 0;
            gauge_slip_holdoff_ = 0;
            frames_since_anchor_ = 0;
            frames_since_health_ = 0;
            last_health_pass_ = false;
        }
    }

    if (live_) {
        ++frames_since_anchor_;
        ++frames_since_health_;
        const auto tracked = tracker_->step(bgr_frame);
        if (!tracked.valid) {
            live_ = false;
            prev_track_rvec_valid_ = false;
        } else {
            result.step_valid = true;
            result.rvec = tracked.rvec;
            result.tvec = tracked.tvec;
            if (prev_track_rvec_valid_) {
                cv::Matx33d rot_prev;
                cv::Matx33d rot_now;
                cv::Rodrigues(prev_track_rvec_, rot_prev);
                cv::Rodrigues(tracked.rvec, rot_now);
                cv::Vec3d turn_delta;
                cv::Rodrigues(rot_prev.t() * rot_now, turn_delta);
                result.turn_deg = cv::norm(turn_delta) * 180.0 / CV_PI;
            }

            // Edge/lattice refinement (P2.8): polish the stepped pose
            // against the image's edges - the OUTPUT pose only, never
            // fed back into the tracker. Feedback was built and measured
            // first: nudging M3T's pose after each step did not reduce
            // quarter-turn registration slips at all (2 with, 2 without
            // on the synthetic free-rotation clip) while it wrecked
            // re-anchor convergence (3 of 4 exit-reentry re-acquisitions
            // failed) - the region modality re-converges to its own
            // optimum from wherever it is put, so the nudge buys nothing
            // steady-state and perturbs exactly the fragile phases.
            // Runs on every LIVE frame, not only detect frames (P6.14).
            // The constraint is downstream: derive_faces, cell sampling
            // and fusion all read the OUTPUT pose, so refine cadence has
            // to follow the frames that FEED reads, and those are the
            // live frames. The app decimates the detector to 1 frame in
            // 3 while locked (scan_session.cpp), so the old
            // detection-found gate polished one fused observation in
            // three and sampled the other two from an unrefined, drifted
            // pose - cross-frame Lab jitter that fragmented fusion into
            // extra generations. Measured on the live-demo retry clip
            // through the app: gated 150 refines / 3 converged faces,
            // ungated 468 refines (offline runner parity 461) / 6
            // converged, on an IDENTICAL 4 anchors and an identical
            // tracked fraction - the anchor and health machinery never
            // moved, which is what identifies refine cadence as the
            // mechanism rather than anything about locking. Cost, measured
            // on the same replay: median pass 3.6 -> 4.4 ms, p95 30.7 ->
            // 32.2 ms, tracked fraction unchanged. Two alternatives were
            // measured and rejected: dropping the detector interval to 1
            // also reaches 6/6 but costs +27 ms median, and scaling the
            // health-fail streak by the decimation constant reached only
            // 4/6 while keying live behaviour to a constant the real
            // camera cadence diverges from.
            //
            // What the detection gate was protecting against - a track
            // that survives the cube's full exit unmeasurable (no
            // corners means no health) while the refiner polishes
            // background clutter - is covered by the refiner's OWN
            // guards, which are the caps that make it refine and never
            // anchor: 12 matched edge points minimum, a search window
            // under half a projected sticker cell, 6 degrees and 0.15
            // model units of total movement. A cube-absent frame either
            // fails to match (refused, pose untouched) or moves the pose
            // less than the cap. Measured on the exit clip
            // (live-demo-p6: the cube leaves permanently at frame ~578 of
            // 1567) - over the 989 post-exit frames the 51 live ones took
            // 12 extra refinements they used to skip, and NOTHING else
            // moved: liveness, anchors (25), epochs, derived faces, read
            // grade and unknown cells are frame-for-frame identical
            // across the whole clip, and the largest post-exit update
            // stayed at 4.5 of the 6 permitted degrees. On the synthetic
            // corpus every one of the 179 extra calls was refused for too
            // few matches - offline, where the detector runs every frame,
            // this is a no-op (all seven clips byte-identical).
            //
            // Health below judges the RAW stepped pose, not the refined
            // one: health is a statement about the TRACKER's state, and
            // judging the polished output was measured to mask drift -
            // the loop stopped re-anchoring (gauge runs 2 -> 1 on the
            // synthetic free-rotation clip) and M3T drifted through an
            // extra quarter-turn slip. Downstream consumers get the
            // refined pose; the drop/re-anchor decision stays keyed to
            // what the tracker itself believes.
            if (refine_enabled_) {
                const auto refined = refine_pose(bgr_frame, result.rvec, result.tvec,
                                                 camera_matrix_for(bgr_frame.size()));
                result.refine_matches = refined.matches;
                result.refine_residual_before_px = refined.mean_residual_before_px;
                result.refine_residual_after_px = refined.mean_residual_after_px;
                if (refined.applied) {
                    result.refine_applied = true;
                    result.refine_update_deg = refined.update_deg;
                    result.rvec = refined.rvec;
                    result.tvec = refined.tvec;
                }
            }

            std::vector<cv::Point3f> model(kSingmasterCornerModel.begin(),
                                           kSingmasterCornerModel.end());
            std::vector<cv::Point2f> projected;
            cv::projectPoints(model, result.rvec, result.tvec, camera_matrix_for(bgr_frame.size()),
                              cv::noArray(), projected);
            for (int i = 0; i < 8; ++i) {
                result.projected[static_cast<std::size_t>(i)] =
                    projected[static_cast<std::size_t>(i)];
            }
            // Stage-D geometry from the same OUTPUT pose (P3.1): faces,
            // quads and sticker centres are projections of the tracked
            // model - the plan's "no more per-frame face hunting".
            result.faces =
                derive_faces(result.rvec, result.tvec, camera_matrix_for(bgr_frame.size()));
            // Stitched epochs speak the epoch-ORIGINAL face ids.
            for (auto& face : result.faces) {
                face.face = stitch_relabel_[static_cast<std::size_t>(face.face)];
            }

            // Raw-pose projection for the health check (see the refine
            // comment above); identical to result.projected on frames
            // the refiner left alone.
            std::array<cv::Point2f, 8> raw_projected = result.projected;
            if (result.refine_applied) {
                std::vector<cv::Point2f> raw;
                cv::projectPoints(model, tracked.rvec, tracked.tvec,
                                  camera_matrix_for(bgr_frame.size()), cv::noArray(), raw);
                for (int i = 0; i < 8; ++i) {
                    raw_projected[static_cast<std::size_t>(i)] = raw[static_cast<std::size_t>(i)];
                }
            }

            // Health against this frame's detections, minimised over the
            // 24 view-canonical rotations.
            if (result.detection.found && confident_corners >= 4) {
                const auto& box = result.detection.box;
                const double diagonal = std::sqrt(static_cast<double>(box.width) * box.width +
                                                  static_cast<double>(box.height) * box.height);
                double best = -1.0;
                int best_perm = -1;
                double pinned_mean = -1.0;
                const auto& perms = singmaster_rotation_permutations();
                for (std::size_t p = 0; p < perms.size(); ++p) {
                    double total = 0.0;
                    int count = 0;
                    for (int id = 0; id < 8; ++id) {
                        const auto& corner = result.detection.corners[static_cast<std::size_t>(id)];
                        if (corner.confidence < OnnxCornerDetector::kDefaultKeypointFloor) {
                            continue;
                        }
                        const int tracked_id = perms[p][static_cast<std::size_t>(id)];
                        total += cv::norm(raw_projected[static_cast<std::size_t>(tracked_id)] -
                                          corner.px);
                        ++count;
                    }
                    if (count >= 4) {
                        const double mean = total / count;
                        if (best < 0.0 || mean < best) {
                            best = mean;
                            best_perm = static_cast<int>(p);
                        }
                        if (static_cast<int>(p) == gauge_pinned_perm_) {
                            pinned_mean = mean;
                        }
                    }
                }
                // P3.3 online gauge: pin on the first measurable detect
                // frame of the epoch; afterwards a persistent pinned-
                // gauge failure WHILE the best gauge passes is a
                // quarter-turn slip - identity is invalid from here, so
                // declare a new epoch and re-pin. Detections stand in
                // for ground truth: the same math as the offline P2.7
                // registration metric.
                // Turn rate hoisted to result.turn_deg (computed once
                // per tracked frame) - the quasi-static gate for gauge
                // decisions AND the P6.2 read grade.
                const double turn_deg = result.turn_deg < 0.0 ? 0.0 : result.turn_deg;
                if (turn_deg > kGaugeEvalMaxTurnDeg) {
                    gauge_settle_ = kGaugeSettleFrames;
                } else if (gauge_settle_ > 0) {
                    --gauge_settle_;
                }
                if (best >= 0.0 && diagonal > 1.0 &&
                    (turn_deg > kGaugeEvalMaxTurnDeg || gauge_settle_ > 0)) {
                    gauge_fail_streak_ = 0;  // undecidable mid-turn or settling; retry once static
                } else if (best >= 0.0 && diagonal > 1.0) {
                    if (gauge_slip_holdoff_ > 0) {
                        --gauge_slip_holdoff_;
                    }
                    if (gauge_pinned_perm_ < 0) {
                        gauge_pinned_perm_ = best_perm;
                        gauge_fail_streak_ = 0;
                    } else if (pinned_mean >= 0.0 && pinned_mean / diagonal > kHealthFailFrac &&
                               best / diagonal <= kHealthFailFrac) {
                        if (++gauge_fail_streak_ >= kHealthFailStreak) {
                            // DIAGNOSTIC ONLY - measured and demoted: the
                            // pinned alignment is VIEW-canonical, and a
                            // legitimate quarter-turn of the cube rotates
                            // the view-canonical frame itself, so the pin
                            // goes stale on every turn (the six-face
                            // sweep fired 3 declarations against 0 real
                            // slips, all mid-hold after turns). Deeper: a
                            // slipped track and a rotated view are the
                            // SAME geometric observation under the cube's
                            // 24-fold symmetry - online slip detection
                            // from geometry alone is impossible in
                            // principle. Identity protection against real
                            // slips belongs to CONTENT (the aggregator's
                            // cross-frame variance refuses a face whose
                            // reads changed colour mid-epoch; centre
                            // keying arbitrates at assembly). Epochs
                            // therefore come from ANCHORS ONLY.
                            if (gauge_slip_holdoff_ == 0) {
                                result.gauge_slip = true;
                            }
                            gauge_pinned_perm_ = best_perm;
                            gauge_fail_streak_ = 0;
                            gauge_slip_holdoff_ = kGaugeSlipHoldoff;
                        }
                    } else {
                        gauge_fail_streak_ = 0;
                    }
                    result.gauge_pinned_err = pinned_mean >= 0.0 ? pinned_mean / diagonal : -1.0;
                }
                if (best >= 0.0 && diagonal > 1.0) {
                    if (frames_since_health_ > kHealthGapFrames) {
                        frames_since_anchor_ = 0;  // gap voids maturity (see header)
                    }
                    frames_since_health_ = 0;
                    last_health_pass_ = best / diagonal <= kHealthFailFrac;
                    result.health_frac = best / diagonal;
                    if (result.health_frac > kHealthFailFrac) {
                        const int streak_needed = frames_since_anchor_ < kTrackMatureFrames
                                                      ? kHealthFailStreakYoung
                                                      : kHealthFailStreak;
                        if (++health_fail_streak_ >= streak_needed) {
                            live_ = false;
                            // A marginal-band drop is stitchable: the
                            // registration was approximately right, so
                            // the next anchor can relabel into this
                            // epoch's frame (see the anchor block).
                            if (result.health_frac <= kStitchableHealthFrac) {
                                drop_pose_valid_ = true;
                                drop_rvec_ = tracked.rvec;
                                frames_since_drop_ = 0;
                            } else {
                                drop_pose_valid_ = false;
                            }
                        }
                    } else {
                        health_fail_streak_ = 0;
                    }
                }
            }
        }
    }
    // P6.2 read grade (see header): only quasi-static, health-current
    // frames may feed reads downstream.
    result.read_grade = live_ && result.step_valid && result.turn_deg >= 0.0 &&
                        result.turn_deg <= kGaugeEvalMaxTurnDeg &&
                        frames_since_health_ <= kHealthGapFrames && last_health_pass_;
    if (live_ && result.step_valid) {
        prev_track_rvec_ = result.rvec;
        prev_track_rvec_valid_ = true;
    } else if (!live_) {
        prev_track_rvec_valid_ = false;
    }
    result.track_live = live_;
    result.gauge_epoch = gauge_epoch_;
    return result;
}

void DetectTrackLoop::reset() {
    if (tracker_ != nullptr) {
        tracker_->reset();
    }
    live_ = false;
    health_fail_streak_ = 0;
    reinits_ = 0;
    frames_since_detect_ = 0;
    gauge_pinned_perm_ = -1;
    gauge_epoch_ = 0;
    gauge_fail_streak_ = 0;
    gauge_slip_holdoff_ = 0;
    gauge_settle_ = 0;
    prev_track_rvec_valid_ = false;
    last_health_pass_ = false;
    drop_pose_valid_ = false;
    frames_since_drop_ = 0;
    stitch_relabel_ = {0, 1, 2, 3, 4, 5};
}

}  // namespace cube::vision::pipeline
