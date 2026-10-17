#pragma once

#include <cube/error.hpp>
#include <cube/model/face.hpp>
#include <cube/model/facelet.hpp>
#include <cube/vision/side_grid.hpp>

#include <opencv2/core.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace cube::vision {

// One side's N x N sticker colours as float Lab (L in [0,100]), in the order the
// reader lays them out (row-major, side_grid.hpp): a robust per-cell colour,
// ready for final naming. Nine for a 3x3, twenty-five for a 5x5; the size is
// the square root of the count.
struct CapturedFace {
    // Nine zeroes until a side is put in: a 3x3's, as the array this was.
    std::vector<cv::Vec3f> lab = std::vector<cv::Vec3f>(9);
};

// ---------------------------------------------------------------------------
// Palette geometry: the numbers every "are these two centres the same
// physical side?" decision must be DERIVED from rather than restate as an
// independent absolute.
//
// They live here, beside resolve_colors(), because this is where the
// measurement was made and where the cube's own physics enters the system:
// a standard cube carries six DISTINCT centre colours, so any predicate
// able to answer "same side" for a pair drawn from two different centres is
// wrong by construction, not merely mistuned.
// ---------------------------------------------------------------------------

// Closest legitimate centre pair on a standard cube: red against orange.
// Every other centre pair sits further apart, so this is the palette's
// minimum inter-centre separation and the hard ceiling on any "same
// physical side" radius.
//
// MEASURED ON CAPTURED CENTRES, not on the reference palette, and the
// difference is the whole point. The reference entries in
// default_palette() put red and orange 30.4 Lab apart; the value that
// matters is what the camera actually delivers after auto-exposure has
// compressed the chroma of a glossy side, and that is materially smaller.
// Over the eleven recordings of the earlier pipeline the closest pair of
// centres carrying different colour names was 22.95 Lab (orange against
// red), with green against yellow next at 23.52. 22.9 sits just inside the
// measured minimum: this is a CEILING on other constants, so it is
// deliberately not rounded down further than the evidence.
inline constexpr float kClosestCentrePairLab = 22.9F;

// Below this lightness a sample is not a white tile whatever its chroma:
// a lattice gap, a shadow, an unlit background or dark clothing behind the
// cube all sit at a*~b*~0, and a chroma-only white test names every one of
// them white at FULL confidence. Set midway across the two-sided gap the
// earlier corpus showed: 54.2 (the darkest true tile reaching the white
// branch) to 66.6 (the darkest genuine white).
inline constexpr float kWhiteLightnessFloor = 60.0F;

// Cluster seed separation: two captured centres closer than this are the
// same colour scanned twice, which no real cube can produce. 12 sits
// comfortably below kClosestCentrePairLab and comfortably above the
// residual spread of a median-fused centre reading.
inline constexpr float kMinCentreSeedSeparationLab = 12.0F;

static_assert(kMinCentreSeedSeparationLab < kClosestCentrePairLab,
              "the seed floor must sit strictly inside the closest legitimate centre pair, or "
              "resolve_colors would reject physically valid cubes");

// ---------------------------------------------------------------------------
// The light a scan was made in
// ---------------------------------------------------------------------------
//
// A cube carries its own white: the centre of the white side. Under neutral
// light it measures a* and b* near 0. Under a warm lamp it measures pink or
// orange, and every other colour is pulled the same way - on the cube this
// was found on, a warm-white LED made yellow read at hue 64 (an orange's
// band) and left red and orange 6 degrees of hue apart. Nothing about the
// light needs to be known in advance: once the white centre is found, what
// it measures says what the light did to everything.
struct WhiteReference {
    // Which of the six faces passed in is the white one.
    std::size_t side = 0;
    // Linear-RGB gains (red, green, blue) that make the white centre grey at
    // its own luminance. All 1 when the light is neutral enough that nothing
    // is worth correcting.
    std::array<float, 3> gain{1.0F, 1.0F, 1.0F};
    // Chroma of the white centre as measured: how far from neutral the light
    // is. 3 to 5 by morning daylight, about 18 under tungsten, 29 under a
    // warm-white LED.
    float cast = 0.0F;
};

// A cast below this is not corrected. Daylight whites measure 3 to 5; the
// correction is a few percent there and changes no name, and leaving it out
// keeps a well-lit scan exactly as it was.
inline constexpr float kMinCorrectedCast = 8.0F;

// The white centre of a complete set of six sides, or nothing when none of
// them is plausibly white. The white is the least chromatic of the centres
// that are not dark - a warm-lit white is pink, but a dark blue is less
// chromatic still and not white - and it has to beat the next candidate by a
// margin and need gains that a real light could ask for, so a set without a
// white (or a yellow or green mistaken for one) is refused rather than
// "corrected". Refusing leaves the scan named exactly as it would have been.
//
// Needs all six: with fewer, the white may simply not have been shown yet,
// and a green seen first would pass for it.
[[nodiscard]] std::optional<WhiteReference> find_white_reference(
    const std::array<CapturedFace, 6>& faces);

// What `lab` would have measured under neutral light, the reference's gains
// applied in linear RGB.
[[nodiscard]] cv::Vec3f neutralised(const WhiteReference& reference, const cv::Vec3f& lab);

// Which rule of classify_lab decided a sample's name.
enum class LabRule : std::uint8_t {
    // Near-neutral and below kWhiteLightnessFloor: unknown, at confidence zero.
    dark_neutral,
    // Near-neutral, light enough and not green-ward: white.
    neutral_white,
    // Chromatic enough to leave the neutral floor but light and warm: white.
    warm_cast_white,
    // Named by hue angle: red, orange, yellow, green or blue.
    hue_band,
};

