#include <cube/vision/color_classification.hpp>

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>
#include <limits>

namespace cube::vision {

namespace {

using cube::model::Facelet;
using cube::model::FacetColor;

[[nodiscard]] cv::Vec3f bgr_to_lab(const cv::Vec3b& bgr) {
    const cv::Mat3f sample(
        1, 1, cv::Vec3f(static_cast<float>(bgr[0]) / 255.0F, static_cast<float>(bgr[1]) / 255.0F,
                        static_cast<float>(bgr[2]) / 255.0F));
    cv::Mat3f lab;
    cv::cvtColor(sample, lab, cv::COLOR_BGR2Lab);
    return lab.at<cv::Vec3f>(0, 0);
}

[[nodiscard]] float lab_distance(const cv::Vec3f& a, const cv::Vec3f& b) {
    const cv::Vec3f diff = a - b;
    return std::sqrt(diff.dot(diff));
}

}  // namespace

ColorPalette default_palette() {
    // Approximate sRGB reference values for standard Rubik's cube sticker
    // colors under neutral lighting (BGR order, matching cv::Vec3b samples).
    return ColorPalette{.entries = {{
                             {FacetColor::white, bgr_to_lab(cv::Vec3b(255, 255, 255))},
                             {FacetColor::yellow, bgr_to_lab(cv::Vec3b(0, 213, 255))},
                             {FacetColor::red, bgr_to_lab(cv::Vec3b(0, 0, 196))},
                             {FacetColor::orange, bgr_to_lab(cv::Vec3b(0, 106, 255))},
                             {FacetColor::green, bgr_to_lab(cv::Vec3b(72, 155, 0))},
                             {FacetColor::blue, bgr_to_lab(cv::Vec3b(173, 70, 0))},
                         }}};
}

Facelet classify_color(const cv::Vec3b& bgr_sample, const ColorPalette& palette) {
    const cv::Vec3f sample_lab = bgr_to_lab(bgr_sample);

    const PaletteEntry* best = &palette.entries.front();
    float best_distance = std::numeric_limits<float>::max();
    for (const auto& entry : palette.entries) {
        const float distance = lab_distance(sample_lab, entry.lab);
        if (distance < best_distance) {
            best_distance = distance;
            best = &entry;
        }
    }

    // Lab distances between clearly-different colors are typically tens of
    // units; normalize so a near-exact match reads as high confidence and a
    // borderline one (e.g. a shadowed sticker) reads as low.
    constexpr float kDistanceForZeroConfidence = 60.0F;
    const float confidence = std::clamp(1.0F - best_distance / kDistanceForZeroConfidence, 0.0F, 1.0F);

    return Facelet{.color = best->color, .confidence = confidence};
}

}  // namespace cube::vision
