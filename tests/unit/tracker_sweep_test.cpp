// TrackerConfig calibration sweep over a recorded corpus - hidden tag,
// never part of the normal suite. This is the M6 calibration MECHANISM;
// the corpus itself is recorded on demand from live hardware (see
// CUBE_VISION_RECORD_DIR in scan_session.cpp) and does not ship with the
// repo, so the sweep SKIPS - never fails - when nothing is recorded yet.
//
//   set CUBE_TRACK_SWEEP_DIR=<corpus root>   (one recording dir, or a
//                                             directory of recording dirs,
//                                             one per clip)
//   set CUBE_TRACK_SWEEP_SHARD=i/N           (optional; run variant subset)
//   cube_unit_tests.exe "[.track-sweep]"
//
// Output is one CSV row per (variant, clip) on stdout: which TrackerConfig
// knob was moved, to what, and the tracker's aggregate behaviour over the
// clip - lock/coast/lost pass counts, gate refusal histogram, epoch
// (re-anchor) count, residual-angle stats. Calibration = eyeballing (or
// scripting over) those rows for the knob values that maximise locked time
// without buying it through the safety gates.
//
// Cost model, and why the sweep is structured detector-once/tracker-many:
// the detector cascade (observe_face/observe_corner/pose) dominates at
// tens of ms per frame, while a tracker pass is a few 24-element scans -
// microseconds. Each clip is therefore replayed through the detectors
// EXACTLY ONCE into a cached TrackerPassInput sequence, and every config
// variant re-runs only the cheap tracker over that cache; sweeping the
// whole variant grid costs barely more than one [.track-replay] pass per
// clip.
//
// Parallelism (the all-cores rule, without hardcoding a job count
// anywhere): variants are enumerated deterministically and
// CUBE_TRACK_SWEEP_SHARD=i/N filters to every N-th one, so a driver can
// launch as many shard processes as the local machine has cores - e.g.
// one cube_unit_tests.exe "[.track-sweep]" per shard with i = 0..N-1 -
// and simply concatenate the CSVs. Shard 0/1 (the default) runs
// everything.
//
// Detector-stage fidelity mirrors [.track-replay] (frame_debug_test.cpp):
// same analysis-scale frames (rotation-identical poses - intrinsics
// derive from frame size), same fully-visible-cells gate on both detector
// families, same corner-first pose precedence. Extras for tracker
// fidelity: the pose-seed chain (live passes seed solvePnP from the
// previous held pose; here the previous VALID pose seeds the next pass,
// expired after the same 450 ms hold the live overlay uses), the M7
// corner hint derived from that same held pose (compute_corner_hint -
// without it corner detections stop firing off the isometric pose and
// every metric this sweep calibrates would be skewed), and the
// stability tracker's motion score (fed the analysis-scale frame - it
// normalises to a fixed internal resolution, so scores are comparable to
// the live full-frame feed).
#ifdef _MSC_VER
#pragma warning(disable : 4996)
#endif

#include <catch2/catch_test_macros.hpp>

#include <cube/model/face.hpp>
#include <cube/vision/corner_observation.hpp>
#include <cube/vision/face_observation.hpp>
#include <cube/vision/frame_recording.hpp>
#include <cube/vision/idle_gate.hpp>
#include <cube/vision/orientation_tracker.hpp>
#include <cube/vision/pose_estimation.hpp>
#include <cube/vision/stability.hpp>

#include <opencv2/imgcodecs.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

using namespace cube::vision;

