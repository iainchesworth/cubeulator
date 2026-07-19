#pragma once

#include <cube/model/face.hpp>
#include <cube/model/facelet.hpp>

#include <array>
#include <cstdint>
#include <optional>

namespace cube::model {

enum class ColorSchemeConfidence : std::uint8_t {
    insufficient_data,
    partial,
    confirmed,
    contradictory,
};

struct ColorSchemeResult {
    ColorSchemeConfidence confidence = ColorSchemeConfidence::insufficient_data;
    std::array<std::optional<FacetColor>, face_count> face_colors{};
};

// Validates observed face-center colors against the standard Western
// opposite-color-pair scheme (white<->yellow, red<->orange, green<->blue)
// and predicts the centers of not-yet-scanned faces wherever their opposite
// face has already been observed. Only checks opposite-face pairing, not
// handedness (which of the two mirror-image standard schemes it is) - that
// needs a full corner (three mutually adjacent faces) to resolve, which is
// out of scope for this progress-display hint.
[[nodiscard]] ColorSchemeResult detect_color_scheme(
    const std::array<std::optional<FacetColor>, face_count>& observed_centers);

}  // namespace cube::model
