// Photometry-lock bookkeeping: everything about the lock that does NOT need
// a camera. These run everywhere, in CI included - the hardware halves live
// in camera_property_probe_test.cpp (capability probe) and
// camera_lock_verification_test.cpp (does the lock actually hold the
// pixels), both hidden and env-gated.
//
// Worth testing on its own because the reporting IS the feature as much as
// the property writes are: the app is required to fail visibly. A lock that
// quietly reports success on a driver that refused it would put the user
// back where they started - colour samples that cannot be classified - with
// no hint that arranging their own lighting is the way out.

#include <catch2/catch_test_macros.hpp>

#include "interfaces/camera.hpp"
#include "platform/detail/opencv_capture_loop.hpp"
#include "platform/detail/photometry_lock.hpp"

#include <cube/error.hpp>

#include <opencv2/videoio.hpp>

#include <cstddef>
#include <string>

using cube::platform::camera::ControlOutcome;
using cube::platform::camera::kPhotometryControls;
using cube::platform::camera::PhotometryControl;
using cube::platform::camera::PhotometryLockReport;
using cube::platform::camera::PhotometryLockRequest;

namespace {

// What each control came to, by name: four outcomes of one type in a row would
// be easy to put in the wrong order.
struct Outcomes {
    ControlOutcome auto_white_balance = ControlOutcome::not_requested;
    ControlOutcome exposure = ControlOutcome::not_requested;
    ControlOutcome brightness = ControlOutcome::not_requested;
    ControlOutcome saturation = ControlOutcome::not_requested;
};

PhotometryLockReport report_with(const Outcomes& outcomes) {
    PhotometryLockReport report;
    report.platform_supported = true;
    for (std::size_t i = 0; i < kPhotometryControls.size(); ++i) {
        report.controls.at(i).control = kPhotometryControls.at(i);
    }
    report.controls.at(static_cast<std::size_t>(PhotometryControl::auto_white_balance)).outcome =
        outcomes.auto_white_balance;
    report.controls.at(static_cast<std::size_t>(PhotometryControl::exposure)).outcome =
        outcomes.exposure;
    report.controls.at(static_cast<std::size_t>(PhotometryControl::brightness)).outcome =
        outcomes.brightness;
    report.controls.at(static_cast<std::size_t>(PhotometryControl::saturation)).outcome =
        outcomes.saturation;
    return report;
}

}  // namespace

// PhotometryLockReport::result() indexes the array by the enumerator instead
// of searching it, which is only correct while kPhotometryControls is
// declared in enum order. Nothing else would catch a reordering: every
// lookup would keep compiling and start returning a different control's
// outcome - "exposure locked" reporting saturation's result, say.
TEST_CASE("photometry control ordering matches the enum", "[camera][photometry]") {
    for (std::size_t i = 0; i < kPhotometryControls.size(); ++i) {
        REQUIRE(static_cast<std::size_t>(kPhotometryControls.at(i)) == i);
    }

    const PhotometryLockReport report = report_with({
        .auto_white_balance = ControlOutcome::applied,
        .exposure = ControlOutcome::rejected,
        .brightness = ControlOutcome::unsupported,
        .saturation = ControlOutcome::not_requested,
    });
    REQUIRE(report.result(PhotometryControl::auto_white_balance).outcome ==
            ControlOutcome::applied);
    REQUIRE(report.result(PhotometryControl::exposure).outcome == ControlOutcome::rejected);
    REQUIRE(report.result(PhotometryControl::brightness).outcome == ControlOutcome::unsupported);
    REQUIRE(report.result(PhotometryControl::saturation).outcome == ControlOutcome::not_requested);
}

