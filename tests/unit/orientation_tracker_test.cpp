#include <catch2/catch_test_macros.hpp>

#include <cube/model/face.hpp>
#include <cube/vision/orientation_lattice.hpp>
#include <cube/vision/orientation_tracker.hpp>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <random>

using namespace cube::vision;
using cube::model::Face;

namespace {

// ---------------------------------------------------------------------------
// Synthetic ground-truth harness. The simulation owns a true body
// orientation q_true(t) advanced by an exact scripted angular velocity,
// and manufactures detector observations THE WAY THE REAL DETECTOR
// MISBEHAVES: the observation is q_true relabelled by whichever lattice
// gauge is nearest the current pose (so the gauge re-keys exactly at the
// 45-degree boundaries, the jump the tracker exists to absorb), then
// perturbed by Gaussian orientation noise. Every simulation starts at the
// identity orientation so the anchor gauge is the identity element and
// the tracker's body frame coincides with the simulation's - assertions
// can then compare q_track against q_true directly.
// ---------------------------------------------------------------------------

constexpr std::int64_t kDtMs = 33;
constexpr float kDtS = 0.033F;

[[nodiscard]] Quat axis_angle(std::array<float, 3> axis, float deg) {
    const float norm = std::sqrt(axis[0] * axis[0] + axis[1] * axis[1] + axis[2] * axis[2]);
    return quat_from_rotvec_deg({axis[0] / norm * deg, axis[1] / norm * deg, axis[2] / norm * deg});
}

// Distinct per-face centre Lab palette (roughly the classifier's reference
// colours) for the belief tests. model::Face order U,R,F,D,L,B.
constexpr std::array<std::array<float, 3>, 6> kPalette = {{
    {90.0F, -5.0F, 80.0F},   // U yellow
    {45.0F, 65.0F, 50.0F},   // R red
    {55.0F, -55.0F, 45.0F},  // F green
    {96.0F, 0.0F, 2.0F},     // D white
    {65.0F, 40.0F, 65.0F},   // L orange
    {35.0F, 25.0F, -60.0F},  // B blue
}};

struct Sim {
    OrientationTracker tracker;
    std::mt19937 rng{20260720};
    Quat q_true{1.0F, 0.0F, 0.0F, 0.0F};
    std::int64_t t_ms = 0;
    float noise_deg = 3.0F;
    bool feed_labs = false;
    // When set, the fed centre lab is this face's palette entry instead of
    // the truly camera-facing face's - the injected colour/geometry
    // contradiction the slip belief must catch.
    std::optional<Face> lab_override;
    // When set, the fed centre lab is this raw value - a wild sample
    // matching NO face's palette (specular burn, motion blur), the noise
    // the veto must ride out. Takes precedence over lab_override.
    std::optional<std::array<float, 3>> lab_raw_override;

    explicit Sim(const TrackerConfig& config = {}) : tracker(config) {}

    void advance(const std::array<float, 3>& omega_deg_s, float dt_s = kDtS) {
        const std::array<float, 3> step{omega_deg_s[0] * dt_s, omega_deg_s[1] * dt_s, omega_deg_s[2] * dt_s};
        q_true = quat_multiply(quat_from_rotvec_deg(step), q_true);
    }

    // Detector-realistic observation: relabelled by the nearest lattice
    // gauge (re-keys at 45-degree boundaries) plus orientation noise.
    [[nodiscard]] Quat observation() {
        const int s = quantize24(q_true).index;
        const Quat relabelled = quat_multiply(q_true, quat_conjugate(kLattice24[static_cast<std::size_t>(s)]));
        std::normal_distribution<float> angle_dist(0.0F, noise_deg);
        std::uniform_real_distribution<float> unit(-1.0F, 1.0F);
        std::array<float, 3> axis{unit(rng), unit(rng), unit(rng)};
        if (std::abs(axis[0]) + std::abs(axis[1]) + std::abs(axis[2]) < 1e-3F) {
            axis = {1.0F, 0.0F, 0.0F};
        }
        return quat_multiply(axis_angle(axis, angle_dist(rng)), relabelled);
    }

    TrackerPassOutput step_obs(std::optional<Quat> forced = std::nullopt) {
        t_ms += kDtMs;
        TrackerPassInput in;
        in.t_ms = t_ms;
        in.q_obs = forced ? *forced : observation();
        if (feed_labs) {
            in.centre_lab_count = 1;
            if (lab_raw_override) {
                in.centre_lab[0] = *lab_raw_override;
            } else {
                const Face shown = lab_override ? *lab_override : nearest_face(q_true);
                in.centre_lab[0] = kPalette[static_cast<std::size_t>(shown)];
            }
        }
        return tracker.on_pass(in);
    }

    TrackerPassOutput step_miss(float motion_score = 0.0F) {
        t_ms += kDtMs;
        TrackerPassInput in;
        in.t_ms = t_ms;
        in.motion_score = motion_score;
        return tracker.on_pass(in);
    }

    // Anchor + acquire until locked (acquire_frames accepted passes).
    void lock_on(int passes = 3) {
        for (int i = 0; i < passes; ++i) {
            step_obs();
        }
        REQUIRE(tracker.state() == TrackState::locked);
    }
};

[[nodiscard]] bool track_matches_truth(const Sim& sim, float tolerance_deg = 12.0F) {
    return quat_angle_deg(sim.tracker.orientation(), sim.q_true) < tolerance_deg;
}

}  // namespace

