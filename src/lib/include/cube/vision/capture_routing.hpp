#pragma once

#include <cube/model/face.hpp>
#include <cube/vision/color_resolution.hpp>
#include <cube/vision/temporal_fusion.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace cube::vision {

// ---------------------------------------------------------------------------
// Identity-keyed capture routing: the slot store behind free-rotation
// scanning.
//
// The routing rule, verbatim from the tracking plan: IDENTITY DECIDES WHERE
// A CAPTURE GOES; CENSUS DECIDES WHETHER IT MAY LAND. A capture arrives
// already keyed by the tracked body face that produced it (the
// OrientationTracker's epoch-local frame); colour is only ever
// verification evidence - it can veto a commit, it can never redirect one.
// The product invariant this class exists to uphold: a captured face is
// NEVER overwritten by an observation of a different physical face. The
// only overwrite path in the whole system is the verified in-place update
// (identity match AND census/centre verification pass); every disagreement
// quarantines - drops the capture and keeps the slot - and repeated
// disagreement escalates to honest failure (force-lost track, or full scan
// invalidation on the layer-turn signature), never to a silent swap.
//
// The store is keyed by BODY FACE, not by fill position: slots_[f] is the
// capture for tracked face f, and fill_order_ remembers arrival order so
// the QML contract (slotSummaries in fill order, faceConfirmed(slotIndex))
// is preserved on top of the keyed store.
//
// Epochs. The tracker's body labels die with its track: every re-anchor
// starts a new epoch whose faces have no a-priori relation to the store's
// keys. The store therefore carries a GAUGE - the lattice face permutation
// mapping current-epoch faces onto store keys - re-established per epoch:
// directly (labels are relative, the roll from ordered stickers - see
// adopt_gauge_direct) while fewer than two slots are filled, and by content
// re-registration (register_epoch_gauge + single-face tight re-association
// + oriented roll and content-equivalence tie-breaks) afterwards, during
// which commits buffer instead of landing ("re-orienting"). Ambiguity
// always defers; nothing is ever written over on a guess.
//
// One case escapes the buffering, and it is the coverage case: a capture
// that resembles NOTHING stored (registration `insufficient`) is not an
// unresolved hypothesis, it is a face the scan has never seen, and it is
// admitted straight to an EMPTY slot by the same direct adoption the
// <= 1-slot regime uses. The invariant is untouched - the newcomer is only
// ever routed to an empty key, and it is refused outright if it reads as a
// captured face (verified_same_face's own bar) or if its centre sits on
// one (the duplicate-centre scan).
//
// That admission does NOT end the re-orienting window, and the distinction
// is the whole safety of it. The lattice index it was filed under was
// picked with no evidence at all, so it is used for that one empty-target
// write and then dropped: the epoch stays in recovery, the slot is marked
// provisional_ and withheld from registration, and every in-place update
// for the rest of the epoch is still routed by a gauge register_epoch_gauge
// resolved - roll included - against content that was never guessed.
//
// The gauge is a 24-element rotation, and BOTH of its components are
// load-bearing on the write path: kFacePermutation says which slot a
// capture belongs in, kBodyRelabel says where its nine stickers go inside
// that slot (relabel_capture). Deciding the first from evidence and the
// second by convenience is the same class of error as deciding neither -
// a slot with the right label and rotated contents corrupts assembly and
// poisons every later oriented comparison scored against it.
//
// Qt-free, stateful but single-threaded by contract: mutated only from the
// GUI thread that serialises all scan state (ScanSession's threading
// rule). Bounded work per call: the gauge search is 24 face permutations
// over at most 6 buffered faces.
// ---------------------------------------------------------------------------

// Rotation-invariant fingerprint of a captured face: how many of its nine
// stickers provisionally classify as each FacetColor (unknown included). A
// genuine re-show of the same face keeps (nearly) the same counts however
// it is rotated; a DIFFERENT face whose washed centre happens to alias an
// existing slot's centre almost never shares its sticker population - the
// exception, two solid faces misread as the same colour (solved cube under
// bad exposure), is exactly why census only ever VERIFIES a commit that
// identity already routed, and never routes one itself. L1 distance
// between count vectors: 0 = identical population, each single sticker
// reclassification costs 2.
using ColorCensus = std::array<int, 7>;
[[nodiscard]] ColorCensus color_census(const CapturedFace& face);
[[nodiscard]] int census_l1_distance(const ColorCensus& a, const ColorCensus& b);

