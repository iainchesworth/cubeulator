#include "interfaces/camera.hpp"
#include "platform/web/photometry_web.hpp"

#include <emscripten.h>
#include <emscripten/threading.h>

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// The browser's camera is getUserMedia(): asynchronous, prompted by the
// browser itself, and reachable only from the page's main thread. This file
// is the C++ half; camera_web.js is the page's half (an Emscripten JS library,
// linked with --js-library, see platform.cmake).
//
// The interface in interfaces/camera.hpp is blocking - start() returns when
// the device is open or has failed, and the app calls it from a QtConcurrent
// worker so the GUI stays alive while a driver takes seconds. That maps onto
// the browser directly: the worker is a Web Worker (this is the threaded
// WebAssembly build), it calls into the page - Emscripten forwards the call to
// the main thread, where getUserMedia() can run - and waits on a condition
// variable until the page answers, which it does from a promise callback, on
// the main thread, through the cube_web_camera_* functions below. A person has no deadline for the
// permission prompt, so the wait is long, and bounded only so that a prompt
// that is never answered cannot hold a pool thread for ever.
//
// That also fixes what this cannot do: a call made ON the main thread would
// wait for an answer only that thread can give, so those are refused. In the
// single-threaded build QtConcurrent runs the function on the calling
// (main) thread, which is why the app is built with threads.

// Provided by camera_web.js. Each is proxied to the main thread there, so they
// may be called from any thread.
extern "C" {
// Asks for the list of video inputs; answered with cube_web_camera_devices().
void cube_web_camera_enumerate(int request);
// Opens a camera (`id` is `length` bytes, UTF-8); answered with
// cube_web_camera_started(). Returns once the page has read `id`.
void cube_web_camera_open(int request, const char* id, int length);
// Lets go of the camera, if one is open, having put back anything a lock
// changed in it.
void cube_web_camera_close();
// Holds the open camera's exposure and white balance where its automatics have
// put them, once it has delivered a few frames (`exposure` and `white_balance`
// are 0 for a control that is not wanted). Returns at once; answered, always,
// with cube_web_camera_lock_reported(). `request` is the camera's.
void cube_web_camera_lock(int request, int exposure, int white_balance);
// Puts back what the lock changed; the camera keeps running.
void cube_web_camera_unlock();
}

namespace cube::platform::camera {

namespace {

constexpr std::chrono::seconds kEnumerateTimeout{15};
constexpr std::chrono::seconds kStartTimeout{180};

// One frame, RGBA, at the most the page ever delivers: 640 wide (the vision
// reader's working width) by up to 1280 tall (a portrait phone). camera_web.js
// scales anything taller to fit.
constexpr std::size_t kFrameCapacity = 640U * 1280U * 4U;

// What camera_web.js answers a start with; mirrored there.
enum class Answer : int { ok = 0, denied = 1, not_found = 2, unavailable = 3, failed = 4 };

CubeError to_error(Answer answer) noexcept {
    switch (answer) {
        case Answer::not_found:
            return CubeError::camera_not_found;
        case Answer::ok:
        case Answer::denied:
        case Answer::unavailable:
        case Answer::failed:
            break;
    }
    return CubeError::camera_start_failed;
}

// Camera device ids the page cannot supply, because it has not been given
// camera access yet (browsers hide the real ids and labels until then). They
// are passed back to camera_web.js as written.
constexpr std::string_view kDefaultId = "default";
constexpr std::string_view kBackFacingId = "facing:environment";
constexpr std::string_view kFrontFacingId = "facing:user";

struct Session {
    std::mutex mutex;
    std::condition_variable changed;

    // Which question the page is answering, one counter for each of the two it
    // can be asked. Bumped per question so that a late answer to one that was
    // given up on (a start that timed out) is recognised and dropped instead of
    // being taken for the current one. Two counters, not one: listing the
    // devices while a camera is open (the settings screen's "look again") must
    // not make the open camera's frames look stale.
    int enumeration = 0;
    int camera = 0;

    bool devices_ready = false;
    std::string devices;

    bool start_ready = false;
    Answer start_answer = Answer::ok;

    bool capturing = false;

    // Written by the page (main thread) for each frame, then copied out by
    // cube_web_camera_frame() on that same thread: nothing reads it elsewhere.
    std::vector<std::uint8_t> frame_buffer;
    // The text of the last list of devices, written by the page.
    std::string string_buffer;

