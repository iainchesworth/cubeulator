#pragma once

#include <cube/model/facelet.hpp>

#include <opencv2/core.hpp>

#include <array>
#include <cstddef>

namespace cube::vision {

// ---------------------------------------------------------------------------
// Session-relative sticker colour naming.
//
// WHY THIS EXISTS. classify_lab() names a sticker against ABSOLUTE hue-band
// edges with a fixed chroma floor - constants tuned once, against one cube,
// under one light (its own comments say as much: the red/orange edge at 44
// degrees is "midway between the closest observed pair"; the white ceiling
// is 15 because "measured whites came in under 9"). Webcam exposure, white
// balance and viewing angle move real tiles across those fixed edges, and
// the resulting per-sticker noise is not a rounding error - it is the
// blocker. Replaying the live corpus and classifying each capture-grade
// face reading independently with classify_lab, ONE physical face read 67
// times produced 57 DIFFERENT nine-sticker strings (live4 centre=1, modal
// reading only 6% of readings); across twelve face/clip pairs the median
// modal share was ~33%. No clip assembled into a physically valid cube, and
// consensus voting over such readings converged on a session-lighting
// artifact rather than on face identity.
//
// resolve_colors() already contains the robust idea - resolve all 54
// stickers RELATIVE to one another using the cube's own structure, so a
// global lighting shift moves every sample of a colour together and
// cancels. But it only runs once a scan COMPLETES, while every per-frame
// routing and verification gate uses the fragile absolute classifier. The
// scan therefore never completes and the good code never runs.
//
// SessionColorModel closes that gap: it applies the relative idea at the
// granularity production actually decides at - one face, one frame - by
// naming stickers against references that TRACK THE SESSION instead of
// against constants baked in at compile time.
//
// THE THREE MECHANISMS, in decreasing order of expected effect:
//
//  1. WITHIN-FACE GROUPING (joint, not per-sticker). The nine stickers of
//     one face are seen through one illumination in one frame, so two
//     stickers of the SAME physical colour sit far closer to each other
//     than to any other colour. Grouping them first and naming the GROUP
//     MEAN averages the per-sample noise down by sqrt(group size) before
//     any threshold is consulted. This is the same "let the cube's own
//     structure do the work" move resolve_colors makes over 54 samples,
//     scaled down to the 9 that a single frame actually delivers.
//
//  2. SESSION-ADAPTIVE REFERENCES. Each named colour carries a Lab
//     reference that is EMA-updated from confidently-named groups, so the
//     decision boundaries MOVE WITH THE SESSION. A session whose oranges
//     all land at hue 55 pulls the orange reference to 55; the red/orange
//     boundary follows, instead of staying pinned at 44 where half of that
//     session's oranges fall on the wrong side.
//
//  3. CHROMA-BASED WHITE. White is structurally not a hue - it is the
//     absence of one - so it is decided on chroma and kept out of the hue
//     competition entirely, exactly as classify_lab does. The ceiling is
//     adaptive but bounded (see kBootstrapWhiteChromaCeiling below).
//
// BOOTSTRAP. At session start there is no distribution, so the absolute
// classifier is the only available prior - and it is used as one, not
// discarded. An UNSEEDED colour is represented by classify_lab's own hue
// BAND: cost 0 anywhere inside the band, growing with the angular distance
// outside it. The argmin over six such band costs IS classify_lab's
// decision, so the very first face of a session is named exactly as the
// current production classifier names it. Evidence then supersedes the
// prior colour by colour: the first confidently-named group of a colour
// seeds that colour's reference, and from then on that colour is judged by
// angular distance to its own observed hue rather than by a fixed band.
// The transition is per-colour and one-way; there is no blend factor to
// tune, because the two regimes are the same argmin over the same cost
// units (degrees).
//
// DEGENERACY. Adaptive references can drift together, and two references
// merging is catastrophic - it is exactly the red/orange confusion that
// already defeats the router at only 27 Lab separation
// (kClosestCentrePairLab). Every update is therefore proposed as a BATCH
// and validated before it is committed: if any pair of seeded references
// would end up closer than kMinReferenceSeparationLab or
// kMinReferenceHueSeparationDeg, the whole batch is discarded and the
// previous references stand. Ambiguous groups - those whose best and
// second-best names are within kUpdateMarginDeg of each other - never
// contribute an update at all, so the model only learns from evidence it
// was already sure about.
//
// THREADING. classify_face() is const and allocation-free: the vision
// worker may call it on any thread. observe() mutates and is the only
// mutating entry point; scan state belongs to the GUI thread, so that is
// where observe() is called. Per-face cost is bounded: 36 pairwise
// distances for the grouping, then at most 6 groups x 6 names.
// ---------------------------------------------------------------------------

// The six nameable sticker colours, in a dense 0..5 index that this file
// uses internally and exposes for reference inspection. cube::model::
// FacetColor reserves 0 for `unknown`, which has no reference.
inline constexpr std::size_t kSessionColorCount = 6;

// Dense index <-> FacetColor. Order matches default_palette()'s intent
// (white first, then the five hues) but nothing depends on the order
// beyond these two helpers.
[[nodiscard]] cube::model::FacetColor session_color_at(std::size_t index);
[[nodiscard]] std::size_t session_color_index(cube::model::FacetColor color);

// Bootstrap white chroma ceiling, inherited from classify_lab, whose
// comment records that measured whites came in under 9. The adaptive
// ceiling is clamped to [kMinWhiteChromaCeiling, kMaxWhiteChromaCeiling]
// and additionally held below the least chromatic SEEDED colour reference,
// so a session that washes out cannot slide the ceiling up over a real
// colour and swallow it into white - the degeneracy guard applied to the
// one boundary that is not a hue.
inline constexpr float kBootstrapWhiteChromaCeiling = 15.0F;
inline constexpr float kMinWhiteChromaCeiling = 8.0F;
inline constexpr float kMaxWhiteChromaCeiling = 26.0F;

// Within-face grouping threshold (complete-link, Lab). Complete-link, not
// single-link: single-link chains, and a chain of intermediate samples
// would let a red sticker and an orange sticker land in one group despite
// being 27 Lab apart. 18 sits comfortably below kClosestCentrePairLab (27,
// the red/orange pair - the closest two colours a real cube can show) and
// comfortably above the cross-frame spread of one tile.
inline constexpr float kIntraFaceMergeLab = 18.0F;

// A group must beat its runner-up name by this many degrees before it is
// allowed to update a reference. Ambiguous evidence is exactly the
// evidence that merges two references.
inline constexpr float kUpdateMarginDeg = 12.0F;

// Degeneracy floors: two seeded references may never come closer than
// this. The Lab floor reuses resolve_colors' own seed-separation constant
// (kMinCentreSeedSeparationLab = 12) rather than restating an independent
// absolute; the hue floor is the same idea on the axis the naming actually
// decides on.
inline constexpr float kMinReferenceSeparationLab = 12.0F;
inline constexpr float kMinReferenceHueSeparationDeg = 15.0F;

// Fastest the references may track. An EMA whose alpha is 1/(n+1) is a
// running mean and stops responding once n is large; flooring alpha keeps
// the model able to follow a light that actually changes mid-session (a
// user walking to a window), at the cost of a permanent sensitivity to the
// most recent ~8 faces of a colour.
inline constexpr float kMinReferenceAlpha = 0.12F;

// Exchange rate between the two cost axes: how many degrees of hue error
// one unit of chroma shortfall is worth. Set so that the bootstrap white
// ceiling's full width (15 chroma units) prices at 45 degrees - wider than
// any single hue band, so a genuinely achromatic sample is named white
// rather than being pulled into whichever band its noise-dominated hue
// angle happens to land in.
inline constexpr float kChromaCostPerUnitDeg = 3.0F;

struct SessionColorReference {
    cv::Vec3f lab{};
    // Effective sample count behind `lab`. Drives the EMA rate, and is 0
    // exactly when the colour is still on its bootstrap band prior.
    float weight = 0.0F;
    bool seeded = false;
};

// One face's nine stickers named against the session.
struct SessionFaceReading {
    std::array<cube::model::Facelet, 9> facelets{};
    // Which within-face colour group each cell landed in (0..group_count-1),
    // exposed so callers and tests can see the grouping the naming acted on
    // rather than having to infer it from the names.
    std::array<int, 9> group{};
    int group_count = 0;
};

class SessionColorModel {
public:
    // Pure: names the nine stickers against the CURRENT references without
    // touching them. Safe to call from the vision worker.
    [[nodiscard]] SessionFaceReading classify_face(const std::array<cv::Vec3f, 9>& lab) const;

    // Feeds a face's evidence back into the references. GUI thread only.
    // Returns true if the proposed batch was committed, false if it was
    // rejected by the degeneracy guard (in which case nothing changed).
    bool observe(const std::array<cv::Vec3f, 9>& lab);

    // classify_face() followed by observe(), for the common caller that
    // wants both and should not pay for the grouping twice.
    SessionFaceReading classify_and_observe(const std::array<cv::Vec3f, 9>& lab);

    void reset();

    [[nodiscard]] const std::array<SessionColorReference, kSessionColorCount>& references() const noexcept {
        return references_;
    }
    [[nodiscard]] bool seeded(cube::model::FacetColor color) const;
    [[nodiscard]] float white_chroma_ceiling() const noexcept { return white_chroma_ceiling_; }

private:
    std::array<SessionColorReference, kSessionColorCount> references_{};
    float white_chroma_ceiling_ = kBootstrapWhiteChromaCeiling;
};

}  // namespace cube::vision
