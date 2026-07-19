#include <catch2/catch_test_macros.hpp>

#include <cube/vision/color_classification.hpp>

#include <array>
#include <utility>

using namespace cube::vision;
using cube::model::FacetColor;

TEST_CASE("classify_color recognizes each reference color with high confidence", "[vision]") {
    const auto palette = default_palette();
    const std::array<std::pair<FacetColor, cv::Vec3b>, 6> reference_samples{{
        {FacetColor::white, cv::Vec3b(255, 255, 255)},
        {FacetColor::yellow, cv::Vec3b(0, 213, 255)},
        {FacetColor::red, cv::Vec3b(0, 0, 196)},
        {FacetColor::orange, cv::Vec3b(0, 106, 255)},
        {FacetColor::green, cv::Vec3b(72, 155, 0)},
        {FacetColor::blue, cv::Vec3b(173, 70, 0)},
    }};

    for (const auto& [expected_color, bgr] : reference_samples) {
        const auto facelet = classify_color(bgr, palette);

        REQUIRE(facelet.color == expected_color);
        REQUIRE(facelet.confidence > 0.9F);
    }
}

TEST_CASE("classify_color prefers the nearer reference for an ambiguous sample", "[vision]") {
    const auto palette = default_palette();

    // Pure red should classify as red, not orange - both cover the "warm"
    // end of the palette but red's reference is the closer match.
    const auto facelet = classify_color(cv::Vec3b(0, 0, 255), palette);

    REQUIRE(facelet.color == FacetColor::red);
}
