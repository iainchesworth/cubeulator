#pragma once

// Tracked-scan fusion (Phase 6): the ONE definition of the sub-epoch
// fusion store, its feed-time takeover detector, the content-based
// cross-epoch registration pass, and the best-sub-epoch selection - shared
// by the offline runner (tools/runner, the acceptance surface where every
// bar below was measured) and the live app's track-mode capture path
// (scan_session.cpp, P6.12). Before this header the runner carried the
// only copy and the app carried NOTHING - the live demo measured track
// mode committing zero faces unconditionally - and the constants would
// otherwise have been hand-transcribed into the app, exactly the drift
// trap capture_gates.hpp exists to close for the commit gates.
//
// The store's shape and the meaning of its keys:
//
//   FusionStore : sub-epoch -> (model-frame face id -> FaceAggregator)
//
// P6.1: pose-derived reads accumulate per (sub-epoch, face id) through the
// SAME FaceAggregator the app's legacy capture path uses - its window /
// convergence machinery is the photometric commit gate the plan keeps.
// P6.6: the outer key is a CONTENT-KEYED SUB-EPOCH, gauge_epoch * 1000 +
// generation. A quarter-turn slip the tracker survives (no drop, no epoch
// bump) refills existing face-id slots with a different physical face, and
// the 8-deep ring evicts the old evidence - measured destroying the only
// shared-face content two epochs had. The feed-time takeover detector
// (add_observation) bumps the generation instead, freezing the whole label
// set (a slip moves every label simultaneously, so one slot's takeover
// invalidates them all - including slots whose own centres happened to
// match). Registration (register_epochs) re-unifies generations by content
// exactly as it re-unifies epochs; finalize picks the best sub-epoch
// (best_sub_epoch).
// P6.15: across a sub-epoch, the labels are six DISTINCT physical faces -
// the cube's own physics, and the one invariant the per-slot takeover
// detector structurally cannot see (kCrossSlotDuplicateLab). It is
// enforced where a duplicate can be built (registration's transplant) and
// counted where completion is decided (distinct_converged_faces, which
// BestSubEpoch::distinct carries to both callers' finalize bars and to the
// app's progress ring).

#include <cube/vision/color_resolution.hpp>
#include <cube/vision/temporal_fusion.hpp>

#include <opencv2/core.hpp>

#include <array>
#include <map>

