#include <cube/model/color_scheme.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <utility>

namespace cube::model {

namespace {

constexpr std::array<std::pair<FacetColor, FacetColor>, 3> kCanonicalPairs{{
    {FacetColor::white, FacetColor::yellow},
    {FacetColor::red, FacetColor::orange},
    {FacetColor::green, FacetColor::blue},
}};

constexpr std::array<std::pair<Face, Face>, 3> kOppositeFaces{{
    {Face::U, Face::D},
    {Face::L, Face::R},
    {Face::F, Face::B},
}};

[[nodiscard]] constexpr std::optional<FacetColor> opposite_of(FacetColor color) {
    for (const auto& [a, b] : kCanonicalPairs) {
        if (color == a) {
            return b;
        }
        if (color == b) {
            return a;
        }
    }
    return std::nullopt;
}

}  // namespace

ColorSchemeResult detect_color_scheme(
    const std::array<std::optional<FacetColor>, face_count>& observed_centers) {
    ColorSchemeResult result;

    if (std::ranges::none_of(observed_centers, [](const auto& c) { return c.has_value(); })) {
        return result;
    }

    result.face_colors = observed_centers;

    bool contradictory = false;
    for (const auto& [face_a, face_b] : kOppositeFaces) {
        const auto idx_a = static_cast<std::size_t>(face_a);
        const auto idx_b = static_cast<std::size_t>(face_b);
        const auto& color_a = observed_centers[idx_a];
        const auto& color_b = observed_centers[idx_b];

        if (color_a && color_b) {
            if (opposite_of(*color_a) != color_b) {
                contradictory = true;
            }
        } else if (color_a) {
            result.face_colors[idx_b] = opposite_of(*color_a);
        } else if (color_b) {
            result.face_colors[idx_a] = opposite_of(*color_b);
        }
    }

    if (contradictory) {
        result.confidence = ColorSchemeConfidence::contradictory;
        return result;
    }

    const bool all_known =
        std::ranges::all_of(result.face_colors, [](const auto& c) { return c.has_value(); });
    result.confidence = all_known ? ColorSchemeConfidence::confirmed : ColorSchemeConfidence::partial;
    return result;
}

}  // namespace cube::model
