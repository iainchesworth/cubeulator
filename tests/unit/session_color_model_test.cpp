// SessionColorModel: session-relative sticker colour naming.
//
// The four properties these tests exist to hold, one per requirement of
// the design the model was built to:
//
//   1. BOOTSTRAP  - with no evidence the model reproduces classify_lab's
//                   absolute decision, because the absolute classifier is
//                   the only prior available at session start.
//   2. ADAPTATION - a session whose illumination has shifted every tile
//                   across a fixed band edge is still named correctly,
//                   which is precisely what classify_lab cannot do.
//   3. DEGENERACY - no sequence of observations may bring two references
//                   together. Two references merging is unrecoverable.
//   4. WHITE      - decided on chroma, not hue, and the adaptive ceiling
//                   may not climb over a real colour.
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cube/vision/color_classification.hpp>
#include <cube/vision/color_resolution.hpp>
#include <cube/vision/session_color_model.hpp>

#include <array>
#include <cmath>
#include <numbers>
#include <string>

using namespace cube::vision;
using cube::model::FacetColor;

namespace {

// A Lab sample at a given lightness, chroma and hue - the parameterisation
// the model actually reasons in, so tests can state intent ("a tile at the
// orange hue, washed to chroma 20") instead of opaque a/b pairs.
cv::Vec3f polar_lab(float lightness, float chroma, float hue_degrees) {
    const float radians = hue_degrees * std::numbers::pi_v<float> / 180.0F;
    return cv::Vec3f{lightness, chroma * std::cos(radians), chroma * std::sin(radians)};
}

std::array<cv::Vec3f, 9> uniform_face(const cv::Vec3f& lab) {
    std::array<cv::Vec3f, 9> face{};
    face.fill(lab);
    return face;
}

char letter(FacetColor color) {
    switch (color) {
        case FacetColor::white: return 'W';
        case FacetColor::yellow: return 'Y';
        case FacetColor::red: return 'R';
        case FacetColor::orange: return 'O';
        case FacetColor::green: return 'G';
        case FacetColor::blue: return 'B';
        case FacetColor::unknown: return '?';
    }
    return '?';
}

std::string pattern(const SessionFaceReading& reading) {
    std::string out;
    for (const auto& facelet : reading.facelets) {
        out.push_back(letter(facelet.color));
    }
    return out;
}

}  // namespace

// ---------------------------------------------------------------------------
// 1. Bootstrap
// ---------------------------------------------------------------------------

// The strong form of the bootstrap claim: not "roughly agrees" but "is the
// same function". Swept across the whole hue circle at a chroma every band
// admits, a virgin model's naming must equal classify_lab's, because an
// unseeded colour is represented by classify_lab's own band and the argmin
// over six band penalties selects the band that contains the hue.
TEST_CASE("SessionColorModel bootstraps to the absolute classifier's decision", "[vision][session-color]") {
    const SessionColorModel model;
    for (int hue = 0; hue < 360; ++hue) {
        const cv::Vec3f sample = polar_lab(60.0F, 40.0F, static_cast<float>(hue));
        const auto reading = model.classify_face(uniform_face(sample));
        INFO("hue " << hue);
        REQUIRE(reading.facelets[0].color == classify_lab(sample).color);
    }
}

// The bootstrap prior must also cover white, which classify_lab decides on
// chroma. A virgin model has no white reference either, so this exercises
// the chroma branch under the same no-evidence condition.
TEST_CASE("SessionColorModel bootstraps white from the absolute chroma floor", "[vision][session-color]") {
    const SessionColorModel model;
    for (int hue = 0; hue < 360; hue += 15) {
        const cv::Vec3f achromatic = polar_lab(88.0F, 4.0F, static_cast<float>(hue));
        REQUIRE(classify_lab(achromatic).color == FacetColor::white);
        REQUIRE(model.classify_face(uniform_face(achromatic)).facelets[0].color == FacetColor::white);
    }
}

// ---------------------------------------------------------------------------
// 2. Adaptation
// ---------------------------------------------------------------------------