TEST_CASE("TrackerConfig defaults hold the live1-calibrated constants", "[tracker]") {
    // The plan's constants table as amended by the live1 calibration sweep
    // (see the TrackerConfig comment): max_interframe_deg 25->30 and
    // acquire_frames 3->2 are the calibrated values; everything else is
    // the table verbatim.
    const TrackerConfig config;
    // live3 recalibration: 40 (was the plan's 33) - see the config
    // comment; the margin gate carries the ambiguity refusals the extra
    // 7 degrees used to.
    CHECK(config.basin_ceiling_deg == 40.0F);
    CHECK(config.max_interframe_deg == 30.0F);
    CHECK(config.gauge_margin_deg == 15.0F);
    // Round-2 reconciliation: held at 4 (the sweep's 8 broke coasting and
    // still left a live4 slip - see the config comment). At 4 the coast-
    // through-dropout guarantee below holds.
    CHECK(config.coast_sigma_rate_deg == 4.0F);
    CHECK(config.acquire_sigma_rate_deg == 2.5F);
    CHECK(config.max_coast_sigma_deg == 35.0F);
    CHECK(config.coast_timeout_ms == 700);
    CHECK(config.acquire_coast_timeout_ms == 1200);
    CHECK(config.outlier_omega_deg_s == 400.0F);
    CHECK(config.outlier_reanchor_count == 3);
    // Round-2 reconciliation: 0.3->0.2 is the actual wrong-identity lever -
    // a calmer rate EMA stops the post-turn overshoot that fed false slips
    // (see the config comment). certain_slip is zero on all four clips.
    CHECK(config.omega_blend == 0.2F);
    // Round-2 corpus sweep: 0.8 bleeds coasted rate faster, trimming the
    // post-turn overshoot that fed the omega gate's refusals.
    CHECK(config.omega_decay == 0.8F);
    CHECK(config.acquire_frames == 2);
    CHECK(config.posterior_lock_ratio == 100.0F);
    // Round-2 corpus sweep COVERAGE levers: 16 tolerates the oblique grip's
    // paler centre Lab samples; -3 makes unlearned faces cheaper to
    // hypothesise so a quarter turn onto a fresh face does not lose to the
    // incumbent. Safe (they certify, they do not veto) - the four-clip
    // replay kept both.
    CHECK(config.centre_sigma_lab == 16.0F);
    CHECK(config.unknown_face_log_likelihood == -3.0F);
    // Held at 4 (the sweep's 2 only SLOWED the veto): 8 - 2 = 6 crossed the
    // +-ln(100) = 4.6 veto in ONE sample (the live3 post-mortem); 4 bounds
    // the single-observation swing under it so a veto needs at least two
    // consecutive agreeing ref-backed samples, while staying prompt on a
    // real slip (see the config comment).
    CHECK(config.max_obs_log_penalty == 4.0F);
}

TEST_CASE("lost claims no identity and anchoring starts an epoch as acquiring", "[tracker]") {
    Sim sim;
    const auto idle = sim.step_miss();
    CHECK(idle.state == TrackState::lost);
    CHECK(idle.gate == TrackGate::no_observation);
    CHECK_FALSE(idle.front_face.has_value());
    CHECK(idle.epoch == 0);

    const auto anchored = sim.step_obs();
    CHECK(anchored.state == TrackState::acquiring);
    CHECK(anchored.gate == TrackGate::anchored);
    CHECK(anchored.epoch == 1);
    // Acquiring has an orientation but must not claim an identity yet.
    CHECK_FALSE(anchored.front_face.has_value());

    // Second accept completes the (calibrated) two-accept streak: anchor
    // plus one gate-chained confirmation promotes to locked.
    const auto second = sim.step_obs();
    CHECK(second.state == TrackState::locked);
    REQUIRE(second.front_face.has_value());
    CHECK(*second.front_face == Face::F);  // anchored at identity: body F faces the camera
}

TEST_CASE("tracks a single-axis 90-degree tour reporting the correct front face", "[tracker]") {
    Sim sim;
    sim.lock_on();
    // 60 deg/s about the display x axis: F rotates away, U (pitching
    // forward) or D arrives depending on sign - 90 degrees over 45 passes.
    const std::array<float, 3> omega{60.0F, 0.0F, 0.0F};
    for (int pass = 0; pass < 45; ++pass) {
        sim.advance(omega);
        const auto out = sim.step_obs();
        REQUIRE(out.state == TrackState::locked);
        CHECK(track_matches_truth(sim));
        // Assert the identity wherever the true pose is decisively
        // frontal; near the 45-degree face handover the "correct" answer
        // is genuinely ambiguous under noise.
        if (frontal_deg(sim.q_true) < 30.0F) {
            REQUIRE(out.front_face.has_value());
            CHECK(*out.front_face == nearest_face(sim.q_true));
        }
    }
    // The tour ended with a different physical face toward the camera and
    // the tracker followed it there - identity carried by orientation, not
    // by anything the detector labelled.
    CHECK(nearest_face(sim.tracker.orientation()) == nearest_face(sim.q_true));
    CHECK(nearest_face(sim.q_true) != Face::F);
}

TEST_CASE("absorbs detector gauge re-keying through a continuous multi-axis tumble", "[tracker]") {
    Sim sim;
    sim.lock_on();
    // Simultaneous multi-axis rotation, the scenario the whole design is
    // shaped around: 120 deg/s about (1, 1, 0.3) for ~5 simulated seconds
    // (~600 degrees total), crossing many 45-degree gauge boundaries.
    const float norm = std::sqrt(1.0F + 1.0F + 0.09F);
    const std::array<float, 3> omega{120.0F / norm, 120.0F / norm, 36.0F / norm};
    Quat previous_obs = sim.observation();
    int rekeys_seen = 0;
    for (int pass = 0; pass < 150; ++pass) {
        sim.advance(omega);
        const Quat obs = sim.observation();
        if (quat_angle_deg(obs, previous_obs) > 40.0F) {
            ++rekeys_seen;  // raw observation jumped a gauge boundary
        }
        previous_obs = obs;
        const auto out = sim.step_obs(obs);
        REQUIRE(out.state == TrackState::locked);
        CHECK(out.gate == TrackGate::accepted);
        CHECK(track_matches_truth(sim));
        if (frontal_deg(sim.q_true) < 30.0F) {
            REQUIRE(out.front_face.has_value());
            CHECK(*out.front_face == nearest_face(sim.q_true));
        }
    }
    // The harness must actually have injected re-keying jumps, or this
    // test would be vacuously passing on smooth observations.
    CHECK(rekeys_seen > 3);
}

