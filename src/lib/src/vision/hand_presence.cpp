#include <cube/vision/hand_presence.hpp>

#include <onnxruntime_cxx_api.h>

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

namespace cube::vision {

namespace {

constexpr int kModelInputSize = 192;
constexpr float kHandPresenceProbabilityThreshold = 0.5F;

// Resizes preserving aspect ratio and center-pads to a square, matching the
// reference preprocessing (opencv_zoo's mp_palmdet.py): the model was
// trained on letterboxed, not stretched, input.
[[nodiscard]] cv::Mat letterbox_to_square(const cv::Mat& bgr_frame, int size) {
    const float scale =
        static_cast<float>(size) / static_cast<float>(std::max(bgr_frame.cols, bgr_frame.rows));
    const int new_w = std::max(1, static_cast<int>(std::lround(bgr_frame.cols * scale)));
    const int new_h = std::max(1, static_cast<int>(std::lround(bgr_frame.rows * scale)));

    cv::Mat resized;
    cv::resize(bgr_frame, resized, cv::Size(new_w, new_h));

    cv::Mat padded = cv::Mat::zeros(size, size, bgr_frame.type());
    resized.copyTo(padded(cv::Rect((size - new_w) / 2, (size - new_h) / 2, new_w, new_h)));
    return padded;
}

// BGR uint8 -> RGB float32 in [0, 1], HWC layout matching the model's
// (1, 192, 192, 3) NHWC input.
[[nodiscard]] cv::Mat to_model_input(const cv::Mat& bgr_frame) {
    cv::Mat rgb;
    cv::cvtColor(letterbox_to_square(bgr_frame, kModelInputSize), rgb, cv::COLOR_BGR2RGB);
    cv::Mat float_image;
    rgb.convertTo(float_image, CV_32FC3, 1.0 / 255.0);
    return float_image;
}

}  // namespace

HandDetectionCapability check_hand_detection_capability(bool model_file_present,
                                                          const std::vector<std::string>& execution_providers,
                                                          bool user_enabled) {
    return HandDetectionCapability{
        .model_file_present = model_file_present,
        .execution_provider_available = !execution_providers.empty(),
        .user_enabled = user_enabled,
    };
}

Result<bool> HeuristicHandPresenceDetector::hand_present(const cv::Mat& /*bgr_frame*/) {
    return false;
}

struct OnnxHandPresenceDetector::Impl {
    // execution_providers isn't acted on yet: onnxruntime's DirectML
    // provider needs a vcpkg build that's confirmed to bundle it (unverified
    // - see docs/dependency-strategy.md's general posture on this
    // dependency), so this MVP runs on the CPU provider onnxruntime always
    // supports rather than risk a provider that may not be linked in.
    explicit Impl(const std::filesystem::path& model_path) : env(ORT_LOGGING_LEVEL_WARNING, "cube_hand_presence") {
        Ort::SessionOptions options;
        session = Ort::Session(env, model_path.c_str(), options);
        cache_io_names();
    }

    Impl(const void* model_data, std::size_t model_data_size)
        : env(ORT_LOGGING_LEVEL_WARNING, "cube_hand_presence") {
        Ort::SessionOptions options;
        session = Ort::Session(env, model_data, static_cast<size_t>(model_data_size), options);
        cache_io_names();
    }

    void cache_io_names() {
        Ort::AllocatorWithDefaultOptions allocator;
        for (std::size_t i = 0; i < session.GetInputCount(); ++i) {
            input_names.emplace_back(session.GetInputNameAllocated(i, allocator).get());
        }
        for (std::size_t i = 0; i < session.GetOutputCount(); ++i) {
            output_names.emplace_back(session.GetOutputNameAllocated(i, allocator).get());
        }
    }

