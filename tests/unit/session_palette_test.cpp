// SessionCentrePalette (P5): the two known daylight boundary failures -
// desaturated green and warm hue-40 orange - must classify correctly
// once the session's own centres have been observed, BY CONSTRUCTION
// (cells land with their own cube's centre cluster; clusters name
// themselves by nearest reference colour, resolve_colors' rule). Probes
// are constructed in HSV as the physical drifts; what the absolute
// bands say about them is documented non-fatally, since band constants
// may move.

#include <cube/vision/color_resolution.hpp>

#include <opencv2/imgproc.hpp>

#include <catch2/catch_test_macros.hpp>

namespace {

using cube::model::FacetColor;
using cube::vision::classify_lab;
using cube::vision::SessionCentrePalette;

cv::Vec3f lab_of_bgr(float b, float g, float r) {
    const cv::Mat3f sample(1, 1, cv::Vec3f(b / 255.0F, g / 255.0F, r / 255.0F));
    cv::Mat3f lab;
    cv::cvtColor(sample, lab, cv::COLOR_BGR2Lab);
    return lab.at<cv::Vec3f>(0, 0);
}

cv::Vec3f blend(const cv::Vec3f& a, const cv::Vec3f& b, float t) {
    return a * (1.0F - t) + b * t;
}

}  // namespace

TEST_CASE("session palette rescues a desaturated green", "[session_palette]") {
    // A dull, washed-out green (low saturation, low value) - the
    // daylight failure class. Constructed in HSV so the drift is the
    // physical one (saturation loss), not a Lab chord artefact.
    cv::Mat3f hsv(1, 1, cv::Vec3f(120.0F, 0.35F, 0.55F));  // hue deg, S, V
    cv::Mat3f bgr;
    cv::cvtColor(hsv, bgr, cv::COLOR_HSV2BGR);
    const cv::Vec3f dull_green =
        lab_of_bgr(bgr.at<cv::Vec3f>(0, 0)[0] * 255.0F, bgr.at<cv::Vec3f>(0, 0)[1] * 255.0F,
                   bgr.at<cv::Vec3f>(0, 0)[2] * 255.0F);
    // Document (non-fatally) what the absolute bands say today - the
    // handoff recorded this class as a live failure.
    CHECK_NOFAIL(classify_lab(dull_green).color != FacetColor::green);

    // Drift-together physics: this cube's green CENTRE is equally dull,
    // so the cell lands with its own kind and the cluster names itself
    // by nearest reference - green, with no band gap to fall into.
    SessionCentrePalette palette;
    palette.observe_centre(dull_green);
    palette.observe_centre(lab_of_bgr(0, 106, 255));    // orange centre
    palette.observe_centre(lab_of_bgr(255, 255, 255));  // white centre
    REQUIRE(palette.cluster_count() == 3);
    const cv::Vec3f cell = dull_green + cv::Vec3f(2.0F, -1.5F, 1.0F);  // a nearby cell read
    REQUIRE(palette.classify(cell).color == FacetColor::green);
}

TEST_CASE("session palette keeps hue-40 orange out of the red band", "[session_palette]") {
    // Orange drifted warm to hue 40 (degrees) - the second recorded
    // daylight failure.
    // Full saturation, moderate value: the recorded failure is a deep
    // warm orange, not a bright one (bright hue-40 reads yellow, which
    // is a DIFFERENT ambiguity owned by the resolve stage).
    cv::Mat3f hsv(1, 1, cv::Vec3f(40.0F, 1.0F, 0.75F));
    cv::Mat3f bgr;
    cv::cvtColor(hsv, bgr, cv::COLOR_HSV2BGR);
    const cv::Vec3f warm_orange =
        lab_of_bgr(bgr.at<cv::Vec3f>(0, 0)[0] * 255.0F, bgr.at<cv::Vec3f>(0, 0)[1] * 255.0F,
                   bgr.at<cv::Vec3f>(0, 0)[2] * 255.0F);
    CHECK_NOFAIL(classify_lab(warm_orange).color != FacetColor::orange);

    SessionCentrePalette palette;
    palette.observe_centre(warm_orange);            // this cube's warm orange centre
    palette.observe_centre(lab_of_bgr(0, 0, 196));  // and its true red centre
    palette.observe_centre(lab_of_bgr(255, 255, 255));
    REQUIRE(palette.classify(warm_orange + cv::Vec3f(1.0F, 1.5F, -1.0F)).color ==
            FacetColor::orange);
}

TEST_CASE("session palette bootstraps to the absolute bands", "[session_palette]") {
    SessionCentrePalette palette;
    const cv::Vec3f green = lab_of_bgr(72, 155, 0);
    // No centres observed: identical to classify_lab.
    REQUIRE(palette.classify(green).color == classify_lab(green).color);
    // One centre is not enough to define session boundaries either.
    palette.observe_centre(green);
    REQUIRE(palette.cluster_count() == 1);
    REQUIRE(palette.classify(green).color == classify_lab(green).color);
}

TEST_CASE("session palette caps at six clusters by merging", "[session_palette]") {
    SessionCentrePalette palette;
    for (float l = 10.0F; l < 90.0F; l += 10.0F) {
        palette.observe_centre(cv::Vec3f(l, l - 50.0F, 50.0F - l));
    }
    REQUIRE(palette.cluster_count() <= 6);
}
