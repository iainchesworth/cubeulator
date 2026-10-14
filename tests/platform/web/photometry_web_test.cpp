// Test cases only the web build can run: what the page's answer about the
// camera's exposure and white balance becomes in the report the rest of the app
// reads (platform/web/photometry_web.hpp). Compiled into
// cube_integration_tests only for the web platform (tests/CMakeLists.txt picks
// tests/platform/<platform name>/) and run under Node with the rest of that
// suite.
//
// The page's half - what it reads off the track, and what it takes for a lock -
// is the browser suite's (tests/web/specs/camera.spec.mjs); this is the half
// after it, where the same honesty has to hold: a number the page sends that is
// not a lock must never come out as one, and a camera it could not ask must not
// be worded as one that refused.
#include <catch2/catch_test_macros.hpp>

#include "interfaces/camera.hpp"
#include "platform/web/photometry_web.hpp"

#include <limits>
#include <string>

namespace {

using cube::platform::camera::ControlOutcome;
using cube::platform::camera::PhotometryControl;
using cube::platform::camera::PhotometryLockReport;
using cube::platform::camera::web::outcome_from_page;
using cube::platform::camera::web::PageControl;
using cube::platform::camera::web::report_from_page;

constexpr int kNotRequested = 0;
constexpr int kUnsupported = 1;
constexpr int kRejected = 2;
constexpr int kApplied = 3;

ControlOutcome outcome_of(const PhotometryLockReport& report, PhotometryControl control) {
    return report.result(control).outcome;
}

}  // namespace

// camera_web.js writes these four numbers as literals (CubeCamera.NOT_REQUESTED
// and the rest): the page and this file have to agree on what each one is.
TEST_CASE("the page's outcome numbers are the report's outcomes", "[camera][photometry][web]") {
    REQUIRE(outcome_from_page(kNotRequested) == ControlOutcome::not_requested);
    REQUIRE(outcome_from_page(kUnsupported) == ControlOutcome::unsupported);
    REQUIRE(outcome_from_page(kRejected) == ControlOutcome::rejected);
    REQUIRE(outcome_from_page(kApplied) == ControlOutcome::applied);
}

// Only the number the page means as "read back and held" is a lock. Anything
// else it might send - a code from a newer page, a corrupt value - is a
// refusal: not "applied", and not silently "nothing was asked".
TEST_CASE("a number the page does not know is a refusal, never a lock",
          "[camera][photometry][web]") {
    for (const int code : {-1, 4, 7, 255, 1000000}) {
        CAPTURE(code);
        REQUIRE(outcome_from_page(code) == ControlOutcome::rejected);

        const PhotometryLockReport report =
            report_from_page(true, {.outcome = code}, {.outcome = code});
        REQUIRE_FALSE(report.exposure_locked());
        REQUIRE_FALSE(report.fully_applied());
        REQUIRE(report.summary().find("Camera locked") == std::string::npos);
    }
}

TEST_CASE("a lock the page read back is reported with what it held", "[camera][photometry][web]") {
    const PhotometryLockReport report =
        report_from_page(true, {.outcome = kApplied, .requested = 166.0, .readback = 166.0},
                         {.outcome = kApplied, .requested = 4650.0, .readback = 4650.0});

    REQUIRE(report.platform_supported);
    REQUIRE(outcome_of(report, PhotometryControl::exposure) == ControlOutcome::applied);
    REQUIRE(outcome_of(report, PhotometryControl::auto_white_balance) == ControlOutcome::applied);
    REQUIRE(report.exposure_locked());
    REQUIRE(report.result(PhotometryControl::exposure).requested == 166.0);
    REQUIRE(report.result(PhotometryControl::exposure).readback == 166.0);
    // The temperature it holds, as the desktop's report gives it; the control
    // itself is the automatic flag, which is off (0).
    REQUIRE(report.white_balance_kelvin.has_value());
    REQUIRE(*report.white_balance_kelvin == 4650.0);
    REQUIRE(report.result(PhotometryControl::auto_white_balance).readback == 0.0);

    // The browser holds exposure and white balance and says it holds no more:
    // brightness and saturation are not the page's to claim.
    REQUIRE(outcome_of(report, PhotometryControl::brightness) == ControlOutcome::not_requested);
    REQUIRE(outcome_of(report, PhotometryControl::saturation) == ControlOutcome::not_requested);
    REQUIRE(report.fully_applied());
    REQUIRE(report.summary() == "Camera locked: auto white balance and exposure.");
}

