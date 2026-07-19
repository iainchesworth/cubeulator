#include <cube/vision/color_resolution.hpp>

#include <cube/vision/color_classification.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <cstddef>
#include <expected>
#include <limits>
#include <numeric>

namespace cube::vision {

namespace {

using cube::model::center_facelet_index;
using cube::model::face_count;
using cube::model::Facelet;
using cube::model::FacetColor;
using cube::model::facelets_per_face;

constexpr std::size_t kSampleCount = face_count * facelets_per_face;

[[nodiscard]] float lab_distance(const cv::Vec3f& a, const cv::Vec3f& b) {
    const cv::Vec3f diff = a - b;
    return std::sqrt(diff.dot(diff));
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
                const float distance = lab_distance(samples[s], means[c]);
                if (distance < nearest) {
                    second = nearest;
                    nearest = distance;
                    nearest_cluster = static_cast<int>(c);
                } else if (distance < second) {
                    second = distance;
                }
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
                const float current =
                    lab_distance(samples[a], means[ca]) + lab_distance(samples[b], means[cb]);
                const float swapped =
                    lab_distance(samples[a], means[cb]) + lab_distance(samples[b], means[ca]);
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
    // chroma floor separates true white (measured whites came in under 9);
    // hue bands (degrees, atan2(b*, a*)) name the rest, with band edges
    // placed midway between the hues measured on real washed tiles: red
    // ~25, orange ~49, yellow ~97, green ~135 saturated / ~226 washed-teal,
    // blue ~281.
    const float chroma = std::hypot(lab[1], lab[2]);
    constexpr float kWhiteChromaCeiling = 15.0F;
    if (chroma < kWhiteChromaCeiling) {
        const float confidence = std::clamp(1.0F - chroma / (2.0F * kWhiteChromaCeiling), 0.0F, 1.0F);
        return Facelet{.color = FacetColor::white, .confidence = confidence};
    }

    float hue = std::atan2(lab[2], lab[1]) * 180.0F / std::numbers::pi_v<float>;
    if (hue < 0.0F) {
        hue += 360.0F;
    }

    // Red/orange boundary at 44: midway between the closest observed pair
    // (saturated red reference ~39.9, washed orange tile ~48.8) - the two
    // hues everything else stays far from.
    FacetColor color = FacetColor::red;  // [340, 44)
    if (hue >= 44.0F && hue < 75.0F) {
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
    std::array<cv::Vec3f, kSampleCount> samples{};
    for (std::size_t face = 0; face < face_count; ++face) {
        for (std::size_t i = 0; i < facelets_per_face; ++i) {
            samples[face * facelets_per_face + i] = faces[face].lab[i];
        }
    }

    // The six centres seed the clusters, so they must be pairwise distinct.
    // 12 Lab units sits comfortably below the closest legitimate centre pair
    // (red/orange at ~27 even under aggressive chroma compression) and
    // comfortably above the residual spread of a median-fused centre
    // reading; two centres inside it means the same colour was scanned
    // twice, which no real cube can produce.
    constexpr float kMinSeedSeparation = 12.0F;
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
        for (std::size_t c = 0; c < face_count; ++c) {
            const auto color = classify_lab(means[c]).color;
            hue_names[c] = static_cast<std::size_t>(color);
            ++name_counts[static_cast<std::size_t>(color)];
        }
        const bool all_distinct =
            std::all_of(name_counts.begin(), name_counts.end(), [](int count) { return count <= 1; });
        if (all_distinct) {
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
        const float own_distance = lab_distance(samples[s], means[own]);
        float rival_distance = std::numeric_limits<float>::max();
        for (std::size_t c = 0; c < face_count; ++c) {
            if (c != own) {
                rival_distance = std::min(rival_distance, lab_distance(samples[s], means[c]));
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
        resolved[s / facelets_per_face][s % facelets_per_face] =
            Facelet{.color = palette.entries[best_permutation[own]].color, .confidence = confidence};
    }
    return resolved;
}

}  // namespace cube::vision