    std::optional<TimedFrame> latest;
    std::optional<PhotometryLockReport> report;
    // True once a lock has been read back as holding something on this camera:
    // the only thing photometry_control_supported() ever claims.
    bool controls_proven = false;
};

Session& session() {
    static Session instance;
    return instance;
}

// Asked of a thread that is about to wait for the page: see the file comment.
bool can_wait_for_page() noexcept {
    return emscripten_is_main_runtime_thread() == 0;
}

struct RawCamera {
    std::string id;
    std::string label;
};

std::vector<RawCamera> parse_cameras(std::string_view text) {
    std::vector<RawCamera> cameras;
    while (!text.empty()) {
        const std::size_t end = std::min(text.find('\n'), text.size());
        const std::string_view line = text.substr(0, end);
        const std::size_t tab = line.find('\t');
        RawCamera camera;
        camera.id = std::string(line.substr(0, tab));
        if (tab != std::string_view::npos) {
            camera.label = std::string(line.substr(tab + 1));
        }
        cameras.push_back(std::move(camera));
        text.remove_prefix(std::min(end + 1, text.size()));
    }
    return cameras;
}

bool contains_word(const std::string& haystack, std::string_view word) {
    std::string lower = haystack;
    std::ranges::transform(lower, lower.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return lower.find(word) != std::string::npos;
}

// What the labels the browser gives say about which way a lens points. Chrome
// on a phone words them "camera2 0, facing back" and "camera2 1, facing front";
// desktop webcams say nothing and stay unknown, which gets the selfie mirror
// they want (see Facing in interfaces/camera.hpp).
Facing facing_from_label(const std::string& label) {
    if (contains_word(label, "back") || contains_word(label, "rear") ||
        contains_word(label, "environment")) {
        return Facing::rear;
    }
    if (contains_word(label, "front") || contains_word(label, "user") ||
        contains_word(label, "face")) {
        return Facing::front;
    }
    return Facing::unknown;
}

// The list the app shows. Until a person has allowed the camera for this site
// the browser hides every id and label, so a device cannot be picked out by
// name; what can be said then is how many there are, and - the one thing a
// phone's two cameras differ in, and the one a cube is scanned with - which
// way they point. The rear camera comes first so it is the one a first launch
// selects: a phone's front camera looks at the person, not the cube.
std::vector<DeviceInfo> device_list(const std::vector<RawCamera>& cameras) {
    const bool identified =
        std::ranges::any_of(cameras, [](const RawCamera& camera) { return !camera.id.empty(); });

    std::vector<DeviceInfo> devices;
    if (identified) {
        int number = 0;
        for (const RawCamera& camera : cameras) {
            ++number;
            devices.push_back(DeviceInfo{
                .identifier = camera.id,
                .display_name =
                    camera.label.empty() ? "Camera " + std::to_string(number) : camera.label,
                .facing = facing_from_label(camera.label),
            });
        }
    } else if (cameras.size() >= 2) {
        devices.push_back(DeviceInfo{.identifier = std::string(kBackFacingId),
                                     .display_name = "Back camera",
                                     .facing = Facing::rear});
        devices.push_back(DeviceInfo{.identifier = std::string(kFrontFacingId),
                                     .display_name = "Front camera",
                                     .facing = Facing::front});
    } else {
        // One camera, or the browser would not even say how many. Either way
        // asking for "a camera" is right: it opens the only one, and where
        // there are several it prefers the one facing away.
        devices.push_back(DeviceInfo{.identifier = std::string(kDefaultId),
                                     .display_name = "Camera",
                                     .facing = Facing::unknown});
    }
    return devices;
}

}  // namespace

}  // namespace cube::platform::camera

// ---------------------------------------------------------------------------
// What the page calls. All of these run on the main thread, from camera_web.js.
// ---------------------------------------------------------------------------
extern "C" {

// Room for a string the page is about to write (the device list). Valid until
// the next call.
EMSCRIPTEN_KEEPALIVE char* cube_web_camera_string_buffer(int bytes) {
    auto& s = cube::platform::camera::session();
    const std::scoped_lock lock(s.mutex);
    s.string_buffer.assign(static_cast<std::size_t>(std::max(bytes, 1)), '\0');
    return s.string_buffer.data();
}

EMSCRIPTEN_KEEPALIVE void cube_web_camera_devices(int request, int length) {
    auto& s = cube::platform::camera::session();
    {
        const std::scoped_lock lock(s.mutex);
        if (request != s.enumeration) {
            return;
        }
        s.devices.assign(s.string_buffer.data(), static_cast<std::size_t>(std::max(length, 0)));
        s.devices_ready = true;
    }
    s.changed.notify_all();
}

// The answer to cube_web_camera_lock(): whether the camera listed a manual
// mode for either control at all (`offered`) and, per control, what became of
// it, the value asked for and the value read back. Turned into the report here
// and nowhere else; what it says is camera_web.js's finding, from the track.
EMSCRIPTEN_KEEPALIVE void cube_web_camera_lock_reported(
    int request, int offered, int exposure_outcome, double exposure_requested,
    double exposure_readback, int white_balance_outcome, double kelvin_requested,
    double kelvin_readback) {
    auto& s = cube::platform::camera::session();
    const std::scoped_lock lock(s.mutex);
    if (request != s.camera || !s.capturing) {
        return;
    }
    const auto report =
        cube::platform::camera::web::report_from_page(offered != 0,
                                                      {.outcome = exposure_outcome,
                                                       .requested = exposure_requested,
                                                       .readback = exposure_readback},
                                                      {.outcome = white_balance_outcome,
                                                       .requested = kelvin_requested,
                                                       .readback = kelvin_readback});
    s.controls_proven =
        report.exposure_locked() ||
        report.result(cube::platform::camera::PhotometryControl::auto_white_balance).outcome ==
            cube::platform::camera::ControlOutcome::applied;
    s.report = report;
}

EMSCRIPTEN_KEEPALIVE void cube_web_camera_started(int request, int answer) {
    auto& s = cube::platform::camera::session();
    {
        const std::scoped_lock lock(s.mutex);
        if (request != s.camera) {
            return;
        }
        s.start_answer = static_cast<cube::platform::camera::Answer>(answer);
        s.start_ready = true;
    }
    s.changed.notify_all();
}

// The buffer the page writes each frame into. Allocated once and never
// resized, so the pointer the page keeps stays good.
EMSCRIPTEN_KEEPALIVE std::uint8_t* cube_web_camera_frame_buffer() {
    auto& s = cube::platform::camera::session();
    if (s.frame_buffer.empty()) {
        s.frame_buffer.resize(cube::platform::camera::kFrameCapacity);
    }
    return s.frame_buffer.data();
}

EMSCRIPTEN_KEEPALIVE int cube_web_camera_frame_capacity() {
    return static_cast<int>(cube::platform::camera::kFrameCapacity);
}

// A frame is in the buffer: convert it (RGBA from a canvas to the BGR every
// other platform hands over) and make it the latest. The stamp is taken here,
// at the mailbox, as camera.hpp says is the closest point to exposure there
// is without a timestamp API.
EMSCRIPTEN_KEEPALIVE void cube_web_camera_frame(int request, int width, int height) {
    auto& s = cube::platform::camera::session();
    if (width <= 0 || height <= 0 ||
        static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4U >
            cube::platform::camera::kFrameCapacity ||
        s.frame_buffer.empty()) {
        return;
    }

    cv::Mat bgr;
    const cv::Mat rgba(height, width, CV_8UC4, s.frame_buffer.data());
    cv::cvtColor(rgba, bgr, cv::COLOR_RGBA2BGR);

    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    const std::scoped_lock lock(s.mutex);
    if (request != s.camera || !s.capturing) {
        return;
    }
    s.latest = cube::platform::camera::TimedFrame{
        .frame = std::move(bgr),
        .t_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now).count()};
}

}  // extern "C"