// Euclidean Lab distance between the two faces' centre stickers - centres
// are rotation-invariant (index 4 stays put however a face is held), so
// this survives re-shows at any quarter turn.
[[nodiscard]] float centre_lab_distance(const CapturedFace& a, const CapturedFace& b);

// All tunables in one place per the plan's constants table (initial values
// transcribed verbatim; the calibration milestone sweeps them over the
// recorded corpus).
struct RoutingConfig {
    // The one "this is the same physical face" radius, shared by the
    // verified in-place update (the ONLY overwrite path in the system) and
    // by the recovery path's tight re-association. It was two separate
    // numbers - a 30.0 verify radius and this 25.0 - and the 30.0 was the
    // bug: red/orange centres are 22.9 Lab apart as captured
    // (kClosestCentrePairLab), so the verifier's own predicate could not
    // distinguish the closest legitimate colour pair on a cube and a
    // DIFFERENT physical face could authenticate into an occupied slot.
    // Measured on the corpus: live3 slot F, pass 104 -> 112, centre class
    // yellow -> red at 28.1 Lab and census L1 2, admitted as `updated`; all
    // eight dihedral cell maps of the old content sat flat at 35.4-38.6
    // mean Lab, so no roll or mirror explained the new stickers - it was
    // simply another face, and it destroyed live3's only captured slot.
    //
    // 25 was the live-tested legacy tight-match radius, chosen when
    // kClosestCentrePairLab was believed to be 27. Re-measuring that
    // constant on captured centres put it at 22.9 (see color_resolution.hpp
    // for the corpus figures), so 25 no longer sits inside the palette at
    // all and the static_assert below - which exists precisely to stop this
    // from being true silently - now forces this down with it.
    //
    // 22.8 is the LARGEST value the corrected ceiling admits, and it is
    // chosen as a maximum rather than picked: the corpus's largest HONEST
    // same-face centre drift is 22.7 Lab (live3 slot F, pass 42 -> 104,
    // yellow -> yellow), so anything lower starts refusing measured,
    // legitimate updates - and a refused update is not free, it takes a
    // strike and conflict_strikes = 2 escalates to force_lost. The
    // remaining room either side is 0.1 Lab below and 0.15 above.
    //
    // Which is the reading that matters, and it is worth stating rather
    // than burying: honest same-face drift (22.7) and the closest
    // legitimate centre pair (22.95) now OVERLAP to within any reasonable
    // measurement error. Centre Lab distance therefore cannot separate
    // "this face again" from "a different face" at ANY threshold - there is
    // no value of this constant that both admits real drift and excludes
    // red-beside-orange. It survives as defence in depth only. The
    // load-bearing term in verified_same_face is centre CLASS equality and
    // the census is the third; that is what refuses the one measured breach
    // (live3 slot F, yellow -> red at 28.1 Lab), not this radius.
    //
    // So: retuning this number is not a way to buy back capture rate, and
    // widening it is not available at all. Improving the centre classifier
    // is the only thing that moves this decision.
    float same_face_centre_lab = 22.8F;
    // Census drift allowed on an in-place update. Left at 6 deliberately:
    // every measured update event in the corpus sat at census L1 <= 2, so
    // the census term was never the deciding gate in either direction and
    // tightening it would only cost coverage for no measured safety.
    int verify_census_drift = 6;
    // New-face duplicate check: a commit routed to an EMPTY slot whose
    // centre sits within this radius of some occupied slot's is slip
    // evidence (tracked identity claims a new face while colour claims a
    // captured one) - quarantined, never written, and counted: repeats on
    // one claimed face escalate to force-lost at the conflict_strikes
    // threshold (identity-policy rule 3's belief penalty).
    float duplicate_centre_radius = 18.0F;
    // Self-duplication refusal radius: paired with centre-CLASS equality,
    // the bar at which a capture routed to an EMPTY key is refused because
    // some filled slot already holds that physical face. Wider than
    // same_face_centre_lab on purpose, and the asymmetry is the whole
    // design:
    //
    //   - Authenticating a re-show OVERWRITES nine stored stickers. Getting
    //     that wrong is unrecoverable, so its radius must sit inside the
    //     palette floor (see same_face_centre_lab).
    //   - Refusing an admission writes NOTHING. Getting that wrong costs
    //     one deferred capture of a face the user can simply show again.
    //
    // Sizing the cheap decision by the expensive decision's constant is
    // what left the [18, 25) window open: duplicate_centre_radius (18) is
    // narrower than honest same-face drift (22.7), so a re-read that
    // drifted into that band was filed BESIDE itself, one physical face
    // occupying two store keys - after which no colour threshold downstream
    // can produce a valid cube.
    //
    // Realised, not latent, and measured on the corpus rather than argued:
    // runA finishes with slot D and slot L both carrying an ORANGE centre
    // 22.98 Lab apart, and live2 with slot F and slot B both GREEN 24.67
    // apart. Both sit above same_face_centre_lab and below this. (runA's
    // six filled slots carrying only four distinct centre names is exactly
    // this defect.)
    //
    // 25 is not a fresh number: it is the live-tested legacy tight-match
    // radius this file used before the never-overwrite work moved
    // authentication down. It is bounded above by needing to leave a
    // genuinely new face admissible when the classifier puts it in the same
    // class as a stored one.
    float self_duplication_centre_lab = 25.0F;
    // Repeated verification failures on ONE face within a locked segment:
    // the track itself is suspect, force-lose it (slots preserved - they
    // were verified when written).
    int conflict_strikes = 2;
    // Census conflicts on this many DISTINCT faces inside the window is
    // the mid-scan layer-turn signature (one conflict is more likely a
    // tracking slip): the static-stickers premise is void, so the whole
    // capture set is invalidated.
    int layer_turn_conflict_faces = 2;
    std::int64_t layer_turn_window_ms = 5000;
    // Per-face refractory between commits: a still-converged aggregator
    // must not machine-gun updates into one slot.
    std::int64_t recommit_holdoff_ms = 800;
    // Single-face tight re-association (recovery): centre AND class AND
    // census must all match for an epoch face to adopt a stored slot -
    // live-tested values inherited from the legacy tight-match path. The
    // centre radius is same_face_centre_lab above; only the census
    // allowance differs, because re-association is choosing between
    // hypotheses rather than authenticating one.
    int reassoc_census = 4;
    // Content equivalence (recovery tie-break): identical census and
    // centres closer than resolve_colors' 12-Lab seed floor means the
    // slots are interchangeable for assembly, so a registration tie among
    // them is harmless and must not wedge a solved/near-solved cube. Taken
    // from resolve_colors' own floor rather than re-stated, so the two can
    // never drift apart.
    float content_equiv_lab = kMinCentreSeedSeparationLab;
    // Epoch registration commit bar: winner's mean per-pair score at most
    // this, runner-up worse by at least the margin. Score units are census
    // L1 points (centre Lab folds in at 2 points per content_equiv_lab -
    // see registration_pair_score).
    float epoch_register_max_score = 6.0F;
    float epoch_register_margin = 4.0F;
    // Oriented roll tie-break (see oriented_cell_lab_distance). Applied
    // ONLY to candidates already tied on the score above, and in raw mean
    // Lab over the eight non-centre stickers - not score points, because
    // it answers a different question (which ROLL) with evidence the
    // score's rotation-invariant terms structurally cannot carry.
    //
    // Bar: even the best roll must be this close in mean per-cell Lab, or
    // the hypothesis is matching the wrong face entirely and no roll of it
    // deserves to win. 20 sits above honest cross-epoch drift on a
    // re-shown face (exposure and white balance move a captured face by
    // single-digit Lab) and below a genuine sticker disagreement (two
    // different cube colours are 40+ Lab apart).
    //
    // EVIDENCE, stated precisely because it is weaker than the rest of
    // this struct's. Every other constant here was swept over the recorded
    // corpus; these two oriented constants were NOT, because the sweep
    // harness (tracker_sweep_test.cpp) replays tracker pose metrics and
    // has no routing surface at all - CaptureRouter lives behind
    // ScanSession, so sweeping RoutingConfig needs a routing replay
    // harness that does not yet exist. What backs them is the synthetic
    // boundary tests below this file's tie-break tests plus two live5
    // registrations (best/runner-up 5.28 vs 28.14 and 8.31 vs 47.56) -
    // enough to show the separation is wide in practice, not enough to
    // call the placement optimal. Treat as provisional until a routing
    // replay can measure them; the safety of the tie-break does not rest
    // on their exact values (the centre-agreement gate and the commit bar
    // do), only its hit rate.
    float epoch_register_oriented_max_lab = 20.0F;
    // Margin: the winning roll must beat the runner-up by this much mean
    // Lab or the tie is NOT broken here and falls through to the existing
    // content-equivalence check. On a rotationally symmetric face (a
    // solved face, or one whose pattern repeats under a quarter turn) all
    // four rolls score identically and this margin is never met - which is
    // the correct outcome, not a failure: the rolls really are
    // interchangeable and content equivalence is the right adjudicator.
    float epoch_register_oriented_margin_lab = 6.0F;
};