namespace {

// Matches the live overlay/seed hold window (kOverlayHoldMs in
// scan_session.cpp): a pose the overlay already dropped is too stale to
// bias the next solve's arbitration.
constexpr std::int64_t kSeedHoldMs = 450;

// Everything one variant may move: the tracker's config plus the idle
// gate's probe cadence (kIdleEscapeEvery made a constructor knob for
// exactly this sweep). The gate lives OUTSIDE TrackerConfig in production
// because it gates the detector cascade, not the tracker - but its skip
// verdicts null real observations, so its calibration question ("does a
// slower probe cadence starve reacquisition?") is only answerable through
// the same replayed tracker metrics as everything else here.
struct SweepSettings {
    TrackerConfig tracker;
    int idle_escape_every = IdleGate::kIdleEscapeEvery;
};

// One swept knob: a name for the CSV, the candidate values, and how to
// apply a value to the settings. The FIRST value of every axis must be
// the shipped default - build_sweep_variants() checks this so the grid
// can never silently drift from the struct's shipped defaults (initially
// the plan's constants table; now the live1-calibrated values).
struct SweepAxis {
    const char* name;
    std::vector<float> values;
    void (*apply)(SweepSettings&, float);
};

// The sweepable knob set. Deliberately excluded: max_coast_sigma_deg
// (fixed by safety) and the belief clamps (their failure modes need
// adversarial captures, not clips). basin_ceiling_deg joined the grid
// with the live3 recalibration (33 -> 40): the snap margin it buys is
// now a measured trade against quarter-turn transit, with the
// wrong-identity counter as the hard constraint. One-factor-
// at-a-time around the baseline rather than a cartesian product: the grid
// stays bounded (a few dozen variants), each row is attributable to one
// knob, and interactions worth chasing show up as two OFAT rows both
// moving a metric - at which point the follow-up is to make the joint
// candidate THE baseline (edit the defaults locally) and re-run: every
// axis then perturbs around the candidate, confirming or refuting it as
// a local optimum with the same one-row-one-knob attribution.
std::vector<SweepAxis> sweep_axes() {
    return {
        {"basin_ceiling_deg", {40.0F, 33.0F, 36.0F, 43.0F},
         [](SweepSettings& s, float v) { s.tracker.basin_ceiling_deg = v; }},
        {"max_interframe_deg", {30.0F, 15.0F, 25.0F, 33.0F},
         [](SweepSettings& s, float v) { s.tracker.max_interframe_deg = v; }},
        {"gauge_margin_deg", {15.0F, 10.0F, 20.0F, 25.0F},
         [](SweepSettings& s, float v) { s.tracker.gauge_margin_deg = v; }},
        {"coast_sigma_rate_deg", {4.0F, 2.0F, 6.0F, 8.0F},
         [](SweepSettings& s, float v) { s.tracker.coast_sigma_rate_deg = v; }},
        {"acquire_sigma_rate_deg", {2.5F, 1.5F, 4.0F},
         [](SweepSettings& s, float v) { s.tracker.acquire_sigma_rate_deg = v; }},
        {"acquire_coast_timeout_ms", {1200.0F, 900.0F, 1600.0F, 2000.0F},
         [](SweepSettings& s, float v) { s.tracker.acquire_coast_timeout_ms = static_cast<std::int64_t>(v); }},
        {"acquire_frames", {2.0F, 3.0F, 4.0F},
         [](SweepSettings& s, float v) { s.tracker.acquire_frames = static_cast<int>(v); }},
        {"motion_sigma_gain_deg", {40.0F, 20.0F, 80.0F},
         [](SweepSettings& s, float v) { s.tracker.motion_sigma_gain_deg = v; }},
        {"coast_timeout_ms", {700.0F, 500.0F, 1000.0F, 1400.0F},
         [](SweepSettings& s, float v) { s.tracker.coast_timeout_ms = static_cast<std::int64_t>(v); }},
        {"outlier_omega_deg_s", {400.0F, 250.0F, 300.0F, 600.0F},
         [](SweepSettings& s, float v) { s.tracker.outlier_omega_deg_s = v; }},
        {"omega_blend", {0.3F, 0.1F, 0.15F, 0.2F, 0.5F},
         [](SweepSettings& s, float v) { s.tracker.omega_blend = v; }},
        {"omega_decay", {0.9F, 0.7F, 0.8F, 0.95F},
         [](SweepSettings& s, float v) { s.tracker.omega_decay = v; }},
        {"posterior_lock_ratio", {100.0F, 30.0F, 300.0F},
         [](SweepSettings& s, float v) { s.tracker.posterior_lock_ratio = v; }},
        {"centre_sigma_lab", {12.0F, 8.0F, 16.0F, 20.0F},
         [](SweepSettings& s, float v) { s.tracker.centre_sigma_lab = v; }},
        {"obs_pose_ceiling_deg", {100.0F, 85.0F, 115.0F},
         [](SweepSettings& s, float v) { s.tracker.obs_pose_ceiling_deg = v; }},
        {"idle_escape_every", {4.0F, 2.0F, 3.0F, 6.0F, 8.0F},
         [](SweepSettings& s, float v) { s.idle_escape_every = static_cast<int>(v); }},
    };
}

struct SweepVariant {
    std::string axis;   // "baseline" for the untouched config
    float value = 0.0F; // the moved knob's value (baseline: 0, unused)
    SweepSettings settings;
};

// Deterministic variant enumeration: baseline first, then each axis's
// non-default values in axis order. Determinism is what makes the shard
// filter a partition rather than a lottery.
std::vector<SweepVariant> build_sweep_variants() {
    std::vector<SweepVariant> variants;
    variants.push_back({"baseline", 0.0F, SweepSettings{}});
    for (const auto& axis : sweep_axes()) {
        for (std::size_t i = 1; i < axis.values.size(); ++i) {
            SweepVariant variant{axis.name, axis.values[i], SweepSettings{}};
            axis.apply(variant.settings, axis.values[i]);
            variants.push_back(std::move(variant));
        }
    }
    return variants;
}

// "i/N" -> {i, N}; nullopt on anything malformed (including i >= N). The
// default when the env var is absent is the run-everything shard {0, 1}.
struct Shard {
    int index = 0;
    int count = 1;
};
std::optional<Shard> parse_shard(const std::string& spec) {
    const std::size_t slash = spec.find('/');
    if (slash == std::string::npos || slash == 0 || slash + 1 >= spec.size()) {
        return std::nullopt;
    }
    try {
        const int index = std::stoi(spec.substr(0, slash));
        const int count = std::stoi(spec.substr(slash + 1));
        if (index < 0 || count < 1 || index >= count) {
            return std::nullopt;
        }
        return Shard{index, count};
    } catch (...) {
        return std::nullopt;
    }
}

// The corpus root is either a single recording dir (has index.csv) or a
// directory of them - the layout the user records clip-by-clip lands in
// naturally (one CUBE_VISION_RECORD_DIR per clip, all under one root).
std::vector<std::filesystem::path> collect_clip_dirs(const std::filesystem::path& root) {
    std::vector<std::filesystem::path> clips;
    std::error_code ec;
    const auto index_name = std::string(recording::kIndexCsvName);
    if (std::filesystem::exists(root / index_name, ec)) {
        clips.push_back(root);
        return clips;
    }
    for (const auto& entry : std::filesystem::directory_iterator(root, ec)) {
        if (entry.is_directory(ec) && std::filesystem::exists(entry.path() / index_name, ec)) {
            clips.push_back(entry.path());
        }
    }
    std::sort(clips.begin(), clips.end());
    return clips;
}

// One cached pass: the tracker input the live handleAnalysisFinished
// would have built, plus the frame's cool-hue presence statistic - the
// idle gate's input, cached alongside so gate-cadence variants can be
// simulated without re-touching pixels (the worker computes the count on
// EVERY pass, skipped ones included, so caching it unconditionally is the
// faithful reproduction, not an approximation).
struct CachedPass {
    TrackerPassInput input;
    int cool_hue_pixels = 0;
};

// Detector stage, run ONCE per clip (see the cost model above): every
// surviving recorded pass becomes the exact TrackerPassInput the live
// handleAnalysisFinished would have built for it.
std::vector<CachedPass> load_clip_passes(const std::filesystem::path& dir) {
    std::vector<CachedPass> passes;
    std::ifstream index_stream(dir / std::string(recording::kIndexCsvName));
    if (!index_stream) {
        return passes;
    }
    const auto rows = recording::read_index(index_stream);
    int max_pass = 0;
    for (const auto& row : rows) {
        max_pass = std::max(max_pass, row.pass);
    }

    const auto cell_in_frame = [](const StickerQuad& cell, const cv::Mat& bounds) {
        cv::Point2f center(0.0F, 0.0F);
        for (const auto& corner : cell.corners) {
            center += corner;
        }
        center *= 0.25F;
        return center.x >= 0.0F && center.y >= 0.0F && center.x < static_cast<float>(bounds.cols) &&
               center.y < static_cast<float>(bounds.rows);
    };

    StabilityTracker stability;
    std::optional<PoseSeed> seed_prior;
    std::int64_t seed_t_ms = 0;
    for (const auto& row : rows) {
        if (recording::frame_overwritten(row.pass, max_pass)) {
            continue;
        }
        const cv::Mat frame = cv::imread((dir / recording::frame_filename(row.pass)).string(), cv::IMREAD_COLOR);
        if (frame.empty() || frame.cols != row.w || frame.rows != row.h) {
            continue;
        }

        CachedPass cached;
        cached.cool_hue_pixels = count_cool_hue_pixels(frame);
        TrackerPassInput& input = cached.input;
        input.t_ms = row.t_ms;
        input.motion_score = stability.update(frame).motion_score;

        // Live seed lifecycle: the held pose expires with the overlay.
        // Expired BEFORE the detector stage because the live pass samples
        // ONE prior at queue time and derives both the corner hint and the
        // solve seed from it (analyze() in scan_session.cpp); expiring only
        // at projection time would hint observe_corner from a pose the live
        // worker would already have dropped.
        if (seed_prior && row.t_ms - seed_t_ms > kSeedHoldMs) {
            seed_prior.reset();
        }
        const PoseSeed* seed = seed_prior ? &*seed_prior : nullptr;

        const auto all_cells_in_frame = [&](const auto& cells) {
            return std::all_of(cells.begin(), cells.end(),
                               [&](const StickerQuad& cell) { return cell_in_frame(cell, frame); });
        };
        // Pose-only fits split off exactly as analyze() splits them: pose
        // yes, colour evidence and capture-grade detection no.
        std::optional<FaceObservation> face;
        std::optional<FaceObservation> pose_only_face;
        if (auto observed = observe_face(frame); observed.has_value() && all_cells_in_frame(observed->cells)) {
            if (observed->pose_only) {
                pose_only_face = std::move(*observed);
            } else {
                face = std::move(*observed);
            }
        }
        // Hinted from the held pose exactly as the live worker hints it
        // (M7): off the isometric pose an unhinted detector simply stops
        // firing, and a sweep replayed that way would understate corner
        // cadence - skewing lost-rate and identity-stability for precisely
        // the mechanism the calibration is meant to tune. Null seed (or a
        // frontal/edge-on prediction) -> null hint -> the historical
        // fixed-pose detector, as live. The hint is rotation-only
        // (compute_corner_hint), so the analysis-scale translation in a
        // replayed seed cannot skew it.
        std::optional<CornerHint> corner_hint;
        if (seed_prior) {
            corner_hint = compute_corner_hint(*seed_prior);
        }
        std::optional<CornerObservation> corner;
        if (auto observed = observe_corner(frame, corner_hint ? &*corner_hint : nullptr);
            observed.has_value() && all_cells_in_frame(observed->top.cells) &&
            all_cells_in_frame(observed->lower_left.cells) && all_cells_in_frame(observed->lower_right.cells)) {
            corner = std::move(*observed);
        }

        std::optional<CubePoseProjection> pose;
        bool corner_pose = false;
        bool face_pose = false;
        if (corner.has_value()) {
            if (auto projected = project_cube_from_corner(*corner, frame.size(), seed); projected.has_value()) {
                pose = *projected;
                corner_pose = true;
            }
        } else if (face.has_value()) {
            if (auto projected = project_cube_from_face(*face, frame.size(), seed); projected.has_value()) {
                pose = *projected;
                face_pose = true;
            }
        } else if (pose_only_face.has_value()) {
            if (auto projected = project_cube_from_face(*pose_only_face, frame.size(), seed); projected.has_value()) {
                pose = *projected;
            }
        }
        // Edge fallback exactly as analyze() runs it: only when nothing
        // richer produced a pose (pose-only, colour-free by contract).
        if (!pose.has_value()) {
            std::optional<EdgeHint> edge_hint;
            if (seed_prior) {
                edge_hint = compute_edge_hint(*seed_prior);
            }
            if (auto edge = observe_edge(frame, edge_hint ? &*edge_hint : nullptr); edge.has_value()) {
                if (auto projected = project_cube_from_edge(*edge, frame.size(), seed); projected.has_value()) {
                    pose = *projected;
                }
            }
        }
        if (pose && pose->orientation_valid) {
            input.q_obs = pose->orientation;
            input.corner_view = corner_pose;
            const auto lab_entry = [](const cv::Vec3f& lab) {
                return std::array<float, 3>{lab[0], lab[1], lab[2]};
            };
            if (corner_pose && corner) {
                input.centre_lab_count = 3;
                input.centre_lab[0] = lab_entry(corner->top.lab[cube::model::center_facelet_index]);
                input.centre_lab[1] = lab_entry(corner->lower_left.lab[cube::model::center_facelet_index]);
                input.centre_lab[2] = lab_entry(corner->lower_right.lab[cube::model::center_facelet_index]);
            } else if (face_pose && face) {
                // Positive face-family provenance, as live: neither an
                // edge pose nor a pose-only pass may borrow a coexisting
                // face observation's colour.
                input.centre_lab_count = 1;
                input.centre_lab[0] = lab_entry(face->lab[cube::model::center_facelet_index]);
            }
            seed_prior = PoseSeed{pose->rvec, pose->tvec};
            seed_t_ms = row.t_ms;
        }
        passes.push_back(cached);
    }
    return passes;
}

// Aggregate tracker behaviour over one clip under one config - the CSV
// row's payload. Everything is a plain count or a mean so shard outputs
// concatenate without any cross-row reduction step.
struct SweepMetrics {
    int passes = 0;
    int observations = 0;
    int accepted = 0;
    int anchored = 0;
    int basin = 0;
    int margin = 0;
    int omega_outlier = 0;
    int implausible = 0;
    int slip = 0;
    int locked = 0;
    int coasting = 0;
    int lost = 0;
    int certain = 0;
    // Commit opportunities: passes both locked AND identity-certain FOR
    // THE FRONT FACE (the face-scoped bar - the exact conjunction the
    // capture layer requires before a facelet commit may fire; `certain`
    // above keeps counting the strictly-harder full-24 bar). THE
    // objective the calibration maximises (locked alone can be bought
    // with a low certainty bar; certain alone with a track that never
    // locks).
    int commit = 0;
    // THE disqualifier: slip_evidence force-losses where the face being
    // contradicted (the front face at the kill) had ITSELF reached
    // commit-grade certainty in this epoch. Colour evidence proved wrong
    // an identity the tracker would have let commit - a wrong-identity
    // event, however the aggregates look. Face-specific on purpose: with
    // the face-scoped bar, most epochs certify SOME face during a frontal
    // burst, so "slip anywhere in an ever-certain epoch" would flag the
    // benign sequence the veto exists to produce (certify F frontally,
    // turn, colour catches post-turn trouble on the still-uncertain new
    // face before it ever gains authority). A slip on a never-certified
    // face is the veto doing its job; a slip on a certified one means
    // commit authority was granted to an identity colour later refuted.
    // Any config with certain_slip > 0 is rejected outright, whatever its
    // lock rate.
    int certain_slip = 0;
    // Detector passes the (simulated) idle gate suppressed - the worker
    // budget the gate reclaims; the probe-cadence axis trades this against
    // the reacquisition metrics above.
    int skipped = 0;
    int epochs = 0;
    double theta_sum = 0.0;
    float theta_max = 0.0F;
    double margin_sum = 0.0;
};

// Every swept-settings member as a comparable tuple - the drift check's
// stand-in for the reflection C++ doesn't have. memcmp over the struct
// would be simpler but reads padding bytes, whose values initialization
// does not define. If a member is added to TrackerConfig (or
// SweepSettings) it must be added here too; the sweep-axis anchoring test
// below is what makes forgetting costly to miss (a new swept axis whose
// default can't be verified).
auto settings_fields(const SweepSettings& s) {
    const TrackerConfig& c = s.tracker;
    return std::make_tuple(c.basin_ceiling_deg, c.max_interframe_deg, c.gauge_margin_deg, c.coast_sigma_rate_deg,
                           c.motion_sigma_gain_deg, c.acquire_sigma_rate_deg, c.max_coast_sigma_deg, c.coast_timeout_ms,
                           c.acquire_coast_timeout_ms, c.outlier_omega_deg_s, c.outlier_reanchor_count,
                           c.outlier_consistency_deg, c.obs_pose_ceiling_deg, c.omega_blend, c.omega_decay,
                           c.acquire_frames, c.posterior_lock_ratio, c.centre_sigma_lab, c.unknown_face_log_likelihood,
                           c.max_obs_log_penalty, c.belief_clamp, c.ref_blend, c.ref_consistency_sigma, c.min_dt_s,
                           c.max_dt_s, s.idle_escape_every);
}

SweepMetrics run_variant(const SweepSettings& settings, const std::vector<CachedPass>& inputs) {
    OrientationTracker tracker(settings.tracker);
    // The idle gate simulated at replay cadence, the REAL class with the
    // variant's escape cadence - one implementation, no drift. Wiring
    // mirrors handleAnalysisFinished exactly: the skip verdict was sampled
    // at queue time (so it applies to THIS pass's detector work before the
    // tracker sees the pass), and the gate is updated after the tracker
    // from the post-pass state. Fidelity note: live, a skipped pass never
    // runs the detectors, while here the cached detector output is nulled
    // after the fact - identical outcomes except when the gate would skip
    // a pass whose detectors WOULD have fired, which is precisely the
    // starvation cost the gate axis exists to measure.
    IdleGate gate(settings.idle_escape_every);
    SweepMetrics m;
    int prev_epoch = -1;
    // Wrong-identity bookkeeping (see SweepMetrics::certain_slip), reset
    // when an anchor starts a new epoch: which faces have reached commit
    // grade, and the last front face reported (the slip pass itself
    // reports lost with no front, so the face colour refuted is the one
    // the track last claimed).
    std::array<bool, 6> epoch_certified{};
    int last_front = -1;
    for (const auto& cached : inputs) {
        TrackerPassInput input = cached.input;
        if (gate.skip_next()) {
            ++m.skipped;
            input.q_obs.reset();
            input.corner_view = false;
            input.centre_lab_count = 0;
        }
        const auto out = tracker.on_pass(input);
        ++m.passes;
        m.observations += input.q_obs.has_value() ? 1 : 0;
        if (out.epoch != prev_epoch) {
            prev_epoch = out.epoch;
            epoch_certified.fill(false);
            last_front = -1;
        }
        // Commit grade = the capture layer's actual bar: locked, with the
        // FRONT face's face-scoped certainty (see identity_certain_for).
        const bool commit_grade = out.state == TrackState::locked && out.front_face &&
                                  out.identity_certain_face[static_cast<std::size_t>(*out.front_face)];
        if (out.gate == TrackGate::slip_evidence && last_front >= 0 &&
            epoch_certified[static_cast<std::size_t>(last_front)]) {
            ++m.certain_slip;
        }
        if (commit_grade) {
            epoch_certified[static_cast<std::size_t>(*out.front_face)] = true;
        }
        if (out.front_face) {
            last_front = static_cast<int>(*out.front_face);
        }
        switch (out.gate) {
            case TrackGate::accepted:
                ++m.accepted;
                m.theta_sum += out.theta_deg;
                m.theta_max = std::max(m.theta_max, out.theta_deg);
                m.margin_sum += out.margin_deg;
                break;
            case TrackGate::anchored:
                ++m.anchored;
                break;
            case TrackGate::basin:
                ++m.basin;
                break;
            case TrackGate::margin:
                ++m.margin;
                break;
            case TrackGate::omega_outlier:
                ++m.omega_outlier;
                break;
            case TrackGate::implausible:
                ++m.implausible;
                break;
            case TrackGate::slip_evidence:
                ++m.slip;
                break;
            case TrackGate::no_observation:
                break;
        }
        switch (out.state) {
            case TrackState::locked:
                ++m.locked;
                break;
            case TrackState::coasting:
                ++m.coasting;
                break;
            case TrackState::lost:
                ++m.lost;
                break;
            case TrackState::acquiring:
                break;
        }
        m.certain += out.identity_certain ? 1 : 0;
        m.commit += commit_grade ? 1 : 0;
        m.epochs = std::max(m.epochs, out.epoch);
        gate.on_pass(cached.cool_hue_pixels, out.state == TrackState::lost);
    }
    return m;
}

}  // namespace

