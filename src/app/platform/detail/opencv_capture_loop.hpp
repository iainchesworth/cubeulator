#pragma once

#include "interfaces/camera.hpp"

#include <cube/error.hpp>

#include <opencv2/videoio.hpp>

#include <mutex>
#include <optional>
#include <stop_token>
#include <thread>
#include <vector>

namespace cube::platform::camera::detail {

// Shared cv::VideoCapture-based capture loop behind a single-slot "latest
// frame wins" mailbox: this is a live-preview use case, so dropping stale
// frames when the consumer is slower than capture is correct, and it avoids
// unbounded memory growth. Each desktop camera_<os>.cpp instantiates one of
// these with its platform's cv::VideoCaptureAPIs backend and forwards every
// interfaces/camera.hpp call to it.
class OpenCvCaptureLoop {
public:
    explicit OpenCvCaptureLoop(cv::VideoCaptureAPIs backend) noexcept;
    ~OpenCvCaptureLoop();

    OpenCvCaptureLoop(const OpenCvCaptureLoop&) = delete;
    OpenCvCaptureLoop& operator=(const OpenCvCaptureLoop&) = delete;
    OpenCvCaptureLoop(OpenCvCaptureLoop&&) = delete;
    OpenCvCaptureLoop& operator=(OpenCvCaptureLoop&&) = delete;

    [[nodiscard]] Result<std::vector<DeviceInfo>> enumerate_devices() const;
    [[nodiscard]] Result<void> start(const DeviceInfo& device);
    void stop();

    [[nodiscard]] std::optional<cv::Mat> try_pop_latest_frame();
    [[nodiscard]] bool is_capturing() const noexcept;

private:
    void run(std::stop_token stop_token);

    cv::VideoCaptureAPIs backend_;
    cv::VideoCapture capture_;
    std::jthread capture_thread_;
    std::mutex mailbox_mutex_;
    cv::Mat latest_frame_;
};

}  // namespace cube::platform::camera::detail
