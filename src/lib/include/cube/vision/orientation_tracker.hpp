#pragma once

#include <cube/model/face.hpp>
#include <cube/vision/orientation_lattice.hpp>

#include <array>
#include <cstdint>
#include <optional>

namespace cube::vision {

// ---------------------------------------------------------------------------
// Continuous cube-orientation tracking over the 24-element lattice.
//
// The tracker NEVER integrates deltas. Every per-frame pose the vision
// pipeline produces is absolute up to one unknown lattice element (which
// face the detector modelled as near, at which quarter turn - see
// orientation_lattice.hpp), so each pass re-resolves that element against
// a constant-angular-velocity prediction and adopts the resolved absolute
// orientation outright. Error therefore cannot accumulate, simultaneous
// multi-axis rotation is handled natively (resolution operates on full 3D
// rotations), and the detector re-keying its labelling at a 45-degree
// in-plane boundary is absorbed by construction - the labelling jump and
// the resolved element jump cancel.
//
// The correctness contract this class exists to uphold: a resolution the
// prediction cannot safely disambiguate is NEVER guessed at. Every
// acceptance passes four gates (raw-pose plausibility, basin, second-best
// margin, implied rate),
// uncertainty inflates while coasting and forces `lost` BEFORE the +-45
// degree lattice basin becomes reachable, and colour evidence - which is
// only ever a verification likelihood, never the identity itself - can
// veto the whole track when it accumulates against it. The failure mode
// is always an honest `lost`, never a silently wrong identity.
//
// Identity: the body frame is fixed at the anchoring detection (its model
// frame BECOMES the body frame, so the anchor's near face is body F by
// definition), and each anchor starts a new epoch. Which PHYSICAL face a
// body face is can therefore change across epochs; mapping epochs onto
// captured slots is deliberately not this class's job (the recovery /
// registration layer owns it) - the tracker only promises that within one
// epoch the identity it reports is geometrically consistent.
//
// Qt-free, OpenCV-free, no locks: a pure state machine mutated by exactly
// one on_pass() per analysis pass, bounded work per pass (a couple of
// 24-element scans - microseconds), safe to own wherever the GUI thread
// serialises scan state.
// ---------------------------------------------------------------------------

// Exponential/log map between a rotation vector (axis * angle, DEGREES,
// display frame) and a unit quaternion. Exposed because the coasting
// prediction, the implied-rate gate and every synthetic test need the same
// two conversions - re-deriving them independently is how frame-convention
// bugs are born.
[[nodiscard]] Quat quat_from_rotvec_deg(const std::array<float, 3>& rotvec_deg) noexcept;
[[nodiscard]] std::array<float, 3> quat_to_rotvec_deg(const Quat& q) noexcept;

// All tunables in one place per the implementation plan's constants table
// (initial values transcribed from it verbatim; the calibration milestone
// sweeps them over the recorded corpus, which is why nothing here is a
// scattered file-local constexpr).
//
// CALIBRATED on live1, a real 786-pass handheld session (oblique two-face
// grip, warm dim indoor light, matte tile cube), via [.track-sweep]:
// one-factor-at-a-time over every sweepable knob, a cartesian refinement
// over the responsive ones (max_interframe_deg x omega_blend x
// omega_decay x acquire_frames x coast_sigma_rate_deg x
// outlier_omega_deg_s, 216 cells), then OFAT re-run around the winner to
// confirm it as a local optimum. Only TWO knobs moved off the plan's
// table - max_interframe_deg 25->30 and acquire_frames 3->2 - taking the
// clip from locked 144 / coasting 157 / zero commit-grade passes to
// locked 185 / coasting 204 (38% -> 49.5% of passes locked+coasting) and
// 56 locked-AND-identity-certain passes, with the wrong-identity counter
// (slip_evidence in an epoch that had reached commit-grade certainty) at
// ZERO, as it was for every candidate on the shortlist. Configs that
// scored a few more locked+coasting passes existed (max +4) but all had
// zero commit-grade passes - continuity bought by never becoming certain
// is worthless to the capture layer.
struct TrackerConfig {
    // Acceptance ceiling on the resolved element's residual angle: the
    // lattice basin half-width is 45 degrees, minus a 12-degree snap
    // margin. Fixed by geometry, not sweepable.
    float basin_ceiling_deg = 33.0F;
    // Per-pass motion budget: with sigma at zero (fresh accept last pass)
    // the acceptance threshold is min(basin_ceiling, sigma + this).
    // Calibrated 25->30 on live1: the plan's 25 assumed a ~33 ms frame
    // cadence, but an analysis PASS is ~96 ms, so a casual wrist turn
    // produces bigger per-pass steps than budgeted and 25 was breaking
    // accept chains with basin refusals (59 -> 49 on the clip). The knock-
    // on effect dominates the direct one: unbroken chains keep epochs
    // alive long enough for colour belief to reach the certainty bar
    // (commit-grade passes 0 -> 56). Still 3 degrees under the 33-degree
    // geometric ceiling, and the implied whole-cube rate at pass cadence
    // (~310 deg/s) stays below the omega glitch gate, so every safety
    // ordering is preserved.
    float max_interframe_deg = 30.0F;
    // Refuse any resolution whose runner-up element is closer than this to
    // the best - the prediction cannot actually disambiguate them.
    float gauge_margin_deg = 15.0F;
    // Uncertainty inflation per pass without an accepted measurement, plus
    // a motion-score term (the stability tracker's score, ~0.02 when
    // steady): faster apparent scene motion means the coasted prediction
    // decays faster.
    float coast_sigma_rate_deg = 4.0F;
    float motion_sigma_gain_deg = 40.0F;
    // Slower inflation rate used IN PLACE of coast_sigma_rate_deg while
    // acquiring (see miss()): the live1 census showed detections arriving
    // in short bursts separated by 2-8 pass gaps, so an acquisition streak
    // that dies on the first miss discards most bursts (68 anchors yielded
    // ~17 locks). Acquiring has no capture authority - commits require
    // locked + identity_certain - so the longer bridge risks only anchor
    // churn, never a wrong capture. 2.5 deg/pass plus the motion term
    // reaches the 35-degree cap after ~10-12 quiet passes (~1.0-1.15 s at
    // the measured ~96 ms cadence), covering the measured gap cluster.
    float acquire_sigma_rate_deg = 2.5F;
    // Sigma above this forces lost. 35 = the pre-basin safety ceiling: the
    // acceptance threshold saturates at basin_ceiling long before, so a
    // coasted track can never inflate its way into accepting a resolution
    // from a neighbouring basin.
    float max_coast_sigma_deg = 35.0F;
    // Wall-clock loss timer since the last ACCEPTED measurement. Covers
    // what per-pass sigma inflation cannot: a single long inter-pass gap
    // (slow worker, app hiccup) crosses basins in one step, so a stale
    // prediction is discarded outright rather than trusted because "only
    // a few passes" elapsed.
    std::int64_t coast_timeout_ms = 700;
    // The acquiring counterpart, selected by state in on_pass()'s timeout
    // check: the miss-tolerant streak (see acquire_sigma_rate_deg) needs a
    // window long enough to bridge the measured 8-12-pass detection gaps,
    // and stretching the bridge is safe exactly where coast_timeout_ms is
    // not - acquiring never carries capture authority, so the worst case
    // is an anchor chained across a stall instead of re-anchored (the
    // three-consistent-accepts rule still gates the promotion). Locked and
    // coasting keep the tight 700 ms: that is where a stale identity could
    // authorise a commit.
    std::int64_t acquire_coast_timeout_ms = 1200;
    // A measurement implying a whole-cube rate faster than this PLUS the
    // currently tracked rate is a flip glitch (IPPE mirror, corner
    // mislabel), not physics - discarded unless this many CONSECUTIVE
    // outliers mutually agree (within the consistency angle,
    // gauge-resolved so detector re-keying between them doesn't break the
    // chain), which means the cube genuinely teleported (regrip behind an
    // occluder) and the track hard re-anchors as a new epoch instead of
    // coasting into a stale pose. The ceiling is relative to |omega|
    // because the measured jump is expected step + glitch: a flat ceiling
    // gates the SUM, so a legitimate fast tumble (120 deg/s is a step plus
    // two endpoints' worth of detector noise inside one 33 ms interval)
    // brushes it and drops lock, while the glitch budget the gate actually
    // owns - the UNEXPECTED component - never changed.
    float outlier_omega_deg_s = 400.0F;
    int outlier_reanchor_count = 3;
    float outlier_consistency_deg = 20.0F;
    // Raw-observation plausibility ceiling: how far a detection's
    // model->display pose may sit from the canonical camera-facing pose
    // before it is discarded as a solver artifact. Detector geometry
    // bounds every REAL observation: the model frame's near face points at
    // the camera by construction, so a face-path pose is within ~62
    // degrees of the frontal identity (45-degree in-plane labelling window
    // plus tilt) and a corner-path pose within ~85 (the isometric model
    // pose plus tolerance). A 180-degree mirror flip lands at 115+. This
    // is the only gate that can catch a flip whose best gauge composition
    // happens to fall INSIDE the basin threshold (theta and implied rate
    // both look sane there - the flip mimics a plausible re-key), and it
    // also keeps a flip from anchoring a garbage epoch while lost, which
    // no prediction-relative gate can do.
    float obs_pose_ceiling_deg = 100.0F;
    // Angular-rate EMA: blend of each measured rate into omega, and
    // per-pass decay while coasting (an unobserved cube in a hand slows;
    // extrapolating full speed through a long gap overshoots).
    float omega_blend = 0.3F;
    float omega_decay = 0.9F;
    // ACCEPTED observations (anchor included) before acquiring promotes to
    // locked. Not consecutive PASSES: while acquiring, a miss coasts the
    // streak instead of killing it (see miss()), so the accepts may be
    // separated by short prediction-bridged gaps. Mutual consistency is
    // still enforced - every accept passes the basin gate and the
    // 15-degree margin gate against the COASTED prediction, so the accepts
    // are geometrically chained by construction and the sigma cap/acquire
    // timeout bound how far apart links may sit.
    // Calibrated 3->2 on live1: the oblique-grip session delivers
    // detections in short bursts, and demanding a third accept spent whole
    // bursts on acquisition that never promoted (locked passes 144 -> 173
    // from this knob alone). Two accepts = anchor plus one gate-chained
    // confirmation; the identity-certainty bar (posterior_lock_ratio) is
    // untouched and is what actually authorises commits, and the sweep's
    // wrong-identity counter stayed zero at 2 across every candidate.
    int acquire_frames = 2;
    // Identity-certainty bar: the no-slip belief must lead every slip
    // hypothesis by ln(this) before downstream may treat identity as
    // commit-grade. The same ratio, accumulated AGAINST the track, force-
    // loses it (colour as veto, never as identity).
    float posterior_lock_ratio = 100.0F;
    // Centre-Lab likelihood width for the belief update, and the fixed
    // per-observation log-likelihood granted to a hypothesis mapping onto
    // a face with no learned reference yet (roughly a 2-sigma match:
    // unknown faces neither win nor lose big against a well-matched one).
    float centre_sigma_lab = 12.0F;
    float unknown_face_log_likelihood = -2.0F;
    // Per-observation and total log-odds clamps: one wild Lab sample must
    // not decide the posterior alone, and a long clean run must not bank
    // so much evidence that a real slip takes seconds to surface.
    // The penalty ceiling must sit BELOW ln(posterior_lock_ratio) + the
    // unknown-face floor or the first clamp is a dead letter: at the
    // original 8, one >= 4-sigma centre sample (specular hit, motion blur)
    // cost identity 8 while every unknown-face hypothesis took the flat -2,
    // a -6 margin swing that crossed the +-ln(100) = 4.6 veto in a SINGLE
    // observation - the live3 post-mortem's exact signature, killing 21 of
    // 43 epochs that never slipped. At 4 the worst single-observation
    // swing is 4 (against a perfectly matching learned slip reference), so
    // a veto always needs at least two consecutive agreeing bad samples.
    float max_obs_log_penalty = 4.0F;
    float belief_clamp = 30.0F;
    // EMA blend for the per-face centre-Lab references the likelihoods
    // compare against (learned online within the epoch - the tracker has
    // no access to, and must not depend on, the capture slot store).
    float ref_blend = 0.3F;
    // A known reference only EMA-refreshes from samples within this many
    // centre_sigma_lab of it. The belief-margin learning guard alone
    // cannot stop laundering: the in-plane stabilizer hypotheses score
    // bit-identically to identity on single-centre evidence, pinning the
    // full margin at exactly zero, so under a sustained contradiction the
    // guard kept learning and the reference EMA-walked toward the
    // contradicting colour - geometrically shrinking identity's penalty
    // and stretching the slip veto toward never firing. Three sigma
    // tracks genuine gradual lighting drift (a bounded step per sample,
    // the reference follows) while refusing to be taught by any sample
    // that contradicts what it is the reference OF.
    float ref_consistency_sigma = 3.0F;
    // Rate-math dt clamps (seconds): a duplicate timestamp must not divide
    // by zero and a multi-second stall must not extrapolate to nonsense.
    float min_dt_s = 0.005F;
    float max_dt_s = 0.5F;
};

enum class TrackState : std::uint8_t { lost, acquiring, locked, coasting };

// Why the pass produced (or refused) a measurement - the TRACK log's and
// the tests' window into the gate machinery.
enum class TrackGate : std::uint8_t {
    accepted,        // measurement adopted
    anchored,        // lost -> acquiring on this observation (new epoch)
    no_observation,  // nothing to gate this pass
    basin,           // residual angle over min(basin_ceiling, sigma + interframe)
    margin,          // runner-up element too close to disambiguate
    omega_outlier,   // implied rate over the glitch ceiling, chain not complete
    implausible,     // raw pose outside detector geometry: mirror artifact
    slip_evidence,   // colour belief accumulated against the track: force-lost
};

// Everything the GUI needs from one pass, by value: current state and
// orientation for display, plus per-pass diagnostics for the TRACK log and
// the calibration sweeps. Fields marked (resolved) are only meaningful
// when the pass had an observation that reached resolution.
struct TrackerPassOutput {
    TrackState state = TrackState::lost;
    TrackGate gate = TrackGate::no_observation;
    Quat q_track{1.0F, 0.0F, 0.0F, 0.0F};
    float sigma_deg = 0.0F;
    int lattice_index = -1;    // (resolved) element S
    float theta_deg = 0.0F;    // (resolved) residual to prediction
    float margin_deg = 0.0F;   // (resolved) runner-up separation
    int quarter_turn_k = -1;   // (resolved) S's in-plane quarter turn
    // The camera-facing body face - reported ONLY while locked or
    // coasting. Lost and acquiring claim no identity, ever: acquiring has
    // an orientation but its identity has not survived the streak yet.
    std::optional<cube::model::Face> front_face;
    // log_belief[no-slip] minus the best slip hypothesis; >= ln(ratio)
    // makes identity_certain. Negative means evidence is accumulating
    // AGAINST the track.
    float belief_margin = 0.0F;
    bool identity_certain = false;
    // Face-scoped certainty (see identity_certain_for), indexed by
    // model::Face: identity leads every hypothesis that would MOVE body
    // face f to a different face. THE commit bar for capturing face f -
    // the full-24 bar above is structurally unreachable on single-centre
    // evidence (the in-plane stabilizers of the one observed face score
    // bit-identically to identity forever), while the quotiented bar is
    // crossed by a few colour accepts of a single frontal burst, and the
    // quotiented-out hypotheses are provably harmless to a commit of f
    // (an in-plane slip only rotates f's stickers: aggregation keys cells
    // through the resolved element, assembly searches all per-face
    // rotations, and router verification - centre + census - is
    // rotation-invariant).
    std::array<bool, 6> identity_certain_face{};
    int epoch = 0;
    std::int64_t dt_ms = 0;  // since the previous pass (0 on the first)
};

// One analysis pass's tracker-relevant facts, sampled by the caller from
// the SAME pass (pose and timestamps must share a frame - correlating
// separate queues is how off-by-one-frame bugs are born).
struct TrackerPassInput {
    std::int64_t t_ms = 0;  // the frame's mailbox timestamp (steady clock)
    // The pass's pose orientation, ONLY when orientation_valid - an
    // invalid pose must be absent, not identity.
    std::optional<Quat> q_obs;
    // Which observation family produced q_obs: selects the obs->body cell
    // table (face path vs corner path model frames differ).
    bool corner_view = false;
    // The stability tracker's motion score (advisory; feeds sigma
    // inflation while coasting).
    float motion_score = 0.0F;
    // Centre Lab of each face visible in the SAME observation the pose
    // came from: face path fills [0] with the near-face centre; corner
    // path fills [0..2] in {top, lower_left, lower_right} view order.
    // Purely verification evidence for the slip belief - passes without
    // colour (count 0) track geometrically just fine.
    int centre_lab_count = 0;
    std::array<std::array<float, 3>, 3> centre_lab{};
};

class OrientationTracker {
public:
    explicit OrientationTracker(const TrackerConfig& config = {});

