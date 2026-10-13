// The learned corner detector's decode contract (Phase 1, stage B): the
// raw-tensor decode is pinned against hand-crafted tensors so the C++
// implementation of the documented interface (assets/models/
// cube_corner_pose_PROVENANCE.md) cannot drift silently, and the
// slot->Singmaster mapping is locked. Real-model inference is a hidden
// env-gated smoke ([.detector-smoke], CUBE_CORNER_MODEL=<onnx>).
// std::getenv is fine here (single-threaded gated harness, read-only);
// silenced the same way the other env-gated test files do.
#ifdef _MSC_VER
#pragma warning(disable : 4996)
#endif

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cube/vision/corner_detector.hpp>

#include <opencv2/core.hpp>

#include <array>
#include <cstdlib>
#include <vector>

using namespace cube::vision;
using Catch::Matchers::WithinAbs;

namespace {

// A [29 x anchors] row-major tensor with every value 0 except anchor
// `hot`, which carries a box at (cx,cy,w,h), score, and 8 keypoints laid
// out per the interface doc.
std::vector<float> craft_tensor(int anchors, int hot, float cx, float cy, float w, float h,
                                float score) {
    std::vector<float> data(static_cast<std::size_t>(29) * static_cast<std::size_t>(anchors), 0.0F);
    const auto at = [&](int row, int a) -> float& {
        return data[static_cast<std::size_t>(row) * static_cast<std::size_t>(anchors) +
                    static_cast<std::size_t>(a)];
    };
    at(0, hot) = cx;
    at(1, hot) = cy;
    at(2, hot) = w;
    at(3, hot) = h;
    at(4, hot) = score;
    for (int slot = 0; slot < 8; ++slot) {
        at(5 + slot * 3, hot) = cx + static_cast<float>(slot);  // distinct x per slot
        at(6 + slot * 3, hot) = cy + 2.0F * static_cast<float>(slot);
        at(7 + slot * 3, hot) = 0.9F;
    }
    return data;
}

}  // namespace

TEST_CASE("decode maps the best anchor through the letterbox to frame pixels", "[detector]") {
    // Letterbox: scale 0.5, pad (0, 80) - a 1280x960 frame into 640x640.
    const auto data = craft_tensor(16, 7, /*cx=*/320.0F, /*cy=*/280.0F, /*w=*/100.0F, /*h=*/60.0F,
                                   /*score=*/0.9F);
    const auto result =
        detail::decode_pose_output(data.data(), 16, 0.25F, /*scale=*/0.5F, /*pad_x=*/0.0F,
                                   /*pad_y=*/80.0F, cv::Size(1280, 960));
    REQUIRE(result.found);
    CHECK_THAT(result.confidence, WithinAbs(0.9, 1e-6));
    // Box: letterbox (270..370, 250..310) -> frame ((270-0)/0.5, (250-80)/0.5) = (540, 340) to
    // (740, 460).
    CHECK_THAT(result.box.x, WithinAbs(540.0, 1e-3));
    CHECK_THAT(result.box.y, WithinAbs(340.0, 1e-3));
    CHECK_THAT(result.box.width, WithinAbs(200.0, 1e-3));
    CHECK_THAT(result.box.height, WithinAbs(120.0, 1e-3));
    // Keypoint slot 0 sits at (320, 280) letterbox -> (640, 400) frame.
    // Slot 0 = binary (-,-,-) relabels to Singmaster 6 (DBL) - a
    // NUMBERING conversion within the view-canonical frame, not a
    // body-frame claim (see the header).
    const auto& slot0 = result.corners[6];
    CHECK_THAT(slot0.px.x, WithinAbs(640.0, 1e-3));
    CHECK_THAT(slot0.px.y, WithinAbs(400.0, 1e-3));
    CHECK_THAT(slot0.confidence, WithinAbs(0.9, 1e-6));
    // Slot 7 = binary (+,+,+) relabels to Singmaster 0 (URF):
    // x = cx+7 -> (327-0)/0.5 = 654.
    CHECK_THAT(result.corners[0].px.x, WithinAbs(654.0, 1e-3));
    CHECK_THAT(result.corners[0].px.y, WithinAbs(428.0, 1e-3));
}

TEST_CASE("decode refuses when no anchor clears the threshold", "[detector]") {
    const auto data = craft_tensor(8, 3, 100.0F, 100.0F, 50.0F, 50.0F, /*score=*/0.2F);
    const auto result =
        detail::decode_pose_output(data.data(), 8, 0.25F, 1.0F, 0.0F, 0.0F, cv::Size(640, 640));
    CHECK_FALSE(result.found);
    CHECK(result.confidence == 0.0F);
}

TEST_CASE("the slot relabel is the binary->Singmaster coordinate map", "[detector]") {
    // Locked as the NUMBERING conversion (slot k's binary coordinates
    // name the Singmaster corner below) and nothing more: ids stay
    // view-canonical up to a cube rotation, which downstream consumers
    // resolve (best-rotation metrics, tracker continuity). History, so
    // the next reader doesn't repeat it: a body-frame remap and an
    // identity map were each tried; the first is unlearnable-by-
    // construction for a symmetric object, the second mixes numbering
    // conventions - both were caught by the corner metric.
    static constexpr std::array<int, 8> kExpected{6, 7, 2, 3, 5, 4, 1, 0};
    CHECK(detail::kSlotToSingmaster == kExpected);
}

TEST_CASE("real model loads and runs one inference", "[.detector-smoke]") {
    const char* model = std::getenv("CUBE_CORNER_MODEL");
    if (model == nullptr) {
        SKIP("CUBE_CORNER_MODEL not set - point it at assets/models/cube_corner_pose.onnx");
    }
    auto detector = OnnxCornerDetector::create(model);
    REQUIRE(detector.has_value());
    const cv::Mat frame(480, 640, CV_8UC3, cv::Scalar(60, 60, 60));
    const auto result = (*detector)->detect(frame);
    REQUIRE(result.has_value());
    // A flat grey frame must not produce a cube (the absent-clip FP rate
    // measured 0/383; a hallucination here is a regression).
    CHECK_FALSE(result->found);
}
