#include <cube/vision/orientation_tracker.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace cube::vision {

namespace {

constexpr float kDegPerRad = 57.29577951308232F;

[[nodiscard]] Quat normalized(const Quat& q) noexcept {
    const float norm = std::sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
    if (norm <= 0.0F) {
        return {1.0F, 0.0F, 0.0F, 0.0F};
    }
    return {q[0] / norm, q[1] / norm, q[2] / norm, q[3] / norm};
}

[[nodiscard]] float lab_distance(const std::array<float, 3>& a, const std::array<float, 3>& b) noexcept {
    const float dl = a[0] - b[0];
    const float da = a[1] - b[1];
    const float db = a[2] - b[2];
    return std::sqrt(dl * dl + da * da + db * db);
}

}  // namespace

Quat quat_from_rotvec_deg(const std::array<float, 3>& rotvec_deg) noexcept {
    const float angle_deg =
        std::sqrt(rotvec_deg[0] * rotvec_deg[0] + rotvec_deg[1] * rotvec_deg[1] + rotvec_deg[2] * rotvec_deg[2]);
    if (angle_deg < 1e-6F) {
        return {1.0F, 0.0F, 0.0F, 0.0F};
    }
    const float half_rad = 0.5F * angle_deg / kDegPerRad;
    const float scale = std::sin(half_rad) / angle_deg;
    return {std::cos(half_rad), rotvec_deg[0] * scale, rotvec_deg[1] * scale, rotvec_deg[2] * scale};
}

std::array<float, 3> quat_to_rotvec_deg(const Quat& q) noexcept {
    // Short-representative log map: q and -q are the same rotation, and
    // only the w >= 0 cover gives the sub-180-degree rotation vector the
    // rate math wants.
    Quat s = q;
    if (s[0] < 0.0F) {
        s = {-s[0], -s[1], -s[2], -s[3]};
    }
    const float w = std::min(s[0], 1.0F);
    const float sin_half = std::sqrt(std::max(0.0F, 1.0F - w * w));
    if (sin_half < 1e-6F) {
        return {0.0F, 0.0F, 0.0F};
    }
    const float angle_deg = 2.0F * std::acos(w) * kDegPerRad;
    const float scale = angle_deg / sin_half;
    return {s[1] * scale, s[2] * scale, s[3] * scale};
}

OrientationTracker::OrientationTracker(const TrackerConfig& config) : config_(config) {}

float OrientationTracker::belief_margin() const noexcept {
    float best_slip = -config_.belief_clamp;
    for (std::size_t j = 0; j < kLatticeSize; ++j) {
        if (j == static_cast<std::size_t>(kIdentityLatticeIndex)) {
            continue;
        }
        best_slip = std::max(best_slip, log_belief_[j]);
    }
    return log_belief_[static_cast<std::size_t>(kIdentityLatticeIndex)] - best_slip;
}

bool OrientationTracker::identity_certain() const noexcept {
    return belief_margin() >= std::log(config_.posterior_lock_ratio);
}

float OrientationTracker::belief_margin_for(cube::model::Face face) const noexcept {
    float best_mover = -config_.belief_clamp;
    for (std::size_t j = 0; j < kLatticeSize; ++j) {
        // face's stabilizer (identity included) is quotiented out: under
        // those hypotheses a capture keyed to `face` still lands on the
        // right physical face, merely rotated in-plane - which every
        // downstream consumer is invariant to.
        if (kFacePermutation[j][static_cast<std::size_t>(face)] == face) {
            continue;
        }
        best_mover = std::max(best_mover, log_belief_[j]);
    }
    return log_belief_[static_cast<std::size_t>(kIdentityLatticeIndex)] - best_mover;
}

bool OrientationTracker::identity_certain_for(cube::model::Face face) const noexcept {
    return belief_margin_for(face) >= std::log(config_.posterior_lock_ratio);
}

void OrientationTracker::reset() noexcept {
    to_lost();
    epoch_ = 0;
    has_pass_time_ = false;
    last_pass_t_ms_ = 0;
    sigma_deg_ = 0.0F;
}

