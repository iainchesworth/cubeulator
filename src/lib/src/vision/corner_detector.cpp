#include <cube/vision/corner_detector.hpp>

#include <onnxruntime_cxx_api.h>

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>
#include <vector>

namespace cube::vision {

namespace {

constexpr int kInputSize = 640;
constexpr int kOutputRows = 29;  // 4 box + 1 score + 8 * (x, y, conf)

struct Letterbox {
    cv::Mat image;      // kInputSize x kInputSize BGR
    float scale = 1.0F; // original * scale = letterboxed
    float pad_x = 0.0F; // left pad in letterbox px
    float pad_y = 0.0F; // top pad
};

// Ultralytics LetterBox, exactly: r = min(size/h, size/w), symmetric pad
// of value 114, the +-0.1 rounding trick on the split so odd pads land
// the same pixels as training-time preprocessing.
Letterbox letterbox(const cv::Mat& bgr, int size) {
    Letterbox out;
    out.scale = std::min(static_cast<float>(size) / static_cast<float>(bgr.rows),
                         static_cast<float>(size) / static_cast<float>(bgr.cols));
    const int new_w = std::max(1, static_cast<int>(std::lround(bgr.cols * out.scale)));
    const int new_h = std::max(1, static_cast<int>(std::lround(bgr.rows * out.scale)));
    const float dw = static_cast<float>(size - new_w) / 2.0F;
    const float dh = static_cast<float>(size - new_h) / 2.0F;
    out.pad_x = dw;
    out.pad_y = dh;

    cv::Mat resized;
    cv::resize(bgr, resized, cv::Size(new_w, new_h), 0.0, 0.0, cv::INTER_LINEAR);
    out.image = cv::Mat(size, size, CV_8UC3, cv::Scalar(114, 114, 114));
    const int left = static_cast<int>(std::lround(dw - 0.1F));
    const int top = static_cast<int>(std::lround(dh - 0.1F));
    resized.copyTo(out.image(cv::Rect(left, top, new_w, new_h)));
    return out;
}

}  // namespace

namespace detail {

CornerDetectionResult decode_pose_output(const float* data, int anchors, float score_threshold, float scale,
                                         float pad_x, float pad_y, cv::Size frame) {
    CornerDetectionResult result;
    const auto row = [&](int r) { return data + static_cast<std::ptrdiff_t>(r) * anchors; };

    int best = -1;
    float best_score = score_threshold;
    const float* scores = row(4);
    for (int a = 0; a < anchors; ++a) {
        if (scores[a] > best_score) {
            best_score = scores[a];
            best = a;
        }
    }
    if (best < 0) {
        return result;
    }
    result.found = true;
    result.confidence = best_score;

    const auto to_frame = [&](float x, float y) {
        return cv::Point2f((x - pad_x) / scale, (y - pad_y) / scale);
    };
    const float cx = row(0)[best];
    const float cy = row(1)[best];
    const float w = row(2)[best];
    const float h = row(3)[best];
    const cv::Point2f tl = to_frame(cx - w / 2.0F, cy - h / 2.0F);
    const cv::Point2f br = to_frame(cx + w / 2.0F, cy + h / 2.0F);
    const cv::Rect2f frame_rect(0.0F, 0.0F, static_cast<float>(frame.width), static_cast<float>(frame.height));
    result.box = cv::Rect2f(tl, br) & frame_rect;

    for (int slot = 0; slot < 8; ++slot) {
        const int base = 5 + slot * 3;
        DetectedCorner corner;
        corner.px = to_frame(row(base)[best], row(base + 1)[best]);
        corner.confidence = row(base + 2)[best];
        result.corners[static_cast<std::size_t>(kSlotToSingmaster[static_cast<std::size_t>(slot)])] = corner;
    }
    return result;
}

}  // namespace detail

struct OnnxCornerDetector::Impl {
    explicit Impl(const std::filesystem::path& model_path) : env(ORT_LOGGING_LEVEL_WARNING, "cube_corner_pose") {
        Ort::SessionOptions options;
        session = Ort::Session(env, model_path.c_str(), options);
        cache_io_names();
    }