    // Exactly one call per analysis pass, observation or not - the pass
    // cadence IS the tracker's clock for sigma inflation and coasting.
    TrackerPassOutput on_pass(const TrackerPassInput& input);

    // External evidence says the track is wrong (e.g. capture-time census
    // conflicts): drop to lost immediately. The next accepted observation
    // anchors a fresh epoch.
    void force_lost() noexcept;

    // Full re-initialisation including the epoch counter - a new scan
    // session, not a mid-scan loss.
    void reset() noexcept;

    [[nodiscard]] TrackState state() const noexcept { return state_; }
    [[nodiscard]] const Quat& orientation() const noexcept { return q_track_; }
    [[nodiscard]] float sigma_deg() const noexcept { return sigma_deg_; }
    [[nodiscard]] int epoch() const noexcept { return epoch_; }
    [[nodiscard]] float belief_margin() const noexcept;
    [[nodiscard]] bool identity_certain() const noexcept;
    // Face-scoped margin/certainty: identity minus the best hypothesis
    // over {j : kFacePermutation[j][face] != face} - the hypotheses under
    // which a capture keyed to `face` would land on the WRONG physical
    // face. Quotients out `face`'s stabilizer (identity plus the three
    // slips that only rotate it in-plane), which is exactly the subgroup
    // single-centre evidence can never separate and exactly the one a
    // commit of `face` is invariant to (see TrackerPassOutput's field
    // comment for the harmlessness argument).
    [[nodiscard]] float belief_margin_for(cube::model::Face face) const noexcept;
    [[nodiscard]] bool identity_certain_for(cube::model::Face face) const noexcept;

private:
    void anchor(const Quat& q_obs, const TrackerPassInput& input, TrackerPassOutput& out);
    void accept(const Quat& q_meas, int lattice_index, const TrackerPassInput& input, TrackerPassOutput& out);
    void miss(const Quat& q_pred, const TrackerPassInput& input, TrackerPassOutput& out, TrackGate gate);
    void to_lost() noexcept;
    // Returns false when the accumulated belief flipped against the track
    // (slip detected) - the caller must force-lose instead of promoting.
    [[nodiscard]] bool update_belief_and_refs(int lattice_index, const TrackerPassInput& input);
    // Stamps the post-transition state/identity fields every exit path
    // shares (state, orientation, belief, front face).
    void finish_output(TrackerPassOutput& out) const noexcept;