void OrientationTracker::force_lost() noexcept { to_lost(); }

void OrientationTracker::to_lost() noexcept {
    // Belief and colour references are epoch-scoped (the body frame they
    // describe dies with the track); they are cleared at the NEXT anchor
    // rather than here only so post-mortem getters still read the final
    // evidence state.
    state_ = TrackState::lost;
    omega_deg_s_ = {0.0F, 0.0F, 0.0F};
    accept_streak_ = 0;
    outlier_count_ = 0;
}

TrackerPassOutput OrientationTracker::on_pass(const TrackerPassInput& input) {
    TrackerPassOutput out;

    const bool had_time = has_pass_time_;
    const std::int64_t prev_t_ms = last_pass_t_ms_;
    has_pass_time_ = true;
    last_pass_t_ms_ = input.t_ms;
    out.dt_ms = had_time ? std::max<std::int64_t>(0, input.t_ms - prev_t_ms) : 0;

    // Wall-clock loss FIRST, before any resolution: per-pass sigma
    // inflation models "many quick passes with nothing accepted", not one
    // long gap - a cube can cross several basins inside a single stalled
    // interval, so a prediction older than the timeout is not a prediction
    // at all. Every non-lost state is covered; the window is selected by
    // state because the two states bound DIFFERENT risks. Locked/coasting
    // keep the tight window: a stale prediction there carries an identity
    // with capture authority, and a cube that crossed a basin during the
    // stall would resolve against it and commit-grade state would survive
    // a jump it never observed. Acquiring gets the longer window (see
    // acquire_coast_timeout_ms): it claims no identity and cannot commit,
    // and the miss-tolerant streak exists precisely to bridge multi-pass
    // detection gaps - killing it at 700 ms would rebuild the anchor churn
    // the bridge was added to fix. Falling through to the lost branch lets
    // the same pass's observation re-anchor immediately (loss is cheap by
    // design).
    const std::int64_t stale_timeout_ms =
        state_ == TrackState::acquiring ? config_.acquire_coast_timeout_ms : config_.coast_timeout_ms;
    if (state_ != TrackState::lost && input.t_ms - last_accept_t_ms_ > stale_timeout_ms) {
        to_lost();
    }

    // Raw-pose plausibility (see obs_pose_ceiling_deg): a pose no detector
    // geometry can produce is a mirror artifact whatever the prediction
    // thinks of it, and it must not anchor an epoch either - checked once
    // here so the lost branch and the tracking path agree.
    const bool obs_plausible =
        !input.q_obs ||
        quat_angle_deg(*input.q_obs, Quat{1.0F, 0.0F, 0.0F, 0.0F}) < config_.obs_pose_ceiling_deg;

    if (state_ == TrackState::lost) {
        if (input.q_obs && obs_plausible) {
            anchor(*input.q_obs, input, out);
        } else {
            out.gate = input.q_obs ? TrackGate::implausible : TrackGate::no_observation;
        }
        finish_output(out);
        return out;
    }

    // Constant-angular-velocity prediction in the display frame. dt is
    // clamped for the rate math only - the timeout above already handled
    // "too stale to predict".
    const float dt_s = std::clamp(static_cast<float>(out.dt_ms) / 1000.0F, config_.min_dt_s, config_.max_dt_s);
    const std::array<float, 3> step{omega_deg_s_[0] * dt_s, omega_deg_s_[1] * dt_s, omega_deg_s_[2] * dt_s};
    const Quat q_pred = normalized(quat_multiply(quat_from_rotvec_deg(step), q_track_));
    out.q_pred = q_pred;

    if (!input.q_obs) {
        miss(q_pred, input, out, TrackGate::no_observation);
        finish_output(out);
        return out;
    }
    if (!obs_plausible) {
        // Discarded BEFORE resolution: a 180-degree flip's best gauge
        // composition can land inside the basin threshold at a modest
        // implied rate (it mimics a plausible re-key), so the
        // prediction-relative gates below cannot be trusted to see it.
        // The raw pose is the only place the flip is unmistakable.
        miss(q_pred, input, out, TrackGate::implausible);
        finish_output(out);
        return out;
    }

    const AlignmentResolution res = resolve_alignment(*input.q_obs, q_pred);
    out.lattice_index = res.lattice_index;
    out.theta_deg = res.theta_deg;
    out.margin_deg = res.margin_deg;
    const Quat q_meas =
        normalized(quat_multiply(*input.q_obs, kLattice24[static_cast<std::size_t>(res.lattice_index)]));

    // Implied-rate gate against the COMMITTED track (not the prediction -
    // the prediction already contains the rate being questioned). A flip
    // glitch implies a physically impossible instantaneous rate; a real
    // teleport (regrip behind the hand) produces CONSISTENT outliers, and
    // consistency is checked gauge-resolved so the detector re-keying
    // between two outlier frames doesn't break the chain. The ceiling is
    // outlier_omega + |omega|: the jump being measured is expected step
    // plus glitch, so the tracked rate must be budgeted for or a fast
    // legitimate tumble (whose step plus two endpoints' detector noise
    // already implies ~420 deg/s over one 33 ms interval) trips the gate
    // and drops lock (see obs_pose_ceiling_deg's config comment for the
    // division of labour with the plausibility gate).
    const float omega_mag_deg_s = std::sqrt(omega_deg_s_[0] * omega_deg_s_[0] + omega_deg_s_[1] * omega_deg_s_[1] +
                                            omega_deg_s_[2] * omega_deg_s_[2]);
    // The rate is judged over the span the jump actually ACCUMULATED - the
    // wall time since the last accepted measurement - not this pass's
    // frame interval. The residual to q_track_ is everything the coasted
    // prediction failed to anticipate since that accept, so dividing it by
    // one 65-130 ms dt manufactured impossible rates out of ordinary
    // mid-turn re-detections (live3: 45 omega-outlier refusals, ~35 of
    // which implied under the ceiling over their real span) and turned
    // this gate into a duplicate basin refusal with a LOWER threshold on
    // short-dt passes. Immediately after an accept the span IS the frame
    // interval, so the single-interval glitch behaviour the gate exists
    // for is unchanged. While an outlier CHAIN is active the per-pass dt
    // stays the denominator: a chain means the last observation already
    // implied an impossible rate over its own honest span, and letting
    // the span dilute the follow-ups would launder a genuine teleport's
    // gauge-absorbed residual (a 120-degree regrip resolves to a
    // 30-degree residual on a 90-degree slip - a silently wrong identity)
    // into an acceptable rate on the very pass that should have completed
    // the chain and re-anchored.
    const float accept_span_s = std::clamp(static_cast<float>(input.t_ms - last_accept_t_ms_) / 1000.0F,
                                           config_.min_dt_s, config_.max_dt_s);
    const float rate_span_s = outlier_count_ > 0 ? dt_s : accept_span_s;
    const float implied_deg_s = quat_angle_deg(q_meas, q_track_) / rate_span_s;
    if (implied_deg_s > config_.outlier_omega_deg_s + omega_mag_deg_s) {
        bool chained = false;
        if (outlier_count_ > 0) {
            const AlignmentResolution chain = resolve_alignment(*input.q_obs, outlier_q_);
            if (chain.theta_deg < config_.outlier_consistency_deg) {
                outlier_q_ =
                    normalized(quat_multiply(*input.q_obs, kLattice24[static_cast<std::size_t>(chain.lattice_index)]));
                ++outlier_count_;
                chained = true;
            }
        }
        if (!chained) {
            outlier_q_ = q_meas;
            outlier_count_ = 1;
        }
        if (outlier_count_ >= config_.outlier_reanchor_count) {
            // The cube genuinely IS somewhere else. The stale identity
            // must not survive the jump - the resolution that placed these
            // outliers was measured against a prediction the cube ignored,
            // so re-anchor as a fresh epoch and let the recovery layer
            // re-associate identity from content.
            to_lost();
            anchor(*input.q_obs, input, out);
            finish_output(out);
            return out;
        }
        miss(q_pred, input, out, TrackGate::omega_outlier);
        finish_output(out);
        return out;
    }
    outlier_count_ = 0;

    // Basin gate: the threshold opens with sigma while coasting but
    // saturates at the basin ceiling - by the time the +-45-degree basin
    // boundary could be in question, sigma's own cap has already forced
    // lost. Margin gate: a best element the runner-up nearly ties is a
    // resolution the prediction cannot actually disambiguate; coast
    // instead of guessing (the plan's identity policy in one line).
    const float threshold_deg = std::min(config_.basin_ceiling_deg, sigma_deg_ + config_.max_interframe_deg);
    if (res.theta_deg >= threshold_deg) {
        miss(q_pred, input, out, TrackGate::basin);
    } else if (res.margin_deg <= config_.gauge_margin_deg) {
        miss(q_pred, input, out, TrackGate::margin);
    } else {
        accept(q_meas, res.lattice_index, input, out);
    }
    finish_output(out);
    return out;
}

