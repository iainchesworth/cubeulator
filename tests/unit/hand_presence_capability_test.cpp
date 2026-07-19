#include <catch2/catch_test_macros.hpp>

#include <cube/vision/hand_presence.hpp>

#include <opencv2/core.hpp>

using namespace cube::vision;

TEST_CASE("HandDetectionCapability::should_use_model requires every gate", "[vision]") {
    REQUIRE(HandDetectionCapability{.model_file_present = true,
                                     .execution_provider_available = true,
                                     .user_enabled = true}
                .should_use_model());

    REQUIRE_FALSE(HandDetectionCapability{.model_file_present = false,
                                           .execution_provider_available = true,
                                           .user_enabled = true}
                      .should_use_model());
    REQUIRE_FALSE(HandDetectionCapability{.model_file_present = true,
                                           .execution_provider_available = false,
                                           .user_enabled = true}
                      .should_use_model());
    REQUIRE_FALSE(HandDetectionCapability{.model_file_present = true,
                                           .execution_provider_available = true,
                                           .user_enabled = false}
                      .should_use_model());
}

TEST_CASE("check_hand_detection_capability derives execution_provider_available from the provider list",
          "[vision]") {
    const auto with_providers =
        check_hand_detection_capability(true, std::vector<std::string>{"CPU"}, true);
    REQUIRE(with_providers.execution_provider_available);

    const auto without_providers = check_hand_detection_capability(true, std::vector<std::string>{}, true);
    REQUIRE_FALSE(without_providers.execution_provider_available);
}

TEST_CASE("HeuristicHandPresenceDetector always reports no hand present", "[vision]") {
    HeuristicHandPresenceDetector detector;
    const cv::Mat frame(120, 160, CV_8UC3, cv::Scalar(0, 0, 0));

    const auto result = detector.hand_present(frame);

    REQUIRE(result.has_value());
    REQUIRE_FALSE(*result);
}

TEST_CASE("make_hand_presence_detector falls back to heuristic when the capability check fails",
          "[vision]") {
    const HandDetectionCapability disabled{
        .model_file_present = false, .execution_provider_available = true, .user_enabled = true};

    auto detector = make_hand_presence_detector(disabled, "does/not/exist.onnx", {"CPU"});

    REQUIRE(detector != nullptr);
    const cv::Mat frame(120, 160, CV_8UC3, cv::Scalar(0, 0, 0));
    const auto result = detector->hand_present(frame);
    REQUIRE(result.has_value());
    REQUIRE_FALSE(*result);
}

TEST_CASE("resolve_hand_model_path falls back to the bundled path when the user path is empty or missing",
          "[vision]") {
    const std::filesystem::path bundled = "models/palm_detection_mediapipe_2023feb_int8.onnx";

    REQUIRE(resolve_hand_model_path("", bundled) == bundled);
    REQUIRE(resolve_hand_model_path("does/not/exist", bundled) == bundled);
}
