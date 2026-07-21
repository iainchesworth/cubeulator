#include "platform/detail/photometry_lock.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
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
        out += items[i];
    }
    return out;
}

}  // namespace

const ControlResult& PhotometryLockReport::result(PhotometryControl control) const noexcept {
    // kPhotometryControls is declared in enum order, so the enumerator
    // doubles as the index - asserted by the unit test rather than by a
    // linear search here, which would run on every UI poll.
    return controls[static_cast<std::size_t>(control)];
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
        auto& slot = as_found.values[static_cast<std::size_t>(control)];
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

}  // namespace

PhotometryLockReport apply_lock(cv::VideoCapture& capture, const PhotometryLockRequest& request,
                                PhotometrySnapshot& as_found) {
    PhotometryLockReport report;
    report.platform_supported = true;
    for (std::size_t i = 0; i < kPhotometryControls.size(); ++i) {
        report.controls[i].control = kPhotometryControls[i];
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
        Step{PhotometryControl::auto_white_balance, request.lock_auto_white_balance, std::nullopt},
        Step{PhotometryControl::exposure, request.lock_exposure, request.exposure},
        Step{PhotometryControl::brightness, request.lock_brightness, request.brightness},
        Step{PhotometryControl::saturation, request.lock_saturation, request.saturation}};

    for (const Step& step : steps) {
        if (!step.requested) {
            continue;
        }
        report.controls[static_cast<std::size_t>(step.control)] =
            apply_one(capture, step.control, step.override_value, as_found);
    }
    return report;
}

void restore(cv::VideoCapture& capture, PhotometrySnapshot& as_found) {
    // Reverse order of apply_lock's steps, so auto white balance is handed
    // back its automatic last - after the values it governs are already back
    // where they were.
    for (std::size_t i = kPhotometryControls.size(); i > 0; --i) {
        auto& slot = as_found.values[i - 1];
        if (!slot.has_value()) {
            continue;
        }
        capture.set(property_id(kPhotometryControls[i - 1]), *slot);
        slot.reset();
    }
}

}  // namespace detail

}  // namespace cube::platform::camera