// The structural guarantee behind the never-overwrite invariant, checked at
// compile time on the shipped defaults so no future retune can quietly undo
// it: a radius that reaches the closest legitimate centre pair cannot
// distinguish two different physical faces, and the ONLY overwrite path in
// the system is gated on exactly this radius. This is a stronger statement
// than "the constant is currently 25" - it is the reason 25 is admissible.
static_assert(RoutingConfig{}.same_face_centre_lab < kClosestCentrePairLab,
              "same_face_centre_lab must sit strictly inside the closest legitimate centre pair "
              "(red/orange, 22.9 Lab measured on captured centres): at or above it the verified "
              "in-place update can authenticate a DIFFERENT physical face into an occupied slot");
static_assert(RoutingConfig{}.duplicate_centre_radius < kClosestCentrePairLab,
              "the duplicate-centre check would otherwise quarantine legitimate red-beside-orange "
              "fills as slip evidence");
static_assert(RoutingConfig{}.self_duplication_centre_lab >= RoutingConfig{}.same_face_centre_lab,
              "the refusal bar may never be narrower than the authentication bar: a reading the "
              "verifier would call a re-show of a captured face must not be admissible as a "
              "newcomer beside it, which is the [18, 25) self-duplication window");
static_assert(RoutingConfig{}.self_duplication_centre_lab >= RoutingConfig{}.duplicate_centre_radius,
              "the centre-alias net is a subset of the self-duplication net, not a wider one");
