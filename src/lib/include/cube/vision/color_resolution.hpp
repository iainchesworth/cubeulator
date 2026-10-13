#pragma once

#include <cube/error.hpp>
#include <cube/model/face.hpp>
#include <cube/model/facelet.hpp>

#include <opencv2/core.hpp>

#include <array>

namespace cube::vision {

// One side's nine sticker colours as float Lab (L in [0,100]), in the order the
// reader lays them out: a robust per-cell colour, ready for final naming.
struct CapturedFace {
    std::array<cv::Vec3f, 9> lab{};
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

// Single-sticker classification against the fixed reference palette. The
// authoritative classification is resolve_colors(), which runs once all six
// sides are captured; this is what it falls back on to name a cluster.
[[nodiscard]] cube::model::Facelet classify_lab(const cv::Vec3f& lab);

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
[[nodiscard]] Result<std::array<std::array<cube::model::Facelet, 9>, 6>> resolve_colors(
    const std::array<CapturedFace, 6>& faces);

}  // namespace cube::vision
