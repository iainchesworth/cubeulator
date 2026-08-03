#include <cube/vision/pipeline/detect_track_loop.hpp>

#include <cube/vision/pose_estimation.hpp>

#include <opencv2/calib3d.hpp>

#include <cmath>
#include <vector>

namespace cube::vision::pipeline {

DetectTrackResult DetectTrackLoop::process(const cv::Mat& bgr_frame) {
    DetectTrackResult result;
    result.reinits = reinits_;
    if (!attached() || bgr_frame.empty()) {
        return result;
    }

    if (auto detected = detector_->detect(bgr_frame); detected.has_value()) {
        result.detection = *detected;
    }

    // Single-frame PnP whenever enough identified corners exist - the
    // anchor pose and the trace's agreement measurement.
    int confident_corners = 0;
    for (const auto& corner : result.detection.corners) {
        confident_corners += corner.confidence >= OnnxCornerDetector::kDefaultKeypointFloor ? 1 : 0;
    }
    if (result.detection.found && confident_corners >= 4) {
        if (auto solved = estimate_pose_from_corners(result.detection, bgr_frame.size()); solved.has_value()) {
            result.pnp = *solved;
        }
    }

    if (tracker_ == nullptr) {
        return result;  // detector-only mode
    }

    if (!live_ && result.pnp.has_value() && result.pnp->corners_used >= kInitMinCorners) {
        if (tracker_->init(result.pnp->rvec, result.pnp->tvec, bgr_frame.size(),
                           approximate_camera_matrix(bgr_frame.size()))) {
            live_ = true;
            health_fail_streak_ = 0;
            result.reinits = ++reinits_;
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
            std::vector<cv::Point3f> model(kSingmasterCornerModel.begin(), kSingmasterCornerModel.end());
            std::vector<cv::Point2f> projected;
            cv::projectPoints(model, tracked.rvec, tracked.tvec, approximate_camera_matrix(bgr_frame.size()),
                              cv::noArray(), projected);
            for (int i = 0; i < 8; ++i) {
                result.projected[static_cast<std::size_t>(i)] = projected[static_cast<std::size_t>(i)];
            }

            // Health against this frame's detections, minimised over the
            // 24 view-canonical rotations.
            if (result.detection.found && confident_corners >= 4) {
                const auto& box = result.detection.box;
                const double diagonal =
                    std::sqrt(static_cast<double>(box.width) * box.width + static_cast<double>(box.height) * box.height);
                double best = -1.0;
                for (const auto& perm : singmaster_rotation_permutations()) {
                    double total = 0.0;
                    int count = 0;
                    for (int id = 0; id < 8; ++id) {
                        const auto& corner = result.detection.corners[static_cast<std::size_t>(id)];
                        if (corner.confidence < OnnxCornerDetector::kDefaultKeypointFloor) {
                            continue;
                        }
                        const int tracked_id = perm[static_cast<std::size_t>(id)];
                        total += cv::norm(result.projected[static_cast<std::size_t>(tracked_id)] - corner.px);
                        ++count;
                    }
                    if (count >= 4) {
                        const double mean = total / count;
                        if (best < 0.0 || mean < best) {
                            best = mean;
                        }
                    }
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
    return result;
}

void DetectTrackLoop::reset() {
    if (tracker_ != nullptr) {
        tracker_->reset();
    }
    live_ = false;
    health_fail_streak_ = 0;
    reinits_ = 0;
}

}  // namespace cube::vision::pipeline