static_assert(RoutingConfig{}.content_equiv_lab <= RoutingConfig{}.same_face_centre_lab,
              "content equivalence is a strictly stronger claim than same-face: two slots declared "
              "interchangeable must also be same-face admissible");

// Re-express a capture from the epoch's body labelling into the store's,
// under gauge `lattice_index`: the write-path counterpart of
// oriented_cell_lab_distance's read-path comparison, and the operation
// kBodyRelabel was declared for. Cell k of `captured` on body face
// `epoch_body_face` becomes cell kBodyRelabel[lattice_index][epoch_body_face]
// .cell[k] of the store face kFacePermutation[lattice_index][epoch_body_face].
//
// Why the write path needs this AT ALL, since the router's decisions are
// made on rotation-invariant evidence: because the STORED BYTES are what
// assembly consumes. A gauge says the epoch's frame and the store's frame
// differ by a whole-cube rotation; kFacePermutation then says which slot the
// capture belongs in, and this says which of its nine stickers goes where
// inside that slot. Storing the epoch-frame array verbatim under a
// non-identity gauge files all nine stickers in the wrong frame - the slot
// label is right and its contents are a rotated lie, which corrupts assembly
// and (worse) poisons every later oriented_cell_lab_distance scored against
// that slot, so registration stops resolving and commits defer forever.
// Measured on the corpus before this existed: live5 F at a clean 90-degree
// roll error, live5 L and live1 D at mirrored maps, each confirmed as the
// same physical face by census L1 0-2 and centre Lab 2.4-18.8, and in all
// three cases registration's own winning oriented reading (5.28 / 8.31 /
// 19.05) matched the independently measured best dihedral fit to within 0.1
// Lab - the router chose the right permutation and the write discarded it.
[[nodiscard]] CapturedFace relabel_capture(const CapturedFace& captured,
                                           cube::model::Face epoch_body_face, int lattice_index);