void OrientationTracker::anchor(const Quat& q_obs, const TrackerPassInput& input, TrackerPassOutput& out) {
    // The anchoring detection's model frame becomes the body frame, so the
    // gauge is the identity element BY DEFINITION - nothing to resolve.
    // Everything epoch-scoped restarts here: a new epoch means new body
    // labels, so beliefs and colour references from the old one would be
    // evidence about the wrong frame.
    ++epoch_;
    q_track_ = normalized(q_obs);
    q_last_accept_ = q_track_;
    last_accept_t_ms_ = input.t_ms;
    sigma_deg_ = 0.0F;
    omega_deg_s_ = {0.0F, 0.0F, 0.0F};
    accept_streak_ = 1;
    outlier_count_ = 0;
    state_ = TrackState::acquiring;
    log_belief_.fill(0.0F);
    slip_evidence_.fill(0.0F);
    face_ref_known_.fill(false);
    (void)update_belief_and_refs(kIdentityLatticeIndex, input);
    out.gate = TrackGate::anchored;
    out.lattice_index = kIdentityLatticeIndex;
    out.quarter_turn_k = 0;
}

void OrientationTracker::accept(const Quat& q_meas, int lattice_index, const TrackerPassInput& input,
                                TrackerPassOutput& out) {
    // Rate from the last ACCEPTED orientation over the wall time between
    // them - measuring across a coasted gap yields the average rate the
    // cube actually turned at, which is exactly what the next coast should
    // extrapolate.
    const float span_s = std::clamp(static_cast<float>(input.t_ms - last_accept_t_ms_) / 1000.0F, config_.min_dt_s,
                                    config_.max_dt_s);
    const std::array<float, 3> delta = quat_to_rotvec_deg(quat_multiply(q_meas, quat_conjugate(q_last_accept_)));
    for (std::size_t i = 0; i < 3; ++i) {
        omega_deg_s_[i] += config_.omega_blend * (delta[i] / span_s - omega_deg_s_[i]);
    }

    q_track_ = q_meas;
    q_last_accept_ = q_meas;
    last_accept_t_ms_ = input.t_ms;
    sigma_deg_ = 0.0F;

    // Colour verification BEFORE state promotion: when the accumulated
    // belief has flipped against the track, this measurement is
    // geometrically consistent with a WRONG identity, and the only honest
    // move is to lose the track - never to keep reporting it.
    if (!update_belief_and_refs(lattice_index, input)) {
        to_lost();
        out.gate = TrackGate::slip_evidence;
        return;
    }

    // The streak counts ACCEPTED observations, not consecutive passes: a
    // bridged miss while acquiring keeps the streak alive (see miss()), so
    // sparse burst detections can still finish acquisition. Consistency is
    // carried by the gates every accept just passed against the coasted
    // prediction, not by adjacency.
    ++accept_streak_;
    if (state_ == TrackState::acquiring && accept_streak_ < config_.acquire_frames) {
        // still acquiring
    } else {
        state_ = TrackState::locked;
    }
    out.gate = TrackGate::accepted;
    out.quarter_turn_k = lattice_index % 4;
}

