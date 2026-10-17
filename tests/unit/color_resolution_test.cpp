#include <catch2/catch_approx.hpp>
#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>

#include "must_have.hpp"

#include <cube/vision/color_classification.hpp>
#include <cube/vision/color_resolution.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <numbers>
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
            scan.faces.at(face).lab.at(i) = palette.entries.at(face).lab;
            scan.truth.at(face).at(i) = palette.entries.at(face).color;
        }
    }
    const auto swap_stickers = [&scan](std::size_t face_a, std::size_t cell_a, std::size_t face_b,
                                       std::size_t cell_b) {
        std::swap(scan.faces.at(face_a).lab.at(cell_a), scan.faces.at(face_b).lab.at(cell_b));
        std::swap(scan.truth.at(face_a).at(cell_a), scan.truth.at(face_b).at(cell_b));
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
            lab(0) += delta;
        }
    }
}

void scale_chroma(SyntheticScan& scan, float factor) {
    for (auto& face : scan.faces) {
        for (auto& lab : face.lab) {
            lab(1) *= factor;
            lab(2) *= factor;
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
struct Noise {
    float sigma = 0.0F;
    std::uint32_t seed = 0;
};

void add_noise(SyntheticScan& scan, const Noise& spec) {
    std::mt19937 rng(spec.seed);
    std::normal_distribution<float> noise(0.0F, spec.sigma);
    for (auto& face : scan.faces) {
        for (std::size_t i = 0; i < face.lab.size(); ++i) {
            if (i == 4) {
                continue;
            }
            for (int channel = 0; channel < 3; ++channel) {
                face.lab.at(i)(channel) +=
                    std::clamp(noise(rng), -1.5F * spec.sigma, 1.5F * spec.sigma);
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
            REQUIRE((*resolved).at(face).at(i).color == scan.truth.at(face).at(i));
            REQUIRE((*resolved).at(face).at(i).confidence >= 0.0F);
            REQUIRE((*resolved).at(face).at(i).confidence <= 1.0F);
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
        lifted(0) += 25.0F;
        REQUIRE(classify_lab(lifted).color == entry.color);

        if (entry.color != cube::model::FacetColor::white) {
            cv::Vec3f washed = entry.lab;
            washed(0) += 20.0F;
            washed(1) *= 0.4F;
            washed(2) *= 0.4F;
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
    const std::array<Sample, 10> whites{
        {
            {.where = "runD U[3] chroma 15.7", .lab = {87.2F, 6.4F, 14.3F}},
            {.where = "runD B[7] chroma 15.8", .lab = {86.1F, 6.4F, 14.4F}},
            {.where = "runD D[2] chroma 17.6", .lab = {82.8F, 7.8F, 15.8F}},
            {.where = "runD B[2] chroma 17.8", .lab = {76.5F, 8.0F, 15.9F}},
            {.where = "runD R[7] chroma 18.7", .lab = {78.0F, 7.8F, 17.1F}},
            {.where = "runD L[6] chroma 18.8", .lab = {78.0F, 7.7F, 17.1F}},
            {.where = "runD D[4] chroma 19.3", .lab = {77.3F, 9.2F, 17.0F}},
            {.where = "runD R[1] chroma 20.2", .lab = {79.3F, 9.3F, 18.0F}},
            {
                .where = "runD F[2] chroma 21.0 - the most chromatic white measured",
                .lab = {69.0F, 9.7F, 18.6F},
            },
            {.where = "runA U[8] L 66.6 - the darkest white measured", .lab = {66.6F, 9.7F, 17.6F}},
        },
    };
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
    const std::array<Sample, 9> tiles{
        {
            {
                .where = "runA R[2] green, chroma 20.5",
                .lab = {22.2F, -11.1F, 17.3F},
                .expected = FacetColor::green,
            },
            {
                .where = "runD B[1] green, chroma 25.8",
                .lab = {39.4F, -14.8F, 21.1F},
                .expected = FacetColor::green,
            },
            {
                .where = "runD D[3] green, chroma 30.7",
                .lab = {40.8F, -18.1F, 24.8F},
                .expected = FacetColor::green,
            },
            {
                .where = "live1 L[4] yellow washed to chroma 16.7 at L 66.4",
                .lab = {66.4F, -1.1F, 16.7F},
                .expected = FacetColor::yellow,
            },
            {
                .where = "live1 D[0] yellow washed to chroma 19.3 at L 66.5",
                .lab = {66.5F, 1.2F, 19.3F},
                .expected = FacetColor::yellow,
            },
            {
                .where = "runA D[1] yellow, chroma 52.8 at L 77.9",
                .lab = {77.9F, 3.5F, 52.7F},
                .expected = FacetColor::yellow,
            },
            {
                .where = "runA F[2] blue, L 67.6 chroma 18.8 - lighter and less chromatic than the "
                         "palest "
                         "white",
                .lab = {67.6F, 3.1F, -18.6F},
                .expected = FacetColor::blue,
            },
            {
                .where = "live2 F[1] blue, L 64.1 chroma 21.6",
                .lab = {64.1F, 3.7F, -21.3F},
                .expected = FacetColor::blue,
            },
            {
                .where = "live2 F[3] blue, L 65.0 chroma 22.6",
                .lab = {65.0F, 3.9F, -22.2F},
                .expected = FacetColor::blue,
            },
        },
    };
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
    // A dark neutral is NOT white: see the dark-neutral case below. The
    // sample that used to sit here (L 30, chroma 2.8) is now unknown.
    REQUIRE(classify_lab(cv::Vec3f{30.0F, 2.0F, 2.0F}).color == FacetColor::unknown);
    // runA F[7], chroma 33.6 at L 54.2: refused by BOTH soft conditions.
    REQUIRE(classify_lab(cv::Vec3f{54.2F, 19.8F, 27.1F}).color == FacetColor::orange);
    // runD D[8], chroma 73.7 - a saturated orange, far from any edge.
    REQUIRE(classify_lab(cv::Vec3f{50.0F, 52.7F, 51.5F}).color == FacetColor::orange);
    // A washed orange that is light enough but still too chromatic.
    REQUIRE(classify_lab(cv::Vec3f{75.0F, 16.0F, 23.0F}).color == FacetColor::orange);
}

// ---------------------------------------------------------------------------
// The dark-neutral reject. A near-achromatic sample carries no hue
// information at all - a black inter-tile gap, a shadowed fingertip, an
// unlit background wall and a genuinely white sticker are all a*=b*=0 and
// differ ONLY in lightness. Naming them all white was the single largest
// source of wrong colour in the corpus, and it did so at the HIGHEST
// confidence the function can emit, which then inverted the retry logic
// that ranks slots by mean confidence.
//
// Every Lab triple below is a real measurement, read out of the
// [.cross-session] `slot` rows for the named clip/face/cell - i.e. cells
// that the shipping capture layer actually committed and handed to
// assembly, each of them classified WHITE at the time.
// ---------------------------------------------------------------------------
TEST_CASE("classify_lab refuses to name a dark neutral white", "[vision]") {
    struct Sample {
        const char* where;
        cv::Vec3f lab;
    };
    const std::array<Sample, 8> darks{
        {
            {
                .where = "good1 D[3] L 1.2 chroma 2.1 - unlit background",
                .lab = {1.21F, -1.60F, 1.36F},
            },
            {.where = "good1 R[8] L 2.4 chroma 6.4", .lab = {2.36F, -3.03F, 5.64F}},
            {
                .where = "runD U[0] L 9.1 chroma 9.6 - one of runD's two dark 'whites'",
                .lab = {9.10F, 1.20F, 9.52F},
            },
            {
                .where = "runD U[2] L 13.0 chroma 10.6 - the other one",
                .lab = {13.00F, 1.60F, 10.48F},
            },
            {.where = "live1 D[8] L 15.2 chroma 3.6 - lattice gap", .lab = {15.20F, 0.90F, 3.49F}},
            {.where = "live1 L[2] L 28.9 chroma 12.5", .lab = {28.90F, 2.50F, 12.25F}},
            {
                .where = "good1 F[1] L 32.3 chroma 14.5 - just under the chroma floor",
                .lab = {32.30F, 3.00F, 14.19F},
            },
            {.where = "live5 U[6] L 42.1 chroma 13.1", .lab = {42.10F, 2.10F, 12.93F}},
        },
    };
    for (const auto& sample : darks) {
        CAPTURE(sample.where);
        const auto facelet = classify_lab(sample.lab);
        REQUIRE(facelet.color == FacetColor::unknown);
        // Confidence must be ZERO, not merely low. mean_confidence() over a
        // face feeds ScanSession::dropMostSuspectSlot as an ARGMIN, so a
        // face made of background has to rank worst, not best.
        REQUIRE(facelet.confidence == 0.0F);
    }
}

// The two sides of the lightness floor on the chroma-floor branch. The
// floor is the same kWhiteLightnessFloor (60) the warm-cast branch already
// applied; this pins that the neutral branch now applies it too, and that
// it did not move.
TEST_CASE("the neutral white branch takes its lightness floor at 60", "[vision]") {
    const cv::Vec3f just_under{59.0F, 1.0F, 1.0F};
    const cv::Vec3f just_over{61.0F, 1.0F, 1.0F};
    REQUIRE(classify_lab(just_under).color == FacetColor::unknown);
    REQUIRE(classify_lab(just_over).color == FacetColor::white);
    // A neutral that clears the floor keeps the confidence it always had:
    // the floor decides IF the branch fires, it does not re-price it.
    REQUIRE(classify_lab(just_over).confidence > 0.9F);
}

// A dark neutral must not fall through to the hue bands either. Its hue is
// the arctangent of two numbers that are both noise, so any band it lands
// in is arbitrary - and the hue path's confidence is clamped to a 0.2
// FLOOR, which would put a background cell above a genuine washed sticker
// in exactly the ranking that matters.
TEST_CASE("a dark neutral does not fall through into a hue band", "[vision]") {
    // The same lightness and chroma, swept right around the hue circle:
    // every one of them must reject, not scatter across six names.
    for (int hue = 0; hue < 360; hue += 15) {
        const auto radians = static_cast<float>(hue) * std::numbers::pi_v<float> / 180.0F;
        const cv::Vec3f dark{20.0F, 6.0F * std::cos(radians), 6.0F * std::sin(radians)};
        CAPTURE(hue);
        REQUIRE(classify_lab(dark).color == FacetColor::unknown);
    }
}

// ---------------------------------------------------------------------------
// The green-ward neutral reject. The chroma floor is an ABSOLUTE threshold,
// so it is calibrated for one cube under one light; a clip whose greens are
// crushed below it has its green stickers short-circuited to white before
// hue is ever consulted. Both Lab triples below are real measurements read
// out of the [.cross-session] slot rows, and both are cells the shipping
// capture layer committed and handed to assembly as WHITE.
// ---------------------------------------------------------------------------
TEST_CASE("classify_lab names a crushed green sticker green, not white", "[vision]") {
    struct Sample {
        const char* where;
        cv::Vec3f lab;
    };
    const std::array<Sample, 2> greens{
        {
            // The named regression. live5 is backlit by a window: its own green
            // CENTRE measures chroma 19.7, so this sticker at chroma 11.5 falls
            // under a floor calibrated on a more chromatic cube. Its hue, 185.8,
            // is dead centre of the 120-250 green band, and its distance to
            // live5's own green centre (22.2) is shorter than to live5's own
            // white centre (27.5). It appears in all six minimal census-fixing
            // relabellings of the clip.
            {
                .where = "live5 F[5] chroma 11.5 hue 185.8 - the pinned defect",
                .lab = {61.39F, -11.46F, -1.16F},
            },
            // The second one, and the reason a SESSION-relative floor could not
            // have been the fix: live4 never captures a white centre, so it has
            // no white reference to place a floor against. Its nearest captured
            // centre is live4's own green at 15.8 Lab; every other centre it
            // captured is 39+ away.
            {
                .where = "live4 L[6] chroma 9.0 hue 214.7 - no white reference in that clip",
                .lab = {71.98F, -7.36F, -5.10F},
            },
        },
    };
    for (const auto& sample : greens) {
        CAPTURE(sample.where);
        const auto facelet = classify_lab(sample.lab);
        REQUIRE(facelet.color == FacetColor::green);
        // Named through the hue path, so priced by it: above the 0.2 floor
        // it applies, and well below a saturated tile's. A zero here would
        // mean the unknown branch answered instead.
        REQUIRE(facelet.confidence >= 0.2F);
        REQUIRE(facelet.confidence < 0.5F);
    }
}

// The guard on the other side of the same gate: the whites the direction
// test must NOT take. Passes before and after by design - it pins the
// boundary rather than demonstrating the fix, and it was verified to FAIL
// against the rejected variant of this change (the same gate at a* < 0),
// which takes live4 L[0] on 1.15 units of noise.
TEST_CASE("the green-ward reject steals no warm-cast or near-neutral white", "[vision]") {
    struct Sample {
        const char* where;
        cv::Vec3f lab;
    };
    const std::array<Sample, 8> whites{
        {
            // runD's warm-cast white face: the clip whose whites are the MOST
            // chromatic in the corpus, reached through the soft ceiling. Every
            // one of them points warm (a* +6.4 to +9.2), so the direction gate
            // must be invisible to them.
            {.where = "runD U[1] chroma 15.8", .lab = {86.15F, 6.37F, 14.41F}},
            {.where = "runD D[2] chroma 17.6", .lab = {82.91F, 7.77F, 15.76F}},
            {.where = "runD U[8] chroma 17.8", .lab = {76.48F, 7.98F, 15.92F}},
            {.where = "runD L[6] chroma 18.8", .lab = {78.07F, 7.67F, 17.11F}},
            {.where = "runD F[2] chroma 18.9", .lab = {77.36F, 7.86F, 17.13F}},
            {.where = "runD D[4] chroma 19.3", .lab = {77.27F, 9.15F, 17.02F}},
            // The most green-ward sample the neutral branch must KEEP, and the
            // measurement that sets -4.0 rather than 0.0: a bright tile whose
            // a* is at noise level and whose b* points hard at yellow.
            {
                .where = "live4 L[0] a* -1.15 - the closest retained white to the gate",
                .lab = {96.70F, -1.15F, 13.54F},
            },
            // A near-neutral that clears the lightness floor by little: the
            // gate must not be doing lightness's job.
            {
                .where = "live5 R[4] chroma 7.3 - live5's own white centre",
                .lab = {82.00F, 5.97F, 4.25F},
            },
        },
    };
    for (const auto& sample : whites) {
        CAPTURE(sample.where);
        REQUIRE(classify_lab(sample.lab).color == FacetColor::white);
    }
}

// The ordering the direction gate must not disturb: the lightness reject
// runs FIRST, so a DARK green-ward neutral is still unknown at confidence
// zero and never reaches a hue band. Sweeping the whole circle at a chroma
// under the floor is the same sweep the dark-neutral case above makes; it is
// repeated here because the direction gate introduced a second way out of
// that branch, and the commit-time unknown-cell gate depends on there being
// none. Passes before and after by design.
TEST_CASE("a dark green-ward neutral is still unknown, not green", "[vision]") {
    for (int hue = 0; hue < 360; hue += 15) {
        const auto radians = static_cast<float>(hue) * std::numbers::pi_v<float> / 180.0F;
        const cv::Vec3f dark{55.0F, 12.0F * std::cos(radians), 12.0F * std::sin(radians)};
        CAPTURE(hue);
        REQUIRE(classify_lab(dark).color == FacetColor::unknown);
        REQUIRE(classify_lab(dark).confidence == 0.0F);
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
            REQUIRE((*resolved).at(face).at(i).confidence > 0.9F);
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
    add_noise(scan, {.sigma = 5.0F, .seed = 20260719U});
    require_recovered(scan);
}

TEST_CASE("resolve_colors survives shift, chroma compression, and noise combined", "[vision]") {
    SyntheticScan scan = make_scan();
    shift_lightness(scan, 25.0F);
    scale_chroma(scan, 0.7F);
    add_noise(scan, {.sigma = 5.0F, .seed = 42U});
    require_recovered(scan);
}

// ---------------------------------------------------------------------------
// The freerot red/orange regression. Every Lab triple below is a REAL
// measurement: the 54 fused cell colours of the synth-freerot clip's
// winning sub-epoch (the runner's resolve input), read out of the fused
// aggregators, with truth taken from the clip's manifest under the
// renderer's known W<->Y global rename.
//
// The R face fused in deep shade: its centre seed sits at L 20.7 while
// the orange seed sits at L 60.1, and the face carries three
// shade-darkened ORANGE cells at Lab ~(33.3, 27.0, 38.8). Shading acts
// as a near-scalar on the whole Lab vector - those cells are the orange
// seed scaled by ~0.58 in all three channels - so under plain Euclidean
// Lab they sit 22.8 from the red seed against 41.2 from their own, and
// STILL nearer red in pure (a,b) (19.0 against 31.2). Plain-Lab
// assignment therefore put all three into the red cluster, and the
// exactly-9 census cascaded three bright reds into orange: 6 misnames,
// independently fatal to assembly. Their hue is unambiguous (55.2
// degrees against the orange seed's 54.2, red at 31.4), which is what
// the hue-aware assignment metric keys on. Fails pre-fix with exactly
// those 6 misnames.
// ---------------------------------------------------------------------------
TEST_CASE("resolve_colors names shade-darkened orange cells orange (freerot)", "[vision]") {
    struct Cell {
        cv::Vec3f lab;
        FacetColor expected;
    };
    constexpr auto W = FacetColor::white;
    constexpr auto Y = FacetColor::yellow;
    constexpr auto R = FacetColor::red;
    constexpr auto O = FacetColor::orange;
    constexpr auto G = FacetColor::green;
    constexpr auto B = FacetColor::blue;
    const std::array<std::array<Cell, 9>, 6> measured{
        {
            // The R face, fused in deep shade: cells 1, 2 and 7 are the
            // shade-darkened oranges this regression is about.
            {
                {
                    {.lab = {52.4384F, 0.0234F, 0.0156F}, .expected = W},
                    {.lab = {33.2703F, 27.0078F, 38.7578F}, .expected = O},
                    {.lab = {33.2031F, 27.3281F, 38.7188F}, .expected = O},
                    {.lab = {46.8567F, -4.8438F, 47.6484F}, .expected = Y},
                    {.lab = {20.7336F, 37.0000F, 22.5781F}, .expected = R},
                    {.lab = {20.6055F, 36.8828F, 22.4219F}, .expected = R},
                    {.lab = {20.6299F, 36.8047F, 22.8203F}, .expected = R},
                    {.lab = {33.1726F, 27.1641F, 38.6484F}, .expected = O},
                    {.lab = {52.4200F, 0.0234F, 0.0156F}, .expected = W},
                },
            },
            {
                {
                    {.lab = {60.1318F, 45.9297F, 63.5938F}, .expected = O},
                    {.lab = {91.1560F, 0.0156F, 0.0000F}, .expected = W},
                    {.lab = {82.1259F, -7.2031F, 76.6094F}, .expected = Y},
                    {.lab = {40.1733F, 59.1250F, 39.5078F}, .expected = R},
                    {.lab = {60.0891F, 45.9531F, 63.5781F}, .expected = O},
                    {.lab = {81.7413F, -6.9453F, 76.4688F}, .expected = Y},
                    {.lab = {58.1055F, -53.3594F, 38.4844F}, .expected = G},
                    {.lab = {58.0627F, -53.3594F, 38.4688F}, .expected = G},
                    {.lab = {59.2316F, 42.9922F, 61.1719F}, .expected = O},
                },
            },
            {
                {
                    {.lab = {34.3872F, 10.8516F, -46.8750F}, .expected = B},
                    {.lab = {34.6008F, 52.8359F, 34.3438F}, .expected = R},
                    {.lab = {72.0795F, -6.3359F, 68.5938F}, .expected = Y},
                    {.lab = {34.2957F, 10.6094F, -46.4531F}, .expected = B},
                    {.lab = {72.0490F, -6.5781F, 68.5859F}, .expected = Y},
                    {.lab = {50.7721F, -47.4531F, 34.0625F}, .expected = G},
                    {.lab = {72.0276F, -6.4688F, 68.5000F}, .expected = Y},
                    {.lab = {34.5337F, 52.8750F, 34.5625F}, .expected = R},
                    {.lab = {50.7812F, -47.5469F, 34.1562F}, .expected = G},
                },
            },
            {
                {
                    {.lab = {38.2904F, 12.3125F, -51.2812F}, .expected = B},
                    {.lab = {79.4220F, -6.9375F, 74.8203F}, .expected = Y},
                    {.lab = {58.0231F, 44.4531F, 61.6719F}, .expected = O},
                    {.lab = {88.1653F, 0.0156F, 0.0000F}, .expected = W},
                    {.lab = {88.1653F, 0.0156F, 0.0000F}, .expected = W},
                    {.lab = {57.8522F, 44.2188F, 61.4922F}, .expected = O},
                    {.lab = {38.7970F, 57.8516F, 38.3125F}, .expected = R},
                    {.lab = {79.3671F, -6.7656F, 74.8203F}, .expected = Y},
                    {.lab = {88.1653F, 0.0156F, 0.0000F}, .expected = W},
                },
            },
            {
                {
                    {.lab = {67.1875F, -6.2188F, 64.5078F}, .expected = Y},
                    {.lab = {74.7070F, 0.0156F, 0.0000F}, .expected = W},
                    {.lab = {31.7963F, 10.1719F, -44.3438F}, .expected = B},
                    {.lab = {31.7993F, 9.9141F, -44.3906F}, .expected = B},
                    {.lab = {31.6925F, 9.9531F, -44.0547F}, .expected = B},
                    {.lab = {47.2290F, -44.7969F, 31.9062F}, .expected = G},
                    {.lab = {47.2076F, -44.7422F, 31.9219F}, .expected = G},
                    {.lab = {31.8115F, 10.1016F, -44.1484F}, .expected = B},
                    {.lab = {31.8665F, 49.8984F, 32.3828F}, .expected = R},
                },
            },
            {
                {
                    {.lab = {83.2550F, 0.0078F, 0.0000F}, .expected = W},
                    {.lab = {54.5441F, 41.7812F, 58.3594F}, .expected = O},
                    {.lab = {35.8307F, 11.3984F, -48.3438F}, .expected = B},
                    {.lab = {35.9009F, 11.6562F, -48.8281F}, .expected = B},
                    {.lab = {53.0609F, -49.2891F, 35.4453F}, .expected = G},
                    {.lab = {52.8717F, -49.1797F, 35.4453F}, .expected = G},
                    {.lab = {36.1847F, 54.6172F, 35.8516F}, .expected = R},
                    {.lab = {83.1573F, 0.0156F, 0.0000F}, .expected = W},
                    {.lab = {52.7893F, -49.0703F, 35.6406F}, .expected = G},
                },
            },
        },
    };

    std::array<CapturedFace, 6> faces{};
    for (std::size_t face = 0; face < 6; ++face) {
        for (std::size_t i = 0; i < 9; ++i) {
            faces.at(face).lab.at(i) = measured.at(face).at(i).lab;
        }
    }
    const auto resolved = resolve_colors(faces);
    REQUIRE(resolved.has_value());
    for (std::size_t face = 0; face < 6; ++face) {
        for (std::size_t i = 0; i < 9; ++i) {
            CAPTURE(face, i);
            REQUIRE((*resolved).at(face).at(i).color == measured.at(face).at(i).expected);
        }
    }
    // The three shaded cells are not merely named right, they are priced
    // as the safe assignments the hue margin says they are - plain-Lab
    // pricing would report confidence zero on them (their plain-Lab
    // nearest mean is red's).
    for (const std::size_t cell : {std::size_t{1}, std::size_t{2}, std::size_t{7}}) {
        CAPTURE(cell);
        REQUIRE((*resolved).at(0).at(cell).color == FacetColor::orange);
        REQUIRE((*resolved).at(0).at(cell).confidence > 0.0F);
    }
}

TEST_CASE("resolve_colors rejects a scan with two identical centres", "[vision]") {
    SyntheticScan scan = make_scan();
    scan.faces.at(1).lab.at(4) = scan.faces.at(0).lab.at(4);

    const auto resolved = resolve_colors(scan.faces);
    REQUIRE_FALSE(resolved.has_value());
    REQUIRE(resolved.error() == cube::CubeError::no_valid_cube_detected);
}

// Non-finite input. Before the finiteness sweep this was not a wrong
// answer, it was memory corruption plus a hang: every distance from a NaN
// sample compares false, so assign_clusters' `nearest_cluster` stayed -1,
// the sample won the margin argmax with kInf, and the loop executed
// `++load[static_cast<std::size_t>(-1)]` - an increment at index SIZE_MAX
// of a six-element stack array - then re-selected the same sample forever.
// An inf sample reaches the identical state through `inf < inf` being
// false. Every one of the 54 slots is a caller-supplied value on a public
// entry point, so each is checked, not just the centres.
TEST_CASE("resolve_colors rejects a NaN sample instead of corrupting memory", "[vision]") {
    const auto nan_value = std::numeric_limits<float>::quiet_NaN();

    SECTION("NaN in an off-centre sticker") {
        SyntheticScan scan = make_scan();
        scan.faces.at(2).lab.at(7)(1) = nan_value;
        const auto resolved = resolve_colors(scan.faces);
        REQUIRE_FALSE(resolved.has_value());
        REQUIRE(resolved.error() == cube::CubeError::no_valid_cube_detected);
    }

    SECTION("NaN in a centre - the seed-separation guard cannot see it") {
        SyntheticScan scan = make_scan();
        scan.faces.at(0).lab.at(4)(0) = nan_value;
        // Proof the existing guard is defeated: every pairwise comparison
        // against a NaN centre is false, so the loop that is supposed to
        // reject implausible seeds passes it through.
        for (std::size_t other = 1; other < 6; ++other) {
            const cv::Vec3f diff = scan.faces.at(0).lab.at(4) - scan.faces.at(other).lab.at(4);
            REQUIRE_FALSE(std::sqrt(diff.dot(diff)) < 12.0F);
        }
        const auto resolved = resolve_colors(scan.faces);
        REQUIRE_FALSE(resolved.has_value());
        REQUIRE(resolved.error() == cube::CubeError::no_valid_cube_detected);
    }
}

TEST_CASE("resolve_colors rejects an infinite sample instead of hanging", "[vision]") {
    SyntheticScan scan = make_scan();
    scan.faces.at(4).lab.at(0)(2) = -std::numeric_limits<float>::infinity();

    const auto resolved = resolve_colors(scan.faces);
    REQUIRE_FALSE(resolved.has_value());
    REQUIRE(resolved.error() == cube::CubeError::no_valid_cube_detected);
}

// ---------------------------------------------------------------------------
// resolve_colors_explained and classify_lab_explained: the same answers with
// their working shown, for the developer diagnostics channel
// (docs/diagnostics.md). Nothing is computed twice; these hold that what is
// reported is what decided the result.
// ---------------------------------------------------------------------------

namespace {

// `lab` with its hue turned by `degrees` and its lightness and chroma kept:
// what a warm light does to a red.
cv::Vec3f hue_turned(const cv::Vec3f& lab, float degrees) {
    const float chroma = std::hypot(lab(1), lab(2));
    const float hue = std::atan2(lab(2), lab(1)) + (degrees * std::numbers::pi_v<float> / 180.0F);
    return {lab(0), chroma * std::cos(hue), chroma * std::sin(hue)};
}

}  // namespace

TEST_CASE("classify_lab_explained says which rule decided and from what", "[vision]") {
    const ColorPalette palette = default_palette();

    // A saturated red goes by its hue band; the numbers are the ones the band
    // was chosen from.
    const cv::Vec3f red = palette.entries.at(2).lab;
    const LabClassification band = classify_lab_explained(red);
    CHECK(band.rule == LabRule::hue_band);
    CHECK(band.facelet.color == FacetColor::red);
    CHECK(std::abs(band.chroma - std::hypot(red(1), red(2))) < 1e-3F);
    CHECK(band.hue_degrees > 0.0F);
    CHECK(band.hue_degrees < 44.0F);

    // A bright neutral is white by the neutral rule, a dark one is unknown,
    // and a light warm-cast sample is white by the warm-cast rule.
    CHECK(classify_lab_explained({92.0F, 0.0F, 0.0F}).rule == LabRule::neutral_white);
    const LabClassification dark = classify_lab_explained({30.0F, 0.0F, 0.0F});
    CHECK(dark.rule == LabRule::dark_neutral);
    CHECK(dark.facelet.color == FacetColor::unknown);
    CHECK(dark.facelet.confidence == 0.0F);
    const LabClassification warm = classify_lab_explained({75.0F, 10.0F, 17.0F});
    CHECK(warm.rule == LabRule::warm_cast_white);
    CHECK(warm.facelet.color == FacetColor::white);

    // classify_lab is this with the rest dropped.
    const std::array<cv::Vec3f, 4> samples = {
        red,
        cv::Vec3f(92.0F, 0.0F, 0.0F),
        cv::Vec3f(30.0F, 0.0F, 0.0F),
        cv::Vec3f(75.0F, 10.0F, 17.0F),
    };
    for (const cv::Vec3f& lab : samples) {
        CHECK(classify_lab(lab).color == classify_lab_explained(lab).facelet.color);
        CHECK(classify_lab(lab).confidence == classify_lab_explained(lab).facelet.confidence);
    }
}

TEST_CASE("resolve_colors_explained returns the answer of resolve_colors and its working",
          "[vision]") {
    const SyntheticScan scan = make_scan();
    const auto plain = resolve_colors(scan.faces);
    const auto explained = resolve_colors_explained(scan.faces);
    REQUIRE(plain.has_value());
    REQUIRE(explained.has_value());

    for (std::size_t face = 0; face < 6; ++face) {
        for (std::size_t cell = 0; cell < 9; ++cell) {
            CAPTURE(face, cell);
            CHECK(explained->faces.at(face).at(cell).color == plain->at(face).at(cell).color);
            CHECK(explained->faces.at(face).at(cell).confidence ==
                  plain->at(face).at(cell).confidence);
        }
    }

    // Six distinct hues: the hue names were taken, and they were the names
    // distance had already given.
    const ColorPalette palette = default_palette();
    CHECK(explained->hue_override == HueOverride::applied);
    CHECK_FALSE(explained->hue_changed_names);
    CHECK(explained->iterations >= 1);
    CHECK(explained->match_cost >= 0.0F);

    std::array<int, 6> members{};
    for (std::size_t c = 0; c < 6; ++c) {
        CAPTURE(c);
        const ClusterNaming& cluster = explained->clusters.at(c);
        CHECK((cluster.seed == scan.faces.at(c).lab.at(4)));
        // The six clusters are the six palette colours, face by face.
        CHECK(cluster.name == palette.entries.at(c).color);
        CHECK(cluster.by_distance == cluster.name);
        CHECK(cluster.hue.facelet.color == cluster.name);
    }
    for (std::size_t face = 0; face < 6; ++face) {
        for (std::size_t cell = 0; cell < 9; ++cell) {
            const std::size_t cluster = explained->cluster_of.at(face).at(cell);
            REQUIRE(cluster < 6);
            ++members.at(cluster);
            CHECK(explained->clusters.at(cluster).name == scan.truth.at(face).at(cell));
        }
    }
    // Exactly nine stickers in each.
    for (const int count : members) {
        CHECK(count == 9);
    }
}

TEST_CASE("resolve_colors_explained says when two clusters share a hue band", "[vision]") {
    // Blue stickers turned 50 degrees into the band green lives in. Hue alone
    // now names two clusters green, which is a collision of two colours that
    // are not neighbours on the wheel, so nothing about the light says which
    // is which: the distance matching has to stand, and it names them right.
    // (Two neighbours in one band are another matter - see the white
    // reference tests - and are split by which is yellower.)
    SyntheticScan scan = make_scan();
    for (std::size_t face = 0; face < 6; ++face) {
        for (std::size_t cell = 0; cell < 9; ++cell) {
            if (scan.truth.at(face).at(cell) == FacetColor::blue) {
                scan.faces.at(face).lab.at(cell) =
                    hue_turned(scan.faces.at(face).lab.at(cell), -50.0F);
            }
        }
    }

    const auto explained = resolve_colors_explained(scan.faces);
    REQUIRE(explained.has_value());
    CHECK(explained->hue_override == HueOverride::collision);
    CHECK_FALSE(explained->hue_changed_names);

    // The palette's face index 4 is green and 5 is blue (make_scan).
    const ClusterNaming& green = explained->clusters.at(4);
    const ClusterNaming& blue = explained->clusters.at(5);
    CHECK(green.hue.facelet.color == FacetColor::green);
    CHECK(blue.hue.facelet.color == FacetColor::green);
    CHECK(blue.hue.hue_degrees < 250.0F);
    CHECK(green.name == FacetColor::green);
    CHECK(blue.name == FacetColor::blue);

    // And the answer is the right one.
    require_recovered(scan);
}

TEST_CASE("resolve_colors_explained says when the hue names replaced a swapped matching",
          "[vision]") {
    // Red lifted 45 L and orange darkened 40: by lightness they have traded
    // places, so matching by distance alone calls the red cluster orange and
    // the orange one red. Their hues are untouched, and name them right.
    SyntheticScan scan = make_scan();
    for (std::size_t face = 0; face < 6; ++face) {
        for (std::size_t cell = 0; cell < 9; ++cell) {
            if (scan.truth.at(face).at(cell) == FacetColor::red) {
                scan.faces.at(face).lab.at(cell)(0) += 45.0F;
            } else if (scan.truth.at(face).at(cell) == FacetColor::orange) {
                scan.faces.at(face).lab.at(cell)(0) -= 40.0F;
            }
        }
    }

    const auto explained = resolve_colors_explained(scan.faces);
    REQUIRE(explained.has_value());
    CHECK(explained->hue_override == HueOverride::applied);
    CHECK(explained->hue_changed_names);
    CHECK(explained->clusters.at(2).by_distance == FacetColor::orange);
    CHECK(explained->clusters.at(2).name == FacetColor::red);
    CHECK(explained->clusters.at(3).by_distance == FacetColor::red);
    CHECK(explained->clusters.at(3).name == FacetColor::orange);
    require_recovered(scan);
}

TEST_CASE("resolve_colors_explained fails exactly where resolve_colors does", "[vision]") {
    SyntheticScan scan = make_scan();
    scan.faces.at(1).lab.at(4) = scan.faces.at(0).lab.at(4);
    const auto plain = resolve_colors(scan.faces);
    const auto explained = resolve_colors_explained(scan.faces);
    REQUIRE_FALSE(plain.has_value());
    REQUIRE_FALSE(explained.has_value());
    CHECK(explained.error() == plain.error());

    SyntheticScan not_finite = make_scan();
    not_finite.faces.at(2).lab.at(0)(1) = std::numeric_limits<float>::quiet_NaN();
    REQUIRE_FALSE(resolve_colors_explained(not_finite.faces).has_value());
}

// ---------------------------------------------------------------------------
// The light a scan was made in
// ---------------------------------------------------------------------------

namespace {

// The six centres of three real scans, in float Lab, in the order white,
// yellow, red, orange, green, blue.
struct MeasuredCube {
    const char* light = "";
    std::array<cv::Vec3f, 6> centres{};
    float cast = 0.0F;  // the white centre's chroma
};

const std::array<FacetColor, 6> kCentreOrder = {
    FacetColor::white,  FacetColor::yellow, FacetColor::red,
    FacetColor::orange, FacetColor::green,  FacetColor::blue,
};

// The cubes are functions, not objects of static storage duration: a Vec3f is
// not a constant expression, and an object built at start-up that could throw
// has no one to catch it.

// Morning daylight, bright blue sky (direct-scan/20261013/s01).
const MeasuredCube& daylight_cube() {
    static const MeasuredCube cube{
        .light = "daylight",
        .centres =
            {
                {
                    {66.1F, 4.5F, -0.6F},
                    {67.6F, 3.4F, 28.0F},
                    {42.9F, 37.4F, 3.1F},
                    {53.2F, 34.2F, 16.8F},
                    {44.6F, -16.4F, 0.6F},
                    {39.8F, 7.0F, -28.8F},
                },
            },
        .cast = 4.6F,
    };
    return cube;
}

// A tungsten lamp (live-demo-p6-retry, 2026-08-07).
const MeasuredCube& tungsten_cube() {
    static const MeasuredCube cube{
        .light = "tungsten",
        .centres =
            {
                {
                    {46.2F, 16.6F, 5.9F},
                    {49.8F, 19.9F, 53.7F},
                    {25.6F, 48.1F, 36.3F},
                    {34.3F, 53.5F, 45.4F},
                    {17.4F, -12.5F, 15.0F},
                    {11.3F, 12.1F, -25.8F},
                },
            },
        .cast = 17.7F,
    };
    return cube;
}

// An evening warm-white LED, 2026-10-14.
const MeasuredCube& evening_led_cube() {
    static const MeasuredCube cube{
        .light = "warm-white LED",
        .centres =
            {
                {
                    {64.9F, 21.8F, 18.6F},
                    {54.1F, 25.8F, 52.9F},
                    {27.7F, 48.0F, 32.6F},
                    {40.5F, 55.8F, 46.4F},
                    {16.8F, -12.6F, 14.8F},
                    {8.9F, 5.2F, -15.0F},
                },
            },
        .cast = 28.7F,
    };
    return cube;
}

std::array<CapturedFace, 6> faces_of(const MeasuredCube& cube) {
    std::array<CapturedFace, 6> faces{};
    for (std::size_t face = 0; face < 6; ++face) {
        faces.at(face).lab.assign(9, cube.centres.at(face));
    }
    // A few stickers traded across sides, as a real scramble does, so the
    // clusters span faces; nine of each colour is kept.
    std::swap(faces.at(0).lab.at(0), faces.at(2).lab.at(8));
    std::swap(faces.at(1).lab.at(1), faces.at(3).lab.at(7));
    std::swap(faces.at(4).lab.at(2), faces.at(5).lab.at(6));
    std::swap(faces.at(2).lab.at(3), faces.at(3).lab.at(5));
    return faces;
}

float chroma_of_centre(const std::array<CapturedFace, 6>& faces, std::size_t face) {
    const cv::Vec3f& centre = faces.at(face).lab.at(4);
    return std::hypot(centre(1), centre(2));
}

float hue_degrees(const cv::Vec3f& lab) {
    const float hue = std::atan2(lab(2), lab(1)) * 180.0F / std::numbers::pi_v<float>;
    return hue < 0.0F ? hue + 360.0F : hue;
}

}  // namespace

TEST_CASE("the white centre is found, and how far the light is from neutral is read from it",
          "[vision][white-reference]") {
    for (const MeasuredCube* cube : {&daylight_cube(), &tungsten_cube(), &evening_led_cube()}) {
        INFO(cube->light);
        const auto found = find_white_reference(faces_of(*cube));
        const WhiteReference& reference = cube::test::must_have(found);
        CHECK(reference.side == 0);
        CHECK(reference.cast == Catch::Approx(cube->cast).margin(0.2));
    }
}

TEST_CASE("a warm light is corrected and a neutral one is left exactly alone",
          "[vision][white-reference]") {
    SECTION("daylight changes nothing") {
        const auto found = find_white_reference(faces_of(daylight_cube()));
        const WhiteReference& reference = cube::test::must_have(found);
        CHECK(reference.cast < kMinCorrectedCast);
        for (const cv::Vec3f& centre : daylight_cube().centres) {
            CHECK(neutralised(reference, centre) == centre);
        }
    }
    for (const MeasuredCube* cube : {&tungsten_cube(), &evening_led_cube()}) {
        SECTION(cube->light) {
            const auto found = find_white_reference(faces_of(*cube));
            const WhiteReference& reference = cube::test::must_have(found);
            CHECK(reference.cast >= kMinCorrectedCast);
            // The white is grey at the lightness it had.
            const cv::Vec3f white = neutralised(reference, cube->centres.at(0));
            CHECK(std::abs(white(1)) < 1.5F);
            CHECK(std::abs(white(2)) < 1.5F);
            CHECK(white(0) == Catch::Approx(cube->centres.at(0)(0)).margin(1.5));
        }
    }
}

TEST_CASE("a warm light puts yellow in the orange band and correcting it puts it back",
          "[vision][white-reference]") {
    for (const MeasuredCube* cube : {&tungsten_cube(), &evening_led_cube()}) {
        INFO(cube->light);
        const cv::Vec3f& yellow = cube->centres.at(1);
        // The premise: as measured, hue classification calls it orange.
        REQUIRE(classify_lab(yellow).color == FacetColor::orange);
        const auto found = find_white_reference(faces_of(*cube));
        const WhiteReference& reference = cube::test::must_have(found);
        const cv::Vec3f corrected = neutralised(reference, yellow);
        CHECK(hue_degrees(corrected) > 85.0F);
        CHECK(classify_lab(corrected).color == FacetColor::yellow);
    }
}

TEST_CASE("red and orange are further apart in hue once the light is neutralised",
          "[vision][white-reference]") {
    const MeasuredCube& cube = evening_led_cube();
    const auto found = find_white_reference(faces_of(cube));
    const WhiteReference& reference = cube::test::must_have(found);
    const float before = hue_degrees(cube.centres.at(3)) - hue_degrees(cube.centres.at(2));
    const float after = hue_degrees(neutralised(reference, cube.centres.at(3))) -
                        hue_degrees(neutralised(reference, cube.centres.at(2)));
    CHECK(before < 8.0F);
    CHECK(after > 12.0F);
}

TEST_CASE("no reference is taken from six centres none of which is plausibly white",
          "[vision][white-reference]") {
    // Built on the tungsten cube, whose lighter centres are all strongly
    // coloured. (A daylight cube with no white would let its pale green
    // through as the least chromatic - which a real cube cannot be: it
    // always has its white centre.)
    SECTION("every centre strongly coloured") {
        MeasuredCube cube = tungsten_cube();
        cube.centres.at(0) = {46.0F, 40.0F, -30.0F};  // a purple where the white was
        CHECK_FALSE(find_white_reference(faces_of(cube)).has_value());
    }
    SECTION("the candidate would need a gain no light asks for") {
        MeasuredCube cube = tungsten_cube();
        // A yellow: chroma 36 is under the cast ceiling and clear of the
        // runner up, so only the gain bound stands between it and "white".
        cube.centres.at(0) = {50.0F, 2.0F, 36.0F};
        CHECK_FALSE(find_white_reference(faces_of(cube)).has_value());
    }
    SECTION("a centre that is not a number") {
        auto faces = faces_of(evening_led_cube());
        faces.at(3).lab.at(4)(1) = std::numeric_limits<float>::quiet_NaN();
        CHECK_FALSE(find_white_reference(faces).has_value());
    }
}

TEST_CASE("a dark blue under a warm light is not mistaken for the white",
          "[vision][white-reference]") {
    // Blue is the least chromatic centre of the cube under the LED (15
    // against the white's 29) and the darkest by far.
    const auto faces = faces_of(evening_led_cube());
    REQUIRE(chroma_of_centre(faces, 5) < chroma_of_centre(faces, 0));
    const auto found = find_white_reference(faces);
    const WhiteReference& reference = cube::test::must_have(found);
    CHECK(reference.side == 0);
}

TEST_CASE("the tungsten cube is named right once the white is known, and was not before",
          "[vision][white-reference]") {
    const auto faces = faces_of(tungsten_cube());

    // As measured, the closest two colours are named backwards: this is the
    // case the exchange of names in assembly existed to rescue.
    const auto raw = resolve_colors(faces);
    REQUIRE(raw.has_value());
    REQUIRE((*raw).at(3).at(4).color != FacetColor::orange);

    const auto found = find_white_reference(faces);
    const WhiteReference& reference = cube::test::must_have(found);
    auto neutral = faces;
    for (CapturedFace& face : neutral) {
        for (cv::Vec3f& lab : face.lab) {
            lab = neutralised(reference, lab);
        }
    }
    const auto named = resolve_colors(neutral, reference.side);
    REQUIRE(named.has_value());
    for (std::size_t face = 0; face < 6; ++face) {
        INFO("face " << face);
        CHECK((*named).at(face).at(4).color == kCentreOrder.at(face));
    }
}

TEST_CASE("two neighbours that arrive in one hue band are told apart by which is yellower",
          "[vision][white-reference]") {
    // Orange at hue 29 and red at 20 degrees: both under classify_lab's edge
    // at 44, so both come out red. Orange is the darker here (as a shaded
    // one is), which is what the distance matching gets backwards: it pairs
    // the lighter cluster with the orange reference.
    MeasuredCube cube = daylight_cube();
    cube.centres.at(2) = {60.0F, 38.0F, 14.0F};  // red, hue 20, light
    cube.centres.at(3) = {36.0F, 40.0F, 22.0F};  // orange, hue 29, dark
    const auto faces = faces_of(cube);
    REQUIRE(classify_lab(cube.centres.at(2)).color == FacetColor::red);
    REQUIRE(classify_lab(cube.centres.at(3)).color == FacetColor::red);

    const auto named = resolve_colors(faces);
    REQUIRE(named.has_value());
    CHECK((*named).at(2).at(4).color == FacetColor::red);
    CHECK((*named).at(3).at(4).color == FacetColor::orange);
}
