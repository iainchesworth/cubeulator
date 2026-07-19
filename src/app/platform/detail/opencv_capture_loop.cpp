#include "platform/detail/opencv_capture_loop.hpp"

#include <exception>
#include <string>
#include <utility>

#if defined(_WIN32)
#include <objbase.h>
#endif

namespace cube::platform::camera::detail {

namespace {

#if defined(_WIN32)
// This file's Windows-only cv::VideoCaptureAPIs backends (MSMF/DSHOW)
// require COM initialized on whichever thread calls into them - not
// something OpenCV can do on our behalf, since it doesn't control thread
// creation. The GUI thread gets this for free from Qt's own startup, but
// enumerate_devices() is deliberately run off it (via QtConcurrent, to
// avoid freezing the UI for the ~15-20s a real hardware probe takes) on a
// Qt thread-pool thread that has no COM initialization of its own.
//
// MTA (COINIT_MULTITHREADED), not STA: MSMF's cap_msmf.cpp delivers frames
// through an async IMFSourceReader callback (SourceReaderCB in its own
// logs) whose completion needs to be marshaled back to the calling
// thread's COM apartment. STA marshaling relies on that thread pumping a
// Windows message loop to receive it - which a QtConcurrent worker thread
// never does - so releasing a real (non-empty-slot) capture opened under
// STA on such a thread reliably stalled for ~14s per device (confirmed
// via cap_msmf's own SourceReaderCB teardown log timestamps) until
// whatever internal timeout eventually recovered it, and the process
// crashed shortly after. MTA doesn't need message-pump-based marshaling
// for calls within the same apartment, which covers this case.
class ScopedCom {
public:
    ScopedCom() : hr_(CoInitializeEx(nullptr, COINIT_MULTITHREADED)) {}
    ~ScopedCom() {
        if (SUCCEEDED(hr_)) {
            CoUninitialize();
        }
    }

    ScopedCom(const ScopedCom&) = delete;
    ScopedCom& operator=(const ScopedCom&) = delete;
    ScopedCom(ScopedCom&&) = delete;
    ScopedCom& operator=(ScopedCom&&) = delete;

private:
    HRESULT hr_;
};
#else
class ScopedCom {};
#endif

}  // namespace

OpenCvCaptureLoop::OpenCvCaptureLoop(cv::VideoCaptureAPIs backend) noexcept : backend_(backend) {}

OpenCvCaptureLoop::~OpenCvCaptureLoop() {
    stop();
}

Result<std::vector<DeviceInfo>> OpenCvCaptureLoop::enumerate_devices() const {
    const ScopedCom scoped_com;

    // A simple index probe: friendly device names aren't reliably available
    // across OpenCV's desktop backends, so devices are identified by index.
    constexpr int kMaxProbeIndex = 8;
    constexpr int kMaxConsecutiveMisses = 2;

    std::vector<DeviceInfo> devices;
    int consecutive_misses = 0;
    for (int index = 0; index <= kMaxProbeIndex && consecutive_misses < kMaxConsecutiveMisses; ++index) {
        cv::VideoCapture probe(index, backend_);
        if (probe.isOpened()) {
            devices.push_back(DeviceInfo{.identifier = std::to_string(index),
                                          .display_name = "Camera " + std::to_string(index)});
            consecutive_misses = 0;
        } else {
            ++consecutive_misses;
        }
        probe.release();
    }
    return devices;
}

Result<void> OpenCvCaptureLoop::start(const DeviceInfo& device) {
    const ScopedCom scoped_com;

    if (is_capturing()) {
        return std::unexpected(CubeError::camera_start_failed);
    }

    int device_index = -1;
    try {
        device_index = std::stoi(device.identifier);
    } catch (const std::exception&) {
        return std::unexpected(CubeError::camera_not_found);
    }

    if (!capture_.open(device_index, backend_)) {
        return std::unexpected(CubeError::camera_start_failed);
    }

    {
        std::scoped_lock lock(mailbox_mutex_);
        latest_frame_ = cv::Mat();
    }

    capture_thread_ = std::jthread([this](std::stop_token token) { run(token); });
    return {};
}

void OpenCvCaptureLoop::stop() {
    if (capture_thread_.joinable()) {
        capture_thread_.request_stop();
        capture_thread_.join();
    }
    capture_.release();
}

std::optional<cv::Mat> OpenCvCaptureLoop::try_pop_latest_frame() {
    std::scoped_lock lock(mailbox_mutex_);
    if (latest_frame_.empty()) {
        return std::nullopt;
    }
    return std::exchange(latest_frame_, cv::Mat());
}

bool OpenCvCaptureLoop::is_capturing() const noexcept {
    return capture_thread_.joinable();
}

void OpenCvCaptureLoop::run(std::stop_token stop_token) {
    const ScopedCom scoped_com;

    cv::Mat frame;
    while (!stop_token.stop_requested()) {
        if (!capture_.read(frame) || frame.empty()) {
            continue;
        }
        std::scoped_lock lock(mailbox_mutex_);
        latest_frame_ = frame.clone();
    }
}

}  // namespace cube::platform::camera::detail
