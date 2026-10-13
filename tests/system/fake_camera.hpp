#pragma once

// The camera the system tests plug in under the app.
//
// cube::platform::camera is a set of free functions with one definition per
// operating system (src/app/interfaces/camera.hpp). The test executable
// defines them itself (fake_camera.cpp), so the platform library's own camera
// object is never pulled in by the linker and DirectScan, CameraController
// and the screens above them run unmodified against a camera that shows
// whatever the test says. The frames are drawn (tests/unit/drawn_cube.hpp):
// nobody is in the picture.

#include "interfaces/camera.hpp"

#include <cube/error.hpp>

#include <opencv2/core.hpp>

#include <deque>
#include <mutex>
#include <optional>
#include <vector>

namespace cube::systest {

class FakeCamera {
public:
    static FakeCamera& instance();

    // Back to a freshly plugged-in camera: one device, idle room, no script,
    // counters at zero.
    void reset();

    // What enumerate_devices() reports. Empty is a machine with no camera.
    void set_devices(std::vector<platform::camera::DeviceInfo> devices);

    // The next start() fails with `error`, once. DirectScan reports it as the
    // camera error on the scan screen.
    void fail_next_start(CubeError error);

    // The camera delivers `frame` for the next `repeat` polls, after whatever
    // is already scripted. Once the script has run out the last frame stays
    // up: a camera does not stop delivering when the scene stops changing.
    void play(const cv::Mat& frame, int repeat = 1);
    // Forget what is scripted but not yet delivered; the last frame stays up.
    void clear_script();
    // Everything scripted has been delivered to the app.
    [[nodiscard]] bool script_drained() const;

    // What the app did with the camera.
    [[nodiscard]] int starts() const;
    [[nodiscard]] int stops() const;
    [[nodiscard]] bool capturing() const;
    // Frames the app took, which is how far through a script it is.
    [[nodiscard]] int frames_delivered() const;

    // Used by the cube::platform::camera definitions.
    [[nodiscard]] Result<std::vector<platform::camera::DeviceInfo>> enumerate() const;
    [[nodiscard]] Result<void> start();
    void stop();
    [[nodiscard]] std::optional<platform::camera::TimedFrame> pop();

private:
    FakeCamera();

    struct Scripted {
        cv::Mat frame;
        int remaining = 0;
    };

    mutable std::mutex mutex_;
    std::vector<platform::camera::DeviceInfo> devices_;
    std::optional<CubeError> next_start_error_;
    std::deque<Scripted> script_;
    cv::Mat current_;
    bool capturing_ = false;
    int starts_ = 0;
    int stops_ = 0;
    int frames_delivered_ = 0;
};

}  // namespace cube::systest