// exposure_locked() is the load-bearing one: exposure is the control the
// probe proved moves pixels, and per-face exposure drift (14-18 L points
// within one scan) is the measured defect the lock exists to remove. It must
// be true for 'applied' and for nothing else.
TEST_CASE("exposure_locked is true only when exposure was verified", "[camera][photometry]") {
    REQUIRE(report_with({
                            .auto_white_balance = ControlOutcome::applied,
                            .exposure = ControlOutcome::applied,
                            .brightness = ControlOutcome::applied,
                            .saturation = ControlOutcome::applied,
                        })
                .exposure_locked());
    REQUIRE_FALSE(report_with({
                                  .auto_white_balance = ControlOutcome::applied,
                                  .exposure = ControlOutcome::rejected,
                                  .brightness = ControlOutcome::applied,
                                  .saturation = ControlOutcome::applied,
                              })
                      .exposure_locked());
    REQUIRE_FALSE(report_with({
                                  .auto_white_balance = ControlOutcome::applied,
                                  .exposure = ControlOutcome::unsupported,
                                  .brightness = ControlOutcome::applied,
                                  .saturation = ControlOutcome::applied,
                              })
                      .exposure_locked());
    REQUIRE_FALSE(report_with({
                                  .auto_white_balance = ControlOutcome::applied,
                                  .exposure = ControlOutcome::not_requested,
                                  .brightness = ControlOutcome::applied,
                                  .saturation = ControlOutcome::applied,
                              })
                      .exposure_locked());
    REQUIRE_FALSE(PhotometryLockReport{}.exposure_locked());
}

// not_requested must not count as a failure - a caller that asked for
// exposure only has a fully applied lock if exposure applied.
TEST_CASE("fully_applied ignores controls nobody asked for", "[camera][photometry]") {
    REQUIRE(report_with({
                            .auto_white_balance = ControlOutcome::not_requested,
                            .exposure = ControlOutcome::applied,
                            .brightness = ControlOutcome::not_requested,
                            .saturation = ControlOutcome::not_requested,
                        })
                .fully_applied());
    REQUIRE_FALSE(report_with({
                                  .auto_white_balance = ControlOutcome::rejected,
                                  .exposure = ControlOutcome::applied,
                                  .brightness = ControlOutcome::applied,
                                  .saturation = ControlOutcome::applied,
                              })
                      .fully_applied());
    REQUIRE_FALSE(report_with({
                                  .auto_white_balance = ControlOutcome::unsupported,
                                  .exposure = ControlOutcome::applied,
                                  .brightness = ControlOutcome::applied,
                                  .saturation = ControlOutcome::applied,
                              })
                      .fully_applied());

    // A platform with no control at all is never "fully applied", however
    // empty its (untouched) control list looks.
    REQUIRE_FALSE(PhotometryLockReport{}.fully_applied());
}

// The summary is what the user reads. Each branch is checked for the two
// things that matter: it must not claim a control it did not verify, and
// when anything failed it must tell the user to sort out their own lighting.
TEST_CASE("summary tells the user the truth in every outcome", "[camera][photometry]") {
    SECTION("no platform support says so, and gives the manual fallback") {
        const std::string text = PhotometryLockReport{}.summary();
        REQUIRE(text.contains("not available on this platform"));
        REQUIRE(text.contains("lighting unchanged"));
    }

    SECTION("a complete lock names every control and asks nothing of the user") {
        const std::string text = report_with({
                                                 .auto_white_balance = ControlOutcome::applied,
                                                 .exposure = ControlOutcome::applied,
                                                 .brightness = ControlOutcome::applied,
                                                 .saturation = ControlOutcome::applied,
                                             })
                                     .summary();
        REQUIRE(text.contains("exposure"));
        REQUIRE(text.contains("auto white balance"));
        REQUIRE(text.contains("brightness"));
        REQUIRE(text.contains("saturation"));
        REQUIRE(!text.contains("would not lock"));
    }

    SECTION("a partial lock names the failure and still asks for fixed lighting") {
        // The realistic Windows case if AUTO_WB turns out to be refused:
        // exposure holds, white balance keeps drifting.
        const std::string text = report_with({
                                                 .auto_white_balance = ControlOutcome::rejected,
                                                 .exposure = ControlOutcome::applied,
                                                 .brightness = ControlOutcome::applied,
                                                 .saturation = ControlOutcome::applied,
                                             })
                                     .summary();
        REQUIRE(text.contains("Camera locked"));
        REQUIRE(text.contains("would not lock auto white balance"));
        REQUIRE(text.contains("lighting unchanged"));
    }

    SECTION("a total refusal never claims a lock") {
        const std::string text = report_with({
                                                 .auto_white_balance = ControlOutcome::rejected,
                                                 .exposure = ControlOutcome::rejected,
                                                 .brightness = ControlOutcome::unsupported,
                                                 .saturation = ControlOutcome::rejected,
                                             })
                                     .summary();
        REQUIRE(!text.contains("Camera locked"));
        REQUIRE(text.contains("refused every setting"));
        REQUIRE(text.contains("lighting unchanged"));
    }
}

