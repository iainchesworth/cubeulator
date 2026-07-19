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
struct TrackerConfig {
    // Acceptance ceiling on the resolved element's residual angle: the
    // lattice basin half-width is 45 degrees, minus a 12-degree snap
    // margin. Fixed by geometry, not sweepable.
    float basin_ceiling_deg = 33.0F;
    // Per-pass motion budget: with sigma at zero (fresh accept last pass)
    // the acceptance threshold is min(basin_ceiling, sigma + this).
    float max_interframe_deg = 25.0F;
    // Refuse any resolution whose runner-up element is closer than this to
    // the best - the prediction cannot actually disambiguate them.
    float gauge_margin_deg = 15.0F;
    // Uncertainty inflation per pass without an accepted measurement, plus
    // a motion-score term (the stability tracker's score, ~0.02 when
    // steady): faster apparent scene motion means the coasted prediction
    // decays faster.
    float coast_sigma_rate_deg = 4.0F;
    float motion_sigma_gain_deg = 40.0F;
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
    // Consecutive accepted passes before acquiring promotes to locked.
    int acquire_frames = 3;
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
    float max_obs_log_penalty = 8.0F;
    float belief_clamp = 30.0F;
    // EMA blend for the per-face centre-Lab references the likelihoods
    // compare against (learned online within the epoch - the tracker has
    // no access to, and must not depend on, the capture slot store).
    float ref_blend = 0.3F;
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
    // Per-body-face centre-Lab references learned within the epoch; the
    // likelihood evidence the belief update scores hypotheses against.
    std::array<std::array<float, 3>, 6> face_ref_{};
    std::array<bool, 6> face_ref_known_{};
};

}  // namespace cube::vision