TEST_CASE("dwell-then-flick stays locked through the rate step", "[tracker]") {
    Sim sim;
    sim.lock_on();
    for (int pass = 0; pass < 30; ++pass) {
        const auto out = sim.step_obs();
        REQUIRE(out.state == TrackState::locked);
    }
    // Flick: 300 deg/s (10 degrees per pass - inside the interframe
    // budget, under the outlier ceiling) for a third of a second.
    const std::array<float, 3> flick{0.0F, 300.0F, 0.0F};
    for (int pass = 0; pass < 10; ++pass) {
        sim.advance(flick);
        const auto out = sim.step_obs();
        REQUIRE(out.state == TrackState::locked);
        CHECK(track_matches_truth(sim));
    }
    for (int pass = 0; pass < 10; ++pass) {
        const auto out = sim.step_obs();
        REQUIRE(out.state == TrackState::locked);
        CHECK(track_matches_truth(sim));
    }
}

TEST_CASE("coasts through short dropouts on the predicted rate and re-locks", "[tracker]") {
    Sim sim;
    sim.lock_on();
    // Warm the rate estimate so the coast has something real to
    // extrapolate.
    const std::array<float, 3> omega{0.0F, 90.0F, 0.0F};
    for (int pass = 0; pass < 20; ++pass) {
        sim.advance(omega);
        REQUIRE(sim.step_obs().state == TrackState::locked);
    }
    // The cube KEEPS ROTATING during a 7-pass detection dropout; the
    // coasted prediction must carry the track across ~21 degrees of unseen
    // rotation.
    for (int pass = 0; pass < 7; ++pass) {
        sim.advance(omega);
        const auto out = sim.step_miss();
        REQUIRE(out.state == TrackState::coasting);
        CHECK(out.sigma_deg > 0.0F);
        CHECK(out.front_face.has_value());  // coasting still shows the held identity
    }
    sim.advance(omega);
    const auto relocked = sim.step_obs();
    CHECK(relocked.state == TrackState::locked);
    CHECK(relocked.gate == TrackGate::accepted);
    CHECK(relocked.epoch == 1);  // same epoch: identity survived the gap
    CHECK(track_matches_truth(sim));
}

TEST_CASE("sigma inflation forces lost before the lattice basin becomes reachable", "[tracker]") {
    Sim sim;
    sim.lock_on();
    // Default rates: sigma grows 4 degrees per missed pass and must cross
    // the 35-degree ceiling (forcing lost) while the acceptance threshold
    // is still capped at 33 - i.e. strictly before a neighbouring basin's
    // element could ever be accepted.
    int passes_to_lost = 0;
    while (sim.tracker.state() != TrackState::lost) {
        const auto out = sim.step_miss();
        CHECK(out.sigma_deg <= 35.0F + 4.0F);  // never wildly past the cap
        ++passes_to_lost;
        REQUIRE(passes_to_lost < 30);
    }
    CHECK(passes_to_lost >= 3);

    // The cube moved 40 degrees while unobserved: a stale track would
    // resolve this into the WRONG lattice element. Lost-then-re-anchor
    // instead starts a fresh epoch that makes no identity claim at all.
    sim.advance({40.0F / kDtS, 0.0F, 0.0F});
    const auto out = sim.step_obs();
    CHECK(out.gate == TrackGate::anchored);
    CHECK(out.state == TrackState::acquiring);
    CHECK(out.epoch == 2);
    CHECK_FALSE(out.front_face.has_value());
}

TEST_CASE("wall-clock coast timeout kills a stale prediction", "[tracker]") {
    SECTION("many slow-inflating passes still lose by the timeout") {
        TrackerConfig config;
        config.coast_sigma_rate_deg = 0.5F;  // sigma cap alone would take ~70 passes
        Sim sim(config);
        sim.lock_on();
        int passes = 0;
        while (sim.tracker.state() != TrackState::lost) {
            sim.step_miss();
            ++passes;
            REQUIRE(passes < 40);
        }
        // 700 ms at 33 ms cadence = 22 passes; the timeout fired long
        // before sigma could have.
        CHECK(passes <= 23);
    }
    SECTION("a single long inter-pass gap re-anchors instead of trusting the stale prediction") {
        Sim sim;
        sim.lock_on();
        sim.t_ms += 900;  // one stalled interval, only ONE pass elapsed
        sim.advance({0.0F, 30.0F, 0.0F}, 0.9F);
        const auto out = sim.step_obs();
        // The observation is accepted - but as a NEW epoch's anchor, not
        // as a continuation of an identity the tracker can no longer
        // vouch for.
        CHECK(out.gate == TrackGate::anchored);
        CHECK(out.epoch == 2);
        CHECK_FALSE(out.front_face.has_value());
    }
    SECTION("the acquire timeout covers acquiring - a stalled streak re-anchors, never chains") {
        Sim sim;
        const auto anchored = sim.step_obs();  // anchor: epoch 1, streak 1
        CHECK(anchored.state == TrackState::acquiring);  // one accept short of locked
        // One stalled interval longer than the ACQUIRING timeout (1200 ms,
        // deliberately wider than locked's 700 ms - acquiring has no
        // capture authority, so the miss-tolerant streak may bridge
        // longer), during which the cube turned 80 degrees - most of the
        // way across a lattice basin. The stalled anchor still has sigma
        // ZERO (nothing inflates it between passes), so without the
        // wall-clock timeout the resolution would run against the stale
        // prediction, find the NEIGHBOURING element a mere ~10 degrees
        // away, accept it, complete the streak and promote a silently
        // wrong identity straight to locked.
        sim.t_ms += 1400;
        sim.advance({0.0F, 80.0F / 1.4F, 0.0F}, 1.4F);
        const auto out = sim.step_obs();
        CHECK(out.gate == TrackGate::anchored);
        CHECK(out.epoch == 2);
        CHECK(out.state == TrackState::acquiring);
        CHECK_FALSE(out.front_face.has_value());
    }
}

