#include <cube/vision/session_color_model.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <numbers>

namespace cube::vision {

namespace {

using cube::model::Facelet;
using cube::model::FacetColor;

constexpr std::size_t kCells = 9;
constexpr std::size_t kWhiteIndex = 0;

// Dense index order. White first because it is the one colour decided on
// chroma rather than hue, and keeping it at a known index lets the cost
// function branch once instead of comparing enum values in the inner loop.
constexpr std::array<FacetColor, kSessionColorCount> kOrder{
    FacetColor::white, FacetColor::red,   FacetColor::orange,
    FacetColor::yellow, FacetColor::green, FacetColor::blue,
};

// classify_lab()'s hue bands, verbatim, as the BOOTSTRAP PRIOR for a colour
// no evidence has seeded yet. Reproduced as data rather than re-derived, so
// that the first face of a session is named exactly as production's
// absolute classifier names it - the property the bootstrap test pins.
// Red wraps through 0, expressed here as [340, 404).
struct HueBand {
    float lo;
    float hi;
};
constexpr std::array<HueBand, kSessionColorCount> kBootstrapBand{
    HueBand{0.0F, 0.0F},        // white: never consulted, decided on chroma
    HueBand{340.0F, 404.0F},    // red    [340, 360) u [0, 44)
    HueBand{44.0F, 75.0F},      // orange
    HueBand{75.0F, 120.0F},     // yellow
    HueBand{120.0F, 250.0F},    // green
    HueBand{250.0F, 340.0F},    // blue
};

[[nodiscard]] float hue_degrees(const cv::Vec3f& lab) {
    float hue = std::atan2(lab[2], lab[1]) * 180.0F / std::numbers::pi_v<float>;
    if (hue < 0.0F) {
        hue += 360.0F;
    }
    return hue;
}

[[nodiscard]] float chroma_of(const cv::Vec3f& lab) { return std::hypot(lab[1], lab[2]); }

// Shortest angular separation of two hues, in [0, 180].
[[nodiscard]] float hue_delta(float a, float b) {
    float diff = std::fmod(std::fabs(a - b), 360.0F);
    return diff > 180.0F ? 360.0F - diff : diff;
}

[[nodiscard]] float lab_distance(const cv::Vec3f& a, const cv::Vec3f& b) {
    const cv::Vec3f diff = a - b;
    return std::sqrt(diff.dot(diff));
}

// Degrees by which `hue` falls OUTSIDE band, 0 when inside. Handles the
// wrapping red band by testing the hue at both +0 and +360.
[[nodiscard]] float band_penalty(float hue, const HueBand& band) {
    // The bands are half-open [lo, hi), and the epsilon is what makes them
    // so. A hue sitting EXACTLY on an edge is inside one band (cost 0) and
    // zero degrees outside its neighbour, so without it every band edge is
    // a tie broken by array order - which named hue 44 red, where
    // classify_lab names it orange, and hue 340 blue, where classify_lab
    // names it red. A tie-break by index cannot fix both, because red is
    // the first chromatic entry and also the band above 340. Charging any
    // strictly positive amount for being at-or-past `hi` reproduces the
    // half-open comparison chain exactly, at every edge.
    constexpr float kEdgeEpsilon = 1e-3F;
    const auto outside = [&](float h) {
        if (h < band.lo) {
            return band.lo - h;
        }
        if (h >= band.hi) {
            return h - band.hi + kEdgeEpsilon;
        }
        return 0.0F;
    };
    return std::min(outside(hue), outside(hue + 360.0F));
}

}  // namespace

FacetColor session_color_at(std::size_t index) { return kOrder[index]; }

std::size_t session_color_index(FacetColor color) {
    for (std::size_t i = 0; i < kSessionColorCount; ++i) {
        if (kOrder[i] == color) {
            return i;
        }
    }
    return kSessionColorCount;
}

namespace {

// Cost of naming `sample` as dense colour `index`, in degrees-equivalent.
//
// White and the five hues are deliberately priced on DIFFERENT axes but in
// ONE unit, so a single argmin can decide between them: white costs the
// chroma by which the sample exceeds the ceiling, a hue costs its angular
// error plus the chroma by which the sample falls SHORT of the ceiling.
// The second term is what stops an achromatic sample - whose hue angle is
// pure noise, since a and b are both near zero - from being handed to
// whichever band that noise points at.
[[nodiscard]] float name_cost(const cv::Vec3f& sample,
                              const std::array<SessionColorReference, kSessionColorCount>& references,
                              float white_ceiling, std::size_t index) {
    const float chroma = chroma_of(sample);
    if (index == kWhiteIndex) {
        return std::max(0.0F, chroma - white_ceiling) * kChromaCostPerUnitDeg;
    }
    const float achromatic_penalty = std::max(0.0F, white_ceiling - chroma) * kChromaCostPerUnitDeg;
    const auto& reference = references[index];
    if (reference.seeded) {
        return hue_delta(hue_degrees(sample), hue_degrees(reference.lab)) + achromatic_penalty;
    }
    return band_penalty(hue_degrees(sample), kBootstrapBand[index]) + achromatic_penalty;
}

// Complete-link agglomerative grouping of the nine samples. Complete-link
// (merge on the MAXIMUM pairwise distance) rather than single-link because
// single-link chains: three samples at 15 Lab spacing would merge into one
// group spanning 30, wide enough to swallow a genuine red/orange pair.
struct Grouping {
    std::array<int, kCells> label{};
    int count = 0;
    std::array<cv::Vec3f, kCells> mean{};
    std::array<int, kCells> size{};
};

[[nodiscard]] Grouping group_face(const std::array<cv::Vec3f, kCells>& lab) {
    std::array<int, kCells> owner{};
    for (std::size_t i = 0; i < kCells; ++i) {
        owner[i] = static_cast<int>(i);
    }

    // Repeatedly merge the closest admissible pair of live groups. Nine
    // samples means at most eight merges over a 9x9 scan - bounded and
    // trivially cheap, and being exhaustive avoids any ordering artefact.
    for (;;) {
        float best = kIntraFaceMergeLab;
        int best_a = -1;
        int best_b = -1;
        for (int a = 0; a < static_cast<int>(kCells); ++a) {
            if (owner[static_cast<std::size_t>(a)] != a) {
                continue;
            }
            for (int b = a + 1; b < static_cast<int>(kCells); ++b) {
                if (owner[static_cast<std::size_t>(b)] != b) {
                    continue;
                }
                // Complete linkage: the pair's cost is the widest distance
                // between any member of a and any member of b.
                float widest = 0.0F;
                for (std::size_t i = 0; i < kCells; ++i) {
                    if (owner[i] != a) {
                        continue;
                    }
                    for (std::size_t j = 0; j < kCells; ++j) {
                        if (owner[j] != b) {
                            continue;
                        }
                        widest = std::max(widest, lab_distance(lab[i], lab[j]));
                    }
                }
                if (widest < best) {
                    best = widest;
                    best_a = a;
                    best_b = b;
                }
            }
        }
        if (best_a < 0) {
            break;
        }
        for (auto& o : owner) {
            if (o == best_b) {
                o = best_a;
            }
        }
    }

    Grouping grouping;
    grouping.label.fill(-1);
    std::array<int, kCells> dense{};
    dense.fill(-1);
    for (std::size_t i = 0; i < kCells; ++i) {
        const auto root = static_cast<std::size_t>(owner[i]);
        if (dense[root] < 0) {
            dense[root] = grouping.count++;
        }
        grouping.label[i] = dense[root];
    }
    for (std::size_t i = 0; i < kCells; ++i) {
        const auto g = static_cast<std::size_t>(grouping.label[i]);
        grouping.mean[g] += lab[i];
        ++grouping.size[g];
    }
    for (int g = 0; g < grouping.count; ++g) {
        const auto index = static_cast<std::size_t>(g);
        grouping.mean[index] *= 1.0F / static_cast<float>(grouping.size[index]);
    }
    return grouping;
}

// Best and runner-up name for a group mean, with the margin between them.
struct NamePick {
    std::size_t best = 0;
    float best_cost = 0.0F;
    float margin = 0.0F;
};

[[nodiscard]] NamePick pick_name(const cv::Vec3f& mean,
                                 const std::array<SessionColorReference, kSessionColorCount>& references,
                                 float white_ceiling) {
    NamePick pick;
    float best = std::numeric_limits<float>::max();
    float second = std::numeric_limits<float>::max();
    for (std::size_t c = 0; c < kSessionColorCount; ++c) {
        const float cost = name_cost(mean, references, white_ceiling, c);
        if (cost < best) {
            second = best;
            best = cost;
            pick.best = c;
        } else if (cost < second) {
            second = cost;
        }
    }
    pick.best_cost = best;
    pick.margin = second - best;
    return pick;
}

// Names are picked per GROUP and independently - deliberately NOT under a
// one-to-one constraint across the face. A thresholded grouping can and
// does split one physical colour into two groups (a shadowed corner tile
// and a lit centre tile of the same colour can sit >18 Lab apart), and
// forcing distinct names would convert that harmless split into a
// GUARANTEED misname of one half. The joint information this design
// exploits is the grouping itself, which is where the noise reduction
// lives: naming a group mean of k stickers instead of k stickers
// individually cuts the sample noise by sqrt(k) before any threshold is
// consulted, and makes all k agree by construction.

}  // namespace

SessionFaceReading SessionColorModel::classify_face(const std::array<cv::Vec3f, kCells>& lab) const {
    const Grouping grouping = group_face(lab);

    SessionFaceReading reading;
    reading.group = grouping.label;
    reading.group_count = grouping.count;

    std::array<NamePick, kCells> picks{};
    for (int g = 0; g < grouping.count; ++g) {
        const auto index = static_cast<std::size_t>(g);
        picks[index] = pick_name(grouping.mean[index], references_, white_chroma_ceiling_);
    }

    for (std::size_t i = 0; i < kCells; ++i) {
        const auto g = static_cast<std::size_t>(grouping.label[i]);
        // Confidence is the group's naming MARGIN, normalised against a
        // full band width, not the sample's chroma: what a downstream
        // consumer needs to know is how nearly this sticker was named
        // something else, and after grouping that question is answered per
        // group rather than per sticker.
        const float confidence = std::clamp(picks[g].margin / 30.0F, 0.05F, 1.0F);
        reading.facelets[i] = Facelet{.color = kOrder[picks[g].best], .confidence = confidence};
    }
    return reading;
}

bool SessionColorModel::observe(const std::array<cv::Vec3f, kCells>& lab) {
    const Grouping grouping = group_face(lab);

    // Propose into a COPY. Nothing is visible to a concurrent classify_face
    // reader, and more importantly the degeneracy check below gets to see
    // the whole batch's effect at once - checking updates one at a time
    // would pass a pair of updates that individually respect the floor but
    // together cross it.
    auto proposed = references_;
    float proposed_ceiling = white_chroma_ceiling_;
    bool any = false;

    for (int g = 0; g < grouping.count; ++g) {
        const auto index = static_cast<std::size_t>(g);
        const NamePick pick = pick_name(grouping.mean[index], references_, white_chroma_ceiling_);
        if (pick.margin < kUpdateMarginDeg) {
            // Ambiguous evidence never teaches. This is the first and
            // cheapest degeneracy guard: a group that could plausibly be
            // red OR orange is exactly the group that would drag the two
            // references together.
            continue;
        }
        auto& reference = proposed[pick.best];
        if (!reference.seeded) {
            reference.lab = grouping.mean[index];
            reference.weight = 1.0F;
            reference.seeded = true;
        } else {
            const float alpha = std::max(kMinReferenceAlpha, 1.0F / (reference.weight + 1.0F));
            reference.lab = reference.lab * (1.0F - alpha) + grouping.mean[index] * alpha;
            reference.weight += 1.0F;
        }
        any = true;

        if (pick.best == kWhiteIndex) {
            // The white ceiling tracks the session's observed whites with
            // a margin, so a session whose whites read at chroma 14 does
            // not lose them to the hue bands. It is then clamped, AND held
            // below the least chromatic seeded colour, so it can never
            // climb over a real colour - the same non-merging invariant
            // the hue references get, applied to the one boundary that is
            // not a hue.
            const float observed = chroma_of(grouping.mean[index]);
            proposed_ceiling = std::clamp(observed + 8.0F, kMinWhiteChromaCeiling, kMaxWhiteChromaCeiling);
        }
    }

    if (!any) {
        return false;
    }

    for (std::size_t c = 1; c < kSessionColorCount; ++c) {
        if (proposed[c].seeded) {
            proposed_ceiling = std::min(proposed_ceiling, 0.75F * chroma_of(proposed[c].lab));
        }
    }
    proposed_ceiling = std::clamp(proposed_ceiling, kMinWhiteChromaCeiling, kMaxWhiteChromaCeiling);

    // THE DEGENERACY GUARD. Two references merging is catastrophic and
    // irreversible: once red and orange name the same region of Lab, every
    // subsequent face reinforces the collapse, and no downstream consensus
    // can recover a distinction the classifier stopped making. The floors
    // are checked over the seeded CHROMATIC references only - white is not
    // in the hue competition, and its own separation is enforced by the
    // ceiling clamp above.
    for (std::size_t a = 1; a < kSessionColorCount; ++a) {
        if (!proposed[a].seeded) {
            continue;
        }
        for (std::size_t b = a + 1; b < kSessionColorCount; ++b) {
            if (!proposed[b].seeded) {
                continue;
            }
            const bool too_close_lab =
                lab_distance(proposed[a].lab, proposed[b].lab) < kMinReferenceSeparationLab;
            const bool too_close_hue = hue_delta(hue_degrees(proposed[a].lab), hue_degrees(proposed[b].lab)) <
                                       kMinReferenceHueSeparationDeg;
            if (too_close_lab || too_close_hue) {
                // Reject the WHOLE batch, not the offending update: the
                // batch is one face's worth of evidence, and a face that
                // would collapse two references is a face that was
                // misgrouped or mis-illuminated, so none of its groups
                // deserve to teach.
                return false;
            }
        }
    }

    references_ = proposed;
    white_chroma_ceiling_ = proposed_ceiling;
    return true;
}

SessionFaceReading SessionColorModel::classify_and_observe(const std::array<cv::Vec3f, kCells>& lab) {
    const SessionFaceReading reading = classify_face(lab);
    observe(lab);
    return reading;
}

void SessionColorModel::reset() {
    references_ = {};
    white_chroma_ceiling_ = kBootstrapWhiteChromaCeiling;
}

bool SessionColorModel::seeded(FacetColor color) const {
    const std::size_t index = session_color_index(color);
    return index < kSessionColorCount && references_[index].seeded;
}

}  // namespace cube::vision