// Content distance used by epoch registration, in census-L1-equivalent
// points: census L1 plus centre Lab scaled so one content_equiv_lab of
// centre drift costs as much as one sticker reclassification (2 points) -
// both signals vote, neither can shout the other down.
[[nodiscard]] float registration_pair_score(const CapturedFace& a, const CapturedFace& b,
                                            const RoutingConfig& config);

// Mean Lab distance over the eight NON-CENTRE stickers between an epoch
// face and a stored slot, once the epoch face's cells are relabelled into
// the stored face's frame by hypothesis `lattice_index` (kBodyRelabel).
// The centre is excluded because it is rotation-invariant - it says
// nothing about roll and registration_pair_score already weighs it.
//
// This is the ONLY signal in the system that can tell the four rolls of a
// face apart. Both census and centre colour are rotation-invariant by
// construction, so exactly four of the 24 permutations (the stabiliser of
// a face) score identically under them - a tie that is structural, not
// noise, and that no amount of extra photometric evidence of the same
// KIND could ever break. The ordered sticker array has carried the
// information all along; nothing consumed it until now.
//
// Precondition: both faces are in solver order for their own body face
// (as CapturedFace always is - see kBodyRelabel's note), `epoch_body_face`
// is the epoch-frame face that `epoch_face` was captured on, and `stored`
// is the slot at kFacePermutation[lattice_index][epoch_body_face].
[[nodiscard]] float oriented_cell_lab_distance(const CapturedFace& epoch_face, const CapturedFace& stored,
                                               cube::model::Face epoch_body_face, int lattice_index);

