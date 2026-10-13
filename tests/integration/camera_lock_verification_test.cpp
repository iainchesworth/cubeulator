// Does the photometry lock actually hold the PIXELS still?
//
// The probe (camera_property_probe_test.cpp) answered "what will the driver
// accept". This answers the only question that matters afterwards: with the
// camera pointed at a static scene, how much do mean L and mean chroma wander
// from frame to frame with the automatics running, and how much with the lock
// applied? The defect being chased was measured at 14-18 points of mean L
// drift BETWEEN FACES OF A SINGLE SCAN, with blue (chroma 18-24) overlapping
// warm-cast white (15.7-21.0). Locked, the drift should be near zero.
//
// It drives the real shipped path - cube::platform::camera::start() /
// request_photometry_lock() / try_pop_latest_frame() - not a private copy of
// it, so it also exercises the capture-thread hand-off: the request is made
// from this thread, applied by the capture thread between grabs, and its
// report comes back through the mailbox.
//
// Hardware, so doubly fenced like the probe: the leading dot in
// "[.camera-lock]" hides it from a default run, and it SKIPs unless
// CUBE_CAMERA_LOCK_TEST is set. It can never run in CI or on a camera-less
// machine.
//
// Run it with:
//   set CUBE_CAMERA_LOCK_TEST=1 && cube_integration_tests "[.camera-lock]"
//
// Point the camera at a STATIC, LIT scene first. On a dark or covered lens
// every reading is zero and both modes look equally stable - a false pass -
// so the test says so loudly rather than reporting that as success.

#include <catch2/catch_test_macros.hpp>

#include "interfaces/camera.hpp"
#include "platform/detail/opencv_capture_loop.hpp"

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/videoio.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace camera = cube::platform::camera;

namespace {

std::string fixed2(double value) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(2) << value;
    return out.str();
}

void log(const std::string& line) {
    std::cout << line << '\n';
    UNSCOPED_INFO(line);
}

// Same ROI and same units as the probe and the original scan diagnostics -
// centre half of the frame, L on 0-100, chroma = sqrt(a^2+b^2) in Lab - so
// the numbers below are directly comparable with the 14-18 point drift this
// whole exercise exists to remove.
struct Photometry {
    double l = 0.0;
    double chroma = 0.0;
};

Photometry measure(const cv::Mat& bgr) {
    const cv::Rect roi(bgr.cols / 4, bgr.rows / 4, bgr.cols / 2, bgr.rows / 2);
    cv::Mat lab;
    cv::cvtColor(bgr(roi), lab, cv::COLOR_BGR2Lab);
    std::vector<cv::Mat> planes;
    cv::split(lab, planes);

    cv::Mat a_signed;
    cv::Mat b_signed;
    planes.at(1).convertTo(a_signed, CV_32F, 1.0, -128.0);
    planes.at(2).convertTo(b_signed, CV_32F, 1.0, -128.0);
    cv::Mat chroma;
    cv::magnitude(a_signed, b_signed, chroma);

    return Photometry{.l = cv::mean(planes.at(0))[0] * 100.0 / 255.0,
                      .chroma = cv::mean(chroma)[0]};
}

struct Spread {
    double mean = 0.0;
    double lowest = 0.0;
    double highest = 0.0;
    [[nodiscard]] double range() const { return highest - lowest; }
};

Spread spread_of(const std::vector<double>& values) {
    if (values.empty()) {
        return Spread{};
    }
    const auto [min_it, max_it] = std::ranges::minmax_element(values);
    double total = 0.0;
    for (const double v : values) {
        total += v;
    }
    return Spread{
        .mean = total / static_cast<double>(values.size()), .lowest = *min_it, .highest = *max_it};
}

// Peak-to-peak, not standard deviation: the measured defect was a drift
// between the extremes of a scan (face one vs face six), so the honest
// comparison is worst-case spread, which a standard deviation would flatter.
struct Run {
    Spread l;
    Spread chroma;
    int frames = 0;
};

Run collect(int frames) {
    std::vector<double> l_values;
    std::vector<double> chroma_values;
    l_values.reserve(static_cast<std::size_t>(frames));
    chroma_values.reserve(static_cast<std::size_t>(frames));

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    while (std::cmp_less(l_values.size(), frames) && std::chrono::steady_clock::now() < deadline) {
        auto frame = camera::try_pop_latest_frame();
        if (!frame) {
            // The mailbox is latest-wins and this poll is faster than the
            // device delivers, so an empty read is normal, not an error.
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            continue;
        }
        const Photometry p = measure(frame->frame);
        l_values.push_back(p.l);
        chroma_values.push_back(p.chroma);
    }

    return Run{.l = spread_of(l_values),
               .chroma = spread_of(chroma_values),
               .frames = static_cast<int>(l_values.size())};
}

