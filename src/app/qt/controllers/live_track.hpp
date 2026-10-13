#pragma once

#include <cube/vision/pose_estimation.hpp>
#include <cube/vision/track_fusion.hpp>

#include <opencv2/core.hpp>

#include <optional>
#include <vector>

namespace cube::app::live_track {

// Everything a tracking pass hands back to ScanSession::analyze(): the pose
// while live, plus the raw translation and HUD facts either way - and, since
// P6.12, the capture surface: this pass's capture-grade tracked faces
// sampled to per-cell Lab (the runner's stage D-F composition, run live)
// with the identity facts the GUI-side fusion feed keys on.
struct Sample {
    std::optional<cube::vision::CubePoseProjection> pose;
    cv::Vec3d tvec{};
    double health_frac = -1.0;
    int reinits = 0;
    bool locked = false;   // the tracker was live and stepped this pass
    int gauge_epoch = -1;  // the loop's content-identity epoch
    std::vector<cube::vision::fusion::TrackedFaceRead> faces;
};

// Which vision pipeline the live app runs (P2.10, docs/pipeline-plan.md):
// the detect->track pipeline is the DEFAULT where the platform has a tracker;
// the legacy cascade is quarantined behind CUBE_VISION_PIPELINE=legacy (it
// remains the only scan-to-solve path until Phases 3-5 replace colour
// capture, and the offline runner's baselines still replay it). It also
// turns false, for good, if the corner model cannot load. Always false where
// the platform has no tracker.
[[nodiscard]] bool track_mode();

// The live continuous-lock pass (see the call site in analyze() for the
// contract). Worker-side state: single-in-flight analysis means no
// concurrent passes. Only meaningful while track_mode() is true.
[[nodiscard]] Sample step(const cv::Mat& analysis_frame, float scale_back);

// There is exactly one definition of this interface per build, chosen by the
// platform's fragment (src/app/platform/<os>/platform.cmake):
// live_track_m3t.cpp where the vendored M3T tracker exists (the desktops),
// live_track_none.cpp where it does not (the phones). The code that calls it
// contains no preprocessor branching.

}  // namespace cube::app::live_track
