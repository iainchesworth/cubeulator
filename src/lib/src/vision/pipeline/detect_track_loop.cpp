#include <cube/vision/pipeline/detect_track_loop.hpp>

#include <cube/vision/pipeline/pose_refiner.hpp>
#include <cube/vision/pose_estimation.hpp>

#include <opencv2/calib3d.hpp>

#include <cmath>
#include <vector>

namespace cube::vision::pipeline {

cv::Matx33d DetectTrackLoop::camera_matrix_for(cv::Size size) const {
    return intrinsics_.has_value() ? intrinsics_->scaled_to(size).matrix() : approximate_camera_matrix(size);
}

DetectTrackResult DetectTrackLoop::process(const cv::Mat& bgr_frame) {
    DetectTrackResult result;
    result.reinits = reinits_;
    if (!attached() || bgr_frame.empty()) {
        return result;
    }

    // Detector decimation applies only while live (a lost loop needs the
    // detector every frame to re-anchor).
    const bool run_detector = !live_ || live_detect_interval_ <= 1 || ++frames_since_detect_ >= live_detect_interval_;
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

    if (!live_ && result.pnp.has_value() && result.pnp->corners_used >= kInitMinCorners) {
        if (tracker_->init(result.pnp->rvec, result.pnp->tvec, bgr_frame.size(),
                           camera_matrix_for(bgr_frame.size()))) {
            live_ = true;
            health_fail_streak_ = 0;
            result.reinits = ++reinits_;
            // A fresh anchor is a fresh identity epoch: the model frame
            // is this frame's view-canonical orientation, and the gauge
            // re-pins on the next measurable detect frame (P3.3).
            ++gauge_epoch_;
            gauge_pinned_perm_ = -1;
            gauge_fail_streak_ = 0;
            gauge_slip_holdoff_ = 0;
        }
    }

    if (live_) {
        const auto tracked = tracker_->step(bgr_frame);
        if (!tracked.valid) {
            live_ = false;
        } else {
            result.step_valid = true;
            result.rvec = tracked.rvec;
            result.tvec = tracked.tvec;

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
            // Gated on THIS frame's detection having found the cube: a
            // track survives a full exit unmeasurable (no corners means
            // no health), and an ungated refiner then polishes
            // background clutter. (Live detector decimation dilutes
            // refine cadence to the detect frames; revisit at the P2.9
            // live gates if that shows.)
            //
            // Health below judges the RAW stepped pose, not the refined
            // one: health is a statement about the TRACKER's state, and
            // judging the polished output was measured to mask drift -
            // the loop stopped re-anchoring (gauge runs 2 -> 1 on the
            // synthetic free-rotation clip) and M3T drifted through an
            // extra quarter-turn slip. Downstream consumers get the
            // refined pose; the drop/re-anchor decision stays keyed to
            // what the tracker itself believes.
            if (refine_enabled_ && result.detection.found) {
                const auto refined =
                    refine_pose(bgr_frame, result.rvec, result.tvec, camera_matrix_for(bgr_frame.size()));
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

            std::vector<cv::Point3f> model(kSingmasterCornerModel.begin(), kSingmasterCornerModel.end());
            std::vector<cv::Point2f> projected;
            cv::projectPoints(model, result.rvec, result.tvec, camera_matrix_for(bgr_frame.size()),
                              cv::noArray(), projected);
            for (int i = 0; i < 8; ++i) {
                result.projected[static_cast<std::size_t>(i)] = projected[static_cast<std::size_t>(i)];
            }
            // Stage-D geometry from the same OUTPUT pose (P3.1): faces,
            // quads and sticker centres are projections of the tracked
            // model - the plan's "no more per-frame face hunting".
            result.faces = derive_faces(result.rvec, result.tvec, camera_matrix_for(bgr_frame.size()));

            // Raw-pose projection for the health check (see the refine
            // comment above); identical to result.projected on frames
            // the refiner left alone.
            std::array<cv::Point2f, 8> raw_projected = result.projected;
            if (result.refine_applied) {
                std::vector<cv::Point2f> raw;
                cv::projectPoints(model, tracked.rvec, tracked.tvec, camera_matrix_for(bgr_frame.size()),
                                  cv::noArray(), raw);
                for (int i = 0; i < 8; ++i) {
                    raw_projected[static_cast<std::size_t>(i)] = raw[static_cast<std::size_t>(i)];
                }
            }

            // Health against this frame's detections, minimised over the
            // 24 view-canonical rotations.
            if (result.detection.found && confident_corners >= 4) {
                const auto& box = result.detection.box;
                const double diagonal =
                    std::sqrt(static_cast<double>(box.width) * box.width + static_cast<double>(box.height) * box.height);
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
                        total += cv::norm(raw_projected[static_cast<std::size_t>(tracked_id)] - corner.px);
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
                if (best >= 0.0 && diagonal > 1.0) {
                    if (gauge_slip_holdoff_ > 0) {
                        --gauge_slip_holdoff_;
                    }
                    if (gauge_pinned_perm_ < 0) {
                        gauge_pinned_perm_ = best_perm;
                        gauge_fail_streak_ = 0;
                    } else if (pinned_mean >= 0.0 && pinned_mean / diagonal > kHealthFailFrac &&
                               best / diagonal <= kHealthFailFrac) {
                        if (++gauge_fail_streak_ >= kHealthFailStreak) {
                            // Within the hold-off this is the SAME drift:
                            // re-pin silently, no new epoch (see header).
                            if (gauge_slip_holdoff_ == 0) {
                                result.gauge_slip = true;
                                ++gauge_epoch_;
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
                    result.health_frac = best / diagonal;
                    if (result.health_frac > kHealthFailFrac) {
                        if (++health_fail_streak_ >= kHealthFailStreak) {
                            live_ = false;
                        }
                    } else {
                        health_fail_streak_ = 0;
                    }
                }
            }
        }
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
}

}  // namespace cube::vision::pipeline