    Impl(const void* model_data, std::size_t model_data_size)
        : env(ORT_LOGGING_LEVEL_WARNING, "cube_corner_pose") {
        Ort::SessionOptions options;
        session = Ort::Session(env, model_data, model_data_size, options);
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

OnnxCornerDetector::OnnxCornerDetector(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
OnnxCornerDetector::~OnnxCornerDetector() = default;
OnnxCornerDetector::OnnxCornerDetector(OnnxCornerDetector&&) noexcept = default;
OnnxCornerDetector& OnnxCornerDetector::operator=(OnnxCornerDetector&&) noexcept = default;

Result<std::unique_ptr<OnnxCornerDetector>> OnnxCornerDetector::create(const std::filesystem::path& model_path) {
    if (!std::filesystem::exists(model_path)) {
        return std::unexpected(CubeError::model_load_failed);
    }
    try {
        auto impl = std::make_unique<Impl>(model_path);
        return std::unique_ptr<OnnxCornerDetector>(new OnnxCornerDetector(std::move(impl)));
    } catch (const Ort::Exception&) {
        return std::unexpected(CubeError::model_load_failed);
    }
}

Result<std::unique_ptr<OnnxCornerDetector>> OnnxCornerDetector::create_from_memory(const void* model_data,
                                                                                   std::size_t model_data_size) {
    if (model_data == nullptr || model_data_size == 0) {
        return std::unexpected(CubeError::model_load_failed);
    }
    try {
        auto impl = std::make_unique<Impl>(model_data, model_data_size);
        return std::unique_ptr<OnnxCornerDetector>(new OnnxCornerDetector(std::move(impl)));
    } catch (const Ort::Exception&) {
        return std::unexpected(CubeError::model_load_failed);
    }
}

Result<CornerDetectionResult> OnnxCornerDetector::detect(const cv::Mat& bgr_frame, float score_threshold) {
    if (bgr_frame.empty() || bgr_frame.type() != CV_8UC3) {
        return std::unexpected(CubeError::inference_failed);
    }

    const Letterbox boxed = letterbox(bgr_frame, kInputSize);
    cv::Mat rgb;
    cv::cvtColor(boxed.image, rgb, cv::COLOR_BGR2RGB);
    cv::Mat float_image;
    rgb.convertTo(float_image, CV_32FC3, 1.0 / 255.0);

    // HWC -> CHW, batch 1.
    std::vector<float> input(static_cast<std::size_t>(3) * kInputSize * kInputSize);
    std::vector<cv::Mat> channels;
    for (int c = 0; c < 3; ++c) {
        channels.emplace_back(kInputSize, kInputSize, CV_32FC1,
                              input.data() + static_cast<std::ptrdiff_t>(c) * kInputSize * kInputSize);
    }
    cv::split(float_image, channels);

    try {
        const std::array<std::int64_t, 4> input_shape{1, 3, kInputSize, kInputSize};
        Ort::MemoryInfo memory_info = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
        Ort::Value input_tensor = Ort::Value::CreateTensor<float>(memory_info, input.data(), input.size(),
                                                                  input_shape.data(), input_shape.size());
        const char* input_name = impl_->input_names.front().c_str();
        const char* output_name = impl_->output_names.front().c_str();
        auto outputs = impl_->session.Run(Ort::RunOptions{nullptr}, &input_name, &input_tensor, 1, &output_name, 1);

        const auto info = outputs.front().GetTensorTypeAndShapeInfo();
        const auto shape = info.GetShape();
        if (shape.size() != 3 || shape[1] != kOutputRows) {
            return std::unexpected(CubeError::inference_failed);
        }
        const int anchors = static_cast<int>(shape[2]);
        const float* data = outputs.front().GetTensorData<float>();
        return detail::decode_pose_output(data, anchors, score_threshold, boxed.scale, boxed.pad_x, boxed.pad_y,
                                          bgr_frame.size());
    } catch (const Ort::Exception&) {
        return std::unexpected(CubeError::inference_failed);
    }
}

}  // namespace cube::vision
