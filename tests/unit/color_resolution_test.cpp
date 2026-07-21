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

// ---------------------------------------------------------------------------
// The warm-cast white rule. Every Lab triple below is a REAL measurement,
// read out of the [.cross-session] slot rows for the named clip/face/cell,
// not a constructed value - the whole point is that the boundary was set
// from this cube's samples rather than guessed.
// ---------------------------------------------------------------------------

// Nine stickers of runD's white face plus runA's palest white. Their chroma
// (15.7-21.0) sits ABOVE the old unconditional 15.0 floor, so before the
// warm-cast branch existed every one of them fell through to the hue path
// and came out ORANGE - runD censused O=21 W=2 where both must be 9. Fails
// pre-fix on all ten samples.
TEST_CASE("classify_lab names this cube's warm-cast white face white", "[vision]") {
    struct Sample {
        const char* where;
        cv::Vec3f lab;
    };
    const std::array<Sample, 10> whites{{
        {"runD U[3] chroma 15.7", {87.2F, 6.4F, 14.3F}},
        {"runD B[7] chroma 15.8", {86.1F, 6.4F, 14.4F}},
        {"runD D[2] chroma 17.6", {82.8F, 7.8F, 15.8F}},
        {"runD B[2] chroma 17.8", {76.5F, 8.0F, 15.9F}},
        {"runD R[7] chroma 18.7", {78.0F, 7.8F, 17.1F}},
        {"runD L[6] chroma 18.8", {78.0F, 7.7F, 17.1F}},
        {"runD D[4] chroma 19.3", {77.3F, 9.2F, 17.0F}},
        {"runD R[1] chroma 20.2", {79.3F, 9.3F, 18.0F}},
        {"runD F[2] chroma 21.0 - the most chromatic white measured", {69.0F, 9.7F, 18.6F}},
        {"runA U[8] L 66.6 - the darkest white measured", {66.6F, 9.7F, 17.6F}},
    }};
    for (const auto& sample : whites) {
        CAPTURE(sample.where);
        const auto facelet = classify_lab(sample.lab);
        REQUIRE(facelet.color == FacetColor::white);
        // Confidence must come from the branch that made the call. The old
        // chroma-only expression (1 - chroma/30) is NEGATIVE for every one
        // of these, so a non-zero value here is itself evidence the
        // lightness term is participating.
        REQUIRE(facelet.confidence > 0.0F);
        REQUIRE(facelet.confidence <= 1.0F);
    }
}

// The theft guard. These are the samples that a white rule using chroma and
// lightness ALONE - no hue condition - takes by mistake; each was checked
// against the raw Lab and is a genuine coloured tile. runA F[2] is the
// proof that the hue condition is load-bearing rather than decorative: at
// L 67.6 / chroma 18.8 it is both lighter AND less chromatic than runA
// U[8], the palest true white (L 66.6 / chroma 20.1), so no threshold pair
// monotone in (chroma, lightness) can admit one and refuse the other.
//
// This case passes against the shipped classifier BOTH before and after the
// warm-cast branch - by design, it pins an invariant across the change. It
// was verified to FAIL against the rejected alternative (the same branch
// with the hue condition deleted), which takes six of these nine.
TEST_CASE("the warm-cast white rule steals no green, yellow or blue", "[vision]") {
    struct Sample {
        const char* where;
        cv::Vec3f lab;
        FacetColor expected;
    };
    const std::array<Sample, 9> tiles{{
        {"runA R[2] green, chroma 20.5", {22.2F, -11.1F, 17.3F}, FacetColor::green},
        {"runD B[1] green, chroma 25.8", {39.4F, -14.8F, 21.1F}, FacetColor::green},
        {"runD D[3] green, chroma 30.7", {40.8F, -18.1F, 24.8F}, FacetColor::green},
        {"live1 L[4] yellow washed to chroma 16.7 at L 66.4",
         {66.4F, -1.1F, 16.7F},
         FacetColor::yellow},
        {"live1 D[0] yellow washed to chroma 19.3 at L 66.5",
         {66.5F, 1.2F, 19.3F},
         FacetColor::yellow},
        {"runA D[1] yellow, chroma 52.8 at L 77.9", {77.9F, 3.5F, 52.7F}, FacetColor::yellow},
        {"runA F[2] blue, L 67.6 chroma 18.8 - lighter and less chromatic than the palest white",
         {67.6F, 3.1F, -18.6F},
         FacetColor::blue},
        {"live2 F[1] blue, L 64.1 chroma 21.6", {64.1F, 3.7F, -21.3F}, FacetColor::blue},
        {"live2 F[3] blue, L 65.0 chroma 22.6", {65.0F, 3.9F, -22.2F}, FacetColor::blue},
    }};
    for (const auto& sample : tiles) {
        CAPTURE(sample.where);
        REQUIRE(classify_lab(sample.lab).color == sample.expected);
    }
}

// The two ends the widened rule must not disturb: a genuinely neutral
// sample still reaches white through the unconditional chroma floor, and a
// true orange still reads orange. runA F[7] is the least chromatic tile
// found anywhere in the orange band across the whole corpus (chroma 33.6),
// so it - not a saturated reference - is what the soft ceiling of 27 has to
// clear. Also an invariant pin: passes before and after.
TEST_CASE("the neutral white floor and true oranges are undisturbed", "[vision]") {
    // runA D[2]: chroma 9.7, already white before the change.
    REQUIRE(classify_lab(cv::Vec3f{81.5F, 6.8F, 6.9F}).color == FacetColor::white);
    // A dark neutral is still white on chroma alone - the floor branch is
    // deliberately not given a lightness condition here.
    REQUIRE(classify_lab(cv::Vec3f{30.0F, 2.0F, 2.0F}).color == FacetColor::white);
    // runA F[7], chroma 33.6 at L 54.2: refused by BOTH soft conditions.
    REQUIRE(classify_lab(cv::Vec3f{54.2F, 19.8F, 27.1F}).color == FacetColor::orange);
    // runD D[8], chroma 73.7 - a saturated orange, far from any edge.
    REQUIRE(classify_lab(cv::Vec3f{50.0F, 52.7F, 51.5F}).color == FacetColor::orange);
    // A washed orange that is light enough but still too chromatic.
    REQUIRE(classify_lab(cv::Vec3f{75.0F, 16.0F, 23.0F}).color == FacetColor::orange);
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