TEST_CASE("acquiring bridges detection gaps: sparse burst accepts still lock", "[tracker]") {
    // The live1 acquisition lesson: real detections arrive in bursts of
    // 1-2 separated by 2-8 pass gaps, so the streak must count ACCEPTED
    // observations across bridged misses, not consecutive passes. An
    // anchor at pass 0 and a confirmation at pass 3 with misses between
    // must promote at that (calibrated second) accept exactly as two
    // consecutive ones would. A three-accept config is exercised too so
    // the bridging property is shown to be streak-length independent, not
    // an artifact of the calibrated count.
    Sim sim;
    const auto anchored = sim.step_obs();
    CHECK(anchored.gate == TrackGate::anchored);
    CHECK(anchored.state == TrackState::acquiring);
    for (int gap = 0; gap < 2; ++gap) {
        const auto miss = sim.step_miss();
        // A bridged miss keeps acquiring (never coasting - that state
        // reports an identity acquiring has not earned) and claims no
        // front face.
        CHECK(miss.state == TrackState::acquiring);
        CHECK_FALSE(miss.front_face.has_value());
        CHECK(miss.sigma_deg > 0.0F);  // the envelope is inflating
    }
    const auto second = sim.step_obs();
    CHECK(second.gate == TrackGate::accepted);
    CHECK(second.state == TrackState::locked);
    CHECK(second.epoch == 1);  // one epoch throughout: the anchor survived the gaps
    REQUIRE(second.front_face.has_value());

    // Same property at acquire_frames = 3: the middle accept neither
    // promotes nor dies across its bridged gaps.
    TrackerConfig config;
    config.acquire_frames = 3;
    Sim sim3(config);
    sim3.step_obs();  // anchor
    for (int gap = 0; gap < 2; ++gap) {
        CHECK(sim3.step_miss().state == TrackState::acquiring);
    }
    const auto mid = sim3.step_obs();
    CHECK(mid.gate == TrackGate::accepted);
    CHECK(mid.state == TrackState::acquiring);
    for (int gap = 0; gap < 2; ++gap) {
        CHECK(sim3.step_miss().state == TrackState::acquiring);
    }
    const auto third = sim3.step_obs();
    CHECK(third.gate == TrackGate::accepted);
    CHECK(third.state == TrackState::locked);
    CHECK(third.epoch == 1);
    REQUIRE(third.front_face.has_value());
}

TEST_CASE("an out-of-basin observation while acquiring misses without killing the streak", "[tracker]") {
    Sim sim;
    sim.step_obs();  // anchor: streak 1
    // 35 degrees from the anchor over a stretched ~200 ms pass: past the
    // 33-degree basin ceiling (so refused whatever sigma has inflated to)
    // but at only ~170 deg/s - under the omega ceiling, so it reaches the
    // basin gate. Under the old rule this single stray dropped straight
    // to lost and discarded the anchor.
    sim.t_ms += 170;
    const Quat outlier = quat_multiply(axis_angle({0.0F, 1.0F, 0.0F}, 35.0F), sim.observation());
    const auto missed = sim.step_obs(outlier);
    CHECK(missed.gate == TrackGate::basin);
    CHECK(missed.state == TrackState::acquiring);
    CHECK_FALSE(missed.front_face.has_value());
    // A consistent accept completes the original streak in the original
    // epoch - the stray observation cost one pass, not the anchor.
    const auto second = sim.step_obs();
    CHECK(second.gate == TrackGate::accepted);
    CHECK(second.state == TrackState::locked);
    CHECK(second.epoch == 1);
}

TEST_CASE("sustained misses while acquiring force lost through the sigma cap", "[tracker]") {
    Sim sim;
    sim.step_obs();  // anchor
    // 2.5 deg/pass (the acquire rate) must reach the 35-degree cap in ~15
    // quiet passes - the bridge is bounded, not indefinite - and no pass
    // along the way may claim an identity.
    int passes = 0;
    while (sim.tracker.state() != TrackState::lost) {
        const auto out = sim.step_miss();
        CHECK_FALSE(out.front_face.has_value());
        ++passes;
        REQUIRE(passes < 20);
    }
    CHECK(passes >= 10);  // strictly slower than coasting's 4 deg/pass decay
    // And the next observation founds a fresh epoch, exactly as any loss.
    const auto anchored = sim.step_obs();
    CHECK(anchored.gate == TrackGate::anchored);
    CHECK(anchored.epoch == 2);
}