// Grid/shard mechanism checks, VISIBLE (fast, corpus-free): the sweep must
// be provably sound before there is any corpus to run it on - a grid whose
// baseline drifted from TrackerConfig's defaults, or a shard filter that
// skips or double-runs a variant, would corrupt every calibration
// conclusion silently.
TEST_CASE("sweep variant grid is anchored to TrackerConfig defaults and partitions cleanly",
          "[tracker_sweep]") {
    const SweepSettings defaults;
    for (const auto& axis : sweep_axes()) {
        // Axis value 0 must BE the default: applying it to a default
        // config must change nothing.
        SweepSettings settings;
        axis.apply(settings, axis.values[0]);
        CHECK(settings_fields(settings) == settings_fields(defaults));
        // Every non-default value must actually move the config.
        for (std::size_t i = 1; i < axis.values.size(); ++i) {
            SweepSettings moved;
            axis.apply(moved, axis.values[i]);
            CHECK(settings_fields(moved) != settings_fields(defaults));
        }
    }

    const auto variants = build_sweep_variants();
    REQUIRE(variants.size() > 1);
    CHECK(variants.front().axis == "baseline");

    // Sharding is a partition: every variant runs in exactly one shard,
    // for any shard count.
    for (int count = 1; count <= 5; ++count) {
        std::vector<int> runs(variants.size(), 0);
        for (int index = 0; index < count; ++index) {
            for (std::size_t v = 0; v < variants.size(); ++v) {
                if (static_cast<int>(v) % count == index) {
                    ++runs[v];
                }
            }
        }
        CHECK(std::all_of(runs.begin(), runs.end(), [](int r) { return r == 1; }));
    }

    // Shard-spec parsing: accepted forms and every malformed shape a shell
    // typo plausibly produces.
    CHECK(parse_shard("0/1").has_value());
    CHECK(parse_shard("3/8").has_value());
    CHECK(parse_shard("3/8")->index == 3);
    CHECK(parse_shard("3/8")->count == 8);
    CHECK_FALSE(parse_shard("8/8").has_value());
    CHECK_FALSE(parse_shard("-1/4").has_value());
    CHECK_FALSE(parse_shard("2").has_value());
    CHECK_FALSE(parse_shard("/4").has_value());
    CHECK_FALSE(parse_shard("2/").has_value());
    CHECK_FALSE(parse_shard("a/b").has_value());
}

