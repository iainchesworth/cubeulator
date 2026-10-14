#pragma once

#include <cube/model/facelet.hpp>

#include <array>
#include <cstddef>
#include <cstdint>

namespace cube::model {

// U/R/F/D/L/B - the six faces of a 3x3x3 cube by position, independent of
// which color happens to be scanned onto each one.
enum class Face : std::uint8_t { U, R, F, D, L, B };

inline constexpr std::size_t face_count = 6;
inline constexpr std::size_t facelets_per_face = 9;
inline constexpr std::size_t center_facelet_index = 4;

struct FaceScan {
    std::array<Facelet, facelets_per_face> facelets{};
    bool confirmed = false;
};

}  // namespace cube::model
