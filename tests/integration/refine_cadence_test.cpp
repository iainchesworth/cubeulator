// Refine cadence follows LIVE frames, not DETECT frames (P6.14).
//
// The live app decimates the ONNX detector while locked
// (ScanSession: set_live_detect_interval(3)) because the detector costs
// tens of milliseconds and the tracking step costs one or two. Everything
// downstream of the loop - derived faces, cell sampling, fusion - reads
// the loop's OUTPUT pose, so a refiner gated on THIS frame's detection
// polishes one fed observation in three and samples the other two from an
// unrefined, drifted pose. That is measured, not theoretical: on the
// live-demo retry clip the gated refiner produced 150 refinements and 3
// converged faces, the ungated one 468 and 6, with anchors and epochs
// unchanged.
//
// So this test holds the loop at the app's decimation and asserts the
// refiner measured the image on EVERY live frame, including the ones the
// detector never saw. Real detector, real committed capture frame, and a
// stand-in tracker that re-serves the anchor pose - with the pose held
// fixed, the only thing the loop can vary is WHICH frames it refines on.
//
// It lives in the integration suite because it needs the deployed ONNX
// model beside the binary (cube_deploy_vision_assets + the suite's
// WORKING_DIRECTORY), which the fast unit suite deliberately has not got.

#include <cube/vision/corner_detector.hpp>
#include <cube/vision/pipeline/detect_track_loop.hpp>
#include <cube/vision/pipeline/pose_tracker.hpp>

#include <opencv2/imgcodecs.hpp>

#include <catch2/catch_test_macros.hpp>

#include <filesystem>

namespace {

using cube::vision::OnnxCornerDetector;
using cube::vision::pipeline::DetectTrackLoop;
using cube::vision::pipeline::PoseTracker;
using cube::vision::pipeline::TrackedPose;

// Serves back the pose it was anchored at, every step. A real tracker's
// own drift would blur the question this test asks.
class FixedPoseTracker final : public PoseTracker {
public:
    bool init(const cv::Vec3d& rvec, const cv::Vec3d& tvec, cv::Size /*frame_size*/,
              const cv::Matx33d& /*camera_matrix*/) override {
        rvec_ = rvec;
        tvec_ = tvec;
        initialized_ = true;
        return true;
    }
    TrackedPose step(const cv::Mat& /*bgr_frame*/) override { return {initialized_, rvec_, tvec_}; }
    void reset() override { initialized_ = false; }
    [[nodiscard]] bool initialized() const override { return initialized_; }

private:
    cv::Vec3d rvec_{};
    cv::Vec3d tvec_{};
    bool initialized_ = false;
};

// Deployed beside the test binary; the suite runs from there.
const std::filesystem::path kModelPath = "models/cube_corner_pose.onnx";
// A real-webcam frame the detector anchors on (score 0.93). Local only:
// webcam frames show the person holding the cube and are not committed.
const std::filesystem::path kFramePath =
    std::filesystem::path(CUBE_TEST_DATA_DIR) / "captures" / "daylight" / "debug" / "frame_analysis_0.png";

}  // namespace

TEST_CASE("pose refinement runs on every live frame, not only detector frames", "[pipeline][refine]") {
    if (!std::filesystem::exists(kModelPath)) {
        SKIP("corner model not deployed beside the test binary");
    }
    if (!std::filesystem::exists(kFramePath)) {
        SKIP("capture frame not present: webcam frames stay out of the repository");
    }
    auto detector = OnnxCornerDetector::create(kModelPath);
    REQUIRE(detector.has_value());
    const cv::Mat frame = cv::imread(kFramePath.string(), cv::IMREAD_COLOR);
    REQUIRE_FALSE(frame.empty());

    FixedPoseTracker tracker;
    DetectTrackLoop loop;
    loop.attach((*detector).get(), &tracker);
    loop.set_live_detect_interval(3);  // the live app's decimation

    int live = 0;
    int detect_frames = 0;
    int detector_skipped = 0;
    int measured = 0;
    int measured_on_skipped = 0;
    for (int i = 0; i < 10; ++i) {
        const auto result = loop.process(frame);
        if (!result.track_live || !result.step_valid) {
            continue;
        }
        ++live;
        // The frame always detects, so a found=false result IS a frame
        // the decimation kept the detector off.
        (result.detection.found ? detect_frames : detector_skipped) += 1;
        if (result.refine_matches > 0) {
            ++measured;
            measured_on_skipped += result.detection.found ? 0 : 1;
        }
    }

    // The premise: the track anchored, and decimation really did keep the
    // detector off most live frames.
    REQUIRE(live > 0);
    CHECK(detect_frames > 0);
    CHECK(detector_skipped > 0);
    // The pin: refinement cadence is the LIVE frame rate, not the detect
    // frame rate. A gate on result.detection.found leaves the skipped
    // frames unmeasured, and those frames still feed reads downstream.
    CHECK(measured == live);
    CHECK(measured_on_skipped == detector_skipped);
}
