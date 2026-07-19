#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cube/vision/color_classification.hpp>
#include <cube/vision/color_resolution.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <random>
#include <utility>

using namespace cube::vision;
using cube::model::FacetColor;

namespace {

struct SyntheticScan {
    std::array<CapturedFace, 6> faces{};
    std::array<std::array<FacetColor, 9>, 6> truth{};
};

// A lightly-scrambled cube built from the reference palette: each face
// starts solid in "its" colour (face index == palette index, so the six
// centres are six distinct colours), then off-centre stickers are swapped
// across faces so clusters span faces the way a real scramble does.
// Exactly 9 stickers per colour is preserved by construction.
SyntheticScan make_scan() {
    const ColorPalette palette = default_palette();
    SyntheticScan scan;
    for (std::size_t face = 0; face < 6; ++face) {
        for (std::size_t i = 0; i < 9; ++i) {
            scan.faces[face].lab[i] = palette.entries[face].lab;
            scan.truth[face][i] = palette.entries[face].color;
        }
    }
    const auto swap_stickers = [&scan](std::size_t face_a, std::size_t cell_a, std::size_t face_b,
                                       std::size_t cell_b) {
        std::swap(scan.faces[face_a].lab[cell_a], scan.faces[face_b].lab[cell_b]);
        std::swap(scan.truth[face_a][cell_a], scan.truth[face_b][cell_b]);
    };
    swap_stickers(0, 0, 2, 8);
    swap_stickers(1, 1, 4, 7);
    swap_stickers(3, 2, 5, 6);
    swap_stickers(0, 6, 3, 1);
    swap_stickers(2, 3, 5, 5);
    swap_stickers(1, 8, 3, 0);
    return scan;
}

void shift_lightness(SyntheticScan& scan, float delta) {
    for (auto& face : scan.faces) {
        for (auto& lab : face.lab) {
            lab[0] += delta;
        }
    }
}

void scale_chroma(SyntheticScan& scan, float factor) {
    for (auto& face : scan.faces) {
        for (auto& lab : face.lab) {
            lab[1] *= factor;
            lab[2] *= factor;
        }
    }
}

// Per-sticker gaussian noise, clamped at 1.5 sigma. The clamp keeps the
// test deterministic across standard libraries (normal_distribution's draw
// order is implementation-defined): even a worst-case clamped draw cannot
// push a sample past the bisector of the closest colour pair (red/orange,
// ~30 Lab units apart), so recovery never hinges on which library produced
// the tail. The unclamped tail models a mis-sampled patch, which temporal
// fusion (not colour resolution) is responsible for rejecting. Centres are
// exempt - by the time resolve_colors() runs they have been median-fused
// across frames, so their residual noise is far below a single raw
// sample's, and clean centres keep the seed-separation gate deterministic
// too.
void add_noise(SyntheticScan& scan, float sigma, std::uint32_t seed) {
    std::mt19937 rng(seed);
    std::normal_distribution<float> noise(0.0F, sigma);
    for (auto& face : scan.faces) {
        for (std::size_t i = 0; i < face.lab.size(); ++i) {
            if (i == 4) {
                continue;
            }
            for (int channel = 0; channel < 3; ++channel) {
                face.lab[i][channel] += std::clamp(noise(rng), -1.5F * sigma, 1.5F * sigma);
            }
        }
    }
}

void require_recovered(const SyntheticScan& scan) {
    const auto resolved = resolve_colors(scan.faces);
    REQUIRE(resolved.has_value());
    for (std::size_t face = 0; face < 6; ++face) {
        for (std::size_t i = 0; i < 9; ++i) {
            CAPTURE(face, i);
            REQUIRE((*resolved)[face][i].color == scan.truth[face][i]);
            REQUIRE((*resolved)[face][i].confidence >= 0.0F);
            REQUIRE((*resolved)[face][i].confidence <= 1.0F);
        }
    }
}

}  // namespace

TEST_CASE("classify_lab names palette colours with full confidence", "[vision]") {
    const ColorPalette palette = default_palette();
    for (const auto& entry : palette.entries) {
        const auto facelet = classify_lab(entry.lab);
        REQUIRE(facelet.color == entry.color);
        REQUIRE(facelet.confidence > 0.99F);
    }
}

TEST_CASE("classify_lab survives a +25 L cast and chroma washing", "[vision]") {
    // Hue-angle classification is invariant to exactly the two distortions
    // real webcam exposure applies: a global lightness cast (L shifts,
    // hue untouched) and chroma washing (a*/b* shrink toward zero along
    // their own direction). The old nearest-reference classifier failed
    // both - a whole scanned face of washed tiles read uniformly as white.
    const ColorPalette palette = default_palette();
    for (const auto& entry : palette.entries) {
        cv::Vec3f lifted = entry.lab;
        lifted[0] += 25.0F;
        REQUIRE(classify_lab(lifted).color == entry.color);

        if (entry.color != cube::model::FacetColor::white) {
            cv::Vec3f washed = entry.lab;
            washed[0] += 20.0F;
            washed[1] *= 0.4F;
            washed[2] *= 0.4F;
            REQUIRE(classify_lab(washed).color == entry.color);
        }
    }
}

TEST_CASE("resolve_colors recovers an unperturbed scan", "[vision]") {
    const SyntheticScan scan = make_scan();
    require_recovered(scan);

    // Noise-free samples sit exactly on their cluster means, so every
    // sticker's margin - and therefore confidence - is at its maximum.
    const auto resolved = resolve_colors(scan.faces);
    REQUIRE(resolved.has_value());
    for (std::size_t face = 0; face < 6; ++face) {
        for (std::size_t i = 0; i < 9; ++i) {
            REQUIRE((*resolved)[face][i].confidence > 0.9F);
        }
    }
}

TEST_CASE("resolve_colors survives a global +25 L lighting shift", "[vision]") {
    SyntheticScan scan = make_scan();
    shift_lightness(scan, 25.0F);
    require_recovered(scan);
}

TEST_CASE("resolve_colors survives chroma compressed to 0.7", "[vision]") {
    SyntheticScan scan = make_scan();
    scale_chroma(scan, 0.7F);
    require_recovered(scan);
}

TEST_CASE("resolve_colors survives per-sample gaussian noise", "[vision]") {
    SyntheticScan scan = make_scan();
    add_noise(scan, 5.0F, 20260719U);
    require_recovered(scan);
}

TEST_CASE("resolve_colors survives shift, chroma compression, and noise combined", "[vision]") {
    SyntheticScan scan = make_scan();
    shift_lightness(scan, 25.0F);
    scale_chroma(scan, 0.7F);
    add_noise(scan, 5.0F, 42U);
    require_recovered(scan);
}

TEST_CASE("resolve_colors rejects a scan with two identical centres", "[vision]") {
    SyntheticScan scan = make_scan();
    scan.faces[1].lab[4] = scan.faces[0].lab[4];

    const auto resolved = resolve_colors(scan.faces);
    REQUIRE_FALSE(resolved.has_value());
    REQUIRE(resolved.error() == cube::CubeError::no_valid_cube_detected);
}
