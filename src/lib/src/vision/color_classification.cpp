#include <cube/vision/color_classification.hpp>

#include <opencv2/imgproc.hpp>

namespace cube::vision {

namespace {

using cube::model::FacetColor;

[[nodiscard]] cv::Vec3f bgr_to_lab(const cv::Vec3b& bgr) {
    const cv::Mat3f sample(
        1, 1,
        cv::Vec3f(static_cast<float>(bgr[0]) / 255.0F, static_cast<float>(bgr[1]) / 255.0F,
                  static_cast<float>(bgr[2]) / 255.0F));
    cv::Mat3f lab;
    cv::cvtColor(sample, lab, cv::COLOR_BGR2Lab);
    return lab.at<cv::Vec3f>(0, 0);
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

}  // namespace cube::vision
