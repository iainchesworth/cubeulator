#pragma once

#include <cube/error.hpp>

#include <opencv2/core.hpp>

#include <cstddef>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace cube::vision {

struct HandDetectionCapability {
    bool model_file_present = false;
    bool execution_provider_available = false;
    bool user_enabled = false;

    [[nodiscard]] bool should_use_model() const noexcept {
        return model_file_present && execution_provider_available && user_enabled;
    }
};

// Pure/I/O-free: the caller resolves model_file_present via
// std::filesystem::exists() so this stays trivially unit-testable.
[[nodiscard]] HandDetectionCapability check_hand_detection_capability(
    bool model_file_present, const std::vector<std::string>& execution_providers,
    bool user_enabled);

// Whether a hand is present/occluding the frame - used to gate auto-advance
// (a hand in frame means the reading isn't trustworthy yet).
class HandPresenceDetector {
public:
    virtual ~HandPresenceDetector() = default;
    [[nodiscard]] virtual Result<bool> hand_present(const cv::Mat& bgr_frame) = 0;
};

// MVP fallback when no model is available or hand detection is disabled:
// always reports no hand, per the explicit "not critical path" scope - the
// rest of the pipeline (color confidence, stability) still gates
// auto-advance without it, just without this extra signal.
class HeuristicHandPresenceDetector final : public HandPresenceDetector {
public:
    [[nodiscard]] Result<bool> hand_present(const cv::Mat& bgr_frame) override;
};

// Runs the OpenCV Zoo MediaPipe palm-detector ONNX model (Apache-2.0,
// assets/models/palm_detection_mediapipe_2023feb_int8.onnx) to answer just
// "is at least one hand in frame" - not the full landmark/bounding-box
// output the model is capable of, since that's all auto-advance needs.
class OnnxHandPresenceDetector final : public HandPresenceDetector {
public:
    ~OnnxHandPresenceDetector() override;
    OnnxHandPresenceDetector(const OnnxHandPresenceDetector&) = delete;
    OnnxHandPresenceDetector& operator=(const OnnxHandPresenceDetector&) = delete;
    OnnxHandPresenceDetector(OnnxHandPresenceDetector&&) noexcept;
    OnnxHandPresenceDetector& operator=(OnnxHandPresenceDetector&&) noexcept;

    [[nodiscard]] static Result<std::unique_ptr<OnnxHandPresenceDetector>> create(
        const std::filesystem::path& model_path,
        const std::vector<std::string>& execution_providers);

    // For a model embedded as an application resource (e.g. a Qt .qrc)
    // rather than deployed as a loose file next to the executable - avoids
    // the whole "is the working directory / deployment step right" class
    // of problem a filesystem path depends on. data must stay valid only
    // for the duration of this call; onnxruntime copies what it needs.
    [[nodiscard]] static Result<std::unique_ptr<OnnxHandPresenceDetector>> create_from_memory(
        const void* model_data, std::size_t model_data_size,
        const std::vector<std::string>& execution_providers);

    [[nodiscard]] Result<bool> hand_present(const cv::Mat& bgr_frame) override;

private:
    struct Impl;
    explicit OnnxHandPresenceDetector(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};

// Tries the ONNX model when the capability check allows it, falling back to
// the heuristic detector if the model can't actually be loaded (a "present"
// file that turns out corrupt, an execution provider that rejects it,
// etc.) - the caller never has to branch on which one it got.
[[nodiscard]] std::unique_ptr<HandPresenceDetector> make_hand_presence_detector(
    const HandDetectionCapability& capability, const std::filesystem::path& model_path,
    const std::vector<std::string>& execution_providers);

// Same as make_hand_presence_detector(), for a model held in memory (e.g.
// loaded from an embedded application resource) rather than a filesystem
// path - see OnnxHandPresenceDetector::create_from_memory().
[[nodiscard]] std::unique_ptr<HandPresenceDetector> make_hand_presence_detector_from_memory(
    const HandDetectionCapability& capability, const void* model_data, std::size_t model_data_size,
    const std::vector<std::string>& execution_providers);

// Prefers a copy of the model under the platform's user-writable model
// directory (where a future installer/updater could place a newer one)
// over the copy bundled next to the executable.
[[nodiscard]] std::filesystem::path resolve_hand_model_path(
    const std::filesystem::path& user_model_search_path,
    const std::filesystem::path& bundled_model_path);

}  // namespace cube::vision