namespace cube::vision::fusion {

// P6.2 capture grade, the geometric half: a tracked face below this
// facing cosine is a grazing sliver no sampler should trust (threshold
// provisional from P3.4, held through the Phase 6 corpus). Consumed by
// stage D of the offline pipeline (legacy_pipeline.cpp) and the app's
// live track-mode sampler - the tracker-quality half (read_grade) rides
// the DetectTrackResult.
inline constexpr double kTrackCaptureFrontality = 0.4;

// P6.6 takeover detector: an incoming centre further than this from a
// settled slot's fused centre means a different physical face took the
// label over (a slip the tracker survived). A legitimate same-face
// illumination jump (measured up to 31.9 Lab across a visibility gap)
// causes a spurious split; that is accepted and healed by registration's
// drift-tolerant verify.
inline constexpr double kTakeoverJumpLab = 24.0;

// P6.15 CROSS-SLOT duplication bar: two slots this close hold ONE
// physical face, whatever their labels say.
//
// The takeover detector above is PER-SLOT - "does this read still look
// like what THIS label held?" - and a slip that moves a physical face
// onto a label the era has not filled yet answers nothing at all: the
// slot is empty, so there is no incumbent to disagree with. Measured on
// live-demo-p6-retry, sub-epoch 1002: the red face fed label 1 for passes
// 195-208, then (passes 226-232, after an 18-pass gap) the SAME face fed
// label 2, which was empty - no split, and the era ended holding one
// physical face under two identities 5.6 Lab apart. The per-slot trigger
// did fire late and just missed: label 1's next read, at pass 254, was a
// different face 23.7 Lab from its settled centre against the 24.0 bar -
// and a split there would have come two labels too late anyway, because
// the duplicate was already built. An adversarial review during P6.6
// named this class in advance ("cross-slot same-face duplication -
// invisible to a per-slot jump trigger"); the live clips are where it
// finally showed.
//
// The bar is kMinCentreSeedSeparationLab, DERIVED not restated: the same
// "two captured centres this close are one colour scanned twice" floor
// resolve_colors seeds its clusters with, sitting below the measured
// closest legitimate centre pair (kClosestCentrePairLab, red against
// orange at 22.9). Two settled slots inside it cannot be two faces of a
// standard cube - the cube's physics, not a tuned threshold - which is
// exactly what color_resolution.hpp's header asks every "same physical
// face" predicate in the stack to derive from.
//
// It is applied where a duplicate can be BUILT (register_epochs) and
// where it must be CAUGHT (distinct_converged_faces), and deliberately
// NOT at the feed. A feed-time version was implemented and measured: it
// splits the generation the moment a face arrives under a second label,
// which is tidier in principle and worse in fact - on the retry clip the
// split stranded the red face in a generation registration's per-face
// verify then refused (best candidate 1.10, refuted; best verified
// candidate 26.30 against a 12.5 radius), finishing at FOUR distinct
// faces where refusing the duplicating transplant alone finishes at five,
// and it perturbed synth-occlusion's whole merge accounting (27->29
// merges, 49->52 refusals, 33->51 splits) for no outcome change. Refusing
// at the transplant is a no-op on all seven synthetic clips, so the store
// keeps its evidence and the invariant still holds where it decides
// anything.
inline constexpr double kCrossSlotDuplicateLab = kMinCentreSeedSeparationLab;

// P6.11 registration bars, re-derived by measurement in the decomposed
// (hue-aware) cell cost by dumping best and runner-up candidate costs for
// every pair evaluation across the seven-clip corpus. TRUE bridges -
// freerot's two merges, occlusion's twenty-seven re-acquisitions, both
// sweeps' hold boundaries, and sweep_gentle's era-3 bridge that plain Lab
// refused at 24.8 - top out at 11.1, and that maximum IS the era-3
// bridge: grazing-oblique against face-on, the largest same-sticker drift
// the corpus produces. The nearest best cost any WRONG pairing produces
// is 13.7 (exit-reentry's 3000<-3001, which its 1.8 margin refuses
// anyway); every other wrong best is 20.8 or beyond. 12.5 splits the
// measured gap - thin, and honestly thin: the gap itself is only 2.6
// wide, so the radius is one more grazing-vs-face-on era away from
// needing the margin and verify to carry a refusal alone.
inline constexpr double kRegisterRadiusHue = 12.5;  // decomposed cost, min per-face mean

// Margin over the runner-up candidate, same min-face basis. Its job is
// ambiguity, not distance: a near-uniform shared face matches several
// rolls almost equally (occlusion pairs measure margins 0.0-6.7, the
// widest 1003<-1025), and committing one would scramble cell indices on a
// coin-flip rotation. The thinnest margin any genuine commit shows is 7.6
// - the era-3 bridge again, runner-up 18.7 against best 11.1 - which the
// old bar of 8 refused; 7.0 sits between the measured 6.7 and 7.6 with
// the same honesty note as the radius.
// P6.13: when the primary selection refuses, registration retries the
// same bars over the candidates the per-face verify does not refute - a
// candidate whose shared centres disagree beyond kRegisterVerifyCentreLab
// could never commit, so it must not crowd the margin either (rationale
// and measurements at the retry in register_epochs).
inline constexpr double kRegisterMarginHue = 7.0;  // best-vs-runner-up, same basis

// P6.6 drift-tolerant per-face verify. The cell-cost radius and margin
// above are the real bars; the per-face centre check is a structural
// backstop against merging different physical faces, not a second radius.
// Measured same-face illumination drift across a visibility gap runs
// 21-32 Lab (which the old kRegisterRadius bound refused, making a
// spurious generation split unhealable), while the minimum cross-face
// centre separation is 57.3 Lab - 40 sits between them.
inline constexpr double kRegisterVerifyCentreLab = 40.0;

// Registration SCORING floor, deliberately below the capture bar
// (FaceAggregator::kMinObservations = 4): a face seen for just 2 frames
// while the cube transits between holds is still content evidence - the
// two reads must agree with each other AND the radius/margin/verify bars
// still apply - and it can be the ONLY face two epochs share. Measured on
// the sweep (P6.11 decomposed cost): the boundary face has 2 obs at 4.1
// mean against the older epoch's 46-obs fusion, with the best wrong
// candidate at 18.5. Final selection and capture stay on
// kMinObservations; never-overwrite keeps thin content out of the
// assembled result.
inline constexpr int kRegisterScoreFloor = 2;

// sub-epoch -> (model-frame face id -> photometric evidence window).
using FusionStore = std::map<int, std::map<int, FaceAggregator>>;

// One capture-grade tracked face's per-cell reads, as stage F samples
// them (quad-masked Lab median + purity, analysis scale) - the wire type
// the app's worker hands to the GUI-side feed. purity 0 means "not
// measured", never "clean" (fails closed): a face whose CENTRE purity is
// 0 must not feed, the "this face was really read" proxy both the runner
// and the app apply.
struct TrackedFaceRead {
    int face = -1;  // model-frame face id, 0..5 = +x,-x,+y,-y,+z,-z
    std::array<cv::Vec3f, 9> lab{};
    std::array<float, 9> purity{};
};

struct FeedResult {
    int sub_epoch = 0;             // the sub-epoch the observation landed in
    bool generation_split = false; // the takeover detector bumped the generation
};

// Feed one capture-grade read into the store under its content-keyed
// sub-epoch. generation_of_epoch carries the current generation per
// gauge_epoch across calls (starts at 0); the takeover detector above
// bumps it so the frozen sub-epoch keeps its evidence and ALL subsequent
// feeds for the epoch land in the new one.
[[nodiscard]] FeedResult add_observation(FusionStore& fusion, std::map<int, int>& generation_of_epoch,
                                         int gauge_epoch, int face_id, const FaceObservation& observation);

struct RegistrationStats {
    int merges = 0;    // verified cross-epoch merges committed
    int refusals = 0;  // candidate merges the per-face verify refused
};

// P6.3 content-based cross-epoch registration. An unstitchable drop (snap
// angle at the basin midpoint) severs identity that CONTENT can restore:
// two epochs' fused faces are the same physical stickers, and the relabel
// between them must be one of the 24 cube rotations' face permutations.
// Each candidate is scored cell-level in the shading-tolerant decomposed
// cost (P6.11); the re-derived bars above apply - commit radius, margin
// over the runner-up, minimum shared faces - and the router's invariants
// hold: never-overwrite (a SETTLED face present in both epochs keeps the
// older epoch's aggregator; a thin incumbent yields) and verify (a shared
// face disagreeing beyond the radius refuses the whole merge).
// Transplanted faces are cell-re-indexed through the same rotation used
// for scoring (P6.6), so they shed their source-frame grid roll. Runs to
// a fixed point; mutates the store in place.
// P6.15: a transplant that would leave the target epoch holding one
// physical colour under two labels is skipped (kCrossSlotDuplicateLab) -
// the merge still commits for every face that does not duplicate, and
// faces transplant strongest-evidence-first so the surviving copy of a
// duplicated colour is the better-supported one rather than the lower
// label. A relabel is a rotation of ONE cube, so a duplicate it produces
// is evidence that this content does not belong in that slot; planting it
// there only manufactures the era finalize must then refuse. Measured on
// live-demo-p6-retry: the second copy of the red face used to take label
// 0 and the era finished six converged slots holding five faces, which
// resolve_colors refused as "no valid cube"; refusing that one transplant
// leaves the same store reporting the honest five.
RegistrationStats register_epochs(FusionStore& fusion);

struct BestSubEpoch {
    int sub_epoch = -1;  // -1 = no sub-epoch carries any scored evidence
    int observed = 0;    // face ids with >= FaceAggregator::kMinObservations
    int converged = 0;   // ... that also passed the aggregator's convergence gate
    // P6.15: ... counting one physical colour ONCE. Six converged slots
    // are not six faces if two of them hold the same face, and the
    // difference is the whole distance between "the scan is done" and "the
    // scan can never finish" - the number a progress indicator may show
    // and a completion gate may test.
    int distinct = 0;
};

// Converged slots of one sub-epoch that are DISTINCT physical faces:
// converged slots in face-id order, each counted only when its fused
// centre clears kCrossSlotDuplicateLab against every slot already
// counted. Order-dependent among a mutually-close group and deliberately
// so - it under-counts rather than over-counts, which is the fail-closed
// direction for a completion test.
[[nodiscard]] int distinct_converged_faces(const std::map<int, FaceAggregator>& faces);

// "Best" = most DISTINCT converged faces (P6.15), ties to most converged
// aggregators (the photometric gate), then to most sufficiently-observed
// - one sub-epoch must carry all six faces, the plan's identity-lifetime
// bar. Distinct leads because distance from a cube is what selection is
// for: a five-face era is one face from finishing, a six-slot era holding
// four faces is two, whatever its slot count says.
[[nodiscard]] BestSubEpoch best_sub_epoch(const FusionStore& fusion);

}  // namespace cube::vision::fusion
