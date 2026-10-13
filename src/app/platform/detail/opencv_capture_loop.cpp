#include "platform/detail/opencv_capture_loop.hpp"

#include <chrono>
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

// How long the automatics are given to converge before a pending lock is
// applied. ~20 frames is a third of a second at 30fps - long enough for a
// UVC auto-exposure loop to settle after open (the probe used 12 settling
// frames per property change and saw clean, repeatable readings), short
// enough that the first face of a scan is still captured under the locked
// photometry. Frames are delivered normally throughout.
constexpr int kPhotometrySettleFrames = 20;

OpenCvCaptureLoop::OpenCvCaptureLoop(cv::VideoCaptureAPIs backend, Photometry photometry) noexcept
    : backend_(backend), photometry_(photometry) {}

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
    for (int index = 0; index <= kMaxProbeIndex && consecutive_misses < kMaxConsecutiveMisses;
         ++index) {
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
        latest_ = TimedFrame{};
    }

    {
        // A previous session's outcome must not be readable as this one's,
        // and a request that arrived while nothing was capturing must not
        // be applied to a device it was never meant for.
        std::scoped_lock lock(photometry_mutex_);
        pending_lock_.reset();
        pending_restore_ = false;
        settle_frames_remaining_ = 0;
        report_.reset();
        as_found_ = PhotometrySnapshot{};
    }

    capture_thread_ = std::jthread([this](std::stop_token token) { run(token); });
    return {};
}

void OpenCvCaptureLoop::stop() {
    if (capture_thread_.joinable()) {
        capture_thread_.request_stop();
        capture_thread_.join();
    }

    // After the join, this thread is the only one that can touch capture_,
    // so the same "never mid-grab" rule that sends locks through the
    // capture thread is satisfied here by the join itself.
    //
    // Restoring is not optional politeness: DirectShow property state lives
    // in the driver and outlives the process (the probe measured a fresh
    // cv::VideoCapture reading back the previous run's exposure and
    // brightness). Skipping this would leave every other camera app on the
    // machine - and this app's own next launch - running under a scan's
    // locked exposure.
    restore(capture_, as_found_);

    capture_.release();
}

std::optional<TimedFrame> OpenCvCaptureLoop::try_pop_latest_frame() {
    std::scoped_lock lock(mailbox_mutex_);
    if (latest_.frame.empty()) {
        return std::nullopt;
    }
    return std::exchange(latest_, TimedFrame{});
}

bool OpenCvCaptureLoop::is_capturing() const noexcept {
    return capture_thread_.joinable();
}

bool OpenCvCaptureLoop::photometry_supported() const noexcept {
    return photometry_ == Photometry::supported;
}

Result<void> OpenCvCaptureLoop::request_photometry_lock(const PhotometryLockRequest& request) {
    if (!is_capturing()) {
        return std::unexpected(CubeError::camera_control_unavailable);
    }

    // An unsupported backend still answers with a report rather than
    // nothing at all: the caller's job is to tell the user whether the scan
    // is running under a locked camera, and "this platform cannot" is an
    // answer they need just as much as "the driver refused". Published
    // straight from this thread - no device call is involved, so there is
    // nothing here the capture thread has to serialise.
    if (!photometry_supported()) {
        std::scoped_lock lock(photometry_mutex_);
        report_ = PhotometryLockReport{};
        return {};
    }

    std::scoped_lock lock(photometry_mutex_);
    pending_lock_ = request;
    pending_restore_ = false;
    settle_frames_remaining_ = kPhotometrySettleFrames;
    return {};
}

Result<void> OpenCvCaptureLoop::request_photometry_restore() {
    if (!is_capturing()) {
        return std::unexpected(CubeError::camera_control_unavailable);
    }
    if (!photometry_supported()) {
        return {};
    }

    std::scoped_lock lock(photometry_mutex_);
    // Drops any lock that had not been applied yet: a restore issued while
    // one is still settling means the caller no longer wants it, and
    // applying it first only to undo it would flash the scene twice.
    pending_lock_.reset();
    settle_frames_remaining_ = 0;
    pending_restore_ = true;
    return {};
}

std::optional<PhotometryLockReport> OpenCvCaptureLoop::take_photometry_report() {
    std::scoped_lock lock(photometry_mutex_);
    if (!report_.has_value()) {
        return std::nullopt;
    }
    return std::exchange(report_, std::nullopt);
}

void OpenCvCaptureLoop::service_photometry_requests() {
    PhotometryLockRequest request;
    bool do_lock = false;
    bool do_restore = false;

    {
        std::scoped_lock lock(photometry_mutex_);
        if (pending_restore_) {
            pending_restore_ = false;
            do_restore = true;
        } else if (pending_lock_.has_value()) {
            if (settle_frames_remaining_ > 0) {
                --settle_frames_remaining_;
            } else {
                request = *pending_lock_;
                pending_lock_.reset();
                do_lock = true;
            }
        }
    }

    // The device calls happen with photometry_mutex_ released (see the
    // member's comment): a driver round-trip must never be able to block a
    // GUI-thread take_photometry_report(). Nothing else touches capture_
    // while this thread is between grabs, so dropping the lock is safe.
    if (do_restore) {
        restore(capture_, as_found_);
        return;
    }
    if (!do_lock) {
        return;
    }

    PhotometryLockReport report = apply_lock(capture_, request, as_found_);
    std::scoped_lock lock(photometry_mutex_);
    report_ = std::move(report);
}

void OpenCvCaptureLoop::run(std::stop_token stop_token) {
    const ScopedCom scoped_com;

    cv::Mat frame;
    while (!stop_token.stop_requested()) {
        if (!capture_.read(frame) || frame.empty()) {
            continue;
        }
        // Between grabs, on the thread that owns the device - the only
        // place a property write is allowed (see interfaces/camera.hpp:
        // request_photometry_lock). Counted per delivered frame, so the
        // settling wait measures real frames from this camera rather than
        // wall-clock time on a device whose CAP_PROP_FPS reads 0.
        service_photometry_requests();
        // Stamped here, after the (blocking, device-rate) read returns, so
        // the timestamp tracks capture time even when the consumer polls
        // slower than the device delivers and overwritten frames vanish
        // without ever being observed - see the TimedFrame contract in
        // interfaces/camera.hpp.
        const auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                std::chrono::steady_clock::now().time_since_epoch())
                                .count();
        std::scoped_lock lock(mailbox_mutex_);
        latest_ = TimedFrame{.frame = frame.clone(), .t_ms = now_ms};
    }
}

}  // namespace cube::platform::camera::detail