TEST_CASE("sweep TrackerConfig variants over the recorded corpus", "[.track-sweep]") {
    const char* root_env = std::getenv("CUBE_TRACK_SWEEP_DIR");
    if (root_env == nullptr) {
        SKIP("CUBE_TRACK_SWEEP_DIR not set - record clips via CUBE_VISION_RECORD_DIR first");
    }
    const std::filesystem::path root(root_env);
    const auto clips = collect_clip_dirs(root);
    if (clips.empty()) {
        SKIP("no recording directories (index.csv) under " << root.string() << " - corpus not recorded yet");
    }

    Shard shard;
    if (const char* shard_env = std::getenv("CUBE_TRACK_SWEEP_SHARD"); shard_env != nullptr) {
        const auto parsed = parse_shard(shard_env);
        // A malformed shard spec must fail loudly: silently defaulting to
        // 0/1 would run the FULL grid in every worker of a parallel launch
        // and the duplicated rows would skew any aggregated statistics.
        REQUIRE(parsed.has_value());
        shard = *parsed;
    }

    // Detector stage once per clip (the expensive part), cached.
    std::vector<std::pair<std::string, std::vector<CachedPass>>> clip_passes;
    for (const auto& clip : clips) {
        auto passes = load_clip_passes(clip);
        std::printf("# clip %s: %zu passes\n", clip.filename().string().c_str(), passes.size());
        if (!passes.empty()) {
            clip_passes.emplace_back(clip.filename().string(), std::move(passes));
        }
    }
    if (clip_passes.empty()) {
        SKIP("corpus directories contain no replayable passes");
    }

    std::printf(
        "variant,axis,value,clip,passes,obs,accepted,anchored,basin,margin,omega,implausible,slip,"
        "locked,coasting,lost,certain,commit,certain_slip,skipped,epochs,lock_rate,mean_theta,max_theta,"
        "mean_margin\n");
    const auto variants = build_sweep_variants();
    std::size_t rows = 0;
    for (std::size_t v = 0; v < variants.size(); ++v) {
        if (static_cast<int>(v) % shard.count != shard.index) {
            continue;
        }
        const auto& variant = variants[v];
        for (const auto& [clip_name, passes] : clip_passes) {
            const auto m = run_variant(variant.settings, passes);
            const double lock_rate = m.passes > 0 ? static_cast<double>(m.locked) / m.passes : 0.0;
            const double mean_theta = m.accepted > 0 ? m.theta_sum / m.accepted : 0.0;
            const double mean_margin = m.accepted > 0 ? m.margin_sum / m.accepted : 0.0;
            std::printf("%zu,%s,%g,%s,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%.4f,%.2f,%.2f,%.2f\n", v,
                        variant.axis.c_str(), static_cast<double>(variant.value), clip_name.c_str(), m.passes,
                        m.observations, m.accepted, m.anchored, m.basin, m.margin, m.omega_outlier, m.implausible,
                        m.slip, m.locked, m.coasting, m.lost, m.certain, m.commit, m.certain_slip, m.skipped, m.epochs,
                        lock_rate, mean_theta, static_cast<double>(m.theta_max), mean_margin);
            ++rows;
        }
    }
    std::printf("# %zu sweep rows (shard %d/%d, %zu variants, %zu clips)\n", rows, shard.index, shard.count,
                variants.size(), clip_passes.size());
    CHECK(rows > 0);
}