// What classify_lab made of one sample, and the two numbers it made it from.
struct LabClassification {
    cube::model::Facelet facelet;
    // atan2(b*, a*) in degrees, folded to [0, 360).
    float hue_degrees = 0.0F;
    // hypot(a*, b*).
    float chroma = 0.0F;
    LabRule rule = LabRule::hue_band;
};

// Single-sticker classification against the fixed reference palette. The
// authoritative classification is resolve_colors(), which runs once all six
// sides are captured; this is what it falls back on to name a cluster.
[[nodiscard]] cube::model::Facelet classify_lab(const cv::Vec3f& lab);
// The same classification with the hue, the chroma and the rule that decided
// it: the values classify_lab computes anyway, kept instead of dropped.
[[nodiscard]] LabClassification classify_lab_explained(const cv::Vec3f& lab);

// Final, global colour assignment for a completed scan: clusters all 54
// captured sticker colours into exactly 6 groups of exactly 9, seeded from
// the six face centres (which must be six distinct colours on any real
// cube). Because the cube's own stickers define the clusters, a global
// lighting shift or an unusual sticker shade moves every sample of that
// colour together and cancels out - the failure mode a fixed reference
// palette can't survive. Cluster identities are then named by nearest
// reference colour so downstream code still speaks FacetColor.
//
// Faces are indexed by cube::model::Face (U,R,F,D,L,B). Fails with
// CubeError::no_valid_cube_detected when the six centre colours don't
// separate into six plausible clusters (e.g. two centres read as the same
// colour - a physically impossible scan), or when any of the 54 Lab samples
// is non-finite.
//
// The finiteness rule is a precondition made into a checked failure rather
// than left to the caller: every gate inside is an inequality, and an
// inequality against NaN is false, so an unchecked NaN would pass the
// centre-separation guard and then leave the clustering with no cluster to
// commit a sample to - which was an out-of-bounds write and a
// non-terminating loop, not a wrong answer.
//
// `white_side`, when the caller has found the white centre (see
// find_white_reference), names that cluster white outright instead of asking
// a lightness floor to - a dim white (L 55 on a cloudy morning) is otherwise
// "unknown" and costs the hue naming of every other cluster.
[[nodiscard]] Result<std::array<std::vector<cube::model::Facelet>, 6>> resolve_colors(
    const std::array<CapturedFace, 6>& faces, std::optional<std::size_t> white_side = std::nullopt);

// What decided the final names, once the clusters were named by distance to
// the palette: the hue names either replaced them or were set aside.
enum class HueOverride : std::uint8_t {
    // The six cluster means fell in six different hue bands, and those names
    // were taken.
    applied,
    // Two means fell in one band (a strong wash puts red and orange together,
    // say): the distance matching stood.
    collision,
    // A mean classified as unknown, which the palette has no entry for: the
    // distance matching stood.
    unknown_cluster,
};

// One of the six clusters, which the face whose centre seeded it indexes.
struct ClusterNaming {
    // The centre that seeded the cluster, and its mean once it settled.
    cv::Vec3f seed;
    cv::Vec3f mean;
    // classify_lab's reading of the mean: the hue and chroma the hue naming
    // used, and the name it gave.
    LabClassification hue;
    // The palette colour best_palette_permutation matched the mean to.
    cube::model::FacetColor by_distance = cube::model::FacetColor::unknown;
    // What the cluster was finally called: by_distance, or hue.facelet.color
    // when the hue names were taken.
    cube::model::FacetColor name = cube::model::FacetColor::unknown;
};

// Six vectors of nine default values: what a side's cells were when they were an
// array of nine.
template <typename T>
[[nodiscard]] std::array<std::vector<T>, 6> nine_each() {
    std::array<std::vector<T>, 6> sides;
    for (std::vector<T>& side : sides) {
        side.resize(9);
    }
    return sides;
}

// resolve_colors' answer together with how it got there, read-only. Every
// value is one the algorithm already held when it finished - building this
// adds no computation, and resolve_colors is this with the rest dropped.
//
// Its vectors are a 3x3's nine unknowns a side until the naming fills them in, as
// the arrays they were.
struct ColorResolution {
    // What resolve_colors returns: [face][cell], each with the confidence the
    // assignment had in its cluster (0 when a rival was as close).
    std::array<std::vector<cube::model::Facelet>, 6> faces = nine_each<cube::model::Facelet>();
    // Indexed by the face whose centre seeded the cluster.
    std::array<ClusterNaming, 6> clusters{};
    // [face][cell]: the cluster each of the 6 N^2 samples went to.
    std::array<std::vector<std::uint8_t>, 6> cluster_of = nine_each<std::uint8_t>();
    // Refinement rounds the clustering ran before its assignment repeated.
    int iterations = 0;
    // The summed Lab distance of the palette matching that by_distance names.
    float match_cost = 0.0F;
    HueOverride hue_override = HueOverride::collision;
    // Whether taking the hue names changed any cluster's name from
    // by_distance's. False for an override that agreed.
    bool hue_changed_names = false;
};

// resolve_colors with its working shown. Fails exactly where resolve_colors
// does.
[[nodiscard]] Result<ColorResolution> resolve_colors_explained(
    const std::array<CapturedFace, 6>& faces, std::optional<std::size_t> white_side = std::nullopt);

}  // namespace cube::vision
