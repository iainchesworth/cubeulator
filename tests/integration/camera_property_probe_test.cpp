// Hardware capability probe for camera photometry control.
//
// WHY THIS EXISTS
// ---------------
// A full day of scan diagnostics traced the "cannot assemble a valid cube"
// failure to the captured colour samples themselves, not to the vision
// pipeline. In the user's ordinary room (warm lamp, camera left on auto
// white balance) BLUE arrived at Lab chroma 18-24 while warm-cast WHITE
// arrived at 15.7-21.0 - the two distributions overlap, so no classifier
// can separate them. Per-face mean L drifted 14-18 points WITHIN a single
// scan as the camera's auto-exposure hunted between faces.
//
// Re-recording the same cube with a front light and a paper backdrop
// (D:/cubeulator-build/vision-test-frames/recordings/good1, 434 frames)
// produced BLUE chroma 35.3-43.7 against WHITE chroma 4.2-14.5 - a clean
// ~20-point gap. So the colour problem is solved by capture conditions,
// and today that fix depends on the user arranging their room by hand.
// Kociemba's cube-vision notes reach the same conclusion: disabling
// automatic white balance and automatic brightness and setting them
// manually gives better results.
//
// Before any of that can be automated we have to know what the actual
// hardware will let us do. OpenCV's set() return value is NOT evidence on
// the DirectShow backend - it frequently returns true for properties the
// driver silently ignores - and a property that reads back correctly may
// still leave the pixels untouched, which is a well known DirectShow trap.
// So this probe reports three independent things per property: the value
// read before any change, whether a changed value survives a read-back,
// and whether the frames themselves measurably moved.
//
// This test touches real hardware, so it is doubly fenced: the leading dot
// in "[.camera-probe]" hides it from a default Catch2 run (it only runs
// when the tag is named explicitly), and it SKIPs unless CUBE_CAMERA_PROBE
// is set. It therefore cannot run in CI or on a machine with no camera.
//
// Run it with:
//   set CUBE_CAMERA_PROBE=1 && cube_integration_tests "[.camera-probe]"

#include <catch2/catch_test_macros.hpp>

#include <opencv2/imgproc.hpp>
#include <opencv2/videoio.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <ios>
#include <iostream>
#include <sstream>
#include <string>
#include <tuple>
#include <vector>

