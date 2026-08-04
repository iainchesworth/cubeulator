#pragma once

#include <cube/error.hpp>
#include <cube/model/face.hpp>
#include <cube/model/facelet.hpp>
#include <cube/vision/temporal_fusion.hpp>

#include <opencv2/core.hpp>

#include <array>
#include <vector>

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

// Closest legitimate centre pair on a standard cube: red against orange.
// Every other centre pair sits further apart, so this is the palette's
// minimum inter-centre separation and the hard ceiling on any "same
// physical face" radius.
//
// MEASURED ON CAPTURED CENTRES, not on the reference palette, and the
// difference is the whole point. The reference entries in
// default_palette() put red and orange 30.4 Lab apart; the value that
// matters is what the camera actually delivers after auto-exposure has
// compressed the chroma of a glossy face, and that is materially smaller.
// Over the eleven recordings, the closest pair of centres carrying
// DIFFERENT colour names in a captured slot is 22.95 Lab (runD, slot L
// orange against slot R red), with runA slot F green against slot U yellow
// next at 23.52. The previous value of 27.0 was therefore above the real
// separation, not below it - which mattered because it is the ceiling the
// static_asserts in capture_routing.hpp certify the never-overwrite
// guarantee against, so those asserts were certifying a bound the corpus
// contradicts.
//
// 22.9 sits just inside the measured minimum. Deliberately not rounded
// down further: this is a CEILING on other constants, and lowering it
// beyond the evidence would force the same-face radii down to where honest
// re-shows stop verifying, costing captures for no measured safety.
inline constexpr float kClosestCentrePairLab = 22.9F;

// Below this lightness a sample is not a white tile whatever its chroma:
// a lattice gap, a shadow, an unlit background or dark clothing behind the
// cube all sit at a*~b*~0, and a chroma-only white test names every one of
// them white at FULL confidence - which corrupted the census, the
// provisional overlay, and (worst) dropMostSuspectSlot's argmin, so the
// retry dropped the best-read face rather than the worst. Set midway
// across the two-sided gap this corpus shows: 54.2 (runA F7, the darkest
// true tile reaching the white branch) to 66.6 (the darkest genuine
// white).
//
// EXPORTED, not file-local, because two classifiers must agree on it:
// classify_lab's neutral branch and SessionColorModel::name_cost's white
// cost. They were allowed to disagree once and the divergence was found by
// a cold-start equivalence test at L 20.3 / chroma 0.4, not by a corpus
// regression - a shared constant makes the agreement structural instead of
// something a test has to keep catching.
inline constexpr float kWhiteLightnessFloor = 60.0F;

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

// P5: session-relative classification for the LIVE path - the same idea
// resolve_colors applies at scan completion (the cube's own stickers
// define the clusters, so lighting shifts and odd sticker shades cancel)
// brought forward to per-frame reads, extending classify_lab's seeding
// rather than running a parallel classifier. Centre cells observed on
// capture-grade tracked faces accumulate into at most six Lab clusters;
// once two or more exist, a cell is named by its NEAREST session
// cluster - whose own name comes from classify_lab on the cluster
// centroid (the absolute bands as bootstrap and naming) - so a
// desaturated green or a hue-40 orange lands with its own kind by
// construction. A cell too far from every cluster (sanity bound) falls
// back to the absolute bands, which may honestly say unknown.
//
// Accumulation is EPOCH-SCOPED by the caller: observations from a track
// whose identity epoch changed must not keep feeding the same clusters.
class SessionCentrePalette {
public:
    // A capture-grade face's centre-cell Lab (float Lab, L in [0,100]).
    void observe_centre(const cv::Vec3f& lab);
    // Session-relative when >=2 clusters exist, absolute-bands fallback
    // otherwise (bootstrap) or when the sample is out of sanity reach.
    [[nodiscard]] cube::model::Facelet classify(const cv::Vec3f& lab) const;
    [[nodiscard]] int cluster_count() const { return static_cast<int>(clusters_.size()); }
    void reset() { clusters_.clear(); }

private:
    struct Cluster {
        cv::Vec3f centroid{};
        int samples = 0;
    };
    std::vector<Cluster> clusters_;
};

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