// 24-hypothesis photometric re-registration of a post-loss epoch against
// the slot store (the recovery path that needs NO specific pose - any
// converged epoch faces will do). Hypotheses are the 24 lattice face
// permutations mapping epoch faces onto store keys; each is constrained by
// tight re-associations (an epoch face that tightly matches exactly ONE
// stored slot pins the permutation there) and scored by content over every
// (epoch face -> occupied slot) pair it induces.
//
//   resolved:     a unique acceptable winner (or a tie whose differences
//                 are provably content-harmless - see the tie-break note).
//   ambiguous:    evidence exists but cannot be disambiguated (aliased
//                 pair, contradictory pins, a tie that would matter): keep
//                 buffering, commit nothing.
//   insufficient: no epoch face tightly matches anything stored - there is
//                 no hypothesis here to be wrong about, because this
//                 function can only score permutations that map a visible
//                 epoch face onto an ALREADY-FILLED slot (pairs > 0), and a
//                 face the scan has never captured has no correct pairing
//                 among them. It is therefore NOT a deferral verdict but a
//                 "this is a new face" verdict, and CaptureRouter::commit
//                 reads it that way: it hands the capture to the ordinary
//                 empty-slot admission path (adopt_gauge_direct) instead of
//                 buffering. Measured over runA..runD before that change:
//                 never-seen faces scored 9.8-16.1 against the 6.0 commit
//                 bar, insufficient 40 times in 79 attempts, and every clip
//                 ceilinged at two filled slots.
//
// Tie-breaking, in the order applied. A tie here is usually the STRUCTURAL
// four-fold one: the four lattice elements fixing a given face differ only
// by roll, so every rotation-invariant score assigns them the same value.
// With a single recognised epoch face and two filled slots that tie is
// unbreakable by score alone unless the second slot happens to be the
// opposite face - which is to say it is unbreakable four times in five, and
// registration simply could not resolve.
//
//  1. Oriented roll tie-break: score the tied candidates by ordered
//     sticker agreement (oriented_cell_lab_distance) and take the winner
//     if it clears the absolute bar, beats the runner-up by the margin,
//     and its induced pairs agree on centre class and centre Lab. This is
//     new evidence, not a loosened threshold: it consumes the cell
//     ordering the rotation-invariant terms discard.
//  2. Content equivalence (the solved-cube termination guarantee):
//     candidates still tied are accepted anyway IF, for every epoch face,
//     the slots they map it onto are all empty or all pairwise
//     content-equivalent - the assignments differ only between
//     interchangeable content, so picking the best-scoring (lowest index
//     on residual ties) is harmless for assembly and unwedges a cube whose
//     faces genuinely cannot be told apart.
//
// Anything surviving both stays ambiguous and keeps buffering.
//
// The safety property, stated as the code actually enforces it. The commit
// bar is applied to whichever candidate is RETURNED, not to the
// best-scoring one: step 1 can return a tied member instead of
// candidates.front(), and tie membership is only "within
// epoch_register_margin of the best", which reaches well above the bar
// whenever the best sits below it. Testing the bar on candidates.front()
// alone was sound while the winner was always that candidate, and stopped
// being sound the moment step 1 could change who the winner is. The bar
// is deliberately NOT used to filter the tied set itself - an over-bar
// member is still a live alternative hypothesis, and the margin exists to
// refuse a winner it cannot separate from one.
//
// Step 1 also resolves WITHOUT step 2's all-six future-binding check,
// which is a deliberate asymmetry rather than an omission. Step 2 accepts
// a tie it cannot break, so it owes a proof that no member's future
// assignments could matter; step 1 breaks the tie, eliminating the other
// members on positive ordered-sticker evidence and leaving one surviving
// hypothesis on exactly the footing of the `unique` basis, which binds the
// future without a further check for the same reason. That argument is
// only as good as the elimination, hence the three independent gates
// above - in particular the centre-agreement gate, without which a
// different physical face whose ring aliases the epoch face could win on
// roll evidence alone while its centre sat a whole colour class away.
struct EpochFaceSample {
    cube::model::Face epoch_face{};
    CapturedFace captured;
};
struct EpochRegistration {
    enum class Status : std::uint8_t { resolved, ambiguous, insufficient };
    // How a resolution was reached, for the debug log. `unique` means the
    // score alone separated the winner; the other two name the tie-break
    // that did. Meaningless unless status == resolved.
    enum class Basis : std::uint8_t { unique, oriented, content_equivalent };
    Status status = Status::insufficient;
    // The face permutation index (into kFacePermutation): store key =
    // kFacePermutation[lattice_index][epoch_face]. Valid only when
    // resolved.
    int lattice_index = 0;
    Basis basis = Basis::unique;
    // Diagnostics - always populated when any candidate survived pinning,
    // so a deferral is explainable from the log alone. Without these a
    // stuck scan reports only "deferred", which is exactly why the
    // four-fold tie above went unnoticed for a whole milestone.
    int candidates = 0;
    int tied = 0;
    float best_score = 0.0F;
    float runner_up_score = 0.0F;
    // Mean non-centre Lab of the best and runner-up TIED candidates under
    // the oriented comparison; equal values mean the roll was genuinely
    // undecidable from the stickers.
    float best_oriented_lab = 0.0F;
    float runner_up_oriented_lab = 0.0F;
};
[[nodiscard]] EpochRegistration register_epoch_gauge(const std::vector<EpochFaceSample>& epoch_faces,
                                                     const std::array<std::optional<CapturedFace>, 6>& slots,
                                                     const RoutingConfig& config);

enum class CommitOutcome : std::uint8_t {
    filled,       // routed to its empty slot and written
    updated,      // verified in-place refresh of its occupied slot
    quarantined,  // conflict evidence - nothing written, slot kept
    deferred,     // recovery: buffered until the epoch gauge resolves
    holdoff,      // per-face refractory still running - nothing written
};

