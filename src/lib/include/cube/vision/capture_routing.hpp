#pragma once

#include <cube/model/face.hpp>
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
// trivially (identity is definitional) while fewer than two slots are
// filled, and by content re-registration (register_epoch_gauge +
// single-face tight re-association + content-equivalence tie-break)
// afterwards, during which commits buffer instead of landing ("re-
// orienting"). Ambiguity always defers; nothing is ever written on a
// guess.
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
    // Verified in-place update - the ONLY overwrite path: a commit onto an
    // occupied slot must have its centre within this Lab radius of the
    // slot's AND its census within this drift, or it is a different
    // physical face wearing a similar centre and the commit quarantines.
    float verify_centre_lab_radius = 30.0F;
    int verify_census_drift = 6;
    // New-face duplicate check: a commit routed to an EMPTY slot whose
    // centre sits within this radius of some occupied slot's is slip
    // evidence (tracked identity claims a new face while colour claims a
    // captured one) - quarantined, never written, and counted: repeats on
    // one claimed face escalate to force-lost at the conflict_strikes
    // threshold (identity-policy rule 3's belief penalty).
    float duplicate_centre_radius = 18.0F;
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
    // live-tested values inherited from the legacy tight-match path.
    float reassoc_centre_lab = 25.0F;
    int reassoc_census = 4;
    // Content equivalence (recovery tie-break): identical census and
    // centres closer than resolve_colors' 12-Lab seed floor means the
    // slots are interchangeable for assembly, so a registration tie among
    // them is harmless and must not wedge a solved/near-solved cube.
    float content_equiv_lab = 12.0F;
    // Epoch registration commit bar: winner's mean per-pair score at most
    // this, runner-up worse by at least the margin. Score units are census
    // L1 points (centre Lab folds in at 2 points per content_equiv_lab -
    // see registration_pair_score).
    float epoch_register_max_score = 6.0F;
    float epoch_register_margin = 4.0F;
};

// Content distance used by epoch registration, in census-L1-equivalent
// points: census L1 plus centre Lab scaled so one content_equiv_lab of
// centre drift costs as much as one sticker reclassification (2 points) -
// both signals vote, neither can shout the other down.
[[nodiscard]] float registration_pair_score(const CapturedFace& a, const CapturedFace& b,
                                            const RoutingConfig& config);

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
//   insufficient: no epoch face tightly matches anything stored yet - the
//                 deferral that keeps a brand-new face out of an empty
//                 slot until other epoch faces are positively recognised.
//
// Tie-break (the solved-cube termination guarantee): candidates tied
// within the margin are accepted anyway IF, for every epoch face, the
// slots the tied candidates map it onto are all empty or all pairwise
// content-equivalent - the assignments differ only between interchangeable
// content, so picking the best-scoring (lowest index on residual ties) is
// harmless for assembly and unwedges a cube whose faces genuinely cannot
// be told apart.
struct EpochFaceSample {
    cube::model::Face epoch_face{};
    CapturedFace captured;
};
struct EpochRegistration {
    enum class Status : std::uint8_t { resolved, ambiguous, insufficient };
    Status status = Status::insufficient;
    // The face permutation index (into kFacePermutation): store key =
    // kFacePermutation[lattice_index][epoch_face]. Valid only when
    // resolved.
    int lattice_index = 0;
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
};

class CaptureRouter {
public:
    explicit CaptureRouter(RoutingConfig config = {});

    // Epoch lifecycle, driven once per pass with the tracker's epoch
    // counter. A change means the body labels commits arrive under are
    // NEW: with >= 2 slots filled the router enters recovery (commits
    // buffer until the gauge re-registers), otherwise the next commit
    // re-derives the gauge directly (identity is definitional when there
    // is at most one slot to disagree with). Conflict strikes and the
    // layer-turn window are epoch-scoped ("within one locked segment") and
    // reset here.
    void on_epoch(int epoch);

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
    // (identity is definitional again, matching on_epoch's own rule) -
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

    // The gauge-resolved commit core (rules 2-6 of the identity policy).
    DirectCommit commit_direct(cube::model::Face epoch_face, const CapturedFace& captured, std::int64_t now_ms,
                               CommitResult& result);
    // Gauge adoption for the <= 1-filled-slot epoch change.
    [[nodiscard]] bool adopt_gauge_direct(cube::model::Face epoch_face, const CapturedFace& captured);
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
