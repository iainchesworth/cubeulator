#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "must_have.hpp"

#include <cube/vision/direct_scan.hpp>
#include <cube/vision/sticker_colour.hpp>

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

#include <array>
#include <cmath>
#include <optional>

using cube::vision::sticker_lab;

namespace {

constexpr int kSide = 61;
constexpr int kHalf = 12;

cv::Point2f frame_centre() {
    return {30.0F, 30.0F};
}

// Plastic and a few things that are not: BGR.
cv::Scalar plastic_white() {
    return {235, 235, 235};
}
cv::Scalar plastic_red() {
    return {40, 40, 200};
}
cv::Scalar dark_gap() {
    return {16, 16, 16};
}

cv::Vec3f lab_of(const cv::Scalar& bgr) {
    const cv::Mat3f source(
        1, 1,
        cv::Vec3f(static_cast<float>(bgr(0)) / 255.0F, static_cast<float>(bgr(1)) / 255.0F,
                  static_cast<float>(bgr(2)) / 255.0F));
    cv::Mat3f lab;
    cv::cvtColor(source, lab, cv::COLOR_BGR2Lab);
    return lab(0, 0);
}

// A frame of `ground` with `patch` laid over the given rectangle.
cv::Mat frame_with(const cv::Scalar& ground, const cv::Rect& patch, const cv::Scalar& colour) {
    cv::Mat frame(kSide, kSide, CV_8UC3, ground);
    cv::rectangle(frame, patch, colour, cv::FILLED);
    return frame;
}

// The patch the tests sample is columns 18..42, rows 18..42.
constexpr int kLeft = 18;
constexpr int kWidth = (2 * kHalf) + 1;

double distance_from(const cv::Mat& frame, const cv::Scalar& expected, const cv::Point2f& centre,
                     int half) {
    const std::optional<cv::Vec3f> lab = sticker_lab(frame, centre, half);
    const auto& value = ::cube::test::must_have(lab);
    return cv::norm(value - lab_of(expected));
}

}  // namespace

TEST_CASE("a sticker of one colour reads as that colour", "[sticker-colour]") {
    const cv::Mat frame(kSide, kSide, CV_8UC3, plastic_white());
    CHECK(distance_from(frame, plastic_white(), frame_centre(), kHalf) < 0.5);
}

TEST_CASE("noise on the plastic does not move the colour", "[sticker-colour]") {
    cv::Mat noise(kSide, kSide, CV_32FC3);
    cv::RNG rng(99);
    rng.fill(noise, cv::RNG::NORMAL, cv::Scalar::all(0), cv::Scalar::all(4));
    cv::Mat frame(kSide, kSide, CV_32FC3, plastic_white());
    frame += noise;
    frame.convertTo(frame, CV_8UC3);
    CHECK(distance_from(frame, plastic_white(), frame_centre(), kHalf) < 1.5);
}

TEST_CASE("something else across the middle of a patch is left out", "[sticker-colour]") {
    // A third of the patch in another colour: a wordmark across a centre
    // sticker. A plain median is carried part of the way towards it.
    const cv::Mat frame =
        frame_with(plastic_white(), cv::Rect(kLeft, kLeft + 8, kWidth, 9), plastic_red());
    CHECK(distance_from(frame, plastic_white(), frame_centre(), kHalf) < 1.0);
}

TEST_CASE("a wordmark over more than half of a patch is still left out", "[sticker-colour]") {
    // Four squares in four colours stepping along the diagonal, each one
    // drawn over the one before: together 385 of the 625 pixels of the patch
    // (columns and rows 18 to 42), 62% of it. White is the other 38%, and no
    // per-channel median is white any longer: the middle value of lightness
    // belongs to a letter. White is still the largest single colour in the
    // patch, though, and that is what its colour is.
    cv::Mat frame(kSide, kSide, CV_8UC3, plastic_white());
    const std::array<cv::Scalar, 4> letters = {
        cv::Scalar(70, 160, 40),
        cv::Scalar(170, 80, 20),
        cv::Scalar(40, 210, 235),
        plastic_red(),
    };
    for (int i = 0; i < 4; ++i) {
        const int corner = kLeft + (5 * i);
        cv::rectangle(frame, cv::Rect(corner, corner, 12, 12),
                      letters.at(static_cast<std::size_t>(i)), cv::FILLED);
    }
    CHECK(distance_from(frame, plastic_white(), frame_centre(), kHalf) < 1.0);
}

TEST_CASE("the dark gap at a sticker's edge is left out", "[sticker-colour]") {
    // A patch placed a little off its sticker takes in the plastic between
    // stickers: here ten of its 25 columns, 40% of it.
    const cv::Mat frame = frame_with(plastic_white(), cv::Rect(kLeft, 0, 10, kSide), dark_gap());
    CHECK(distance_from(frame, plastic_white(), frame_centre(), kHalf) < 1.0);
}

TEST_CASE("a patch that is mostly something else reads as that", "[sticker-colour]") {
    // The limit, stated: the colour is the larger part of the patch, and a
    // sticker the patch sits wrongly on is not made white by being asked.
    const cv::Mat frame =
        frame_with(plastic_white(), cv::Rect(kLeft, kLeft, kWidth, 17), plastic_red());
    CHECK(distance_from(frame, plastic_red(), frame_centre(), kHalf) < 1.5);
}

TEST_CASE("a patch off the edge of the frame is clipped to it", "[sticker-colour]") {
    const cv::Mat frame(kSide, kSide, CV_8UC3, plastic_white());
    // Half of this patch is outside the picture.
    CHECK(distance_from(frame, plastic_white(), cv::Point2f(2.0F, 30.0F), kHalf) < 0.5);

    // And one wholly outside has no colour to give.
    const std::optional<cv::Vec3f> none = sticker_lab(frame, cv::Point2f(-40.0F, -40.0F), kHalf);
    CHECK_FALSE(none.has_value());
}

TEST_CASE("warming the colour conversion up changes no colour and may be asked for twice",
          "[sticker-colour]") {
    // The conversion builds its tables on first use, which is a quarter of a
    // second in a browser. Asking for that ahead of time is asked for by the
    // scan while a camera opens; what it must not do is change a colour, and a
    // second or a concurrent ask must be a no-op.
    cube::vision::warm_up_colour_conversion();
    cube::vision::warm_up_colour_conversion();

    const cv::Mat frame(kSide, kSide, CV_8UC3, cv::Scalar(40, 120, 200));
    const auto lab = ::cube::test::must_have(sticker_lab(frame, frame_centre(), kHalf));
    // The same colour the conversion gives a pixel of its own.
    const cv::Mat3f pixel(1, 1, cv::Vec3f(40.0F / 255.0F, 120.0F / 255.0F, 200.0F / 255.0F));
    cv::Mat3f expected;
    cv::cvtColor(pixel, expected, cv::COLOR_BGR2Lab);
    for (int channel = 0; channel < 3; ++channel) {
        CHECK(lab(channel) == Catch::Approx(expected(0, 0)(channel)).margin(1.0e-3));
    }
    // And back, as the screen's colour chips do it.
    const cv::Vec3b bgr = cube::vision::lab_to_bgr(lab);
    CHECK(std::abs(static_cast<int>(bgr(0)) - 40) <= 1);
    CHECK(std::abs(static_cast<int>(bgr(1)) - 120) <= 1);
    CHECK(std::abs(static_cast<int>(bgr(2)) - 200) <= 1);
}
