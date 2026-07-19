#include <catch2/catch_test_macros.hpp>

#include <cube/vision/idle_gate.hpp>

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <cstdio>
#include <cstdlib>
#include <string>

using namespace cube::vision;

namespace {

// Drive the gate to engagement: kIdleConsecutive low-count passes with the
// tracker lost.
void engage(IdleGate& gate) {
    for (int i = 0; i < IdleGate::kIdleConsecutive; ++i) {
        gate.on_pass(0, /*tracker_lost=*/true);
    }
    REQUIRE(gate.skip_next());
}

}  // namespace

TEST_CASE("IdleGate engages only after consecutive low passes while lost", "[vision]") {
    IdleGate gate;
    CHECK_FALSE(gate.skip_next());  // a fresh gate never skips
    // Two low passes are not enough - a dark/blurred frame or two
    // mid-manipulation must not idle the detectors.
    gate.on_pass(0, true);
    CHECK_FALSE(gate.skip_next());
    gate.on_pass(0, true);
    CHECK_FALSE(gate.skip_next());
    gate.on_pass(0, true);
    CHECK(gate.skip_next());
}

TEST_CASE("IdleGate hysteresis is broken by a single pass over the floor", "[vision]") {
    IdleGate gate;
    gate.on_pass(0, true);
    gate.on_pass(0, true);
    gate.on_pass(IdleGate::kIdlePixelFloor, true);  // cube glints into view
    gate.on_pass(0, true);
    gate.on_pass(0, true);
    CHECK_FALSE(gate.skip_next());  // the streak restarted; still 2 of 3
    gate.on_pass(0, true);
    CHECK(gate.skip_next());
}

TEST_CASE("IdleGate forces an escape pass every fourth idle pass", "[vision]") {
    IdleGate gate;
    engage(gate);  // idle span pass 1: skip
    // The count stays low and the tracker stays lost for a long absence;
    // exactly every kIdleEscapeEvery-th span pass must run the detectors
    // anyway - the bounded-latency escape hatch.
    int full_passes = 0;
    for (int span = 2; span <= 1 + 2 * IdleGate::kIdleEscapeEvery; ++span) {
        gate.on_pass(0, true);
        // After the span-th idle pass the verdict for the NEXT pass is an
        // escape exactly when the span count hits the escape modulus.
        const bool expect_escape = span % IdleGate::kIdleEscapeEvery == 0;
        CHECK(gate.skip_next() != expect_escape);
        full_passes += gate.skip_next() ? 0 : 1;
    }
    CHECK(full_passes == 2);
}

TEST_CASE("cube arrival clears an engaged gate immediately", "[vision]") {
    IdleGate gate;
    engage(gate);
    // The presence statistic is computed on EVERY pass, skipped ones
    // included, so the cube's return is seen without waiting for an
    // escape pass - reacquisition latency is one pass.
    gate.on_pass(IdleGate::kIdlePixelFloor + 40, true);
    CHECK_FALSE(gate.skip_next());
}

TEST_CASE("a non-lost tracker never idles", "[vision]") {
    IdleGate gate;
    // However empty the frame statistic looks, an acquiring/locked/
    // coasting tracker's passes carry tracking value.
    for (int i = 0; i < 10; ++i) {
        gate.on_pass(0, /*tracker_lost=*/false);
        CHECK_FALSE(gate.skip_next());
    }
    // And an engaged gate releases the moment the tracker stops being
    // lost (an escape pass anchored something).
    engage(gate);
    gate.on_pass(0, /*tracker_lost=*/false);
    CHECK_FALSE(gate.skip_next());
}

TEST_CASE("IdleGate reset restarts a fresh scan detecting", "[vision]") {
    IdleGate gate;
    engage(gate);
    gate.reset();
    CHECK_FALSE(gate.skip_next());
    gate.on_pass(0, true);
    CHECK_FALSE(gate.skip_next());  // streak restarted too
}

// Calibration scan - hidden, corpus-driven: prints per-pass cool-hue
// counts (and a few candidate alternative bands) over a recording so the
// idle gate's floor can be set against measured bimodality instead of
// guessed. Usage: CUBE_TRACK_REPLAY_DIR=<recording> "[.cool-hue-scan]".
#ifdef _MSC_VER
#pragma warning(disable : 4996)
#endif
TEST_CASE("scan a recording's cool-hue statistics", "[.cool-hue-scan]") {
    const char* dir = std::getenv("CUBE_TRACK_REPLAY_DIR");
    if (dir == nullptr) {
        SKIP("CUBE_TRACK_REPLAY_DIR not set");
    }
    const auto band_count = [](const cv::Mat& bgr, int h0, int h1, int s0, int v0) {
        cv::Mat small;
        cv::resize(bgr, small, cv::Size(160, 120), 0.0, 0.0, cv::INTER_AREA);
        cv::Mat hsv;
        cv::cvtColor(small, hsv, cv::COLOR_BGR2HSV);
        cv::Mat mask;
        cv::inRange(hsv, cv::Scalar(h0, s0, v0), cv::Scalar(h1, 255, 255), mask);
        return cv::countNonZero(mask);
    };
    std::printf("pass,production,s80v60,s100v60,s120v80\n");
    for (int pass = 0;; ++pass) {
        char name[32];
        std::snprintf(name, sizeof name, "rec_%06d.jpg", pass);
        const cv::Mat frame = cv::imread(std::string(dir) + "/" + name, cv::IMREAD_COLOR);
        if (frame.empty()) {
            break;
        }
        std::printf("%d,%d,%d,%d,%d\n", pass, count_cool_hue_pixels(frame), band_count(frame, 35, 135, 80, 60),
                    band_count(frame, 35, 135, 100, 60), band_count(frame, 35, 135, 120, 80));
    }
    SUCCEED();
}

TEST_CASE("count_cool_hue_pixels separates cube tiles from warm clutter", "[vision]") {
    // Empty and pitch-black frames: nothing cool-hued.
    CHECK(count_cool_hue_pixels(cv::Mat()) == 0);
    CHECK(count_cool_hue_pixels(cv::Mat::zeros(480, 640, CV_8UC3)) == 0);

    // A warm dim scene - skin tones, wooden desk - is exactly what fooled
    // looser presence proxies on live1 (forehead blobs, curtain speckle):
    // hue ~10 (OpenCV halved), well outside the cool band, whatever its
    // saturation.
    cv::Mat warm(480, 640, CV_8UC3, cv::Scalar(140, 160, 200));
    CHECK(count_cool_hue_pixels(warm) == 0);

    // The same scene with a modest green tile patch (~3% of frame) crosses
    // the floor decisively - the statistic is computed on a 160x120
    // downsample, so 3% is ~570 pixels against a floor of 20.
    cv::Mat with_cube = warm.clone();
    cv::rectangle(with_cube, cv::Rect(300, 200, 110, 110), cv::Scalar(60, 180, 60), cv::FILLED);
    CHECK(count_cool_hue_pixels(with_cube) > IdleGate::kIdlePixelFloor);
}
