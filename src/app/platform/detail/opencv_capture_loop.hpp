#pragma once

#include "interfaces/camera.hpp"
#include "platform/detail/photometry_lock.hpp"

#include <cube/error.hpp>

#include <opencv2/videoio.hpp>

#include <atomic>
#include <cstdint>
#include <mutex>
#include <optional>
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
    // Whether this backend is allowed to try photometry locking at all.
    // Only CAP_DSHOW has been measured against real hardware (see the probe
    // in tests/integration/camera_property_probe_test.cpp), so it is the
    // only backend constructed with 'yes'. The others report an honest
    // "unsupported on this platform" instead of issuing property writes
    // whose effect nobody has verified - a lock that silently does nothing
    // is worse than no lock, because the user would be told it worked.
    enum class Photometry : std::uint8_t { unsupported, supported };

    explicit OpenCvCaptureLoop(cv::VideoCaptureAPIs backend,
                               Photometry photometry = Photometry::unsupported) noexcept;
    ~OpenCvCaptureLoop();

    OpenCvCaptureLoop(const OpenCvCaptureLoop&) = delete;
    OpenCvCaptureLoop& operator=(const OpenCvCaptureLoop&) = delete;
    OpenCvCaptureLoop(OpenCvCaptureLoop&&) = delete;
    OpenCvCaptureLoop& operator=(OpenCvCaptureLoop&&) = delete;

    [[nodiscard]] Result<std::vector<DeviceInfo>> enumerate_devices() const;
    [[nodiscard]] Result<void> start(const DeviceInfo& device);
    void stop();

    [[nodiscard]] std::optional<TimedFrame> try_pop_latest_frame();
    [[nodiscard]] bool is_capturing() const noexcept;

    [[nodiscard]] bool photometry_supported() const noexcept;
    [[nodiscard]] Result<void> request_photometry_lock(const PhotometryLockRequest& request);
    [[nodiscard]] Result<void> request_photometry_restore();
    [[nodiscard]] std::optional<PhotometryLockReport> take_photometry_report();

private:
    void run();
    // Called from run() only, between grabs - never with a read() in
    // flight. Consumes whatever request is pending and publishes a report.
    void service_photometry_requests();
    // Puts a frame in the mailbox, stamped now.
    void publish(const cv::Mat& frame);

    cv::VideoCaptureAPIs backend_;
    Photometry photometry_;
    cv::VideoCapture capture_;
    // A plain thread and a flag, not std::jthread/std::stop_token: those are
    // still experimental in Apple's libc++ and the NDK's (opt-in behind
    // -fexperimental-library), so macOS, iOS and Android would not compile.
    std::thread capture_thread_;
    std::atomic<bool> stop_requested_{false};
    std::mutex mailbox_mutex_;
    // Guards the photometry request/report hand-off between the caller's
    // thread and the capture thread. Held only around the plain-data
    // copies, NEVER while cv::VideoCapture::set/get is executing: those run
    // on the capture thread while it owns the device, and holding a lock
    // across them would drag the GUI thread into the driver's latency.
    std::mutex photometry_mutex_;
    std::optional<PhotometryLockRequest> pending_lock_;
    bool pending_restore_ = false;
    // Frames still to be discarded before a pending lock is applied. The
    // camera's automatics need a moment after open to converge on the
    // scene; locking to a value read before then would freeze the whole
    // scan at whatever the sensor showed in its first instants. Frames keep
    // being delivered during the wait - this delays only the lock, so
    // open/first-frame latency (the ~1.0-1.4s DSHOW budget that dictated
    // the backend choice) is untouched.
    int settle_frames_remaining_ = 0;
    std::optional<PhotometryLockReport> report_;
    PhotometrySnapshot as_found_;
    // The single-slot mailbox: an empty .frame means "nothing new". t_ms is
    // stamped on the capture thread at push (see interfaces/camera.hpp for
    // the timestamp contract) so the stamp reflects when the frame was
    // captured, not when the GUI thread got around to polling for it.
    TimedFrame latest_;
};

// The cv::VideoCaptureAPIs backend this platform's camera implementation
// opens devices through, and its name. camera_<os>.cpp both defines these and
// constructs its loop from them, so anything that wants "what the shipped app
// opens" - the hardware probes under tests/integration - asks here rather
// than guessing. Defined by the platforms whose camera goes through
// OpenCvCaptureLoop (Windows, Linux, macOS); the mobile cameras do not use
// OpenCV, and nothing built for them calls these.
[[nodiscard]] cv::VideoCaptureAPIs capture_backend() noexcept;
[[nodiscard]] const char* capture_backend_name() noexcept;

}  // namespace cube::platform::camera::detail