    Ort::Env env;
    Ort::Session session{nullptr};
    std::vector<std::string> input_names;
    std::vector<std::string> output_names;
};

OnnxHandPresenceDetector::OnnxHandPresenceDetector(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
OnnxHandPresenceDetector::~OnnxHandPresenceDetector() = default;
OnnxHandPresenceDetector::OnnxHandPresenceDetector(OnnxHandPresenceDetector&&) noexcept = default;
OnnxHandPresenceDetector& OnnxHandPresenceDetector::operator=(OnnxHandPresenceDetector&&) noexcept = default;

Result<std::unique_ptr<OnnxHandPresenceDetector>> OnnxHandPresenceDetector::create(
    const std::filesystem::path& model_path, const std::vector<std::string>& /*execution_providers*/) {
    if (!std::filesystem::exists(model_path)) {
        return std::unexpected(CubeError::model_load_failed);
    }
    try {
        auto impl = std::make_unique<Impl>(model_path);
        return std::unique_ptr<OnnxHandPresenceDetector>(new OnnxHandPresenceDetector(std::move(impl)));
    } catch (const Ort::Exception&) {
        return std::unexpected(CubeError::model_load_failed);
    }
}

Result<std::unique_ptr<OnnxHandPresenceDetector>> OnnxHandPresenceDetector::create_from_memory(
    const void* model_data, std::size_t model_data_size, const std::vector<std::string>& /*execution_providers*/) {
    if (model_data == nullptr || model_data_size == 0) {
        return std::unexpected(CubeError::model_load_failed);
    }
    try {
        auto impl = std::make_unique<Impl>(model_data, model_data_size);
        return std::unique_ptr<OnnxHandPresenceDetector>(new OnnxHandPresenceDetector(std::move(impl)));
    } catch (const Ort::Exception&) {
        return std::unexpected(CubeError::model_load_failed);
    }
}

Result<bool> OnnxHandPresenceDetector::hand_present(const cv::Mat& bgr_frame) {
    if (bgr_frame.empty()) {
        return std::unexpected(CubeError::inference_failed);
    }

    const cv::Mat input_image = to_model_input(bgr_frame);
    const std::array<int64_t, 4> input_shape{1, kModelInputSize, kModelInputSize, 3};

    Ort::MemoryInfo memory_info = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
    Ort::Value input_tensor = Ort::Value::CreateTensor<float>(
        memory_info, reinterpret_cast<float*>(input_image.data),
        input_image.total() * static_cast<std::size_t>(input_image.channels()), input_shape.data(),
        input_shape.size());

    std::vector<const char*> input_name_ptrs;
    input_name_ptrs.reserve(impl_->input_names.size());
    for (const auto& name : impl_->input_names) {
        input_name_ptrs.push_back(name.c_str());
    }
    std::vector<const char*> output_name_ptrs;
    output_name_ptrs.reserve(impl_->output_names.size());
    for (const auto& name : impl_->output_names) {
        output_name_ptrs.push_back(name.c_str());
    }

    try {
        auto outputs = impl_->session.Run(Ort::RunOptions{nullptr}, input_name_ptrs.data(), &input_tensor, 1,
                                           output_name_ptrs.data(), output_name_ptrs.size());

        // The confidence tensor is whichever output has a trailing dimension
        // of 1 (the box+landmark tensor's trailing dimension is 18) - this
        // avoids depending on output ordering being stable across exports.
        const Ort::Value* score_tensor = nullptr;
        for (const auto& output : outputs) {
            const auto shape = output.GetTensorTypeAndShapeInfo().GetShape();
            if (!shape.empty() && shape.back() == 1) {
                score_tensor = &output;
                break;
            }
        }
        if (score_tensor == nullptr) {
            return std::unexpected(CubeError::inference_failed);
        }

        const float* scores = score_tensor->GetTensorData<float>();
        const std::size_t count = score_tensor->GetTensorTypeAndShapeInfo().GetElementCount();
        if (count == 0) {
            return false;
        }
        const float max_logit = *std::max_element(scores, scores + count);
        const float max_probability = 1.0F / (1.0F + std::exp(-max_logit));

        return max_probability > kHandPresenceProbabilityThreshold;
    } catch (const Ort::Exception&) {
        return std::unexpected(CubeError::inference_failed);
    }
}

std::unique_ptr<HandPresenceDetector> make_hand_presence_detector(
    const HandDetectionCapability& capability, const std::filesystem::path& model_path,
    const std::vector<std::string>& execution_providers) {
    if (capability.should_use_model()) {
        auto onnx_detector = OnnxHandPresenceDetector::create(model_path, execution_providers);
        if (onnx_detector.has_value()) {
            return std::move(*onnx_detector);
        }
    }
    return std::make_unique<HeuristicHandPresenceDetector>();
}

std::unique_ptr<HandPresenceDetector> make_hand_presence_detector_from_memory(
    const HandDetectionCapability& capability, const void* model_data, std::size_t model_data_size,
    const std::vector<std::string>& execution_providers) {
    if (capability.should_use_model()) {
        auto onnx_detector = OnnxHandPresenceDetector::create_from_memory(model_data, model_data_size,
                                                                            execution_providers);
        if (onnx_detector.has_value()) {
            return std::move(*onnx_detector);
        }
    }
    return std::make_unique<HeuristicHandPresenceDetector>();
}

std::filesystem::path resolve_hand_model_path(const std::filesystem::path& user_model_search_path,
                                               const std::filesystem::path& bundled_model_path) {
    if (!user_model_search_path.empty()) {
        const auto candidate = user_model_search_path / bundled_model_path.filename();
        if (std::filesystem::exists(candidate)) {
            return candidate;
        }
    }
    return bundled_model_path;
}

}  // namespace cube::vision