// THE MEASURED FAILURE, reproduced and then fixed.
//
// classify_lab puts the red/orange edge at 44 degrees. A session under a
// warmer light - or simply a different webcam's white balance - rotates
// every tile's hue. Here every tile is rotated +18 degrees, which carries
// this cube's reds from 32 to 50: past the fixed edge, so the absolute
// classifier calls them ORANGE, and calls the session's oranges (55 -> 73)
// orange as well. Two physical colours, one name: exactly the collapse
// that defeats the router at 27 Lab separation.
//
// The session model sees the same rotated tiles. Once evidence has seeded
// both references from where THIS session's colours actually sit, the
// boundary between them sits between them, and the two stay distinct.
TEST_CASE("SessionColorModel adapts to a shifted-illumination session", "[vision][session-color]") {
    constexpr float kShiftDeg = 18.0F;
    const cv::Vec3f red = polar_lab(45.0F, 45.0F, 32.0F + kShiftDeg);
    const cv::Vec3f orange = polar_lab(62.0F, 48.0F, 55.0F + kShiftDeg);

    // The absolute classifier collapses them.
    REQUIRE(classify_lab(red).color == classify_lab(orange).color);

    SessionColorModel model;
    // A plausible session: a few faces showing this cube's palette. Faces
    // mixing two colours are what a scrambled cube actually presents, and
    // are what lets the model see both references in one observation.
    const cv::Vec3f green = polar_lab(50.0F, 40.0F, 140.0F + kShiftDeg);
    const cv::Vec3f blue = polar_lab(40.0F, 42.0F, 270.0F + kShiftDeg);
    const cv::Vec3f yellow = polar_lab(85.0F, 55.0F, 95.0F + kShiftDeg);
    const cv::Vec3f white = polar_lab(90.0F, 5.0F, 0.0F);

    for (int round = 0; round < 4; ++round) {
        std::array<cv::Vec3f, 9> mixed{red, red,   orange, orange, green,
                                       blue, blue, yellow, white};
        model.observe(mixed);
    }

    REQUIRE(model.seeded(FacetColor::red));
    REQUIRE(model.seeded(FacetColor::orange));

    const auto red_face = model.classify_face(uniform_face(red));
    const auto orange_face = model.classify_face(uniform_face(orange));
    REQUIRE(red_face.facelets[0].color == FacetColor::red);
    REQUIRE(orange_face.facelets[0].color == FacetColor::orange);
}

// Adaptation must not be a one-shot latch: a light that changes MID-session
// (the user turning toward a window) has to be tracked, which is what the
// alpha floor buys. Seed the model on one illumination, then drive it with
// another, and the references must follow rather than stay pinned.
//
// WHICH colour is asserted on matters, and the reason is the whole design
// tension. This cube's red and orange sit only 23 degrees apart, so a
// shifted red lands within kUpdateMarginDeg of BOTH references and the
// ambiguity guard refuses to learn from it - correctly, because "the light
// moved 10 degrees" and "that is the other colour" predict identical
// pixels, and guessing is exactly how two references merge. Green's
// nearest neighbour is 45 degrees away, so a shifted green is unambiguous
// and does teach. That asymmetry is the guard doing its job, not a
// limitation worked around: the model tracks illumination wherever
// tracking is identifiable and freezes wherever it is not.
TEST_CASE("SessionColorModel tracks illumination that changes mid-session", "[vision][session-color]") {
    SessionColorModel model;
    const auto drive = [&](float shift, int rounds) {
        for (int round = 0; round < rounds; ++round) {
            std::array<cv::Vec3f, 9> mixed{
                polar_lab(45.0F, 45.0F, 32.0F + shift), polar_lab(45.0F, 45.0F, 32.0F + shift),
                polar_lab(62.0F, 48.0F, 55.0F + shift), polar_lab(50.0F, 40.0F, 140.0F + shift),
                polar_lab(40.0F, 42.0F, 270.0F + shift), polar_lab(85.0F, 55.0F, 95.0F + shift),
                polar_lab(90.0F, 5.0F, 0.0F),            polar_lab(90.0F, 5.0F, 0.0F),
                polar_lab(50.0F, 40.0F, 140.0F + shift)};
            model.observe(mixed);
        }
    };

    const auto reference_hue = [&](FacetColor color) {
        const auto& reference = model.references()[session_color_index(color)];
        return std::atan2(reference.lab[2], reference.lab[1]) * 180.0F / std::numbers::pi_v<float>;
    };

    drive(0.0F, 6);
    const float settled_green = reference_hue(FacetColor::green);
    drive(10.0F, 12);
    const float tracked_green = reference_hue(FacetColor::green);

    // The reference FOLLOWED the light rather than staying pinned where it
    // was first seeded - the property the alpha floor exists to provide.
    // Without a floor the EMA is a running mean, and after 18 faces a
    // change would move it by a twentieth of the shift.
    REQUIRE(tracked_green > settled_green + 5.0F);

    // Every colour is still named correctly under the new light, including
    // the two the guard declined to re-learn: their bootstrap-era
    // references remain closer to their own shifted tiles than to each
    // other, which is why freezing is a safe answer and merging is not.
    REQUIRE(model.classify_face(uniform_face(polar_lab(50.0F, 40.0F, 150.0F))).facelets[0].color ==
            FacetColor::green);
    REQUIRE(model.classify_face(uniform_face(polar_lab(45.0F, 45.0F, 42.0F))).facelets[0].color ==
            FacetColor::red);
    REQUIRE(model.classify_face(uniform_face(polar_lab(62.0F, 48.0F, 65.0F))).facelets[0].color ==
            FacetColor::orange);
}

