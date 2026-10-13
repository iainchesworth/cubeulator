#pragma once

#include <cube/error.hpp>

#include <opencv2/core.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace cube::platform::camera {

// Which way the lens points relative to the user. This drives the UI's
// preview-mirror convention: front-facing previews are mirrored selfie-style
// (users expect a mirror when the lens looks at them), and 'unknown' gets
// the same treatment because it is what desktop webcam APIs report and
// those cameras face the user. Rear cameras must NOT be mirrored - their
// preview shows the scene, not the user, and mirroring would flip the
// cube's sticker layout left-to-right. Mobile platforms carry both kinds
// on one device, so their enumerate_devices() implementations need special
// attention to distinguish front vs rear per device (user requirement)
// rather than assuming a single value for the whole platform.
enum class Facing : std::uint8_t { front, rear, unknown };

// A capture device as enumerated by the host platform's camera API.
struct DeviceInfo {
    std::string identifier;
    std::string display_name;
    Facing facing = Facing::unknown;
};

// There is exactly one definition of each function below per platform,
// living in platform/<os>/camera_<os>.cpp. CMake compiles only the file
// matching the target OS, so the code contains no preprocessor branching.
[[nodiscard]] Result<std::vector<DeviceInfo>> enumerate_devices();
[[nodiscard]] Result<void> start(const DeviceInfo& device);
void stop();

// Opens the OS's app-specific settings page, for when a permission the app
// needs (camera) has been denied and there's no in-app "ask again" API left
// - Android's Settings.ACTION_APPLICATION_DETAILS_SETTINGS, iOS's
// UIApplication.openSettingsURLString. The permission check/request itself
// goes through Qt's own cross-platform QCameraPermission API (see
// CameraPermission.hpp) rather than duplicating that per platform here; this
// is only for the one piece Qt doesn't provide. No-op on desktop platforms,
// which have no per-app camera permission gate to escape.
void open_permission_settings();

// A captured frame plus the moment it entered the mailbox. t_ms is
// std::chrono::steady_clock milliseconds (monotonic, arbitrary epoch -
// only differences between frames are meaningful). The stamp exists for
// motion prediction downstream: the mailbox is latest-wins, so consumers
// polling slower than capture see irregular inter-frame gaps and must not
// assume a constant dt - the poll cadence says nothing about how old the
// frame actually is.
struct TimedFrame {
    cv::Mat frame;
    std::int64_t t_ms = 0;
};

// The most recently captured frame (BGR) not yet returned by a previous
// call, or nullopt if nothing new has arrived since the last call or no
// capture is running. Desktop platforms back this with a single-slot
// mailbox fed by a capture thread and stamp t_ms at mailbox push (the
// closest observable point to exposure without per-driver timestamp
// APIs); mobile backends, whose native capture callbacks hand frames over
// directly, stamp at delivery into whatever mailbox they maintain - the
// contract is "as close to capture as the platform allows", never "when
// the consumer happened to poll". Mobile stubs currently return nullopt.
[[nodiscard]] std::optional<TimedFrame> try_pop_latest_frame();
[[nodiscard]] bool is_capturing() noexcept;