// The controls are exactly the ones the hardware probe proved. GAIN and
// CONTRAST read back on the probed device but showed no pixel effect above
// the noise floor, and WB_TEMPERATURE/AUTO_EXPOSURE/GAMMA are not
// implemented by it at all - none of them may quietly appear here, because
// a control in this list is a control the user is told was locked.
TEST_CASE("only probe-proven properties are wired up", "[camera][photometry]") {
    using cube::platform::camera::detail::property_id;
    REQUIRE(kPhotometryControls.size() == 4);
    REQUIRE(property_id(PhotometryControl::auto_white_balance) == cv::CAP_PROP_AUTO_WB);
    REQUIRE(property_id(PhotometryControl::exposure) == cv::CAP_PROP_EXPOSURE);
    REQUIRE(property_id(PhotometryControl::brightness) == cv::CAP_PROP_BRIGHTNESS);
    REQUIRE(property_id(PhotometryControl::saturation) == cv::CAP_PROP_SATURATION);
}

// Read-back, not set()'s return value, is the evidence - and it is compared
// with a tolerance because white-balance-class properties quantise to a
// driver step. The tolerance must stay far tighter than the gap between
// adjacent meaningful values (one exposure stop, say) or a rejected write
// would pass as applied.
TEST_CASE("read-back tolerance accepts quantisation but not refusal", "[camera][photometry]") {
    using cube::platform::camera::detail::read_back_matches;
    REQUIRE(read_back_matches(-6.0, -6.0));
    REQUIRE(read_back_matches(128.0, 128.0));
    REQUIRE(read_back_matches(6500.0, 6496.0));     // quantised, still accepted
    REQUIRE_FALSE(read_back_matches(-6.0, -5.0));   // a whole stop away: refused
    REQUIRE_FALSE(read_back_matches(0.0, 1.0));     // auto-flag left on
    REQUIRE_FALSE(read_back_matches(128.0, -1.0));  // the unsupported sentinel
}

// The lock is a request against a running capture. With nothing capturing
// there is no device to write to and no capture thread to write from, so it
// must fail loudly rather than silently succeeding - and it must not open a
// device to try, which is what makes this test safe to run in CI.
TEST_CASE("locking without a capture fails without touching hardware", "[camera][photometry]") {
    REQUIRE_FALSE(cube::platform::camera::is_capturing());

    const auto locked = cube::platform::camera::request_photometry_lock(PhotometryLockRequest{});
    REQUIRE_FALSE(locked.has_value());
    REQUIRE(locked.error() == cube::CubeError::camera_control_unavailable);

    const auto restored = cube::platform::camera::request_photometry_restore();
    REQUIRE_FALSE(restored.has_value());
    REQUIRE(restored.error() == cube::CubeError::camera_control_unavailable);

    REQUIRE_FALSE(cube::platform::camera::take_photometry_report().has_value());
}

// Support is claimed only where it was measured. Only CAP_DSHOW has been
// measured against real hardware (the probe in
// camera_property_probe_test.cpp), so a platform's camera may claim
// photometry control if and only if it opens through that backend; every
// other platform must answer no rather than guess, and its refusal must be
// the "platform" flavour the summary words differently from a driver
// refusal. Asked of the backend the platform actually opens through, so no
// platform is named here.
TEST_CASE("photometry support is claimed only where it was measured", "[camera][photometry]") {
    const bool measured = cube::platform::camera::detail::capture_backend() == cv::CAP_DSHOW;
    REQUIRE(cube::platform::camera::photometry_control_supported() == measured);
    // A report nobody has produced yet is never one a backend vouched for.
    REQUIRE_FALSE(PhotometryLockReport{}.platform_supported);
}