void OrientationTracker::miss(const Quat& q_pred, const TrackerPassInput& input, TrackerPassOutput& out,
                              TrackGate gate) {
    out.gate = gate;
    // A miss coasts in EVERY tracking state now - acquiring included. The
    // original design dropped acquiring straight to lost ("the streak's
    // whole point is consecutive agreement"), and the live1 census showed
    // what that costs on a real detector: detections arrive in bursts of
    // 1-2 separated by 2-8 pass gaps (93 of 169 inter-observation gaps
    // were a single pass), so 68 anchors yielded only ~17 locks - the
    // acquisition rule discarded most bursts. Consecutiveness was never
    // the safety mechanism anyway: every accept is chained to the coasted
    // prediction by the basin + margin gates, so three accepts within the
    // coast envelope are mutually consistent by construction, however many
    // bridged misses sit between them. What DOES change by state is the
    // envelope's growth rate and label: acquiring keeps its own state (it
    // must not report an identity, which locked->coasting would) and
    // inflates sigma at the slower acquire rate - it has no capture
    // authority, so a longer bridge risks only anchor churn.
    if (state_ != TrackState::acquiring) {
        state_ = TrackState::coasting;
    }
    // Advance along the prediction so the track keeps turning through the
    // gap, decay the rate (an unobserved cube in a hand slows), and
    // inflate uncertainty - by rate plus how much the scene is visibly
    // moving. The cap forces lost long before the basin gate could be
    // asked to judge a neighbouring element.
    q_track_ = q_pred;
    for (auto& component : omega_deg_s_) {
        component *= config_.omega_decay;
    }
    const float sigma_rate_deg =
        state_ == TrackState::acquiring ? config_.acquire_sigma_rate_deg : config_.coast_sigma_rate_deg;
    sigma_deg_ += sigma_rate_deg + config_.motion_sigma_gain_deg * std::max(0.0F, input.motion_score);
    if (sigma_deg_ > config_.max_coast_sigma_deg) {
        to_lost();
    }
}