TEST_CASE("a gap exceeding the acquire timeout re-anchors a new epoch", "[tracker]") {
    SECTION("inside the window the streak chains") {
        Sim sim;
        sim.step_obs();  // anchor: streak 1
        sim.t_ms += 1000;  // long, but inside acquire_coast_timeout_ms
        const auto out = sim.step_obs();
        // Same epoch, streak completed: the cube did not move, so the
        // resolution against the (still fresh enough) anchor is clean and
        // the accept promotes the two-accept streak.
        CHECK(out.gate == TrackGate::accepted);
        CHECK(out.epoch == 1);
        CHECK(out.state == TrackState::locked);
    }
    SECTION("beyond the window the anchor is discarded") {
        Sim sim;
        sim.step_obs();  // anchor: epoch 1
        sim.t_ms += 1300;  // past acquire_coast_timeout_ms
        const auto out = sim.step_obs();
        CHECK(out.gate == TrackGate::anchored);
        CHECK(out.epoch == 2);
        CHECK(out.state == TrackState::acquiring);
    }
}

TEST_CASE("isolated flip glitches are discarded by the implied-omega gate", "[tracker]") {
    Sim sim;
    sim.noise_deg = 2.0F;
    sim.lock_on();
    for (int pass = 0; pass < 10; ++pass) {
        REQUIRE(sim.step_obs().state == TrackState::locked);
    }
    const int epoch_before = sim.tracker.epoch();
    // A 20-degree instantaneous jump sits INSIDE the basin threshold but
    // implies ~600 deg/s - only the rate gate can reject it. Isolated
    // glitches (never two consistent in a row) must be discarded without
    // disturbing the track.
    for (int burst = 0; burst < 4; ++burst) {
        const Quat glitch = quat_multiply(axis_angle({0.0F, 1.0F, 0.0F}, 20.0F), sim.observation());
        const auto out = sim.step_obs(glitch);
        CHECK(out.gate == TrackGate::omega_outlier);
        CHECK(out.state == TrackState::coasting);
        // Two clean passes between glitches: the chain must reset.
        for (int clean = 0; clean < 2; ++clean) {
            const auto clean_out = sim.step_obs();
            REQUIRE(clean_out.state == TrackState::locked);
            CHECK(track_matches_truth(sim));
        }
    }
    CHECK(sim.tracker.epoch() == epoch_before);

    // Mid-tumble mirror flips must likewise never drag the track. Far from
    // a lattice pose the flip's best gauge composition can land INSIDE the
    // basin threshold at a believable implied rate (it mimics a plausible
    // re-key), so neither theta nor the rate gate can be relied on - what
    // catches it is the raw-pose plausibility ceiling: a flipped model
    // frame faces away from the camera, a pose no real detection can
    // produce.
    const std::array<float, 3> omega{80.0F, 60.0F, 0.0F};
    for (int pass = 0; pass < 40; ++pass) {
        sim.advance(omega);
        const bool inject = pass % 7 == 3 && quantize24(sim.q_true).theta_deg > 20.0F;
        const Quat obs = inject ? quat_multiply(axis_angle({0.0F, 1.0F, 0.0F}, 180.0F), sim.observation())
                                : sim.observation();
        const auto out = sim.step_obs(obs);
        if (inject) {
            CHECK(out.gate == TrackGate::implausible);
        }
        REQUIRE((out.state == TrackState::locked || out.state == TrackState::coasting));
        CHECK(track_matches_truth(sim, 15.0F));
    }
}

TEST_CASE("a mirror-flipped pose can neither anchor nor join an epoch", "[tracker]") {
    Sim sim;
    // While lost: a flipped observation must not found an epoch - anchoring
    // adopts the observation AS the body frame with no prediction to check
    // it against, so the plausibility ceiling is the only gate standing
    // between a solver artifact and a garbage epoch every later resolution
    // would be measured against.
    const Quat flipped = quat_multiply(axis_angle({0.0F, 1.0F, 0.0F}, 180.0F), sim.observation());
    const auto refused = sim.step_obs(flipped);
    CHECK(refused.gate == TrackGate::implausible);
    CHECK(refused.state == TrackState::lost);
    CHECK(refused.epoch == 0);
    // The next clean observation anchors normally - the refusal cost
    // nothing but one pass.
    const auto anchored = sim.step_obs();
    CHECK(anchored.gate == TrackGate::anchored);
    CHECK(anchored.epoch == 1);
}

TEST_CASE("the opened basin re-locks a per-gap step the old ceiling refused", "[tracker]") {
    // live3's refused-pose histogram: 24 of 144 basin/margin refusals sat
    // in the 30-40 degree bucket - a casual turn resumed after a short
    // detection gap lands there, and the old 33-degree cap refused it no
    // matter how honestly sigma had grown. About a CORNER axis the
    // runner-up element is far away (the nearest non-identity candidates
    // are the 120-degree corner rotations), so the margin gate has no
    // ambiguity objection either: the step must be accepted, in-epoch.
    Sim sim;
    sim.noise_deg = 0.0F;
    sim.lock_on();
    for (int pass = 0; pass < 5; ++pass) {
        REQUIRE(sim.step_obs().state == TrackState::locked);
    }
    for (int pass = 0; pass < 3; ++pass) {
        REQUIRE(sim.step_miss().state == TrackState::coasting);  // sigma opens the threshold
    }
    // 38 degrees accumulated about (1,1,1) across the gap: past the old
    // 33-degree ceiling, inside the new 40, and only ~290 deg/s over the
    // real four-pass span.
    const float component = 38.0F / std::sqrt(3.0F) / (4 * kDtS);
    sim.advance({component, component, component}, 4 * kDtS);
    const auto out = sim.step_obs();
    CHECK(out.gate == TrackGate::accepted);
    CHECK(out.state == TrackState::locked);
    CHECK(out.epoch == 1);
    CHECK(track_matches_truth(sim));
}