struct CommitResult {
    CommitOutcome outcome = CommitOutcome::deferred;
    // Fill-order indices written by this call - normally just the commit
    // itself, but a recovery resolution flushes every buffered epoch face
    // at once. The caller emits faceConfirmed for each.
    std::vector<int> confirmed_slots;
    // Escalations, valid whichever face tripped them: layer_turn means the
    // store already cleared itself (the caller invalidates the scan);
    // force_lost means a strike limit was hit - repeated verification
    // conflicts on one occupied slot OR repeated duplicate-centre
    // quarantines on one claimed empty slot (the caller force-loses the
    // tracker - slots are PRESERVED here).
    bool layer_turn = false;
    bool force_lost = false;
    // The quarantine was the duplicate-centre kind: tracked identity
    // claimed a NEW face while colour claimed a captured one (identity-
    // policy rule 3 - slip evidence). Surfaced distinctly because it is
    // the only signature a wrong epoch-boundary gauge ever produces: the
    // caller's log must be able to tell it from an occupied-slot
    // verification conflict, and repeats escalate through force_lost
    // above.
    bool duplicate_centre = false;
    // The registration attempt this commit made, when it made one (the
    // recovery path only). Present on a deferral above all: "deferred" on
    // its own cannot distinguish "nothing recognised yet" from "recognised
    // but the roll is undecidable", and those call for opposite responses.
    std::optional<EpochRegistration> registration;
};

class CaptureRouter {
public:
    explicit CaptureRouter(RoutingConfig config = {});

    // Epoch lifecycle, driven once per pass with the tracker's epoch
    // counter. A change means the body labels commits arrive under are
    // NEW: with >= 2 slots filled the router enters recovery (commits
    // buffer until the gauge re-registers), otherwise the next commit
    // re-derives the gauge directly (labels are relative when there is at
    // most one slot to disagree with - but the ROLL is not, see
    // adopt_gauge_direct). Conflict strikes and the layer-turn window are
    // epoch-scoped ("within one locked segment") and reset here.
    void on_epoch(int epoch);

    // Call whenever the TRACKER is reset (not merely re-anchored). Its
    // epoch counter restarts from zero, so the next anchor can hand back an
    // epoch NUMBER this router has already seen - and on_epoch() early-
    // returns on an unchanged number, silently keeping a gauge that
    // describes the body frame of a track that no longer exists. Every
    // later commit is then routed under a dead permutation, which surfaces
    // as capture stalling into duplicate-centre quarantines and repeated
    // force-lost after a retake. Forgetting the number restores the
    // invariant that a gauge only ever outlives the track it was
    // registered against. Slots are deliberately KEPT: a retake re-reads
    // one face, it does not discard the faces already captured.
    void on_tracker_reset();

    // True while a post-loss epoch is buffering against an unresolved
    // gauge - the UI's "re-orienting" chip.
    [[nodiscard]] bool reorienting() const noexcept { return recovering_; }

    // Routes one converged capture, keyed by the CURRENT epoch's body
    // face, applying the full policy (verify / quarantine / strikes /
    // layer turn / duplicate check / holdoff / recovery buffering).
    // now_ms shares the analysis pass's steady-clock time base.
    [[nodiscard]] CommitResult commit(cube::model::Face epoch_face, const CapturedFace& captured,
                                      std::int64_t now_ms);

    // Full capture reset: every slot, the fill order, recovery state and
    // the gauge. restartScan and the layer-turn response both land here.
    void clear();

    // User-driven per-slot retake (fill-order index): the sanctioned way a
    // persistently conflicted slot is emptied - explicitly, never
    // silently. Returns false when the index names no filled slot. If a
    // retake drops the store to <= 1 slot mid-recovery, recovery ends
    // (labels are relative again, matching on_epoch's own rule) -
    // registration can never resolve against a store it cannot score.
    bool clear_slot(int fill_index);

    [[nodiscard]] int filled_count() const noexcept { return static_cast<int>(fill_order_.size()); }
    // Fill-order accessors backing slotSummaries/finalize; nullptr /
    // nullopt past the filled range.
    [[nodiscard]] const CapturedFace* slot_at(int fill_index) const;
    [[nodiscard]] std::optional<cube::model::Face> face_at(int fill_index) const;
    // Outstanding verification-conflict strikes on the slot (fill-order
    // index) - the faceCaptureStates property's "conflict" signal.
    [[nodiscard]] int strikes_at(int fill_index) const;

private:
    struct Slot {
        CapturedFace captured;
        int strikes = 0;
        std::int64_t last_commit_ms = 0;
    };
    struct DirectCommit {
        CommitOutcome outcome;
        int slot_index = -1;
    };

    // Everything a new locked segment invalidates: the gauge, the buffered
    // epoch faces, and the epoch-scoped conflict evidence. Shared by
    // on_epoch() and on_tracker_reset() so the two can never drift.
    void begin_epoch_scope();