// ---------------------------------------------------------------------------
// 3. Degeneracy
// ---------------------------------------------------------------------------

// The invariant that must survive ANY input, including adversarial input:
// two seeded references never come within the separation floors. This is
// the guard whose absence would be catastrophic, so it is tested by
// actively trying to break it - feeding faces engineered to drag one
// reference onto another - rather than by observing well-behaved data.
TEST_CASE("SessionColorModel never merges two colour references", "[vision][session-color]") {
    SessionColorModel model;

    // Establish a normal palette first, so there are references to merge.
    for (int round = 0; round < 5; ++round) {
        std::array<cv::Vec3f, 9> mixed{polar_lab(45.0F, 45.0F, 32.0F),  polar_lab(62.0F, 48.0F, 55.0F),
                                       polar_lab(85.0F, 55.0F, 95.0F),  polar_lab(50.0F, 40.0F, 140.0F),
                                       polar_lab(40.0F, 42.0F, 270.0F), polar_lab(90.0F, 5.0F, 0.0F),
                                       polar_lab(45.0F, 45.0F, 32.0F),  polar_lab(62.0F, 48.0F, 55.0F),
                                       polar_lab(50.0F, 40.0F, 140.0F)};
        model.observe(mixed);
    }

    // Now attack: walk faces of "orange" steadily backwards onto red's
    // hue, one degree at a time, which is the smoothest possible attempt
    // to slide a reference across without ever presenting an obviously
    // ambiguous sample.
    for (int step = 0; step <= 40; ++step) {
        const float hue = 55.0F - static_cast<float>(step);
        model.observe(uniform_face(polar_lab(62.0F, 48.0F, hue)));

        const auto& references = model.references();
        for (std::size_t a = 1; a < kSessionColorCount; ++a) {
            if (!references[a].seeded) {
                continue;
            }
            for (std::size_t b = a + 1; b < kSessionColorCount; ++b) {
                if (!references[b].seeded) {
                    continue;
                }
                const cv::Vec3f diff = references[a].lab - references[b].lab;
                INFO("step " << step << " colours " << a << "," << b);
                REQUIRE(std::sqrt(diff.dot(diff)) >= kMinReferenceSeparationLab);
            }
        }
    }

    // And the point of the invariant: red and orange are still two names.
    REQUIRE(model.classify_face(uniform_face(polar_lab(45.0F, 45.0F, 32.0F))).facelets[0].color !=
            model.classify_face(uniform_face(polar_lab(62.0F, 48.0F, 55.0F))).facelets[0].color);
}

// An ambiguous group must not teach at all. Feed only samples sitting on
// the red/orange boundary - the single most dangerous evidence there is -
// and no reference may be seeded from them.
TEST_CASE("SessionColorModel refuses to learn from ambiguous samples", "[vision][session-color]") {
    SessionColorModel model;
    for (int round = 0; round < 10; ++round) {
        // Exactly between the bootstrap red band's upper edge (44) and the
        // orange band's lower edge (44): zero margin by construction.
        model.observe(uniform_face(polar_lab(55.0F, 45.0F, 44.0F)));
    }
    REQUIRE_FALSE(model.seeded(FacetColor::red));
    REQUIRE_FALSE(model.seeded(FacetColor::orange));
}