TEST_CASE("the implied-rate gate judges the span since the last accept, not one frame", "[tracker]") {
    SECTION("a displaced re-detection after coasting is refused by geometry, not rate") {
        Sim sim;
        sim.noise_deg = 0.0F;
        sim.lock_on();
        for (int pass = 0; pass < 5; ++pass) {
            REQUIRE(sim.step_obs().state == TrackState::locked);
        }
        const int epoch_before = sim.tracker.epoch();
        // Five coasted misses at ~100 ms cadence: ~500 ms with nothing
        // accepted, inside the coast timeout.
        for (int pass = 0; pass < 5; ++pass) {
            sim.t_ms += 67;  // step_miss adds the remaining 33
            REQUIRE(sim.step_miss().state == TrackState::coasting);
        }
        // The cube turned 44 degrees DURING the gap - 88 deg/s over the
        // real span, casual wrist speed. Dividing by the final 100 ms
        // frame interval instead "implies" 440 deg/s, which the old
        // per-dt denominator discarded as a flip glitch and chained
        // toward a teleport re-anchor (live3's 45 omega-outlier refusals
        // mid-turn). The honest verdict is the geometry one: the coasted
        // prediction cannot vouch for a 44-degree residual, so the basin
        // gate refuses it and the track keeps coasting in its epoch.
        sim.t_ms += 67;
        sim.advance({0.0F, 440.0F, 0.0F}, 0.1F);
        const auto out = sim.step_obs();
        CHECK(out.gate == TrackGate::basin);
        CHECK(out.state == TrackState::coasting);
        CHECK(sim.tracker.epoch() == epoch_before);
    }
    SECTION("a jump inside one frame interval still trips the gate") {
        Sim sim;
        sim.noise_deg = 0.0F;
        sim.lock_on();
        for (int pass = 0; pass < 5; ++pass) {
            REQUIRE(sim.step_obs().state == TrackState::locked);
        }
        // Immediately after an accept the span IS the frame interval:
        // a 30-degree jump in 33 ms (~900 deg/s) sits inside the basin
        // threshold, so the rate gate is the only thing standing between
        // this glitch and a wrongly adopted measurement - it must still
        // fire exactly as before the denominator change.
        const Quat glitch = quat_multiply(axis_angle({0.0F, 1.0F, 0.0F}, 30.0F), sim.observation());
        const auto out = sim.step_obs(glitch);
        CHECK(out.gate == TrackGate::omega_outlier);
        CHECK(out.state == TrackState::coasting);
    }
}

TEST_CASE("three consistent outliers hard re-anchor into a new epoch", "[tracker]") {
    Sim sim;
    sim.lock_on();
    for (int pass = 0; pass < 5; ++pass) {
        REQUIRE(sim.step_obs().state == TrackState::locked);
    }
    // The cube genuinely teleports (regripped behind an occluder): every
    // subsequent observation consistently shows the new pose. The first
    // two are discarded as potential glitches; the third proves the move
    // real and re-anchors - as a NEW epoch, because the old identity
    // cannot be carried across a jump the prediction never followed.
    sim.advance({0.0F, 120.0F / kDtS, 0.0F});  // 120 degrees in one pass
    const auto first = sim.step_obs();
    CHECK(first.gate == TrackGate::omega_outlier);
    const auto second = sim.step_obs();
    CHECK(second.gate == TrackGate::omega_outlier);
    const auto third = sim.step_obs();
    CHECK(third.gate == TrackGate::anchored);
    CHECK(third.epoch == 2);
    CHECK(third.state == TrackState::acquiring);
    CHECK_FALSE(third.front_face.has_value());
    // And the new epoch tracks the new pose cleanly.
    const auto relocked = [&] {
        TrackerPassOutput out{};
        for (int pass = 0; pass < 2; ++pass) {
            out = sim.step_obs();
        }
        return out;
    }();
    CHECK(relocked.state == TrackState::locked);
}

TEST_CASE("centre-colour belief certifies a clean track and vetoes a slipped one", "[tracker]") {
    Sim sim;
    sim.feed_labs = true;
    sim.lock_on();
    // Show F, then tour to R and back so the epoch learns centre
    // references for both faces.
    for (int pass = 0; pass < 10; ++pass) {
        REQUIRE(sim.step_obs().state == TrackState::locked);
    }
    const std::array<float, 3> to_r{0.0F, -90.0F, 0.0F};  // R arrives at the camera
    for (int pass = 0; pass < 30; ++pass) {
        sim.advance(to_r);
        REQUIRE(sim.step_obs().state == TrackState::locked);
    }
    CHECK(nearest_face(sim.q_true) == Face::R);
    for (int pass = 0; pass < 10; ++pass) {
        REQUIRE(sim.step_obs().state == TrackState::locked);
    }
    const std::array<float, 3> back{0.0F, 90.0F, 0.0F};
    for (int pass = 0; pass < 30; ++pass) {
        sim.advance(back);
        REQUIRE(sim.step_obs().state == TrackState::locked);
    }
    CHECK(nearest_face(sim.q_true) == Face::F);
    // With two known references matched over many passes, the no-slip
    // posterior must have cleared the certainty bar.
    auto out = sim.step_obs();
    CHECK(out.identity_certain);
    CHECK(out.belief_margin > 0.0F);

    // Now the contradiction: geometry keeps showing F while the centre
    // colour persistently reads as R's reference - the signature of a
    // physical slip the pose stream missed. The belief must accumulate
    // against the track and force lost in bounded time; continuing to
    // report F would be exactly the silent wrong identity the invariant
    // forbids.
    sim.lab_override = Face::R;
    int passes_to_lost = 0;
    while (sim.tracker.state() != TrackState::lost) {
        out = sim.step_obs();
        ++passes_to_lost;
        // Bounded, not instant: the per-observation swing cap (see
        // max_obs_log_penalty) means a kill takes several consecutive
        // contradicting samples - sustained evidence, never one noisy one.
        REQUIRE(passes_to_lost <= 15);
    }
    CHECK(out.gate == TrackGate::slip_evidence);
    CHECK_FALSE(out.front_face.has_value());
}

