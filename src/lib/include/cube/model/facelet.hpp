#pragma once

#include <cstdint>

namespace cube::model {

// The 6 colors a facelet can read as, plus `unknown` for an unclassified or
// not-yet-scanned facelet.
enum class FacetColor : std::uint8_t {
    unknown,
    white,
    yellow,
    red,
    orange,
    green,
    blue,
};

struct Facelet {
    FacetColor color = FacetColor::unknown;
    float confidence = 0.0F;
};

}  // namespace cube::model