namespace {

// The app opens Windows cameras through CAP_DSHOW (see
// src/app/platform/windows/camera_windows.cpp): CAP_MSMF took ~9-10s to
// cold-open the user's StreamCam against DSHOW's ~1.0-1.4s. Probing any
// other backend would tell us nothing about what the shipped app can do,
// since property support is a property of the backend, not of the device.
#if defined(_WIN32)
constexpr cv::VideoCaptureAPIs kBackend = cv::CAP_DSHOW;
constexpr const char* kBackendName = "CAP_DSHOW";
#else
constexpr cv::VideoCaptureAPIs kBackend = cv::CAP_ANY;
constexpr const char* kBackendName = "CAP_ANY";
#endif

struct PropertySpec {
    int id;
    const char* name;
    // Candidate values swept to discover what the driver accepts.
    //
    // An earlier version of this probe tried to recover each range by
    // setting +/-1e6 and reading back what the driver clamped to. That
    // worked for CAP_PROP_EXPOSURE and was actively misleading for
    // everything else: this device REJECTS an out-of-range write outright,
    // leaving the property at its previous value, so the clamp probe
    // reported "unsettable" for properties that in fact accept ordinary
    // in-range values. Sweeping plausible in-range candidates and printing
    // the read-back is the honest version - nothing here is asserted to be
    // the device's range, it is only the set of values we asked about, and
    // the report says which ones came back.
    std::vector<double> candidates;
};

const std::vector<PropertySpec>& property_specs() {
    // Spans the encodings the UVC/DirectShow world actually uses, since we
    // must not hardcode one device's numbers: 0..255 for the IAMVideoProcAmp
    // sliders, Kelvin for white balance, log2-seconds for DirectShow
    // exposure, and the 0.25=manual / 0.75=auto convention that OpenCV's
    // DSHOW backend commonly (but not universally) exposes for auto flags.
    static const std::vector<PropertySpec> specs = {
        {cv::CAP_PROP_AUTO_WB, "CAP_PROP_AUTO_WB", {0, 1}},
        {cv::CAP_PROP_WB_TEMPERATURE,
         "CAP_PROP_WB_TEMPERATURE",
         {2000, 2800, 3200, 4000, 5000, 6500, 10000}},
        {cv::CAP_PROP_AUTO_EXPOSURE, "CAP_PROP_AUTO_EXPOSURE", {0, 0.25, 0.75, 1, 2, 3}},
        {cv::CAP_PROP_EXPOSURE, "CAP_PROP_EXPOSURE", {-11, -9, -7, -6, -5, -4, -3, -2, -1, 0}},
        {cv::CAP_PROP_BRIGHTNESS, "CAP_PROP_BRIGHTNESS", {0, 32, 64, 128, 192, 255}},
        {cv::CAP_PROP_CONTRAST, "CAP_PROP_CONTRAST", {0, 32, 64, 128, 192, 255}},
        {cv::CAP_PROP_SATURATION, "CAP_PROP_SATURATION", {0, 32, 64, 128, 192, 255}},
        {cv::CAP_PROP_GAIN, "CAP_PROP_GAIN", {0, 32, 64, 128, 192, 255}},
        {cv::CAP_PROP_GAMMA, "CAP_PROP_GAMMA", {1, 2, 5, 72, 100, 150, 200, 300}},
        {cv::CAP_PROP_SHARPNESS, "CAP_PROP_SHARPNESS", {0, 32, 64, 128, 192, 255}},
        {cv::CAP_PROP_BACKLIGHT, "CAP_PROP_BACKLIGHT", {0, 1, 2, 3}},
        {cv::CAP_PROP_ZOOM, "CAP_PROP_ZOOM", {100, 150, 200, 300, 500}},
        {cv::CAP_PROP_FOCUS, "CAP_PROP_FOCUS", {0, 30, 60, 120, 250}},
        {cv::CAP_PROP_AUTOFOCUS, "CAP_PROP_AUTOFOCUS", {0, 1}},
    };
    return specs;
}

// Photometry of a fixed centre ROI, in the same units the scan diagnostics
// quoted so the numbers are directly comparable: L on 0-100 and chroma as
// sqrt(a^2+b^2) in Lab units about the neutral axis.
struct Photometry {
    double mean_l = 0.0;
    double mean_chroma = 0.0;
    double mean_b = 0.0;
    double mean_g = 0.0;
    double mean_r = 0.0;
};

Photometry measure(const cv::Mat& bgr) {
    // Centre half of the frame: away from vignetting and from whatever is
    // at the edge of the desk, and the same region the scanner samples.
    const cv::Rect roi(bgr.cols / 4, bgr.rows / 4, bgr.cols / 2, bgr.rows / 2);
    const cv::Mat patch = bgr(roi);

    const cv::Scalar bgr_mean = cv::mean(patch);

    cv::Mat lab;
    cv::cvtColor(patch, lab, cv::COLOR_BGR2Lab);
    std::vector<cv::Mat> planes;
    cv::split(lab, planes);

    cv::Mat a_signed;
    cv::Mat b_signed;
    // OpenCV's 8-bit Lab stores a/b biased by 128 and L scaled by 255/100.
    planes[1].convertTo(a_signed, CV_32F, 1.0, -128.0);
    planes[2].convertTo(b_signed, CV_32F, 1.0, -128.0);
    cv::Mat chroma;
    cv::magnitude(a_signed, b_signed, chroma);

    Photometry out;
    out.mean_l = cv::mean(planes[0])[0] * 100.0 / 255.0;
    out.mean_chroma = cv::mean(chroma)[0];
    out.mean_b = bgr_mean[0];
    out.mean_g = bgr_mean[1];
    out.mean_r = bgr_mean[2];
    return out;
}

// Cameras answer a property change several frames later (the driver's own
// pipeline is in flight), so every measurement throws away a settling run
// before averaging. Undersampling here would make a working property look
// like a no-op, which is exactly the mistake this probe exists to avoid.
constexpr int kSettleFrames = 12;
constexpr int kAverageFrames = 10;

Photometry sample(cv::VideoCapture& capture) {
    cv::Mat frame;
    for (int i = 0; i < kSettleFrames; ++i) {
        capture.read(frame);
    }

    Photometry total;
    int counted = 0;
    for (int i = 0; i < kAverageFrames; ++i) {
        if (!capture.read(frame) || frame.empty()) {
            continue;
        }
        const Photometry one = measure(frame);
        total.mean_l += one.mean_l;
        total.mean_chroma += one.mean_chroma;
        total.mean_b += one.mean_b;
        total.mean_g += one.mean_g;
        total.mean_r += one.mean_r;
        ++counted;
    }
    if (counted == 0) {
        return Photometry{};
    }
    const auto n = static_cast<double>(counted);
    return Photometry{total.mean_l / n, total.mean_chroma / n, total.mean_b / n, total.mean_g / n,
                      total.mean_r / n};
}

std::string fixed2(double value) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(2) << value;
    return out.str();
}