TEST_CASE("a single wild centre sample never vetoes; only ref-backed evidence kills", "[tracker]") {
    // The live3 belief post-mortem: 21 of 43 epochs died to slip_evidence
    // with only ONE face ever colour-observed and no slip anywhere - one
    // blurred/specular Lab sample penalised identity to the clamp while
    // the 20 unknown-face hypotheses took the flat floor, and that pure
    // floor-arithmetic lead crossed the veto. Two properties fix it, each
    // pinned here: the per-observation swing is bounded under the veto bar
    // (max_obs_log_penalty), and an unknown-floor hypothesis - one never
    // scored against a learned reference - can never win the veto at all.
    SECTION("a single-face epoch survives sustained wild samples") {
        Sim sim;
        sim.feed_labs = true;
        sim.lock_on();
        for (int pass = 0; pass < 5; ++pass) {
            REQUIRE(sim.step_obs().state == TrackState::locked);
        }
        // Wild samples ~10 sigma from F's learned reference and matching
        // no other face: identity takes the clamped penalty, every
        // alternative is an unknown floor. Under the old arithmetic pass
        // three force-lost the track; now nothing here may kill it, ever -
        // the certainty margin degrades (that is honest) but the track
        // holds.
        sim.lab_raw_override = std::array<float, 3>{20.0F, 80.0F, -80.0F};
        for (int pass = 0; pass < 6; ++pass) {
            const auto out = sim.step_obs();
            CHECK(out.gate == TrackGate::accepted);
            REQUIRE(out.state == TrackState::locked);
        }
        // And clean samples recover the margin instead of re-anchoring.
        sim.lab_raw_override.reset();
        for (int pass = 0; pass < 6; ++pass) {
            REQUIRE(sim.step_obs().state == TrackState::locked);
        }
        CHECK(sim.tracker.epoch() == 1);
    }
    SECTION("with two learned references one wild sample survives, sustained contradiction kills") {
        Sim sim;
        sim.feed_labs = true;
        sim.lock_on();
        // Learn F, tour to R and back so a slip hypothesis IS ref-backed.
        for (int pass = 0; pass < 10; ++pass) {
            REQUIRE(sim.step_obs().state == TrackState::locked);
        }
        const std::array<float, 3> to_r{0.0F, -90.0F, 0.0F};
        for (int pass = 0; pass < 30; ++pass) {
            sim.advance(to_r);
            REQUIRE(sim.step_obs().state == TrackState::locked);
        }
        const std::array<float, 3> back{0.0F, 90.0F, 0.0F};
        for (int pass = 0; pass < 30; ++pass) {
            sim.advance(back);
            REQUIRE(sim.step_obs().state == TrackState::locked);
        }
        REQUIRE(nearest_face(sim.q_true) == Face::F);

        // One sample that reads exactly as R's reference while geometry
        // shows F: the strongest possible single contradiction, against a
        // ref-backed hypothesis - and still not enough on its own.
        sim.lab_override = Face::R;
        const auto contradicted = sim.step_obs();
        CHECK(contradicted.gate == TrackGate::accepted);
        REQUIRE(contradicted.state == TrackState::locked);
        sim.lab_override.reset();
        for (int pass = 0; pass < 4; ++pass) {
            REQUIRE(sim.step_obs().state == TrackState::locked);
        }

        // The same contradiction SUSTAINED is a real slip signature and
        // must still kill in bounded time - the veto lost none of its
        // authority over evidence, only over noise.
        sim.lab_override = Face::R;
        int passes_to_lost = 0;
        TrackerPassOutput out{};
        while (sim.tracker.state() != TrackState::lost) {
            out = sim.step_obs();
            ++passes_to_lost;
            REQUIRE(passes_to_lost <= 15);
        }
        CHECK(out.gate == TrackGate::slip_evidence);
        CHECK(passes_to_lost >= 2);
    }
}

TEST_CASE("a noise-suppressed margin does not wedge later reference learning", "[tracker]") {
    // The live3 replay's epoch-7 flatline: noisy centre samples drove the
    // full margin negative through unknown-floor arithmetic, and the old
    // full-margin learning guard then refused to learn ANY new reference
    // for the rest of the epoch - a newly shown face collected 11 colour
    // accepts and taught nothing, so no evidence could ever accumulate to
    // recover. Learning is gated on ref-backed leads only (a slip
    // candidate with real evidence), so a wild-sample dip must not stop a
    // new face from certifying afterwards.
    Sim sim;
    sim.feed_labs = true;
    sim.lock_on();
    for (int pass = 0; pass < 4; ++pass) {
        REQUIRE(sim.step_obs().state == TrackState::locked);
    }
    // Wild samples matching no reference: identity takes the clamped
    // penalty (-max_obs_log_penalty = -4) while the unknown floor takes
    // -3, so each wild pass closes the clean head start by only ~1 log unit
    // (the round-2 unknown-face floor -2 -> -3 deliberately sits close to
    // the penalty cap, so pure noise depresses the full margin far more
    // slowly than a real slip does - the point of keeping the veto on
    // ref-backed evidence). It still eventually inverts: enough wild passes
    // outlast the head start and the floor hypotheses lead, driving the
    // full margin (honestly) negative.
    sim.lab_raw_override = std::array<float, 3>{20.0F, 80.0F, -80.0F};
    for (int pass = 0; pass < 24; ++pass) {
        REQUIRE(sim.step_obs().state == TrackState::locked);
    }
    sim.lab_raw_override.reset();
    REQUIRE(sim.step_obs().belief_margin < 0.0F);
    // Tour to R: its reference must still be learnable, and its
    // face-scoped certainty reachable, despite the depressed full margin.
    const std::array<float, 3> to_r{0.0F, -90.0F, 0.0F};
    for (int pass = 0; pass < 30; ++pass) {
        sim.advance(to_r);
        REQUIRE(sim.step_obs().state == TrackState::locked);
    }
    REQUIRE(nearest_face(sim.q_true) == Face::R);
    TrackerPassOutput out{};
    for (int pass = 0; pass < 8; ++pass) {
        out = sim.step_obs();
        REQUIRE(out.state == TrackState::locked);
    }
    CHECK(out.identity_certain_face[static_cast<std::size_t>(Face::R)]);
}