// ---------------------------------------------------------------------------
// Photometry locking
//
// WHY THIS EXISTS
// A full day of scan diagnostics traced "the scanner cannot assemble a valid
// cube" to the captured colour samples, not to the vision pipeline. With the
// camera left on its own automatics, BLUE arrived at Lab chroma 18-24 and
// warm-cast WHITE at 15.7-21.0 - overlapping distributions no classifier can
// separate - and mean L drifted 14-18 points BETWEEN FACES OF A SINGLE SCAN
// as auto-exposure hunted. Re-recording under a fixed front light gave BLUE
// 35.3-43.7 against WHITE 4.2-14.5, a clean ~20-point gap. The fix is
// constant photometry across a scan; this API is how the app asks the device
// for it instead of asking the user to arrange their room.
//
// WHAT THE HARDWARE ACTUALLY ALLOWS (probe: tests/integration/
// camera_property_probe_test.cpp, run against the user's StreamCam through
// CAP_DSHOW - the same path camera_windows.cpp opens)
//   - EXPOSURE: settable, and PROVEN to move pixels (monotone L ramp across
//     its whole accepted -11..-2 range). The monotone ramp is itself the
//     proof that auto-exposure is no longer fighting the write, which is how
//     DirectShow behaves: writing IAMCameraControl exposure sets the manual
//     flag as a side effect.
//   - BRIGHTNESS: settable, proven (42 L points of authority).
//   - SATURATION: settable, proven (monotone in chroma with L flat).
//   - AUTO_WB: a plain 0/1 boolean that round-trips cleanly. Clearing it
//     does NOT freeze the balance: the driver falls back to a stored manual
//     temperature (4000 K here - 12% bluer than the automatic's choice in
//     daylight, measured 2026-10-13 with the lens uncovered).
//   - WHITE BALANCE TEMPERATURE: present, under CAP_PROP_WHITE_BALANCE_BLUE_U.
//     It reads the kelvin the automatics chose, and writes move the picture
//     monotonically (2800 K blue/green 1.25, 4700 K 1.03, 6500 K 0.79) about
//     half a second later. The lock writes the automatic's temperature back
//     after clearing AUTO_WB and checks the picture did not move.
//   - CAP_PROP_WB_TEMPERATURE, AUTO_EXPOSURE (reads), GAMMA: absent. get()
//     returns OpenCV's -1 "unsupported" sentinel and writes are rejected.
//     The first of these is why the temperature control above went
//     unnoticed: it is the obvious name and it is dead in this backend.
//   - GAIN and CONTRAST read back but showed no pixel effect above the noise
//     floor of the probe's dark scene. Deliberately NOT exposed here: an
//     unproven control in a photometry lock is worse than a missing one,
//     because it would be reported to the user as locked.
// ---------------------------------------------------------------------------

// The controls a lock may touch. Exactly the set the probe proved, plus
// auto_white_balance, whose read-back is proven and whose pixel effect the
// report will tell the truth about. Kept as an enum + kPhotometryControls so
// callers can iterate a report generically instead of naming fields.
enum class PhotometryControl : std::uint8_t {
    auto_white_balance,
    exposure,
    brightness,
    saturation,
};

inline constexpr std::array<PhotometryControl, 4> kPhotometryControls{
    PhotometryControl::auto_white_balance, PhotometryControl::exposure,
    PhotometryControl::brightness, PhotometryControl::saturation};

[[nodiscard]] constexpr std::string_view to_string(PhotometryControl control) noexcept {
    switch (control) {
        case PhotometryControl::auto_white_balance:
            return "auto white balance";
        case PhotometryControl::exposure:
            return "exposure";
        case PhotometryControl::brightness:
            return "brightness";
        case PhotometryControl::saturation:
            return "saturation";
    }
    return "unknown control";
}

// What became of one control. The distinction between the three failure
// states is the whole point of reporting at all: 'unsupported' means the
// backend/platform has no such knob (OpenCV's get() answers with its -1
// sentinel), 'rejected' means the driver took the write and then kept its
// old value - the classic DirectShow trap where set() returns true and
// nothing happens - and only 'applied' means a read-back confirmed it.
// set()'s own return value is never used as evidence anywhere below.
enum class ControlOutcome : std::uint8_t {
    not_requested,
    unsupported,
    rejected,
    applied,
};

struct ControlResult {
    PhotometryControl control = PhotometryControl::exposure;
    ControlOutcome outcome = ControlOutcome::not_requested;
    // The value written and what the device read back afterwards. Both are
    // meaningless when outcome is not_requested/unsupported.
    double requested = 0.0;
    double readback = 0.0;
};