TEST_CASE("every control of a report says which control it is", "[camera][photometry][web]") {
    const PhotometryLockReport report = report_from_page(false, {}, {});
    for (std::size_t i = 0; i < cube::platform::camera::kPhotometryControls.size(); ++i) {
        REQUIRE(report.controls.at(i).control == cube::platform::camera::kPhotometryControls.at(i));
    }
}

// A camera the page could not ask - Safari, Firefox, a phone's front lens - is
// "this platform cannot", which the summary words differently from a camera
// that was asked and said no. Both controls are marked unsupported, which is
// what was found, rather than left as never asked.
TEST_CASE("a camera with nothing to ask is not worded as a refusal", "[camera][photometry][web]") {
    const PhotometryLockReport report =
        report_from_page(false, {.outcome = kUnsupported}, {.outcome = kUnsupported});

    REQUIRE_FALSE(report.platform_supported);
    REQUIRE(outcome_of(report, PhotometryControl::exposure) == ControlOutcome::unsupported);
    REQUIRE(outcome_of(report, PhotometryControl::auto_white_balance) ==
            ControlOutcome::unsupported);
    REQUIRE_FALSE(report.exposure_locked());
    REQUIRE_FALSE(report.fully_applied());
    REQUIRE_FALSE(report.white_balance_kelvin.has_value());
    REQUIRE(report.summary().find("not available on this platform") != std::string::npos);
    REQUIRE(report.summary().find("lighting unchanged") != std::string::npos);
}

TEST_CASE("a camera that was asked and would not hold is worded as a refusal",
          "[camera][photometry][web]") {
    const PhotometryLockReport report =
        report_from_page(true, {.outcome = kRejected, .requested = 166.0, .readback = 40.0},
                         {.outcome = kRejected, .requested = 4650.0, .readback = 3000.0});

    REQUIRE(report.platform_supported);
    REQUIRE_FALSE(report.exposure_locked());
    REQUIRE_FALSE(report.fully_applied());
    // A refusal holds no temperature, whatever the camera read back.
    REQUIRE_FALSE(report.white_balance_kelvin.has_value());
    // What was asked and what came back are kept: the diagnostics channel
    // shows them, and they are how a person finds out why.
    REQUIRE(report.result(PhotometryControl::exposure).requested == 166.0);
    REQUIRE(report.result(PhotometryControl::exposure).readback == 40.0);
    REQUIRE(report.summary().find("Camera locked") == std::string::npos);
    REQUIRE(report.summary().find("refused every setting") != std::string::npos);
}

TEST_CASE("one held control and one refused is a partial lock, said as one",
          "[camera][photometry][web]") {
    const PhotometryLockReport report =
        report_from_page(true, {.outcome = kApplied, .requested = 166.0, .readback = 166.0},
                         {.outcome = kRejected, .requested = 4650.0, .readback = 4650.0});

    REQUIRE(report.exposure_locked());
    REQUIRE_FALSE(report.fully_applied());
    REQUIRE_FALSE(report.white_balance_kelvin.has_value());
    REQUIRE(report.summary().find("Camera locked: exposure.") != std::string::npos);
    REQUIRE(report.summary().find("would not lock auto white balance") != std::string::npos);
}

// A control nobody asked for (the request had it off) is not a failure.
TEST_CASE("a control that was not requested does not count against the lock",
          "[camera][photometry][web]") {
    const PhotometryLockReport report =
        report_from_page(true, {.outcome = kApplied, .requested = 166.0, .readback = 166.0},
                         {.outcome = kNotRequested});

    REQUIRE(report.exposure_locked());
    REQUIRE(report.fully_applied());
    REQUIRE(outcome_of(report, PhotometryControl::auto_white_balance) ==
            ControlOutcome::not_requested);
}

// The numbers cross as doubles from JavaScript, where a value that is not a
// number is NaN. The report is serialised by the diagnostics channel and shown
// by screens; it carries finite values or none.
TEST_CASE("values that are not numbers do not reach the report", "[camera][photometry][web]") {
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double inf = std::numeric_limits<double>::infinity();
    const PhotometryLockReport report =
        report_from_page(true, {.outcome = kApplied, .requested = nan, .readback = inf},
                         {.outcome = kApplied, .requested = 4650.0, .readback = nan});

    REQUIRE(report.result(PhotometryControl::exposure).requested == 0.0);
    REQUIRE(report.result(PhotometryControl::exposure).readback == 0.0);
    // No temperature is held that was not read back as one.
    REQUIRE_FALSE(report.white_balance_kelvin.has_value());
}