TEST_CASE("face-scoped certainty is reachable on single-centre evidence; the full bar is not", "[tracker]") {
    // The live3 deadlock in miniature: a frontal hold observing one face's
    // centre. The observed face's in-plane stabilizers map it onto itself,
    // so they receive bit-identical likelihood to identity on every
    // observation - the full-24 margin is structurally capped at zero and
    // ln(100) is unreachable IN PRINCIPLE (480/480 passes certain=0 live).
    // Quotienting out exactly that stabilizer makes the bar a few colour
    // accepts away, and the quotiented hypotheses cannot hurt a commit of
    // that face (in-plane rotation only - assembly searches per-face
    // rotations, router verification is rotation-invariant).
    Sim sim;
    sim.feed_labs = true;
    sim.lock_on();
    TrackerPassOutput out{};
    for (int pass = 0; pass < 6; ++pass) {
        out = sim.step_obs();
        REQUIRE(out.state == TrackState::locked);
        // Degeneracy regression: however long the frontal burst runs, the
        // full-24 margin never rises above zero...
        CHECK(out.belief_margin <= 0.0F);
        CHECK_FALSE(out.identity_certain);
    }
    // ...while the face-scoped bar for the observed face crossed within
    // the burst (the 20 moving hypotheses sit at the unknown floor, -3
    // per colour accept since the round-2 recalibration: two accepts beat
    // ln(100) = 4.6, where the old -2 floor needed three).
    REQUIRE(out.front_face.has_value());
    CHECK(*out.front_face == Face::F);
    CHECK(out.identity_certain_face[static_cast<std::size_t>(Face::F)]);
    CHECK(sim.tracker.identity_certain_for(Face::F));
    CHECK(sim.tracker.belief_margin_for(Face::F) > sim.tracker.belief_margin());
    // The certainty is scoped to the face the evidence is ABOUT: for any
    // other face, F's zero-belief stabilizers are movers (a quarter turn
    // about the F axis relocates U), so nothing else may reach the bar.
    CHECK_FALSE(out.identity_certain_face[static_cast<std::size_t>(Face::U)]);
    CHECK_FALSE(out.identity_certain_face[static_cast<std::size_t>(Face::R)]);

    // Two-face evidence breaks the degeneracy for real: after touring to R
    // and back (both references learned and repeatedly matched), even the
    // full-24 bar is reachable - the scoped bar is a bridge for the
    // single-face grip, not a lowered ceiling.
    const std::array<float, 3> to_r{0.0F, -90.0F, 0.0F};
    for (int pass = 0; pass < 30; ++pass) {
        sim.advance(to_r);
        REQUIRE(sim.step_obs().state == TrackState::locked);
    }
    const std::array<float, 3> back{0.0F, 90.0F, 0.0F};
    for (int pass = 0; pass < 30; ++pass) {
        sim.advance(back);
        REQUIRE(sim.step_obs().state == TrackState::locked);
    }
    out = sim.step_obs();
    CHECK(out.identity_certain);
    CHECK(out.identity_certain_face[static_cast<std::size_t>(Face::F)]);
}

TEST_CASE("reset and force_lost drop state without leaking across scans", "[tracker]") {
    Sim sim;
    sim.lock_on();
    sim.tracker.force_lost();
    CHECK(sim.tracker.state() == TrackState::lost);
    // Next observation re-anchors as a new epoch of the same session.
    CHECK(sim.step_obs().epoch == 2);

    sim.tracker.reset();
    CHECK(sim.tracker.state() == TrackState::lost);
    CHECK(sim.tracker.epoch() == 0);
    // After a full reset the pass clock restarts too: the first pass must
    // not compute a dt against the previous session.
    const auto fresh = sim.step_obs();
    CHECK(fresh.dt_ms == 0);
    CHECK(fresh.epoch == 1);
}

TEST_CASE("rotation-vector exp/log round-trip within float tolerance", "[tracker]") {
    std::mt19937 rng(7);
    std::uniform_real_distribution<float> component(-179.0F, 179.0F);
    for (int i = 0; i < 200; ++i) {
        std::array<float, 3> v{component(rng), component(rng), component(rng)};
        const float norm = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
        if (norm > 179.0F) {  // stay inside the log map's principal branch
            for (auto& c : v) {
                c *= 170.0F / norm;
            }
        }
        const auto round_tripped = quat_to_rotvec_deg(quat_from_rotvec_deg(v));
        for (std::size_t k = 0; k < 3; ++k) {
            CHECK(std::abs(round_tripped[k] - v[k]) < 0.05F);
        }
    }
    // Identity edge case.
    const auto zero = quat_to_rotvec_deg({1.0F, 0.0F, 0.0F, 0.0F});
    CHECK(zero == std::array<float, 3>{0.0F, 0.0F, 0.0F});
}
