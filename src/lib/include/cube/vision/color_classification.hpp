#pragma once

#include <cube/model/facelet.hpp>

#include <opencv2/core.hpp>

#include <array>

namespace cube::vision {

struct PaletteEntry {
    cube::model::FacetColor color;
    cv::Vec3f lab;
};

// The 6 sticker colors' CIELAB reference values.
struct ColorPalette {
    std::array<PaletteEntry, 6> entries;
};

// A fixed set of CIELAB reference colors for a standard Rubik's cube sticker
// set - not runtime-calibrated: a single scrambled face may show anywhere
// from 1 to 9 distinct colors, so there's nothing sound to calibrate from
// until multiple faces are seen. Manual per-face confirmation in the UI is
// the real correction mechanism for lighting-driven misreads.
[[nodiscard]] ColorPalette default_palette();

// Classifies a single BGR pixel sample against the palette by its
// Euclidean (CIE76) distance to the nearest reference color in CIELAB
// space. CIE76 rather than the more elaborate CIEDE2000 is a deliberate
// simplification: the 6 reference colors are widely separated in color
// space, so the extra perceptual-uniformity correction CIEDE2000 adds
// doesn't change nearest-neighbor classification here, and CIE76 is far
// simpler to implement correctly.
[[nodiscard]] cube::model::Facelet classify_color(const cv::Vec3b& bgr_sample,
                                                  const ColorPalette& palette);

}  // namespace cube::vision
