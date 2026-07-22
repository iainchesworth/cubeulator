#pragma once

#include <cube/error.hpp>
#include <cube/model/face.hpp>
#include <cube/model/facelet.hpp>
#include <cube/vision/temporal_fusion.hpp>

#include <opencv2/core.hpp>

#include <array>

namespace cube::vision {

// ---------------------------------------------------------------------------
// Palette geometry: the two numbers every "are these two centres the same
// physical face?" decision in the vision stack must be DERIVED from rather
// than restate as an independent absolute.
//
// They live here, beside resolve_colors(), because this is where the
// measurement was made and where the cube's own physics enters the system:
// a standard cube carries six DISTINCT centre colours, so any predicate
// able to answer "same face" for a pair drawn from two different centres is
// wrong by construction, not merely mistuned. capture_routing.hpp's
// verification radii are sized against kClosestCentrePairLab for exactly
// that reason - see the static_asserts there.
// ---------------------------------------------------------------------------

// Closest legitimate centre pair on a standard cube: red against orange,
// measured at ~27 Lab even under the aggressive chroma compression a
// phone's auto-exposure applies to a glossy face. Every other centre pair
// sits further apart, so this is the palette's minimum inter-centre
// separation and the hard ceiling on any "same physical face" radius.
inline constexpr float kClosestCentrePairLab = 27.0F;

// Cluster seed separation: two captured centres closer than this are the
// same colour scanned twice, which no real cube can produce. 12 sits
// comfortably below kClosestCentrePairLab and comfortably above the
// residual spread of a median-fused centre reading.
inline constexpr float kMinCentreSeedSeparationLab = 12.0F;

static_assert(kMinCentreSeedSeparationLab < kClosestCentrePairLab,
              "the seed floor must sit strictly inside the closest legitimate centre pair, or "
              "resolve_colors would reject physically valid cubes");

// Provisional single-sticker classification against the fixed reference
// palette - used only for live UI display while scanning (overlay cell
// tints, progress cube). The authoritative classification is
// resolve_colors(), which runs once all six faces are captured.
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
// Faces are indexed by cube::model::Face (U,R,F,D,L,B), matching
// ScanProgress. Fails with CubeError::no_valid_cube_detected when the six
// centre colours don't separate into six plausible clusters (e.g. two
// centres read as the same colour - a physically impossible scan), or when
// any of the 54 Lab samples is non-finite.
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