// set() then get(): the read-back, not set()'s own return value, is the
// only trustworthy evidence on DSHOW.
double set_and_read(cv::VideoCapture& capture, int prop, double value) {
    capture.set(prop, value);
    return capture.get(prop);
}

void log(const std::string& line) {
    std::cout << line << '\n';
    UNSCOPED_INFO(line);
}

}  // namespace

TEST_CASE("camera property probe reports real DirectShow capability", "[.camera-probe]") {
    if (std::getenv("CUBE_CAMERA_PROBE") == nullptr) {
        SKIP("set CUBE_CAMERA_PROBE=1 to run the hardware camera probe");
    }

    const auto open_start = std::chrono::steady_clock::now();
    cv::VideoCapture capture(0, kBackend);
    if (!capture.isOpened()) {
        log("FAILED to open device 0 via a backend - is the camera in use by another app?");
        REQUIRE(capture.isOpened());
    }

    cv::Mat first;
    const bool got_first = capture.read(first);
    const auto open_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                             std::chrono::steady_clock::now() - open_start)
                             .count();

    log(std::string("backend=") + kBackendName + " open+first-frame=" + std::to_string(open_ms) +
        "ms first_frame_ok=" + (got_first ? "yes" : "no") + " size=" + std::to_string(first.cols) +
        "x" + std::to_string(first.rows) +
        " fourcc=" + std::to_string(static_cast<long long>(capture.get(cv::CAP_PROP_FOURCC))) +
        " fps=" + fixed2(capture.get(cv::CAP_PROP_FPS)));
    REQUIRE(got_first);

    const Photometry baseline = sample(capture);
    log("baseline ROI photometry: L=" + fixed2(baseline.mean_l) +
        " chroma=" + fixed2(baseline.mean_chroma) + " B=" + fixed2(baseline.mean_b) +
        " G=" + fixed2(baseline.mean_g) + " R=" + fixed2(baseline.mean_r));

    // The read-back half of this probe is scene-independent, but the pixel
    // half is not: against a black frame every property looks like a no-op,
    // which would be a false negative on exactly the question that matters.
    // Say so loudly rather than letting a dark run masquerade as evidence.
    if (baseline.mean_l < 5.0) {
        log("WARNING: scene mean L=" + fixed2(baseline.mean_l) +
            " - lens is covered or the room is dark. Read-back results below are still valid; "
            "pixel-change results are NOT trustworthy until this is re-run on a lit scene.");
    }

    for (const PropertySpec& spec : property_specs()) {
        const double original = capture.get(spec.id);

        std::vector<double> accepted;
        std::string sweep;
        for (const double candidate : spec.candidates) {
            const double readback = set_and_read(capture, spec.id, candidate);
            // Tolerance rather than equality: white-balance temperature in
            // particular is quantised to a driver step, so an accepted
            // 6500 can legitimately read back as 6496.
            const bool ok =
                std::abs(readback - candidate) <= std::max(0.5, 0.02 * std::abs(candidate));
            sweep += " " + fixed2(candidate) + "->" + fixed2(readback) + (ok ? "[ok]" : "[no]");
            if (ok) {
                accepted.push_back(candidate);
            }
        }
        capture.set(spec.id, original);
        const double restored = capture.get(spec.id);

        log(std::string("\n== ") + spec.name + " default=" + fixed2(original) +
            " restored=" + fixed2(restored) + " accepted=" + std::to_string(accepted.size()) + "/" +
            std::to_string(spec.candidates.size()));
        log("   sweep:" + sweep);

        if (accepted.empty()) {
            continue;
        }

        // Pixel evidence. A property that reads back but leaves the image
        // alone is not usable for photometric locking, and reporting it as
        // usable would be worse than reporting nothing - this is the
        // classic DirectShow trap the probe exists to catch.
        const double lowest = accepted.front();
        const double highest = accepted.back();
        for (const double target : (lowest == highest ? std::vector<double>{lowest}
                                                      : std::vector<double>{lowest, highest})) {
            const double applied = set_and_read(capture, spec.id, target);
            const Photometry after = sample(capture);
            log("   set " + fixed2(target) + " -> readback " + fixed2(applied) +
                " | dL=" + fixed2(after.mean_l - baseline.mean_l) +
                " dChroma=" + fixed2(after.mean_chroma - baseline.mean_chroma) +
                " dB=" + fixed2(after.mean_b - baseline.mean_b) +
                " dG=" + fixed2(after.mean_g - baseline.mean_g) +
                " dR=" + fixed2(after.mean_r - baseline.mean_r));
        }

        capture.set(spec.id, original);
        sample(capture);
    }

    // Dependency ordering. On UVC hardware the manual white-balance and
    // manual exposure values are only honoured once the corresponding auto
    // flag is off, so a sweep taken with auto still running can report a
    // property as dead when it is merely being overridden. Re-sweep the two
    // properties the lock actually needs, this time with auto disabled
    // first - if the answer differs, the eventual implementation has a
    // mandatory ordering constraint.
    {
        const double auto_wb_readback = set_and_read(capture, cv::CAP_PROP_AUTO_WB, 0);
        std::string wb_sweep;
        for (const double kelvin : {2800.0, 4000.0, 5000.0, 6500.0}) {
            wb_sweep += " " + fixed2(kelvin) + "->" +
                        fixed2(set_and_read(capture, cv::CAP_PROP_WB_TEMPERATURE, kelvin));
        }
        log("\nAUTO_WB=0 readback=" + fixed2(auto_wb_readback) +
            " then WB_TEMPERATURE sweep:" + wb_sweep);

        // Pixel proof that white balance actually moves the image: warm vs
        // cool should shift the blue and red channel means in opposite
        // directions. This is the measurement that decides whether the
        // white-reference calibration described in the plan is possible at
        // all.
        capture.set(cv::CAP_PROP_WB_TEMPERATURE, 2800);
        const Photometry warm = sample(capture);
        capture.set(cv::CAP_PROP_WB_TEMPERATURE, 6500);
        const Photometry cool = sample(capture);
        log("   WB 2800K: L=" + fixed2(warm.mean_l) + " B=" + fixed2(warm.mean_b) +
            " R=" + fixed2(warm.mean_r) + " | WB 6500K: L=" + fixed2(cool.mean_l) +
            " B=" + fixed2(cool.mean_b) + " R=" + fixed2(cool.mean_r) + " | dB=" +
            fixed2(cool.mean_b - warm.mean_b) + " dR=" + fixed2(cool.mean_r - warm.mean_r));

        for (const double auto_value : {0.0, 0.25, 0.75, 1.0, 3.0}) {
            const double ae = set_and_read(capture, cv::CAP_PROP_AUTO_EXPOSURE, auto_value);
            const double exp_after = set_and_read(capture, cv::CAP_PROP_EXPOSURE, -6);
            log("   AUTO_EXPOSURE=" + fixed2(auto_value) + " readback=" + fixed2(ae) +
                " then EXPOSURE=-6 readback=" + fixed2(exp_after));
        }
    }

    // Monotone response curves. On a dark or covered scene an absolute
    // pixel reading proves nothing, but a monotone response across the
    // whole accepted range still does: if mean L rises step by step as
    // exposure is lengthened, the write is reaching the sensor AND nothing
    // is auto-correcting it back. That second half is the important one -
    // it is the evidence that the driver has dropped into manual exposure,
    // which on DirectShow is a side effect of writing CAP_PROP_EXPOSURE
    // rather than something CAP_PROP_AUTO_EXPOSURE has to grant us.
    for (const auto& [prop, name, values] :
         std::vector<std::tuple<int, const char*, std::vector<double>>>{
             {cv::CAP_PROP_EXPOSURE, "EXPOSURE", {-11, -9, -7, -5, -3, -2}},
             {cv::CAP_PROP_BRIGHTNESS, "BRIGHTNESS", {0, 64, 128, 192, 255}},
             {cv::CAP_PROP_GAIN, "GAIN", {0, 64, 128, 192, 255}},
             {cv::CAP_PROP_SATURATION, "SATURATION", {0, 64, 128, 192, 255}}}) {
        const double original = capture.get(prop);
        std::string curve;
        for (const double value : values) {
            capture.set(prop, value);
            const Photometry p = sample(capture);
            curve += " " + fixed2(value) + ":L=" + fixed2(p.mean_l) + ",C=" + fixed2(p.mean_chroma);
        }
        capture.set(prop, original);
        sample(capture);
        log(std::string("\nresponse curve ") + name + ":" + curve);
    }

    // Ordering question: several DirectShow drivers only honour property
    // writes issued after streaming has begun. If that is true here, the
    // eventual lock has to run on the capture thread after the first grab,
    // not at open time - which also matches the threading constraint that
    // property changes must not be issued from the UI thread mid-grab.
    //
    // Probed with EXPOSURE and BRIGHTNESS deliberately: an earlier version
    // asked this question with AUTO_EXPOSURE, which this device does not
    // implement at all, so the answer was "rejected either way" and told us
    // nothing about ordering.
    capture.release();
    {
        cv::VideoCapture fresh(0, kBackend);
        REQUIRE(fresh.isOpened());
        const double exposure_before = set_and_read(fresh, cv::CAP_PROP_EXPOSURE, -9);
        const double brightness_before = set_and_read(fresh, cv::CAP_PROP_BRIGHTNESS, 64);
        cv::Mat frame;
        fresh.read(frame);
        const double exposure_survived = fresh.get(cv::CAP_PROP_EXPOSURE);
        const double brightness_survived = fresh.get(cv::CAP_PROP_BRIGHTNESS);
        const double exposure_after = set_and_read(fresh, cv::CAP_PROP_EXPOSURE, -7);
        const double brightness_after = set_and_read(fresh, cv::CAP_PROP_BRIGHTNESS, 192);
        log("\nordering: before first grab EXPOSURE=-9->" + fixed2(exposure_before) +
            " BRIGHTNESS=64->" + fixed2(brightness_before) + "; survived first grab as " +
            fixed2(exposure_survived) + "/" + fixed2(brightness_survived) +
            "; after first grab EXPOSURE=-7->" + fixed2(exposure_after) + " BRIGHTNESS=192->" +
            fixed2(brightness_after));
        fresh.release();
    }

    // Persistence across a close/open cycle. DirectShow property state
    // lives in the driver, not in the cv::VideoCapture, so a value written
    // by one run can silently outlive it - which matters both for the
    // eventual lock (we must write every setting explicitly rather than
    // assuming defaults) and for this probe (a "default" read at the top
    // may be a leftover from the previous run, not a factory value).
    {
        cv::VideoCapture reopened(0, kBackend);
        REQUIRE(reopened.isOpened());
        cv::Mat frame;
        reopened.read(frame);
        log("persistence after reopen: EXPOSURE=" + fixed2(reopened.get(cv::CAP_PROP_EXPOSURE)) +
            " BRIGHTNESS=" + fixed2(reopened.get(cv::CAP_PROP_BRIGHTNESS)));
        // Leave the device as we found it so the next run and the app both
        // start from the driver's own defaults.
        reopened.set(cv::CAP_PROP_EXPOSURE, -5);
        reopened.set(cv::CAP_PROP_BRIGHTNESS, 128);
        reopened.release();
    }

    capture.open(0, kBackend);
    REQUIRE(capture.isOpened());

    // CAP_PROP_SETTINGS pops the driver's own modal dialog, which would
    // hang an unattended run, so it needs its own opt-in even inside this
    // already-gated probe. It is worth knowing about only as a manual
    // fallback if programmatic control turns out to be unavailable.
    if (std::getenv("CUBE_CAMERA_PROBE_SETTINGS_DIALOG") != nullptr) {
        const bool opened = capture.set(cv::CAP_PROP_SETTINGS, 1);
        log("\nCAP_PROP_SETTINGS set() returned " + std::string(opened ? "true" : "false"));
    } else {
        log("\nCAP_PROP_SETTINGS not attempted (set CUBE_CAMERA_PROBE_SETTINGS_DIALOG=1; it opens "
            "a modal dialog)");
    }

    capture.release();
}