bool OrientationTracker::update_belief_and_refs(int lattice_index, const TrackerPassInput& input) {
    // The reference-learning guard, hoisted out of the loop: teaching is
    // suspended while some hypothesis holds positive evidence-based
    // advantage (see slip_evidence_) - a slip candidate colour actually
    // voted for may be the truth, and references taught under it would
    // launder the slip into the evidence base. Deliberately NOT the full
    // log_belief_ margin: on real frames a run of blurred/specular centre
    // samples drives that margin negative through unknown-floor
    // arithmetic alone, and live3's replay showed a full-margin guard
    // wedging the epoch permanently - a newly shown face could never
    // learn its first reference (11 colour accepts of U, belief
    // flatlined), so no evidence could ever accumulate to recover.
    float best_advantage = 0.0F;
    for (std::size_t j = 0; j < kLatticeSize; ++j) {
        best_advantage = std::max(best_advantage, slip_evidence_[j]);
    }
    const bool may_learn = best_advantage <= 0.0F;
    if (input.centre_lab_count > 0) {
        const auto observed_faces = [&](std::size_t obs) {
            return input.corner_view ? kCornerObsToBody[static_cast<std::size_t>(lattice_index)][obs].body_face
                                     : kFaceObsToBody[static_cast<std::size_t>(lattice_index)].body_face;
        };
        const std::size_t count =
            std::min<std::size_t>(static_cast<std::size_t>(input.centre_lab_count), input.corner_view ? 3 : 1);
        for (std::size_t obs = 0; obs < count; ++obs) {
            const cube::model::Face f0 = observed_faces(obs);
            const std::array<float, 3>& lab = input.centre_lab[obs];
            // Score every slip hypothesis: under slip j the face we
            // geometrically label f0 is really kFacePermutation[j][f0],
            // so its centre should match THAT face's learned reference.
            // Identity's own likelihood is computed first - it is the
            // baseline each ref-scored hypothesis's advantage is measured
            // against on this same sample.
            const auto score = [&](std::size_t face_index) {
                float log_lik = config_.unknown_face_log_likelihood;
                if (face_ref_known_[face_index]) {
                    const float d = lab_distance(lab, face_ref_[face_index]) / config_.centre_sigma_lab;
                    log_lik = std::max(-config_.max_obs_log_penalty, -0.5F * d * d);
                }
                return log_lik;
            };
            const float id_log_lik = score(static_cast<std::size_t>(f0));
            for (std::size_t j = 0; j < kLatticeSize; ++j) {
                const auto hyp_face = static_cast<std::size_t>(kFacePermutation[j][static_cast<std::size_t>(f0)]);
                const float log_lik = score(hyp_face);
                log_belief_[j] += log_lik;
                // Evidence-based advantage: only a sample the hypothesis
                // was actually REF-SCORED on may move its veto credit -
                // floor observations contribute exactly nothing, in
                // either direction (see the member comment).
                if (j != static_cast<std::size_t>(kIdentityLatticeIndex) && face_ref_known_[hyp_face]) {
                    slip_evidence_[j] = std::clamp(slip_evidence_[j] + log_lik - id_log_lik, -config_.belief_clamp,
                                                   config_.belief_clamp);
                }
            }
            // Learn/refresh the no-slip reference only while no ref-backed
            // slip hypothesis leads (see may_learn above). The guard alone
            // is not enough - the stabilizer degeneracy pins the backed
            // margin at exactly zero on single-centre evidence - so a
            // KNOWN reference additionally refuses samples that contradict
            // it (see ref_consistency_sigma): drift may teach it stepwise,
            // a different colour may not teach it at all.
            if (may_learn) {
                const auto ref_face = static_cast<std::size_t>(f0);
                if (face_ref_known_[ref_face]) {
                    if (lab_distance(lab, face_ref_[ref_face]) <=
                        config_.ref_consistency_sigma * config_.centre_sigma_lab) {
                        for (std::size_t i = 0; i < 3; ++i) {
                            face_ref_[ref_face][i] += config_.ref_blend * (lab[i] - face_ref_[ref_face][i]);
                        }
                    }
                } else {
                    face_ref_[ref_face] = lab;
                    face_ref_known_[ref_face] = true;
                }
            }
        }
        // Renormalise (max entry 0) and clamp the spread so the posterior
        // stays bounded and a real slip can surface in bounded time.
        const float max_belief = *std::ranges::max_element(log_belief_);
        for (auto& value : log_belief_) {
            value = std::max(value - max_belief, -config_.belief_clamp);
        }
    }
    // Slip veto at the same ratio the lock uses, symmetric by design:
    // colour needs as much evidence to kill a track as to certify one -
    // and the evidence is the per-hypothesis ADVANTAGE accumulator, never
    // the log_belief_ margin (see slip_evidence_'s member comment). A
    // single-face epoch therefore can never be colour-vetoed, and a
    // noise-degraded margin cannot be retroactively weaponised by one
    // ref scoring: "identity's samples look noisy" degrades certainty
    // (the margin getters see the full 24), it never kills the track.
    float final_advantage = 0.0F;
    for (std::size_t j = 0; j < kLatticeSize; ++j) {
        final_advantage = std::max(final_advantage, slip_evidence_[j]);
    }
    return final_advantage < std::log(config_.posterior_lock_ratio);
}

void OrientationTracker::finish_output(TrackerPassOutput& out) const noexcept {
    out.state = state_;
    out.q_track = q_track_;
    if (out.q_pred[0] == 1.0F && out.q_pred[1] == 0.0F && out.q_pred[2] == 0.0F && out.q_pred[3] == 0.0F) {
        // Never predicted this pass (lost branch, or anchored): report the
        // adopted orientation so a consumer never mistakes the default for
        // a real prediction of the frontal pose.
        out.q_pred = q_track_;
    }
    out.sigma_deg = sigma_deg_;
    out.epoch = epoch_;
    out.belief_margin = belief_margin();
    out.identity_certain = identity_certain();
    for (std::size_t f = 0; f < out.identity_certain_face.size(); ++f) {
        out.identity_certain_face[f] = identity_certain_for(static_cast<cube::model::Face>(f));
    }
    if (state_ == TrackState::locked || state_ == TrackState::coasting) {
        out.front_face = nearest_face(q_track_);
    }
}

}  // namespace cube::vision
