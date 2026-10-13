#include <catch2/catch_test_macros.hpp>

#include <cube/vision/hand_presence.hpp>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <vector>

using namespace cube::vision;

namespace {

// cube_deploy_vision_assets() copies the model next to the test binary at
// build time (see cmake/VisionAssets.cmake); this is relative to the
// working directory ctest runs the binary from. If it isn't found - a
// direct invocation from a different directory, a checkout that hasn't
// built the deploy step yet - the tests below skip rather than fail, per
// the project's convention of graceful degradation over a hard CI failure
// for an environment-dependent asset.
const std::filesystem::path kModelPath = "models/palm_detection_mediapipe_2023feb_int8.onnx";

}  // namespace

TEST_CASE("OnnxHandPresenceDetector loads the bundled model and runs inference without crashing",
          "[integration][vision]") {
    if (!std::filesystem::exists(kModelPath)) {
        SKIP("Model asset not found at " << kModelPath
                                         << " relative to the test's working directory");
    }

    auto detector = OnnxHandPresenceDetector::create(kModelPath, {"CPU"});
    REQUIRE(detector.has_value());

    const cv::Mat blank_frame(480, 640, CV_8UC3, cv::Scalar(30, 30, 30));
    const auto result = (*detector)->hand_present(blank_frame);

    REQUIRE(result.has_value());
    // An empty, hand-free frame should not read as a confident detection.
    REQUIRE_FALSE(*result);
}

TEST_CASE("OnnxHandPresenceDetector::create reports model_load_failed for a missing file",
          "[integration][vision]") {
    const auto detector = OnnxHandPresenceDetector::create("does/not/exist.onnx", {"CPU"});

    REQUIRE_FALSE(detector.has_value());
    REQUIRE(detector.error() == cube::CubeError::model_load_failed);
}

TEST_CASE("OnnxHandPresenceDetector::create_from_memory loads a model held entirely in memory",
          "[integration][vision]") {
    if (!std::filesystem::exists(kModelPath)) {
        SKIP("Model asset not found at " << kModelPath
                                         << " relative to the test's working directory");
    }

    std::ifstream file(kModelPath, std::ios::binary);
    const std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(file)),
                                           std::istreambuf_iterator<char>());
    REQUIRE_FALSE(bytes.empty());

    auto detector =
        OnnxHandPresenceDetector::create_from_memory(bytes.data(), bytes.size(), {"CPU"});
    REQUIRE(detector.has_value());

    const cv::Mat blank_frame(480, 640, CV_8UC3, cv::Scalar(30, 30, 30));
    REQUIRE((*detector)->hand_present(blank_frame).has_value());
}

TEST_CASE(
    "OnnxHandPresenceDetector::create_from_memory reports model_load_failed for an empty buffer",
    "[integration][vision]") {
    const auto detector = OnnxHandPresenceDetector::create_from_memory(nullptr, 0, {"CPU"});

    REQUIRE_FALSE(detector.has_value());
    REQUIRE(detector.error() == cube::CubeError::model_load_failed);
}

TEST_CASE("make_hand_presence_detector uses the real model when the capability check passes",
          "[integration][vision]") {
    if (!std::filesystem::exists(kModelPath)) {
        SKIP("Model asset not found at " << kModelPath
                                         << " relative to the test's working directory");
    }

    const auto capability = check_hand_detection_capability(true, {"CPU"}, true);
    auto detector = make_hand_presence_detector(capability, kModelPath, {"CPU"});

    REQUIRE(detector != nullptr);
    const cv::Mat blank_frame(480, 640, CV_8UC3, cv::Scalar(30, 30, 30));
    REQUIRE(detector->hand_present(blank_frame).has_value());
}