namespace cube::platform::camera {

Result<std::vector<DeviceInfo>> enumerate_devices() {
    if (!can_wait_for_page()) {
        return std::unexpected(CubeError::camera_not_found);
    }

    Session& s = session();
    int request = 0;
    {
        const std::scoped_lock lock(s.mutex);
        request = ++s.enumeration;
        s.devices_ready = false;
        s.devices.clear();
    }
    cube_web_camera_enumerate(request);

    std::unique_lock lock(s.mutex);
    if (!s.changed.wait_for(lock, kEnumerateTimeout, [&] { return s.devices_ready; })) {
        // The page did not answer: say so by returning what it would have
        // said with nothing to say, which still lets the app try a start.
        return device_list({});
    }
    return device_list(parse_cameras(s.devices));
}

Result<void> start(const DeviceInfo& device) {
    if (!can_wait_for_page()) {
        return std::unexpected(CubeError::camera_start_failed);
    }
    stop();

    Session& s = session();
    int request = 0;
    {
        const std::scoped_lock lock(s.mutex);
        request = ++s.camera;
        s.start_ready = false;
        s.start_answer = Answer::ok;
        s.latest.reset();
        s.report.reset();
        s.controls_proven = false;
    }
    // Returns once the page has read the id, so its storage is still ours
    // while it does.
    cube_web_camera_open(request, device.identifier.c_str(),
                         static_cast<int>(device.identifier.size()));

    std::unique_lock lock(s.mutex);
    if (!s.changed.wait_for(lock, kStartTimeout, [&] { return s.start_ready; })) {
        // The prompt was never answered. Give up on this request - the id moves
        // on, so a late answer is dropped - and tell the page to let go.
        ++s.camera;
        lock.unlock();
        cube_web_camera_close();
        return std::unexpected(CubeError::camera_start_failed);
    }
    if (s.start_answer != Answer::ok) {
        return std::unexpected(to_error(s.start_answer));
    }
    s.capturing = true;
    return {};
}

void stop() {
    Session& s = session();
    {
        const std::scoped_lock lock(s.mutex);
        if (!s.capturing) {
            return;
        }
        s.capturing = false;
        s.latest.reset();
        s.report.reset();
        s.controls_proven = false;
    }
    // Called from the GUI thread, which is the main thread: this runs in place
    // and does not wait for anything. The page puts back what a lock changed
    // before it lets the camera go.
    cube_web_camera_close();
}

// A browser has no per-app settings page for a page to send a person to; the
// permission lives in the address bar's site settings, which no page can open.
// The permission screen's wording covers it.
void open_permission_settings() {}

std::optional<TimedFrame> try_pop_latest_frame() {
    Session& s = session();
    const std::scoped_lock lock(s.mutex);
    return std::exchange(s.latest, std::nullopt);
}

bool is_capturing() noexcept {
    Session& s = session();
    const std::scoped_lock lock(s.mutex);
    return s.capturing;
}

// Locking exposure and white balance is what the scan wants, and a browser has
// a way to ask for it: MediaStreamTrack.applyConstraints() with exposureMode
// and whiteBalanceMode, in Chromium, on a camera that lists them. Whether this
// browser and this camera do is a fact about the open camera, and is found by
// reading its track and then reading it back after the lock (camera_web.js),
// not by naming a browser: Safari and Firefox offer a page no such control,
// and in Chromium it is the camera's driver that decides.
//
// So this claims support only for a camera on which a lock has been read back
// as holding something (see the long note in interfaces/camera.hpp: an
// unproven control reported as locked is worse than a missing one). Before the
// lock has reported, and for a camera it could not hold, the answer is no, and
// the scan runs on the camera's own automatics - which is also what the report
// says.
bool photometry_control_supported() noexcept {
    Session& s = session();
    const std::scoped_lock lock(s.mutex);
    return s.capturing && s.controls_proven;
}

// Asks the page. It waits for the camera's automatics to settle, as the
// desktop loop does, and answers - always, if only with "unsupported" - through
// cube_web_camera_lock_reported(); the scan waits for that answer, not for the
// lock. The overrides in the request are the desktop driver's units (a
// DirectShow exposure is a power of two seconds) and mean nothing to a browser,
// which holds only what the camera itself chose.
Result<void> request_photometry_lock(const PhotometryLockRequest& request) {
    Session& s = session();
    int camera = 0;
    {
        const std::scoped_lock lock(s.mutex);
        if (!s.capturing) {
            return std::unexpected(CubeError::camera_control_unavailable);
        }
        // An answer to an earlier request is not this one's.
        s.report.reset();
        s.controls_proven = false;
        camera = s.camera;
    }
    cube_web_camera_lock(camera, request.lock_exposure ? 1 : 0,
                         request.lock_auto_white_balance ? 1 : 0);
    return {};
}

Result<void> request_photometry_restore() {
    Session& s = session();
    {
        const std::scoped_lock lock(s.mutex);
        if (!s.capturing) {
            return std::unexpected(CubeError::camera_control_unavailable);
        }
        s.report.reset();
        s.controls_proven = false;
    }
    cube_web_camera_unlock();
    return {};
}

std::optional<PhotometryLockReport> take_photometry_report() {
    Session& s = session();
    const std::scoped_lock lock(s.mutex);
    return std::exchange(s.report, std::nullopt);
}

}  // namespace cube::platform::camera
