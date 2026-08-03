#pragma once

// Phase 1 learned cube-body detector (docs/pipeline-plan.md): a
// YOLO11n-pose ONNX model that finds the cube content-agnostically and
// localises its 8 outer corners with per-corner confidence. This is the
// pipeline's stage-B primary (the classical spike failed the occlusion
// kill criterion: 26% real-occlusion detection vs the 90% bar; this model
// measured 92% TP on the same footage with zero off-cube boxes — see
// assets/models/cube_corner_pose_PROVENANCE.md for the full evidence).
//
// Corner ids are SINGMASTER order (0 URF, 1 UFL, 2 ULB, 3 UBR, 4 DFR,
// 5 DLF, 6 DBL, 7 DRB) — the ground-truth sidecar convention
// (docs/ground-truth-format.md); the implementation maps the model's
// internal keypoint slot order to it.
//
// CPU execution provider only for now (the vcpkg onnxruntime is the bare
// port — no DirectML; enabling it is a deliberate manifest change with
// its own measurements). Follows OnnxHandPresenceDetector's ownership
// pattern: one Env+Session per detector, load from file or memory,
// Result<> for every fallible operation.

#include <cube/error.hpp>

#include <opencv2/core.hpp>

#include <array>
#include <cstddef>
#include <filesystem>
#include <memory>

namespace cube::vision {

struct DetectedCorner {
    cv::Point2f px{};        // frame pixels (un-letterboxed)
    float confidence = 0.0F; // keypoint confidence (sigmoid)
};

struct CornerDetectionResult {
    bool found = false;       // best anchor cleared the score threshold
    float confidence = 0.0F;  // cube score of the best anchor
    cv::Rect2f box{};         // frame pixels, clipped to the frame
    // Indexed by Singmaster corner id. Corners the model places with low
    // confidence are still reported (with their confidence) - the caller
    // owns thresholds; kDefaultKeypointFloor is the measured-sane default.
    std::array<DetectedCorner, 8> corners{};
};

class OnnxCornerDetector {
public:
    static constexpr float kDefaultScoreThreshold = 0.25F;   // matches the eval protocol
    static constexpr float kDefaultKeypointFloor = 0.30F;    // per-corner display/consume floor

    [[nodiscard]] static Result<std::unique_ptr<OnnxCornerDetector>> create(const std::filesystem::path& model_path);
    [[nodiscard]] static Result<std::unique_ptr<OnnxCornerDetector>> create_from_memory(const void* model_data,
                                                                                        std::size_t model_data_size);

    ~OnnxCornerDetector();
    OnnxCornerDetector(OnnxCornerDetector&&) noexcept;
    OnnxCornerDetector& operator=(OnnxCornerDetector&&) noexcept;

    // One inference on a BGR frame of any size. Returns found=false (not
    // an error) when no anchor clears `score_threshold`.
    [[nodiscard]] Result<CornerDetectionResult> detect(const cv::Mat& bgr_frame,
                                                       float score_threshold = kDefaultScoreThreshold);

private:
    struct Impl;
    explicit OnnxCornerDetector(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};

namespace detail {

// The raw-tensor decode, exposed for the unit tests: given output0 data
// ([1, 29, anchors] row-major: rows 0-3 box cx,cy,w,h in letterbox px,
// row 4 cube score, rows 5..28 = 8 x (x, y, conf)), the letterbox
// parameters (scale r, left/top pad), and the original frame size,
// produce the best-anchor detection in frame pixels with keypoint slots
// mapped to Singmaster ids.
[[nodiscard]] CornerDetectionResult decode_pose_output(const float* data, int anchors, float score_threshold,
                                                       float scale, float pad_x, float pad_y, cv::Size frame);

// Model keypoint slot (synthdata binary corner order) -> Singmaster id.
inline constexpr std::array<int, 8> kSlotToSingmaster{6, 7, 2, 3, 5, 4, 1, 0};

}  // namespace detail

}  // namespace cube::vision