    // The gauge-resolved commit core (rules 2-6 of the identity policy).
    DirectCommit commit_direct(cube::model::Face epoch_face, const CapturedFace& captured, std::int64_t now_ms,
                               CommitResult& result);
    // Gauge adoption for the <= 1-filled-slot epoch change, and for the
    // never-seen face admitted mid-recovery (registration `insufficient`).
    // The slot LABEL is relative in this regime and chosen by convention -
    // subject to the one hard rule that an unrecognised capture is only ever
    // routed to an EMPTY key; the ROLL is not relative, and is chosen from
    // ordered-sticker evidence whenever the capture re-associates with a
    // stored slot (which is exactly when the roll decides what overwrites
    // it). Returns false when the capture must not be admitted at all: its
    // centre aliases a captured face without authenticating as it, two
    // stored slots both claim it, or the store is full.
    [[nodiscard]] bool adopt_gauge_direct(cube::model::Face epoch_face, const CapturedFace& captured);
    // "Some filled slot already holds this physical face." The refusal half
    // of the same-face question, shared by ALL THREE admission paths so one
    // physical face cannot be filed beside itself: see
    // RoutingConfig::self_duplication_centre_lab for why the refusal bar is
    // deliberately wider than the authentication bar.
    [[nodiscard]] bool reads_as_a_stored_face(const CapturedFace& captured) const;
    void buffer_epoch_face(cube::model::Face epoch_face, const CapturedFace& captured);
    [[nodiscard]] int fill_index_of(cube::model::Face store_face) const;

    RoutingConfig config_;
    // Keyed by store face; fill_order_ is the QML-facing arrival order.
    std::array<std::optional<Slot>, 6> slots_{};
    std::vector<cube::model::Face> fill_order_;

    // Gauge: store key = kFacePermutation[gauge_][epoch_face]. Unknown
    // between an epoch change and its first successful adoption.
    int gauge_ = 0;
    bool gauge_known_ = false;
    int current_epoch_ = -1;
    bool recovering_ = false;
    // Recovery buffer: the latest converged capture per epoch face, plus
    // arrival order so a resolution flushes captures in the order the user
    // showed them.
    std::array<std::optional<CapturedFace>, 6> epoch_faces_{};
    std::vector<cube::model::Face> epoch_order_;
    // Slots filled THIS epoch by the mid-recovery newcomer admission, i.e.
    // under a gauge chosen with no evidence at all (registration came back
    // `insufficient`, so nothing tight-matched anything and the lattice
    // index was simply the first one landing the capture on an empty key).
    //
    // Their CONTENT is honest - the capture really is that physical face -
    // but the FRAME they are written in is a guess, so they are not evidence
    // about this epoch: register_epoch_gauge is shown the store WITHOUT
    // them. Otherwise the guess pins the epoch to itself, registration
    // resolves onto the very lattice index that was guessed, and the roll of
    // every later in-place update follows a gauge nothing ever tested -
    // measured on runD as a good capture rewritten 180 degrees out (see the
    // insufficient branch in commit()).
    //
    // Epoch-scoped: a guess may not be used as evidence inside the epoch
    // that made it. If that epoch later earns real evidence the guesses are
    // discarded and re-derived under the resolved gauge (see the resolved
    // branch in commit()); at the next epoch boundary they are ordinary
    // stored content like anything else. The invariant this state carries:
    // a face is flagged here only while recovering_ and only while its
    // capture is still in epoch_faces_, so discarding a flagged slot can
    // never lose a capture.
    std::array<bool, 6> provisional_{};

    // Layer-turn detector: (store face, time) of recent verification
    // conflicts; pruned to the window on every new conflict.
    std::vector<std::pair<cube::model::Face, std::int64_t>> conflict_events_;
    // Duplicate-centre quarantines per CLAIMED (empty) store face this
    // epoch - the strike counter behind rule 3's escalation. Epoch-scoped
    // like slot strikes: a new gauge means the old accusations were
    // against labels that no longer exist.
    std::array<int, 6> duplicate_quarantines_{};
};

}  // namespace cube::vision
