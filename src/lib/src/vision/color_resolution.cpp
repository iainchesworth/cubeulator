#include <cube/vision/color_resolution.hpp>

#include <cube/profiling.hpp>
#include <cube/vision/color_classification.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <expected>
#include <limits>
#include <numbers>
#include <numeric>

namespace cube::vision {

namespace {

using cube::model::center_facelet_index;
using cube::model::face_count;
using cube::model::Facelet;
using cube::model::facelets_per_face;
using cube::model::FacetColor;

constexpr std::size_t kSampleCount = face_count * facelets_per_face;

// Plain Euclidean Lab. Still the right metric where it remains in use:
// the seed-separation guard (kMinCentreSeedSeparationLab and the
// kClosestCentrePairLab ceiling above it were MEASURED in this metric, so
// changing the guard's metric would silently re-derive both constants)
// and the cluster-naming permutation (which has the hue-first override
// for exactly the distortion that defeats it). Per-sample cluster
// ASSIGNMENT must not use it - see assignment_distance below.
[[nodiscard]] float lab_distance(const cv::Vec3f& a, const cv::Vec3f& b) {
    const cv::Vec3f diff = a - b;
    return std::sqrt(diff.dot(diff));
}

// Down-weight on the two shading-carried Lab axes (lightness and chroma)
// in the per-sample assignment metric; the hue residual keeps full
// weight. MEASURED on the freerot clip's winning fused sub-epoch, where
// plain Euclidean assignment produced 6 red/orange misnames: replaying
// that scan through this clustering gives ZERO misnames for every weight
// in [0.05, 0.5], so 0.2 sits mid-plateau, away from both cliffs - 1.0
// is the defect, and 0.0 (pure hue) collapses white, because a
// near-neutral sample carries no hue evidence at all and L and C are the
// only axes that separate white from everything washed (measured: 12
// misnames at 0.0, all white/near-neutral cells). That white constraint
// is also why this is ONE weight on both axes rather than a lower one on
// chroma: the same shading argument applies to both, and chroma is half
// of what keeps white apart.
constexpr float kShadingAxisWeight = 0.2F;

// Assignment distance between a sample and a cluster mean: the Lab
// difference split into lightness / chroma / hue components (dC along
// the mean chroma direction, dH the residual - CIE94's decomposition
// without its scaling knees), with the shading-carried axes L and C
// down-weighted and hue at full weight. At weight 1.0 this IS
// lab_distance.
//
// WHY HUE-AWARE AND NOT MERELY L-WEIGHTED. Measured on the freerot fused
// sub-epoch: shading acts as a near-scalar on the whole Lab vector - the
// R face's shade-darkened orange cell (33.3, 27.0, 38.8) is its own
// orange seed (60.1, 45.9, 63.6) scaled by ~0.58 in all three channels.
// It therefore sits nearer the RED seed under plain Lab (22.8 against
// 41.2) and STILL nearer in pure (a,b) (19.0 against 31.2): chroma
// shrinks with the shade, so wL*dL^2 + da^2 + db^2 cannot fix the
// assignment at ANY wL (measured: 6 misnames at wL 0.5, 0.2, 0.1 and
// 0.0 alike). What shading does not move is the hue direction - 55.2
// degrees on that cell against 54.2 on its seed, with red at 31.4 - so
// the hue residual is the axis that must keep full weight.
//
// WHITE/YELLOW REGRESSION RISK, measured both ways. On freerot the
// white/yellow assignment margin is 10.6 under this metric against 23.7
// plain - reduced, never close. On the closest white/yellow material the
// real 11-recording corpus produced (runD's warm-cast whites against
// live1's washed yellows, seeds 14.9 Lab apart), the worst white
// sample's margin is -2.0 under plain Lab - nearer the yellow mean,
// rescued only by the exactly-9 constraint - and +3.5 here: the L
// dominance that carried the shading was also drowning the hue evidence
// that actually separates that pair. No second regime needed: the
// down-weighted L and C terms stay decisive precisely when hue carries
// no information (near-neutral samples), which is the only case that
// needs them.
[[nodiscard]] float assignment_distance(const cv::Vec3f& sample, const cv::Vec3f& mean) {
    const float dl = sample[0] - mean[0];
    const float da = sample[1] - mean[1];
    const float db = sample[2] - mean[2];
    const float chroma_sample = std::hypot(sample[1], sample[2]);
    const float chroma_mean = std::hypot(mean[1], mean[2]);
    const float dc = chroma_sample - chroma_mean;
    // |dc| <= sqrt(da^2+db^2) by the reverse triangle inequality, so the
    // hue residual is non-negative up to rounding; the clamp guards the
    // rounding.
    const float dh_squared = std::max(0.0F, da * da + db * db - dc * dc);
    return std::sqrt(kShadingAxisWeight * (dl * dl + dc * dc) + dh_squared);
}

// Exactly-9-per-cluster assignment of the 54 samples to the 6 cluster
// means, with each face's centre pinned to its own seed cluster (the seeds
// *are* the centres, so re-assigning one would let a noisy off-centre
// sticker hijack a cluster's identity). Greedy by margin - the sample with
// the most to lose by missing its nearest open cluster commits first - then
// pairwise-swap descent to a local optimum. 54x6 is small enough that being
// exhaustive at every step is cheaper than being clever.
[[nodiscard]] std::array<int, kSampleCount> assign_clusters(
    const std::array<cv::Vec3f, kSampleCount>& samples,
    const std::array<cv::Vec3f, face_count>& means) {
    constexpr float kInf = std::numeric_limits<float>::infinity();

    std::array<int, kSampleCount> cluster{};
    cluster.fill(-1);
    std::array<std::size_t, face_count> load{};

    for (std::size_t face = 0; face < face_count; ++face) {
        cluster[face * facelets_per_face + center_facelet_index] = static_cast<int>(face);
        ++load[face];
    }

    // Margins shift as clusters fill, so re-scan all unassigned samples for
    // the current best margin on every pass rather than sorting once.
    for (;;) {
        std::size_t best_sample = kSampleCount;
        int best_cluster = -1;
        float best_margin = -1.0F;
        for (std::size_t s = 0; s < kSampleCount; ++s) {
            if (cluster[s] >= 0) {
                continue;
            }
            float nearest = kInf;
            float second = kInf;
            int nearest_cluster = -1;
            for (std::size_t c = 0; c < face_count; ++c) {
                if (load[c] >= facelets_per_face) {
                    continue;
                }
                const float distance = assignment_distance(samples[s], means[c]);
                if (distance < nearest) {
                    second = nearest;
                    nearest = distance;
                    nearest_cluster = static_cast<int>(c);
                } else if (distance < second) {
                    second = distance;
                }
            }
            // No open cluster produced a finite comparison. resolve_colors
            // rejects non-finite input before calling this, so this is
            // defence in depth rather than a live path - but the failure it
            // guards is severe enough to be structural: with
            // nearest_cluster still -1 the sample would win the margin
            // argmax below with kInf, and the loop would then execute
            // `++load[static_cast<std::size_t>(-1)]` (an out-of-bounds
            // increment at index SIZE_MAX of a six-element array) and
            // re-select the same sample forever, because `cluster[s]` was
            // never set. Skipping it here means the loop can only ever
            // commit a real cluster, and terminates because every
            // iteration assigns exactly one sample.
            if (nearest_cluster < 0) {
                continue;
            }
            // With one open cluster left the margin is effectively infinite:
            // the sample has no alternative, so it commits immediately.
            const float margin = (second == kInf) ? kInf : second - nearest;
            if (margin > best_margin) {
                best_margin = margin;
                best_sample = s;
                best_cluster = nearest_cluster;
            }
        }
        if (best_sample == kSampleCount) {
            break;
        }
        cluster[best_sample] = best_cluster;
        ++load[static_cast<std::size_t>(best_cluster)];
    }

    // Pairwise swaps preserve the exactly-9 loads by construction; each swap
    // strictly lowers the total cost by at least epsilon, so the descent
    // terminates.
    constexpr float kMinImprovement = 1e-3F;
    bool improved = true;
    while (improved) {
        improved = false;
        for (std::size_t a = 0; a < kSampleCount; ++a) {
            if (a % facelets_per_face == center_facelet_index) {
                continue;
            }
            for (std::size_t b = a + 1; b < kSampleCount; ++b) {
                if (b % facelets_per_face == center_facelet_index || cluster[a] == cluster[b]) {
                    continue;
                }
                const auto ca = static_cast<std::size_t>(cluster[a]);
                const auto cb = static_cast<std::size_t>(cluster[b]);
                const float current = assignment_distance(samples[a], means[ca]) +
                                      assignment_distance(samples[b], means[cb]);
                const float swapped = assignment_distance(samples[a], means[cb]) +
                                      assignment_distance(samples[b], means[ca]);
                if (swapped + kMinImprovement < current) {
                    std::swap(cluster[a], cluster[b]);
                    improved = true;
                }
            }
        }
    }

    return cluster;
}

}  // namespace

Facelet classify_lab(const cv::Vec3f& lab) {
    // Hue-angle classification, NOT nearest-reference-distance: webcam
    // exposure washes strongly-coloured tiles toward pastel, which loses
    // CHROMA (the a/b magnitude) while preserving HUE (the a/b direction).
    // Under a fixed saturated palette every washed colour then sits nearer
    // the white reference than its own - observed live, where a whole
    // scanned face of salmon-red Lab(64,35,16) / washed-orange (75,21,24) /
    // teal-green (62,-21,-22) samples classified uniformly as white. A
    // chroma floor separates true white; hue bands (degrees, atan2(b*, a*))
    // name the rest, with band edges placed midway between the hues
    // measured on real washed tiles: red ~25, orange ~49, yellow ~97, green
    // ~135 saturated / ~226 washed-teal, blue ~281.
    const float chroma = std::hypot(lab[1], lab[2]);

    float hue = std::atan2(lab[2], lab[1]) * 180.0F / std::numbers::pi_v<float>;
    if (hue < 0.0F) {
        hue += 360.0F;
    }

    // Red/orange boundary at 44: midway between the closest observed pair
    // (saturated red reference ~39.9, washed orange tile ~48.8) - the two
    // hues everything else stays far from.
    constexpr float kOrangeHueMinDeg = 44.0F;
    constexpr float kOrangeHueMaxDeg = 75.0F;

    // WHITE, decided on chroma AND lightness AND hue, not chroma alone.
    //
    // The floor was originally the whole test, at 15, because the whites of
    // the cube it was tuned on came in under chroma 9. On the cube and
    // camera the corpus was recorded with, the white face's own nine
    // stickers measure chroma 15.7 15.8 17.6 17.8 18.7 18.8 19.3 20.2 21.0
    // (runD) - every one of them just ABOVE that floor - so all nine fell
    // through to the hue path, landed in the orange band, and runD's
    // captured cube censused O=21 W=2 where both must be 9. The camera's
    // white balance is warm here: a neutral surface does not read as
    // a*=b*=0, it reads as a small POSITIVE a*/b* pair, i.e. residual
    // chroma pointing into the orange band.
    //
    // Raising the floor alone makes things WORSE, because chroma does not
    // separate white from the rest on this cube:
    //   green tiles       chroma 20.5 - 30.7  (runA R2 20.5, runD B1 25.8)
    //   blue  tiles       chroma 17.6 - 24.3
    //   the white face    chroma 15.7 - 21.0
    // The three ranges overlap outright. Two further measurements say what
    // does separate them:
    //   1. LIGHTNESS. Every white sample in the corpus sits at L 66.6-87.2
    //      (runA U8 is the darkest); the greens and blues sharing its
    //      chroma range sit at L 18.6-48.8.
    //   2. HUE. The whites all sit at hue 57.1-66.2 - inside the orange
    //      band, which is exactly why orange is the colour that steals
    //      them. Everything else in that chroma range points elsewhere.
    // Lightness alone is not sufficient and cannot be made sufficient:
    // runA F2 is a genuine BLUE tile at L 67.6, chroma 18.8, which is
    // BRIGHTER and LESS chromatic than the palest true white in the corpus
    // (runA U8, L 66.6, chroma 20.1). No rule monotone in (chroma,
    // lightness) can admit U8 and refuse F2 - so the hue condition is not a
    // convenience, it is the only thing that makes the pair separable.
    // Measured: dropping it steals runA F2 (blue), live1 L4 and live1 D0
    // (yellow, L 66.4-66.5 at chroma 16.7/19.3) and live2 F1/F3 (blue).
    //
    // So: a chroma floor (for a genuinely neutral sample, subject to the
    // direction gate documented with kNeutralWhiteMinA below), plus a soft
    // ceiling that only reopens the orange band, and only for a sample light
    // enough to be a white tile.
    //   soft ceiling 27  - midway across the measured 21.0 (palest white)
    //                      to 33.6 (runA F7, the least chromatic true tile
    //                      anywhere in the orange band) gap.
    //   lightness floor 60 - midway across the 54.2 (that same runA F7) to
    //                      66.6 (darkest white) gap.
    // Both are set from the two-sided gaps this corpus actually shows, not
    // from one side of one of them, which is how 15 came to be wrong here.
    constexpr float kWhiteChromaFloor = 15.0F;
    constexpr float kWhiteChromaSoftCeiling = 27.0F;
    static_assert(kWhiteChromaFloor < kWhiteChromaSoftCeiling,
                  "the soft ceiling only ever widens the floor; inverting them would make the "
                  "orange band's whites HARDER to reach than a neutral sample's");

    // DIRECTION, the third thing the floor branch has to consult.
    //
    // The floor is an ABSOLUTE chroma threshold, so it is only ever right
    // for a cube-and-lighting combination whose colours are at least that
    // chromatic. live5 is backlit by a window and its greens are crushed:
    // that clip's own GREEN CENTRE measures chroma 19.7, and its face F
    // cell 5 - a genuine green sticker - measures L 61.4, a* -11.46,
    // b* -1.16, chroma 11.5, hue 185.8 (dead centre of the 120-250 green
    // band). At chroma 11.5 it is short-circuited to white before hue is
    // ever consulted. Its Lab distance to live5's own green centre is 22.2
    // against 27.5 to live5's own white centre, so the palette agrees with
    // the hue: green is nearer, and white is the wrong answer.
    //
    // WHY NOT A SESSION-RELATIVE FLOOR. Both obvious forms were measured
    // across all eleven recordings and both are worse:
    //   - Midway between the session's white reference and its least
    //     chromatic other reference. On live5 that is (7.3 + 19.7)/2 = 13.5,
    //     which leaves F[5] at 11.5 on the WHITE side - it does not fix the
    //     defect at all. On runB/C/D the same rule lands at 23.3-24.2 and
    //     swallows twelve genuine BLUE stickers (chroma 19.4-24.3), which,
    //     being dark, then come out UNKNOWN under the lightness floor and
    //     block their faces from committing.
    //   - Anchored near the white reference (white chroma + 3). That does
    //     fix live5, but on the warm-cast clips the same anchor climbs to
    //     22.1-22.3 - runD's own whites measure chroma 15.7-21.0 - and it
    //     takes eight blues and one green on runA/B/C/D.
    // Availability kills it independently of tuning: only five of the
    // eleven clips ever capture a white centre at all, so on the other six
    // - live4 among them, which has a second green-stolen-as-white cell at
    // L[6] - a session floor has no reference to be relative TO.
    //
    // WHAT THE CORPUS DOES SEPARATE ON is the DIRECTION of the residual
    // chroma, not its magnitude. This camera's neutrals render WARM: that
    // is the whole premise of the soft ceiling above, and every one of the
    // 48 remaining cells the white branch names across the corpus carries
    // a* >= -1.15 (live4 L[0]), with 44 of them above +3.5. The two cells
    // it takes wrongly are the only two pointing hard the other way -
    // live5 F[5] at a* -11.46 and live4 L[6] at a* -7.36, whose nearest
    // session centre is live4's own green centre at 15.8 Lab against 39+
    // for every other centre it captured. A near-neutral sample aimed at
    // GREEN is not a white tile under a warm cast; it is a colour whose
    // chroma has been crushed, and the hue path already knows how to name
    // those.
    //
    // -4.0 is the midpoint of the two-sided gap the corpus shows: -1.15
    // (live4 L[0], the most green-ward sample the branch must KEEP - a
    // bright L 96.7 tile whose a* is at noise level) to -7.36 (live4 L[6],
    // the least green-ward sample it must REFUSE). Margin 2.85 on one side,
    // 3.36 on the other. Deliberately not 0.0: the sign of a near-zero a*
    // is the sign of a noise term, and keying on it flips live4 L[0] to
    // yellow on the strength of 1.15 units of nothing.
    constexpr float kNeutralWhiteMinA = -4.0F;
    static_assert(kNeutralWhiteMinA < 0.0F,
                  "the direction gate exists to refuse GREEN-ward neutrals; a non-negative bound "
                  "would refuse the warm neutrals the branch is for");

    // The lightness floor applies to BOTH white branches, not just the
    // warm-cast one below.
    //
    // Chroma alone cannot tell a white sticker from anything else that is
    // near-achromatic, and the frame is full of things that are: the black
    // inter-tile lattice, a shadowed fingertip, an unlit background wall.
    // All of them read a*=b*=0 and differ from a white tile ONLY in
    // lightness - which is the same argument the block above makes for the
    // warm-cast branch, and it is no weaker here. Measured on the corpus'
    // committed slots: 17 of the 73 cells this branch named white sat below
    // L=60, the darkest at L=1.2, and runD contributed two at L 9.1 and
    // 13.0 to a face that then censused W=11 where W must be 9.
    //
    // Below the floor the answer is UNKNOWN, not a hue-band guess. A sample
    // this neutral has a hue that is the arctangent of two noise terms, so
    // any band it lands in is arbitrary, and the hue path's confidence is
    // clamped to a 0.2 FLOOR - which would rank a background cell above a
    // genuine washed sticker. Unknown at confidence ZERO is what every
    // consumer needs:
    //   - color_census() has always had a slot for it (ColorCensus is
    //     7-wide, unknown included, per its own header) and it has been
    //     structurally zero until now;
    //   - the commit-time unknown-cell gate ("the stationary-finger
    //     defeat", scan_session.cpp) has been a constant-false branch for
    //     the same reason and starts working;
    //   - mean_confidence() feeds dropMostSuspectSlot as an ARGMIN, so a
    //     face made of background must score WORST. At confidence ~1.0 it
    //     scored best and a correctly-read face was dropped instead.
    const bool near_neutral = chroma < kWhiteChromaFloor;
    // The lightness reject is tested FIRST and unconditionally, ahead of the
    // direction gate, and the order is load-bearing rather than stylistic. A
    // dark near-neutral has a hue that is the arctangent of two noise terms
    // - which means it also has an a* SIGN that is the sign of a noise term.
    // Letting such a sample reach the direction gate would hand roughly half
    // the lattice gaps and shadowed background cells a green-ward a*, drop
    // them through to the hue bands, and hand them the 0.2 confidence FLOOR
    // the bands apply - dismantling the unknown reject and the commit-time
    // unknown-cell gate that depends on it. Below the lightness floor the
    // answer stays unknown at confidence zero no matter which way the sample
    // points.
    if (near_neutral && lab[0] <= kWhiteLightnessFloor) {
        return Facelet{.color = FacetColor::unknown, .confidence = 0.0F};
    }
    if (near_neutral && lab[1] > kNeutralWhiteMinA) {
        const float confidence = std::clamp(1.0F - chroma / (2.0F * kWhiteChromaFloor), 0.0F, 1.0F);
        return Facelet{.color = FacetColor::white, .confidence = confidence};
    }
    // A green-ward near-neutral falls THROUGH to the hue bands below. It
    // cannot be caught by the warm-cast branch on the way: that branch
    // requires hue in [44, 75), which requires a* > 0, and this sample's a*
    // is at most -4. Its confidence therefore comes from the hue path's
    // chroma/30 expression - low, as a crushed tile should be, but above the
    // 0.2 floor rather than at zero, because a hue this far from the warm
    // axis is a real measurement and not noise.
    const bool warm_cast_white = chroma < kWhiteChromaSoftCeiling &&
                                 lab[0] > kWhiteLightnessFloor && hue >= kOrangeHueMinDeg &&
                                 hue < kOrangeHueMaxDeg;
    if (warm_cast_white) {
        // Confidence from BOTH admitting conditions, weakest-link, because
        // either one failing would have changed the answer - the old
        // chroma-only expression is not just miscalibrated up here, it goes
        // negative for every sample this branch exists to catch.
        //
        // The chroma term is scaled to meet the floor branch's value at the
        // boundary (1 - 15/30 = 0.5) so a sample does not gain confidence
        // by becoming MORE chromatic as it crosses chroma 15. The lightness
        // term ramps from 0 at the floor to 1 at 80, spanning the measured
        // white range (66.6-87.2) so a barely-light-enough sample is
        // reported as the marginal call it is.
        constexpr float kFloorBranchBoundaryConfidence = 0.5F;
        constexpr float kWhiteLightnessConfidenceSpan = 20.0F;
        const float chroma_term = kFloorBranchBoundaryConfidence *
                                  (kWhiteChromaSoftCeiling - chroma) /
                                  (kWhiteChromaSoftCeiling - kWhiteChromaFloor);
        const float lightness_term =
            std::clamp((lab[0] - kWhiteLightnessFloor) / kWhiteLightnessConfidenceSpan, 0.0F, 1.0F);
        return Facelet{.color = FacetColor::white,
                       .confidence = std::clamp(std::min(chroma_term, lightness_term), 0.0F, 1.0F)};
    }

    FacetColor color = FacetColor::red;  // [340, 44)
    if (hue >= kOrangeHueMinDeg && hue < kOrangeHueMaxDeg) {
        color = FacetColor::orange;
    } else if (hue >= 75.0F && hue < 120.0F) {
        color = FacetColor::yellow;
    } else if (hue >= 120.0F && hue < 250.0F) {
        color = FacetColor::green;
    } else if (hue >= 250.0F && hue < 340.0F) {
        color = FacetColor::blue;
    }

    // More chroma = a more confidently-coloured tile; saturates at a level
    // even washed tiles comfortably reach so glare-dimmed cells rank lower
    // without ever zeroing out.
    const float confidence = std::clamp(chroma / 30.0F, 0.2F, 1.0F);
    return Facelet{.color = color, .confidence = confidence};
}

Result<std::array<std::array<Facelet, 9>, 6>> resolve_colors(
    const std::array<CapturedFace, 6>& faces) {
    CUBE_ZONE_SCOPED_N("vision.resolve_colors");
    std::array<cv::Vec3f, kSampleCount> samples{};
    for (std::size_t face = 0; face < face_count; ++face) {
        for (std::size_t i = 0; i < facelets_per_face; ++i) {
            samples[face * facelets_per_face + i] = faces[face].lab[i];
        }
    }

    // Finiteness first, before ANY comparison is made on this data. Every
    // subsequent gate in this function is an inequality, and an inequality
    // against NaN is false - so a single NaN does not merely produce a bad
    // answer, it silently passes the centre-separation guard below (whose
    // whole job is to reject implausible seeds) and then defeats the
    // clustering: see assign_clusters' note for the out-of-bounds write and
    // non-terminating loop that follow. inf reaches the same state because
    // `inf < inf` is false too.
    //
    // These 54 values are caller-supplied on a public entry point and the
    // producers upstream include divisions by sample counts and homography
    // projections, either of which can emit a non-finite mean without any
    // caller doing something unreasonable. Refusing the whole scan is the
    // right answer rather than dropping the offending cell: a cell whose
    // colour could not be measured is not evidence that can be clustered,
    // and the exactly-9-per-cluster constraint has no meaning without all
    // 54.
    for (const cv::Vec3f& sample : samples) {
        if (!std::isfinite(sample[0]) || !std::isfinite(sample[1]) || !std::isfinite(sample[2])) {
            return std::unexpected(CubeError::no_valid_cube_detected);
        }
    }

    // The six centres seed the clusters, so they must be pairwise distinct.
    // The floor and the closest-pair measurement backing it are now declared
    // in the header (kMinCentreSeedSeparationLab / kClosestCentrePairLab) so
    // capture routing's same-face radii can be derived from the same
    // numbers instead of restating an independent absolute - which is how
    // verify_centre_lab_radius came to sit ABOVE the red/orange pair and let
    // a different physical face verify into an occupied slot.
    constexpr float kMinSeedSeparation = kMinCentreSeedSeparationLab;
    std::array<cv::Vec3f, face_count> means{};
    for (std::size_t face = 0; face < face_count; ++face) {
        means[face] = faces[face].lab[center_facelet_index];
    }
    for (std::size_t a = 0; a < face_count; ++a) {
        for (std::size_t b = a + 1; b < face_count; ++b) {
            if (lab_distance(means[a], means[b]) < kMinSeedSeparation) {
                return std::unexpected(CubeError::no_valid_cube_detected);
            }
        }
    }

    // Lloyd-style refinement under the exactly-9 constraint. Each round's
    // assignment is a deterministic function of the means, so a repeated
    // assignment is a fixed point and iteration can stop; the cap guards
    // against a rare 2-cycle rather than being the expected exit.
    constexpr int kMaxIterations = 10;
    std::array<int, kSampleCount> assignment{};
    assignment.fill(-1);
    for (int iteration = 0; iteration < kMaxIterations; ++iteration) {
        const std::array<int, kSampleCount> next = assign_clusters(samples, means);

        std::array<cv::Vec3f, face_count> sums{};
        for (std::size_t s = 0; s < kSampleCount; ++s) {
            // assign_clusters assigns every sample given finite input, which
            // the sweep above has established. Checked rather than assumed
            // because the cost of being wrong here is an out-of-bounds write
            // at index SIZE_MAX, not a wrong mean.
            if (next[s] < 0) {
                continue;
            }
            sums[static_cast<std::size_t>(next[s])] += samples[s];
        }
        for (std::size_t c = 0; c < face_count; ++c) {
            means[c] = sums[c] * (1.0F / static_cast<float>(facelets_per_face));
        }

        const bool stable = next == assignment;
        assignment = next;
        if (stable) {
            break;
        }
    }

    // Naming is a one-to-one matching, not six independent nearest-lookups:
    // under a global cast the nearest reference to a shifted cluster can be
    // a *different* colour (red lifted ~25 L sits nearer the orange
    // reference than the red one), but the matching that must name every
    // cluster at once still prefers the consistent labelling because the
    // displaced colour would leave its own reference unpaired at a far
    // larger cost. 6! = 720 permutations - brute force is exact and trivial.
    const ColorPalette palette = default_palette();
    std::array<std::size_t, face_count> permutation{};
    std::iota(permutation.begin(), permutation.end(), std::size_t{0});
    std::array<std::size_t, face_count> best_permutation = permutation;
    float best_total = std::numeric_limits<float>::max();
    do {
        float total = 0.0F;
        for (std::size_t c = 0; c < face_count; ++c) {
            total += lab_distance(means[c], palette.entries[permutation[c]].lab);
        }
        if (total < best_total) {
            best_total = total;
            best_permutation = permutation;
        }
    } while (std::next_permutation(permutation.begin(), permutation.end()));

    // Hue-first override of the distance matching: washed cluster means
    // suffer the same distortion classify_lab() was rebuilt around (chroma
    // shrinks, hue survives), and distance matching can swap the red and
    // orange clusters under a strong wash even with the global 1-1
    // constraint. When hue classification names the six clusters
    // DISTINCTLY, trust it outright; on any collision (two washed clusters
    // in one hue band) keep the exact global-distance permutation instead.
    {
        std::array<std::size_t, face_count> hue_names{};
        std::array<int, face_count + 1> name_counts{};
        bool any_unknown = false;
        for (std::size_t c = 0; c < face_count; ++c) {
            const auto color = classify_lab(means[c]).color;
            any_unknown = any_unknown || color == FacetColor::unknown;
            hue_names[c] = static_cast<std::size_t>(color);
            ++name_counts[static_cast<std::size_t>(color)];
        }
        const bool all_distinct = std::all_of(name_counts.begin(), name_counts.end(),
                                              [](int count) { return count <= 1; });
        // An UNKNOWN cluster mean disqualifies the whole override, even
        // when the six names are formally distinct. The palette has no
        // `unknown` entry, so the lookup below would find no match for that
        // cluster and silently leave it on the distance permutation while
        // every other cluster moved to its hue name - which can land two
        // clusters on the SAME palette colour and break the exactly-9
        // census the assembler relies on. The exact global-distance
        // matching is a complete answer; fall back to it wholesale.
        if (all_distinct && !any_unknown) {
            for (std::size_t c = 0; c < face_count; ++c) {
                for (std::size_t p = 0; p < face_count; ++p) {
                    if (static_cast<std::size_t>(palette.entries[p].color) == hue_names[c]) {
                        best_permutation[c] = p;
                        break;
                    }
                }
            }
        }
    }

    std::array<std::array<Facelet, 9>, 6> resolved{};
    for (std::size_t s = 0; s < kSampleCount; ++s) {
        const auto own = static_cast<std::size_t>(assignment[s]);
        // Priced in the ASSIGNMENT metric, necessarily: the margin reports
        // how safely the sample sits in the cluster the assignment chose,
        // and pricing it in a different metric would zero out exactly the
        // cells the metric exists for - a freerot shade-darkened orange
        // cell sits 22.8 plain-Lab from the red mean against 41.2 from its
        // own orange mean, so plain-Lab pricing calls a correct,
        // hue-unambiguous assignment confidence zero.
        const float own_distance = assignment_distance(samples[s], means[own]);
        float rival_distance = std::numeric_limits<float>::max();
        for (std::size_t c = 0; c < face_count; ++c) {
            if (c != own) {
                rival_distance =
                    std::min(rival_distance, assignment_distance(samples[s], means[c]));
            }
        }
        // Relative margin between the sample's own cluster and the nearest
        // rival: 1 when the sample sits on its mean far from any rival, 0
        // when a rival is as close - or closer, which the exactly-9
        // constraint can legitimately force for a borderline sticker.
        const float confidence =
            rival_distance > 0.0F
                ? std::clamp((rival_distance - own_distance) / rival_distance, 0.0F, 1.0F)
                : 0.0F;
        resolved[s / facelets_per_face][s % facelets_per_face] = Facelet{
            .color = palette.entries[best_permutation[own]].color, .confidence = confidence};
    }
    return resolved;
}

}  // namespace cube::vision
