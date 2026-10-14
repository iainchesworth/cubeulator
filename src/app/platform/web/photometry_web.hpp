#pragma once

#include "interfaces/camera.hpp"

#include <cmath>
#include <cstddef>

// What the page says of the camera's exposure and white balance, as the report
// the rest of the app reads (interfaces/camera.hpp).
//
// The decision is camera_web.js's: only it can read the track back, and it
// answers with a number per control - what became of it, the value asked for
// and the value read back. This is the other side of that, kept apart from
// camera_web.cpp (which cannot be linked into a test, for it calls the page)
// so that what it will and will not turn into a report is tested under Node
// with the rest of the web build's cases (tests/platform/web).
namespace cube::platform::camera::web {

// camera_web.js answers with these numbers (NOT_REQUESTED, UNSUPPORTED,
// REJECTED, APPLIED in CubeCamera): they are the enumerators' values.
static_assert(static_cast<int>(ControlOutcome::not_requested) == 0);
static_assert(static_cast<int>(ControlOutcome::unsupported) == 1);
static_assert(static_cast<int>(ControlOutcome::rejected) == 2);
static_assert(static_cast<int>(ControlOutcome::applied) == 3);

// One control as the page answers (camera_web.js, CubeCamera.lock): `outcome`
// is a ControlOutcome's value.
struct PageControl {
    int outcome = 0;
    double requested = 0.0;
    double readback = 0.0;
};

// The outcome the page's number stands for. A number that is not one the page
// knows is not a lock: it reads as refused, never as applied.
[[nodiscard]] constexpr ControlOutcome outcome_from_page(int code) noexcept {
    switch (code) {
        case static_cast<int>(ControlOutcome::not_requested):
            return ControlOutcome::not_requested;
        case static_cast<int>(ControlOutcome::unsupported):
            return ControlOutcome::unsupported;
        case static_cast<int>(ControlOutcome::applied):
            return ControlOutcome::applied;
        case static_cast<int>(ControlOutcome::rejected):
        default:
            return ControlOutcome::rejected;
    }
}

// The report for an answer. `offered` is whether the camera listed a manual
// mode for either control at all: false is "this browser or this camera has no
// control", which the summary words differently from a camera that was asked
// and would not (PhotometryLockReport::platform_supported).
//
// What the browser can hold is exposure and white balance - the two the
// automatics move while a cube is turned. Brightness and saturation are not
// touched, and the report says so (not_requested) rather than claiming them.
// The white balance control is the report's auto_white_balance: its
// requested/readback are the automatic flag (0 is "off"), as on the desktop,
// and the temperature it was held at is white_balance_kelvin.
[[nodiscard]] inline PhotometryLockReport report_from_page(bool offered,
                                                           const PageControl& exposure,
                                                           const PageControl& white_balance) {
    const auto finite = [](double value) { return std::isfinite(value) ? value : 0.0; };

    PhotometryLockReport report;
    for (std::size_t i = 0; i < kPhotometryControls.size(); ++i) {
        report.controls.at(i).control = kPhotometryControls.at(i);
    }
    report.platform_supported = offered;

    auto& awb = report.controls.at(static_cast<std::size_t>(PhotometryControl::auto_white_balance));
    awb.outcome = outcome_from_page(white_balance.outcome);
    if (awb.outcome == ControlOutcome::applied) {
        awb.requested = 0.0;
        awb.readback = 0.0;
        // A temperature that is not a positive number is not one it is holding.
        if (std::isfinite(white_balance.readback) && white_balance.readback > 0.0) {
            report.white_balance_kelvin = white_balance.readback;
        }
    } else if (awb.outcome == ControlOutcome::rejected) {
        awb.requested = 0.0;
        awb.readback = 1.0;
    }

    auto& exp = report.controls.at(static_cast<std::size_t>(PhotometryControl::exposure));
    exp.outcome = outcome_from_page(exposure.outcome);
    if (exp.outcome == ControlOutcome::applied || exp.outcome == ControlOutcome::rejected) {
        exp.requested = finite(exposure.requested);
        exp.readback = finite(exposure.readback);
    }
    return report;
}

}  // namespace cube::platform::camera::web
