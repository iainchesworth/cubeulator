#include "platform/detail/photometry_lock.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

namespace cube::platform::camera {

namespace {

std::string join(const std::vector<std::string_view>& items) {
    std::string out;
    for (std::size_t i = 0; i < items.size(); ++i) {
        if (i != 0) {
            out += (i + 1 == items.size()) ? " and " : ", ";
        }
        out += items.at(i);
    }
    return out;
}

}  // namespace

const ControlResult& PhotometryLockReport::result(PhotometryControl control) const noexcept {
    // kPhotometryControls is declared in enum order, so the enumerator
    // doubles as the index - asserted by the unit test rather than by a
    // linear search here, which would run on every UI poll.
    return controls.at(static_cast<std::size_t>(control));
}

bool PhotometryLockReport::exposure_locked() const noexcept {
    return result(PhotometryControl::exposure).outcome == ControlOutcome::applied;
}

bool PhotometryLockReport::fully_applied() const noexcept {
    if (!platform_supported) {
        return false;
    }
    return std::ranges::none_of(controls, [](const ControlResult& entry) {
        return entry.outcome == ControlOutcome::rejected ||
               entry.outcome == ControlOutcome::unsupported;
    });
}

std::string PhotometryLockReport::summary() const {
    // Every branch below ends by telling the user what to DO. A silent or
    // technical failure here would leave them with the original bug -
    // colour samples that cannot be classified - and no idea that arranging
    // their own lighting is the workaround.
    if (!platform_supported) {
        return "Automatic camera control is not available on this platform. Light the cube "
               "evenly from the front and keep the lighting unchanged for the whole scan.";
    }

    std::vector<std::string_view> applied;
    std::vector<std::string_view> refused;
    for (const ControlResult& entry : controls) {
        if (entry.outcome == ControlOutcome::applied) {
            applied.push_back(to_string(entry.control));
        } else if (entry.outcome == ControlOutcome::rejected ||
                   entry.outcome == ControlOutcome::unsupported) {
            refused.push_back(to_string(entry.control));
        }
    }

    if (applied.empty()) {
        if (refused.empty()) {
            return "No camera settings were locked.";
        }
        return "This camera refused every setting (" + join(refused) +
               "). Light the cube evenly from the front and keep the lighting unchanged for "
               "the whole scan.";
    }

    std::string out = "Camera locked: " + join(applied) + ".";
    if (!refused.empty()) {
        out += " This camera would not lock " + join(refused) +
               ", so keep the room lighting unchanged while you scan.";
    }
    return out;
}

namespace detail {

int property_id(PhotometryControl control) noexcept {
    switch (control) {
        case PhotometryControl::auto_white_balance:
            return cv::CAP_PROP_AUTO_WB;
        case PhotometryControl::exposure:
            return cv::CAP_PROP_EXPOSURE;
        case PhotometryControl::brightness:
            return cv::CAP_PROP_BRIGHTNESS;
        case PhotometryControl::saturation:
            return cv::CAP_PROP_SATURATION;
    }
    return cv::CAP_PROP_EXPOSURE;
}

bool read_back_matches(double requested, double readback) noexcept {
    return std::abs(readback - requested) <= std::max(0.5, 0.02 * std::abs(requested));
}

namespace {

// OpenCV's videoio answers a property its backend does not implement with
// -1. On this device that is what CAP_PROP_WB_TEMPERATURE, AUTO_EXPOSURE and
// GAMMA all return, and every write to them is rejected.
//
// It is only ever consulted AFTER a write failed its read-back, never
// before: -1 is a legal value for a log2-seconds exposure, and a device that
// genuinely accepted -1 would have matched the read-back and been reported
// applied long before this constant is reached.
constexpr double kUnsupportedSentinel = -1.0;

ControlResult apply_one(cv::VideoCapture& capture, PhotometryControl control,
                        std::optional<double> override_value, PhotometrySnapshot& as_found) {
    const int prop = property_id(control);
    const double original = capture.get(prop);

    ControlResult out{.control = control};

    // The default target is the device's own current value, written straight
    // back to it. For exposure that is the whole trick: DirectShow drops out
    // of auto-exposure as a side effect of an IAMCameraControl write, so
    // writing back the value the automatics just chose freezes the scan at
    // that exposure instead of imposing a number picked on someone else's
    // camera. It also cannot be out of range, having just come from the
    // driver. auto_white_balance is the exception - the point there is to
    // turn the automatics OFF, so its target is 0 regardless.
    double target = original;
    if (control == PhotometryControl::auto_white_balance) {
        target = 0.0;
    }
    if (override_value.has_value()) {
        target = *override_value;
    }

    // Nothing to write back if we never got a readable value to begin with.
    if (!override_value.has_value() && original == kUnsupportedSentinel &&
        control != PhotometryControl::auto_white_balance) {
        out.outcome = ControlOutcome::unsupported;
        return out;
    }

    capture.set(prop, target);
    const double readback = capture.get(prop);

    out.requested = target;
    out.readback = readback;
    if (read_back_matches(target, readback)) {
        out.outcome = ControlOutcome::applied;
        // Only remember the pre-lock value if this is the first time we have
        // touched the control, so re-locking mid-session (a settings screen,
        // a second scan) still restores what the app found on arrival.
        auto& slot = as_found.values.at(static_cast<std::size_t>(control));
        if (!slot.has_value()) {
            slot = original;
        }
    } else if (original == kUnsupportedSentinel) {
        out.outcome = ControlOutcome::unsupported;
    } else {
        // Took the write, kept its old value. The DirectShow trap: set()
        // very likely returned true here, which is why it is never consulted.
        out.outcome = ControlOutcome::rejected;
    }
    return out;
}

// The white-balance temperature, in kelvin. In OpenCV 4's DirectShow backend
// this is IAMVideoProcAmp's WhiteBalance under its legacy name;
// CAP_PROP_WB_TEMPERATURE reads -1 there and refuses every write, which is
// how this control went unnoticed.
constexpr int kWhiteBalanceKelvin = cv::CAP_PROP_WHITE_BALANCE_BLUE_U;
// The range UVC cameras accept. A write outside a device's own range is
// refused, and a refused write ends the search.
constexpr double kMinKelvin = 2800.0;
constexpr double kMaxKelvin = 6500.0;
// A temperature write reaches the picture some frames later: more than 12
// on the workstation webcam, so a measurement taken sooner reads the
// previous setting.
constexpr int kWhiteBalanceSettleFrames = 18;
// How close the held picture's blue/green balance must come to the
// automatic one, as a fraction. The stored preset this replaces was 12% out.
constexpr double kWhiteBalanceTolerance = 0.03;
constexpr int kWhiteBalanceSearchSteps = 6;

// Mean blue over mean green of the frame `frames` reads from now. Nothing if
// the device stops delivering.
std::optional<double> blue_green_after(cv::VideoCapture& capture, int frames,
                                       const FrameSink& on_frame) {
    cv::Mat frame;
    for (int i = 0; i < frames; ++i) {
        if (!capture.read(frame) || frame.empty()) {
            return std::nullopt;
        }
        if (on_frame) {
            on_frame(frame);
        }
    }
    const cv::Scalar mean = cv::mean(frame);
    if (mean[1] <= 0.0) {
        return std::nullopt;
    }
    return mean[0] / mean[1];
}

// Turns auto white balance off AND keeps the balance it had reached.
//
// Switching the automatic off does not freeze it. The driver falls back to
// its stored manual temperature - 4000 K on the workstation webcam, 12% bluer
// than the automatic's own choice in daylight and amber under tungsten - so
// a lock that only cleared the flag tinted every scan by whatever light the
// room was not. The temperature has to be written back.
//
// The driver reports the temperature its automatics settled on, and writing
// that back is normally the whole job. It can also report a stale manual
// value for a while after the automatic is re-enabled, so the result is
// checked against the picture: if the held frame's blue/green balance has
// moved, the temperature is searched for the one that restores it (blue
// falls as the temperature rises).
ControlResult hold_white_balance(cv::VideoCapture& capture, PhotometrySnapshot& as_found,
                                 const FrameSink& on_frame, std::optional<double>& held_kelvin) {
    const std::optional<double> automatic = blue_green_after(capture, 2, on_frame);
    const double reported = capture.get(kWhiteBalanceKelvin);

    ControlResult result =
        apply_one(capture, PhotometryControl::auto_white_balance, std::nullopt, as_found);
    if (result.outcome != ControlOutcome::applied || !automatic.has_value()) {
        return result;
    }

    // How far the picture at `kelvin` sits from the automatic one: positive
    // is bluer. Nothing if the device has no temperature control.
    const auto error_at = [&](double kelvin) -> std::optional<double> {
        capture.set(kWhiteBalanceKelvin, kelvin);
        if (!read_back_matches(kelvin, capture.get(kWhiteBalanceKelvin))) {
            return std::nullopt;
        }
        const auto held = blue_green_after(capture, kWhiteBalanceSettleFrames, on_frame);
        if (!held.has_value()) {
            return std::nullopt;
        }
        return (*held / *automatic) - 1.0;
    };

    if (reported >= kMinKelvin && reported <= kMaxKelvin) {
        const auto error = error_at(reported);
        if (!error.has_value()) {
            return result;
        }
        if (std::abs(*error) <= kWhiteBalanceTolerance) {
            held_kelvin = reported;
            return result;
        }
    }

    double low = kMinKelvin;
    double high = kMaxKelvin;
    double best = 0.0;
    double best_error = std::numeric_limits<double>::max();
    double current = 0.0;
    for (int step = 0; step < kWhiteBalanceSearchSteps; ++step) {
        current = std::round((low + high) / 2.0);
        const auto error = error_at(current);
        if (!error.has_value()) {
            // No temperature control here: the balance stays wherever the
            // driver put it, which is what this lock did before.
            return result;
        }
        if (std::abs(*error) < best_error) {
            best_error = std::abs(*error);
            best = current;
        }
        if (best_error <= kWhiteBalanceTolerance) {
            break;
        }
        (*error > 0.0 ? low : high) = current;
    }
    if (best != current) {
        capture.set(kWhiteBalanceKelvin, best);
        (void)blue_green_after(capture, kWhiteBalanceSettleFrames, on_frame);
    }
    held_kelvin = best;
    return result;
}

}  // namespace

PhotometryLockReport apply_lock(cv::VideoCapture& capture, const PhotometryLockRequest& request,
                                PhotometrySnapshot& as_found, const FrameSink& on_frame) {
    PhotometryLockReport report;
    report.platform_supported = true;
    for (std::size_t i = 0; i < kPhotometryControls.size(); ++i) {
        report.controls.at(i).control = kPhotometryControls.at(i);
    }

    // Auto white balance first: turning an automatic off before fixing the
    // values it drives is the ordering that holds on UVC hardware generally,
    // even though this particular driver was measured to have no ordering
    // constraint at all (writes are honoured before the first grab and
    // after it, and survive the first grab).
    struct Step {
        PhotometryControl control;
        bool requested;
        std::optional<double> override_value;
    };
    const std::array<Step, kPhotometryControls.size()> steps{
        Step{.control = PhotometryControl::auto_white_balance,
             .requested = request.lock_auto_white_balance,
             .override_value = std::nullopt},
        Step{.control = PhotometryControl::exposure,
             .requested = request.lock_exposure,
             .override_value = request.exposure},
        Step{.control = PhotometryControl::brightness,
             .requested = request.lock_brightness,
             .override_value = request.brightness},
        Step{.control = PhotometryControl::saturation,
             .requested = request.lock_saturation,
             .override_value = request.saturation}};

    for (const Step& step : steps) {
        if (!step.requested) {
            continue;
        }
        report.controls.at(static_cast<std::size_t>(step.control)) =
            step.control == PhotometryControl::auto_white_balance
                ? hold_white_balance(capture, as_found, on_frame, report.white_balance_kelvin)
                : apply_one(capture, step.control, step.override_value, as_found);
    }
    return report;
}

void restore(cv::VideoCapture& capture, PhotometrySnapshot& as_found) {
    // Reverse order of apply_lock's steps, so auto white balance is handed
    // back its automatic last - after the values it governs are already back
    // where they were.
    for (std::size_t i = kPhotometryControls.size(); i > 0; --i) {
        auto& slot = as_found.values.at(i - 1);
        if (!slot.has_value()) {
            continue;
        }
        capture.set(property_id(kPhotometryControls.at(i - 1)), *slot);
        slot.reset();
    }
}

}  // namespace detail

}  // namespace cube::platform::camera