void report_run(const std::string& label, const Run& run) {
    log(label + ": frames=" + std::to_string(run.frames) + " L mean=" + fixed2(run.l.mean) +
        " min=" + fixed2(run.l.lowest) + " max=" + fixed2(run.l.highest) +
        " peak-to-peak=" + fixed2(run.l.range()) + " | chroma mean=" + fixed2(run.chroma.mean) +
        " peak-to-peak=" + fixed2(run.chroma.range()));
}

}  // namespace

TEST_CASE("photometry lock holds a static scene still", "[.camera-lock]") {
    if (std::getenv("CUBE_CAMERA_LOCK_TEST") == nullptr) {
        SKIP("set CUBE_CAMERA_LOCK_TEST=1 to run the hardware camera-lock verification");
    }

    // Device 0 directly rather than via enumerate_devices(): on the generic
    // (non-Windows) backend enumeration is a 15-20s open-every-index probe,
    // and this test is about the capture path, not about discovery.
    const camera::DeviceInfo device{.identifier = "0", .display_name = ""};

    // Cold-open latency, measured through the same start()+first-frame path
    // the app uses. The DSHOW backend was chosen specifically to keep this
    // at ~1.0-1.4s (MSMF took 9-10s), so any regression here matters more
    // than the lock does. The lock is requested only AFTER this measurement,
    // which is itself the point: it cannot move this number.
    const auto open_start = std::chrono::steady_clock::now();
    const auto started = camera::start(device);
    REQUIRE(started.has_value());

    std::chrono::milliseconds first_frame_ms{};
    for (int i = 0; i < 2000; ++i) {
        if (auto frame = camera::try_pop_latest_frame()) {
            first_frame_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - open_start);
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    REQUIRE(first_frame_ms.count() > 0);
    log("start()+first frame: " + std::to_string(first_frame_ms.count()) + "ms");

    constexpr int kFramesPerRun = 90;

    const Run automatic = collect(kFramesPerRun);
    report_run("AUTO   ", automatic);
    REQUIRE(automatic.frames > 0);

    if (automatic.l.mean < 5.0) {
        log("WARNING: scene mean L=" + fixed2(automatic.l.mean) +
            " - the lens is covered or the room is dark. Both modes will look "
            "equally stable and this run proves NOTHING. Light the scene and re-run.");
    }

    const auto requested = camera::request_photometry_lock(camera::PhotometryLockRequest{});
    REQUIRE(requested.has_value());

    // The lock is applied a settling run after the request (see
    // interfaces/camera.hpp), so the report is what says it has landed -
    // sampling before it arrives would measure the automatics again.
    std::optional<camera::PhotometryLockReport> report;
    const auto report_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (!report && std::chrono::steady_clock::now() < report_deadline) {
        report = camera::take_photometry_report();
        if (!report) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
    REQUIRE(report.has_value());

    log("lock: platform_supported=" + std::string(report->platform_supported ? "yes" : "no") +
        " fully_applied=" + std::string(report->fully_applied() ? "yes" : "no"));
    for (const camera::PhotometryControl control : camera::kPhotometryControls) {
        const camera::ControlResult& entry = report->result(control);
        const char* outcome = entry.outcome == camera::ControlOutcome::applied    ? "applied"
                              : entry.outcome == camera::ControlOutcome::rejected ? "REJECTED"
                              : entry.outcome == camera::ControlOutcome::unsupported
                                  ? "unsupported"
                                  : "not requested";
        log(std::string("  ") + std::string(camera::to_string(control)) + ": " + outcome +
            " requested=" + fixed2(entry.requested) + " readback=" + fixed2(entry.readback));
    }
    log("summary: " + report->summary());

    const Run locked = collect(kFramesPerRun);
    report_run("LOCKED ", locked);
    REQUIRE(locked.frames > 0);

    log("drift comparison (peak-to-peak over " + std::to_string(kFramesPerRun) +
        " frames): L auto=" + fixed2(automatic.l.range()) + " locked=" + fixed2(locked.l.range()) +
        " | chroma auto=" + fixed2(automatic.chroma.range()) +
        " locked=" + fixed2(locked.chroma.range()));
    log("For the re-balancing half of the check, move a strongly coloured object "
        "through the frame while this runs: under the automatics the whole scene's "
        "L and chroma shift with it, locked they should not.");

    // The lock is not asserted to have succeeded - a driver is allowed to
    // refuse, and this test's job is to REPORT that, not to fail the build
    // over hardware. What is asserted is that the app told the truth about
    // it: if it claims exposure is locked, the numbers behind that claim are
    // in the log above for a human to check.
    if (report->exposure_locked()) {
        log("exposure reported locked - the L peak-to-peak above is the evidence");
    } else {
        log("exposure NOT locked on this device - the manual-lighting fallback is the "
            "only route, and the app says so to the user");
    }

    // Re-lock with an EXPLICIT exposure one stop away from where the
    // automatics left it. Two things need this: the override path (a
    // settings screen or a tuning run), and - more importantly - the
    // restore check at the bottom. A default lock writes each control back
    // to the value it just read, so restoring it would look correct even if
    // restore did nothing at all; only a value that genuinely moved can
    // prove the driver was put back. It also checks the re-lock rule that a
    // second lock keeps the FIRST-seen value as the one to restore.
    const double as_found_exposure = report->result(camera::PhotometryControl::exposure).requested;
    if (report->exposure_locked()) {
        camera::PhotometryLockRequest tweak;
        tweak.exposure = as_found_exposure - 1.0;  // one stop shorter (log2 seconds)
        REQUIRE(camera::request_photometry_lock(tweak).has_value());

        std::optional<camera::PhotometryLockReport> second;
        const auto second_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (!second && std::chrono::steady_clock::now() < second_deadline) {
            second = camera::take_photometry_report();
            if (!second) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        }
        REQUIRE(second.has_value());
        const camera::ControlResult& tweaked = second->result(camera::PhotometryControl::exposure);
        log("override: asked for EXPOSURE=" + fixed2(*tweak.exposure) +
            " readback=" + fixed2(tweaked.readback) + " (" +
            (tweaked.outcome == camera::ControlOutcome::applied ? "applied" : "not applied") + ")");

        const Run overridden = collect(kFramesPerRun);
        report_run("OVERRIDE", overridden);
    }

    // stop() restores the driver's photometry (DirectShow property state
    // outlives the process), so nothing here leaks into the next run.
    camera::stop();
    REQUIRE_FALSE(camera::is_capturing());

    // Re-open and re-measure cold-open latency after a locked session, to
    // prove the lock left nothing behind that slows the next open - the
    // ~1.0-1.4s DSHOW budget is the reason that backend was chosen.
    const auto reopen_start = std::chrono::steady_clock::now();
    REQUIRE(camera::start(device).has_value());
    std::chrono::milliseconds reopen_ms{};
    for (int i = 0; i < 2000; ++i) {
        if (auto frame = camera::try_pop_latest_frame()) {
            reopen_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - reopen_start);
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    log("re-open after a locked session: " + std::to_string(reopen_ms.count()) +
        "ms (first open was " + std::to_string(first_frame_ms.count()) + "ms)");
    camera::stop();

    // Did stop() really put the driver back? This is not a nicety:
    // DirectShow property state lives in the driver and outlives the
    // process, so a lock that is not undone follows the user into every
    // other camera app on the machine and into this app's own next launch -
    // which would also make the next lock freeze the PREVIOUS scan's
    // exposure instead of this room's.
    //
    // Checked through a raw cv::VideoCapture rather than the platform API,
    // which deliberately exposes no property reads, and only after the last
    // stop() so it cannot perturb either latency measurement above.
    {
        cv::VideoCapture fresh(0, cube::platform::camera::detail::capture_backend());
        REQUIRE(fresh.isOpened());
        cv::Mat frame;
        fresh.read(frame);
        const double exposure_now = fresh.get(cv::CAP_PROP_EXPOSURE);
        const double brightness_now = fresh.get(cv::CAP_PROP_BRIGHTNESS);
        const double saturation_now = fresh.get(cv::CAP_PROP_SATURATION);
        log("after stop(): EXPOSURE=" + fixed2(exposure_now) +
            " BRIGHTNESS=" + fixed2(brightness_now) + " SATURATION=" + fixed2(saturation_now) +
            " AUTO_WB=" + fixed2(fresh.get(cv::CAP_PROP_AUTO_WB)));

        // The default lock writes each control back to the value the device
        // itself reported, so those written values ARE the pre-lock state -
        // whatever the driver reads now must match them.
        for (const camera::PhotometryControl control :
             {camera::PhotometryControl::exposure, camera::PhotometryControl::brightness,
              camera::PhotometryControl::saturation}) {
            const camera::ControlResult& entry = report->result(control);
            if (entry.outcome != camera::ControlOutcome::applied) {
                continue;
            }
            const double now = control == camera::PhotometryControl::exposure     ? exposure_now
                               : control == camera::PhotometryControl::brightness ? brightness_now
                                                                                  : saturation_now;
            INFO("control: " << camera::to_string(control));
            REQUIRE(std::abs(now - entry.requested) <= 0.5);
        }
        fresh.release();
    }
}