// ---------------------------------------------------------------------------
// 4. White
// ---------------------------------------------------------------------------

// White is structurally not a hue. The adaptive ceiling must follow the
// session's actual whites, but is not allowed to climb over a real colour -
// a session that washes out must lose neither its whites to the hue bands
// nor its palest colour to white.
TEST_CASE("SessionColorModel keeps white chroma-based and bounded", "[vision][session-color]") {
    SessionColorModel model;

    // A session whose whites sit at chroma 12 - above classify_lab's fixed
    // ceiling of 9-ish measurements but well within its 15 constant - and
    // whose palest real colour is a washed blue at chroma 24.
    for (int round = 0; round < 6; ++round) {
        std::array<cv::Vec3f, 9> mixed{polar_lab(92.0F, 12.0F, 80.0F),  polar_lab(92.0F, 12.0F, 200.0F),
                                       polar_lab(45.0F, 45.0F, 32.0F),  polar_lab(62.0F, 48.0F, 55.0F),
                                       polar_lab(85.0F, 55.0F, 95.0F),  polar_lab(50.0F, 40.0F, 140.0F),
                                       polar_lab(40.0F, 24.0F, 270.0F), polar_lab(92.0F, 12.0F, 300.0F),
                                       polar_lab(50.0F, 40.0F, 140.0F)};
        model.observe(mixed);
    }

    // The whites are white regardless of which way their near-zero a/b
    // noise happens to point - the property a hue-only classifier cannot
    // have.
    for (int hue = 0; hue < 360; hue += 30) {
        REQUIRE(model.classify_face(uniform_face(polar_lab(92.0F, 12.0F, static_cast<float>(hue))))
                    .facelets[0]
                    .color == FacetColor::white);
    }

    // And the ceiling did not climb over the washed blue.
    REQUIRE(model.white_chroma_ceiling() < 24.0F);
    REQUIRE(model.classify_face(uniform_face(polar_lab(40.0F, 24.0F, 270.0F))).facelets[0].color ==
            FacetColor::blue);
}

// ---------------------------------------------------------------------------
// The mechanism the measurement rests on
// ---------------------------------------------------------------------------

// Within-face grouping is what makes a face read the SAME way twice: the
// nine stickers are named as at most a handful of group means, so
// independent per-sticker noise has to move a whole group across a
// boundary before the emitted string changes. Two noisy readings of one
// physical face must therefore produce one string.
TEST_CASE("SessionColorModel makes noisy readings of one face agree", "[vision][session-color]") {
    SessionColorModel model;
    const cv::Vec3f red = polar_lab(45.0F, 45.0F, 32.0F);
    const cv::Vec3f orange = polar_lab(62.0F, 48.0F, 58.0F);
    const cv::Vec3f green = polar_lab(50.0F, 40.0F, 140.0F);

    const std::array<cv::Vec3f, 9> truth{red, red, orange, green, red, orange, green, green, orange};
    for (int round = 0; round < 4; ++round) {
        model.observe(truth);
    }

    // Two readings whose per-sticker hues jitter by +-6 degrees, enough to
    // carry individual red tiles across the fixed 44-degree edge that
    // classify_lab would consult.
    const auto jitter = [&](float sign) {
        std::array<cv::Vec3f, 9> noisy{};
        for (std::size_t i = 0; i < 9; ++i) {
            const cv::Vec3f& base = truth[i];
            const float hue = std::atan2(base[2], base[1]) * 180.0F / std::numbers::pi_v<float>;
            const float chroma = std::hypot(base[1], base[2]);
            const float wobble = sign * (6.0F - 1.5F * static_cast<float>(i % 3));
            noisy[i] = polar_lab(base[0] + sign * 3.0F, chroma, hue + wobble);
        }
        return noisy;
    };

    REQUIRE(pattern(model.classify_face(jitter(+1.0F))) == pattern(model.classify_face(jitter(-1.0F))));
    REQUIRE(pattern(model.classify_face(jitter(+1.0F))) == pattern(model.classify_face(truth)));
}