// The outcome of one lock attempt, in the order of kPhotometryControls.
struct PhotometryLockReport {
    std::array<ControlResult, kPhotometryControls.size()> controls{};
    // False when the platform has no photometry control at all (every
    // non-Windows backend today) - distinct from "the device refused", and
    // the two must read differently to the user.
    bool platform_supported = false;
    // The white-balance temperature the lock is holding, when the device has
    // one to hold. Empty means auto white balance is off but the balance sits
    // wherever the driver's own fallback put it.
    std::optional<double> white_balance_kelvin;

    [[nodiscard]] const ControlResult& result(PhotometryControl control) const noexcept;
    // Locked exposure is the single measurement that matters: it is the one
    // proven to move pixels and the one whose drift was measured at 14-18 L
    // points within a scan.
    [[nodiscard]] bool exposure_locked() const noexcept;
    // True only if every control that was asked for came back applied.
    [[nodiscard]] bool fully_applied() const noexcept;
    // One line fit for showing a user, e.g. "Camera locked: exposure,
    // brightness, saturation; auto white balance refused by the driver."
    // Never claims success it did not verify - a partial or failed lock says
    // so, so the user knows to arrange their lighting by hand.
    [[nodiscard]] std::string summary() const;
};

// What to hold fixed. Every value defaults to "whatever the device's own
// automatics have currently settled on", read back from the driver at lock
// time and written straight back to it: that is both the deliberate value
// Kociemba sets by hand with sliders and the only way to obey "values must
// be probed/derived, never hardcoded to one device's numbers" - a value the
// driver just reported is in range by construction.
//
// White balance is held the same way: the camera's own AWB settles on the
// scene, and the lock freezes it at the temperature it chose.
struct PhotometryLockRequest {
    bool lock_auto_white_balance = true;
    bool lock_exposure = true;
    bool lock_brightness = true;
    bool lock_saturation = true;

    // Explicit overrides, for a settings screen or a tuning experiment. An
    // out-of-range override is REJECTED outright by this class of driver
    // (not clamped), and the report will say so rather than pretending.
    std::optional<double> exposure;
    std::optional<double> brightness;
    std::optional<double> saturation;
};

// True if this platform's backend can control photometry at all. Windows
// (CAP_DSHOW -> IAMVideoProcAmp/IAMCameraControl) is the only one proven, so
// it is the only one that answers true; everywhere else this is an honest
// "no" rather than an untested guess, and request_photometry_lock() there
// still produces a report - one whose platform_supported is false.
[[nodiscard]] bool photometry_control_supported() noexcept;

// Asks for the lock. Deliberately a REQUEST, not an apply: the device is
// owned by the capture thread, and issuing set() from the GUI thread while
// that thread sits inside a blocking read() is exactly the mid-grab call the
// capture model forbids. The capture thread applies it between grabs and
// publishes the outcome for take_photometry_report().
//
// It also deliberately does not apply immediately. The camera's automatics
// need a moment after open to converge, and locking to an unconverged value
// would freeze the scan at whatever the sensor happened to show in its first
// few frames. The capture loop lets a short settling run pass first, so the
// value frozen is the one the automatics arrived at. Frames keep flowing
// throughout; nothing about open or first-frame latency changes.
//
// Fails only if no capture is running.
[[nodiscard]] Result<void> request_photometry_lock(const PhotometryLockRequest& request);

// Puts back the values read at lock time. DirectShow property state lives in
// the driver and outlives the process - the probe confirmed a fresh
// VideoCapture reads back the previous run's exposure - so without this the
// app would silently poison every other camera app on the machine, and its
// own next launch. stop() calls this automatically; the explicit entry point
// exists for a settings screen that wants to unlock mid-session.
[[nodiscard]] Result<void> request_photometry_restore();

// The most recent lock outcome not yet taken, or nullopt if nothing new.
// Same latest-wins hand-off as the frame mailbox, for the same reason: the
// GUI polls, the capture thread publishes.
[[nodiscard]] std::optional<PhotometryLockReport> take_photometry_report();

}  // namespace cube::platform::camera