    TrackerConfig config_;
    TrackState state_ = TrackState::lost;
    Quat q_track_{1.0F, 0.0F, 0.0F, 0.0F};
    float sigma_deg_ = 0.0F;
    // Angular rate EMA, rotation-vector form (deg/s, display frame),
    // multi-axis by construction.
    std::array<float, 3> omega_deg_s_{};
    int accept_streak_ = 0;
    int epoch_ = 0;

    bool has_pass_time_ = false;
    std::int64_t last_pass_t_ms_ = 0;
    Quat q_last_accept_{1.0F, 0.0F, 0.0F, 0.0F};
    std::int64_t last_accept_t_ms_ = 0;

    // Outlier chain (see outlier_omega_deg_s): the composed orientation of
    // the latest outlier run and how many consecutive outliers agreed.
    int outlier_count_ = 0;
    Quat outlier_q_{1.0F, 0.0F, 0.0F, 0.0F};

    // 24-slip log-belief (epoch-local): log_belief_[j] is the accumulated
    // log-likelihood of "the body labels are really relabelled by lattice
    // element j" - kIdentityLatticeIndex is the no-slip hypothesis. Kept
    // renormalised (max entry 0) and clamped (see belief_clamp).
    std::array<float, kLatticeSize> log_belief_{};
    // Per-hypothesis "carries real evidence" bit: set once the hypothesis
    // has been scored against a LEARNED face reference at least once. Only
    // a ref-backed hypothesis may win the slip veto: an unknown-floor
    // hypothesis's lead is pure floor arithmetic (identity got penalised,
    // the unknown floor didn't), which is evidence that identity's samples
    // were noisy, not that any particular slip happened - live3 lost 21
    // epochs to exactly that non-evidence before the bit existed.
    std::array<bool, kLatticeSize> hyp_ref_backed_{};
    // Per-body-face centre-Lab references learned within the epoch; the
    // likelihood evidence the belief update scores hypotheses against.
    std::array<std::array<float, 3>, 6> face_ref_{};
    std::array<bool, 6> face_ref_known_{};
};

}  // namespace cube::vision
