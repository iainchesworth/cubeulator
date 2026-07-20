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
    // lattice basin half-width is 45 degrees, minus a snap margin. The
    // plan fixed this at 33 ("geometry, not sweepable"), but the live3
    // post-mortem showed the 12-degree snap margin was buying safety the
    // margin gate already provides while refusing exactly the poses a
    // quarter turn transits: of 144 basin/margin refusals, 24 sat in the
    // 30-40 degree bucket (per-gap steps of a casual turn resumed after a
    // detection gap), and the 33-cap meant no amount of honest sigma
    // could ever open the threshold to them. 40 keeps a 5-degree snap
    // margin inside the basin, the runner-up margin gate still refuses
    // every genuinely ambiguous resolution near the midpoint, and the
    // sigma cap (35) still forces lost before the threshold could grow
    // past this ceiling on its own. Sweepable now - the [.track-sweep]
    // wrong-identity counter is the hard constraint on moving it.
    float basin_ceiling_deg = 40.0F;
    // Per-pass motion budget: with sigma at zero (fresh accept last pass)
    // the acceptance threshold is min(basin_ceiling, sigma + this).
    // Calibrated 25->30 on live1: the plan's 25 assumed a ~33 ms frame
    // cadence, but an analysis PASS is ~96 ms, so a casual wrist turn
    // produces bigger per-pass steps than budgeted and 25 was breaking
    // accept chains with basin refusals (59 -> 49 on the clip). The knock-
    // on effect dominates the direct one: unbroken chains keep epochs
    // alive long enough for colour belief to reach the certainty bar
    // (commit-grade passes 0 -> 56). Still under the basin ceiling, and
    // the implied whole-cube rate at pass cadence (~310 deg/s) stays
    // below the omega glitch gate, so every safety ordering is preserved.
    float max_interframe_deg = 30.0F;
    // Refuse any resolution whose runner-up element is closer than this to
    // the best - the prediction cannot actually disambiguate them.
    float gauge_margin_deg = 15.0F;
    // Uncertainty inflation per pass without an accepted measurement, plus
    // a motion-score term (the stability tracker's score, ~0.02 when
    // steady): faster apparent scene motion means the coasted prediction
    // decays faster. Held at 4 - the round-2 corpus sweep briefly took this
    // to 8 to mask a wrong-identity event, but the four-clip replay showed
    // that was the wrong lever: 8 forces lost after ~4 missed passes, which
    // discards the occlusion coasting the capture layer depends on (a hand
    // regrip is a 5-8 pass dropout) AND still left one certain_slip on
    // live4. The false slips are a COLOUR artefact, so the fix is the
    // colour swing cap (see max_obs_log_penalty) plus a calmer rate
    // estimate (see omega_blend); with those, 4 coasts through the measured
    // dropouts and certain_slip is zero on all four clips. At 4 the sigma
    // cap (35) is reached at ~9 quiet passes, still strictly before the
    // basin threshold could open onto a neighbouring element.
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
    // Calibrated 0.3->0.2 in the round-2 four-clip reconciliation: a less
    // reactive rate EMA is the actual wrong-identity lever. An over-eager
    // omega overshoots on the pass after a fast turn, so the next coasted
    // prediction points slightly off the true pose, the resolution lands on
    // an in-plane-rotated element, and its centre colour then contradicts
    // the held face - a false slip against a commit-grade identity. 0.2
    // keeps enough rate to extrapolate real turns (locked+coasting fraction
    // and capture throughput hold) while calming that overshoot; combined
    // with omega_decay 0.8 and the restored colour swing cap it takes
    // certain_slip to zero on all four clips. 0.15 over-damps and lets a
    // lagged prediction re-introduce a live4 slip; 0.3 leaves a live1 one.
    float omega_blend = 0.2F;
    // 0.9->0.8 in the round-2 sweep: bleeds the coasted rate faster, so a
    // hand that slows behind an occluder is not extrapolated at full turn
    // speed - trims the post-turn overshoot the omega EMA above fights.
    float omega_decay = 0.8F;
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
    // a face with no learned reference yet. These are the round-2 COVERAGE
    // levers (12->16, -2->-3), and the four-clip replay confirms they are
    // safe - they certify faces, they do not veto. 16 tolerates the oblique
    // grip's paler, off-axis centre samples so a real face still matches
    // its own reference; -3 makes an unlearned face cheaper to hypothesise,
    // so the 20 in-plane-stabiliser hypotheses of the observed face sit at
    // -3 each and a quarter turn onto a fresh face reaches face-scoped
    // certainty in two colour accepts instead of three (distinct commit
    // faces and capture-grade passes rise on every clip). The veto is
    // governed separately (see max_obs_log_penalty); widening the floor
    // here does not weaken it.
    float centre_sigma_lab = 16.0F;
    float unknown_face_log_likelihood = -3.0F;
    // Per-observation and total log-odds clamps: one wild Lab sample must
    // not decide the posterior alone, and a long clean run must not bank
    // so much evidence that a real slip takes seconds to surface.
    // This is THE veto swing cap, and the round-2 four-clip replay pinned
    // it back to 4 after the sweep briefly tried 2. It bounds the advantage
    // one contradicting centre sample can grant a ref-backed slip
    // hypothesis (its log-likelihood minus identity's - see slip_evidence_).
    // The cap must sit under the veto bar ln(posterior_lock_ratio) = 4.6 so
    // no single observation can veto: at the original 8, one >= 4-sigma
    // sample cost identity 8 against a matched slip reference, crossing the
    // veto in ONE observation (the live3 post-mortem, 21 of 43 clean epochs
    // killed). At 4 the worst single swing is 4 < 4.6, so a veto always
    // needs at least two consecutive agreeing ref-backed samples. Lowering
    // it further to 2 does NOT buy safety - it SLOWS the veto (a real slip
    // then takes ~2x the samples to surface), which is why the sweep had to
    // over-inflate coasting to compensate; 4 keeps the veto both prompt on
    // genuine slips and immune to single-sample noise, and with omega_blend
    // 0.2 the false slips it was chasing never fire (certain_slip == 0 on
    // all four clips).
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
    // --- Epoch bridging (see anchor()) -------------------------------------
    // A re-anchor mints new body labels, so the capture layer must
    // re-RECOGNISE a stored face before it may file a new one. That is the
    // dominant refusal on the corpus: live5 registration insufficient 9 of
    // 22 commit attempts, live4 4 of 12 - the user is forced to re-show a
    // known face after every dropout before any new face can land. Bridging
    // lets the GAUGE survive a loss the TRACK does not: at the anchor, the
    // new epoch's labels are compared against the dying epoch's last
    // accepted pose over the lattice, and when the geometry proves a unique
    // relabelling the router folds it into its existing gauge instead of
    // starting a registration search from nothing.
    //
    // WHERE THIS IS MEASURED AND WHY IT IS HERE AND NOT AT THE BASIN GATE.
    // The obvious version of this idea - admit a refused observation as a
    // continuation when some lattice element explains it - is BOTH a no-op
    // and unsafe, and the boundary census says so twice over. It is a no-op
    // because resolve_alignment already minimises over all 24 elements, so
    // the theta the basin gate tests IS the best lattice-composed residual;
    // there is no untried element. It is unsafe because best-vs-runner-up
    // separation degrades monotonically with theta (adjacent elements are
    // 90 degrees apart, so at the 45-degree basin midpoint the runner-up
    // ties): over 351 measured basin refusals the median separation is 11.1
    // degrees at theta 40-45 and 3.3 at 55-65, and only 42% clear the
    // existing 15-degree margin bar. Admitting those is silent relabelling,
    // which is strictly worse than a re-anchor. The premise was false as
    // well: of 75 measured epoch boundaries, ZERO are a quarter turn, corner
    // roll or flip - the maximum lattice-reduced rotation at any boundary is
    // 59.9 degrees.
    //
    // At the RE-ANCHOR the same geometry is unambiguous, which is the whole
    // reason the bridge lives here. Best/runner-up over the 75 boundaries:
    //   best < 30 deg  (n=25): separation min 30.3, median 42.5
    //   best 30-45 deg (n=33): separation min  0.7, median 22.0
    //   best 45-60 deg (n=17): separation min  0.1, median  6.8
    // Sub-basin boundaries are separable and the 45-60 band is a genuine
    // tie that must be refused wholesale. Nothing here relaxes a gate: the
    // track still loses and still re-anchors exactly as before, so every
    // coast-through-dropout guarantee is untouched by construction. The
    // bridge only ever carries a gauge across a boundary the geometry
    // proves; when it cannot prove one it publishes nothing and the router
    // falls back to the registration search unchanged.
    //
    // Ceiling: the basin half-width. Above it the runner-up separation
    // collapses (median 6.8, and 0 of 17 boundaries clear any usable bar),
    // so no amount of margin makes the 45-60 band safe.
    float bridge_max_deg = 45.0F;
    // Centre-Lab agreement radius for the bridge's colour check - the
    // constant that makes bridging SOUND rather than merely unique, and the
    // reason the geometry above is not shipped on its own.
    //
    // THE DEGENERACY, because it is subtle and it invalidates the obvious
    // version of this feature. The 24 candidate orientations q_obs * L are
    // pairwise at least 90 degrees apart (the lattice is, and left
    // multiplication is an isometry), so AT MOST ONE can sit within 45
    // degrees of the dying epoch's last accepted pose: the resolution is
    // geometrically unique and its margin is enormous, essentially always
    // near 90. That uniqueness is real and it is also not the property that
    // matters. It says which lattice element best explains the observation
    // ASSUMING the cube is still where the old epoch last saw it - and the
    // assumption is the entire content of the claim. A cube that genuinely
    // quarter-turned during the dropout lands exactly on another lattice
    // element, so it presents an equally unique, equally wide-margin
    // resolution that is confidently WRONG by 90 degrees.
    //
    // This is not a corner case. A quarter turn during a detection dropout
    // is the single most likely thing to happen at a scan boundary - it is
    // what the user does to show the next face, and the turn is exactly
    // when tracking drops out. Nor can any measurement of boundary angles
    // taken MODULO THE LATTICE see it: reducing mod 24 maps a genuine
    // quarter turn onto zero, so a census of lattice-reduced boundary
    // rotations reports "the cube barely moved" for precisely the case that
    // breaks the method. Geometry alone cannot separate the two hypotheses,
    // because the discriminating evidence - what the cube did while
    // unobserved - is what the dropout destroyed.
    //
    // So the bridge asks colour, which is degenerate in a DIFFERENT
    // direction and therefore actually independent: the candidate
    // relabellings name different old-epoch faces, and the dying epoch has
    // already learned centre-Lab references for the faces it saw. Geometry
    // proposes the relabelling and colour may veto it - the same division
    // of labour the rest of this class runs on (colour is verification
    // evidence, never identity), and the same one the router applies on the
    // write path. A bridge with no colour to check refuses.
    //
    // 25 Lab matches RoutingConfig::same_face_centre_lab and is bounded by
    // the same argument: the closest legitimate centre pair on a cube
    // (red/orange) is ~27 Lab apart, so a radius at or above that could not
    // tell them apart and the check would stop discriminating exactly where
    // it is needed.
    float bridge_centre_lab = 25.0F;
    // Uniqueness bar. At 20 degrees, 45 of the 75 boundaries bridge and
    // every ambiguous one is refused; all 25 sub-30-degree boundaries clear
    // it with at least 30.3 degrees to spare. This is the safety constant:
    // a bridge that picked the wrong element would silently relabel every
    // later capture of the epoch, so the bar refuses rather than guesses -
    // the same policy the margin gate applies to acceptance.
    float bridge_margin_deg = 20.0F;
    // Wall-clock window since the dying epoch's last accepted measurement.
    // Matches acquire_coast_timeout_ms: beyond it the "the cube is still
    // roughly where the old epoch left it" premise the whole comparison
    // rests on stops being a premise and becomes a guess. Of the 75
    // boundaries, 37 fall within 1200 ms and 43 within 2000; started at
    // 1200 deliberately, to be widened only on evidence.
    //
    // Note this window does NOT extend anyone's capture authority - the
    // distinction from coast_timeout_ms, which is why that one stays at
    // 700. A coast timeout bounds how long a STALE IDENTITY may keep
    // authorising commits; this bounds how long a DEAD epoch's gauge may be
    // used to interpret a FRESH anchor, and the fresh anchor re-earns
    // acquisition and certainty from scratch either way.
    std::int64_t bridge_window_ms = 1200;

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
    // The constant-omega prediction this pass's observation was gated
    // AGAINST, before any adoption - diagnostics only, nothing in the
    // pipeline steers on it. Exposed because every question about a
    // REFUSAL ("how far off was it", "would some other lattice element
    // have explained it") is a question about the prediction, and
    // reconstructing it offline from the CSV is impossible: omega_ is an
    // EMA over accepted measurements only, so a coasted prediction cannot
    // be re-derived from the accepted poses alone. Equals q_track on the
    // lost/anchor paths, where there is no prediction to speak of.
    Quat q_pred{1.0F, 0.0F, 0.0F, 0.0F};
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
    // Epoch bridge, present ONLY on the pass that anchored a new epoch and
    // only when the geometry proved a unique relabelling (see the bridge_*
    // config constants). `bridge_relabel` is a kBodyRelabel/kFacePermutation
    // index carrying THIS epoch's body labels into `bridge_from_epoch`'s:
    // face f of the new epoch is face kFacePermutation[bridge_relabel][f] of
    // the old one, and its stickers move by kBodyRelabel[bridge_relabel][f].
    //
    // Absent means "no claim", never "identity" - a consumer must treat the
    // two differently, because identity is a positive assertion that the
    // labels did not move and absence is the refusal to assert anything.
    // The tracker's own state is entirely unaffected either way: the epoch
    // really did die and really did re-anchor, and nothing downstream of
    // this field can revive it.
    std::optional<int> bridge_relabel;
    int bridge_from_epoch = 0;
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
    // Colour veto on a proposed epoch bridge (see bridge_centre_lab): does
    // every face this observation shows, named through `relabel` in the
    // DYING epoch's labels, agree with what that epoch learned those faces
    // look like? Requires at least one face to be checkable against a
    // learned reference - an unverifiable bridge is refused, not assumed.
    // Must be called BEFORE anchor() clears the epoch-scoped references.
    [[nodiscard]] bool bridge_colour_agrees(int relabel, const TrackerPassInput& input) const;
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
    // Per-hypothesis evidence-based advantage over identity, accumulated
    // ONLY on observations where the hypothesis itself was scored against
    // a LEARNED face reference (its log-likelihood minus identity's, both
    // from the same sample; clamped to +-belief_clamp). This - not the
    // log_belief_ margin - is what the slip veto judges: a hypothesis's
    // log_belief_ lead can be built entirely from floor arithmetic
    // (identity penalised by noisy samples while the unknown floor is
    // not), which is evidence that identity's samples were noisy, not
    // that any particular slip happened. live3 lost 21 epochs to exactly
    // that non-evidence, and a mere scored-once eligibility bit was not
    // enough either: a hypothesis whose lead accrued at the floor could
    // become veto-eligible retroactively by ONE later ref scoring. The
    // accumulator isolates the floor entirely - a real slip's signature
    // (samples matching the slipped-to face's reference while mismatching
    // identity's) earns positive advantage in two or three observations,
    // while any amount of wild noise earns none.
    std::array<float, kLatticeSize> slip_evidence_{};
    // Per-body-face centre-Lab references learned within the epoch; the
    // likelihood evidence the belief update scores hypotheses against.
    std::array<std::array<float, 3>, 6> face_ref_{};
    std::array<bool, 6> face_ref_known_{};
};

}  // namespace cube::vision
