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
// families, same corner-first pose precedence. Two extras for tracker
// fidelity: the pose-seed chain (live passes seed solvePnP from the
// previous held pose; here the previous VALID pose seeds the next pass,
// expired after the same 450 ms hold the live overlay uses), and the
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

// One swept knob: a name for the CSV, the candidate values, and how to
// apply a value to a config. The FIRST value of every axis must be the
// TrackerConfig default - build_sweep_variants() checks this so the grid
// can never silently drift from the struct's shipped defaults (which are
// the plan's constants table, transcribed verbatim).
struct SweepAxis {
    const char* name;
    std::vector<float> values;
    void (*apply)(TrackerConfig&, float);
};

// The sweepable subset of the plan's constants table. Deliberately
// excluded: basin_ceiling_deg and max_coast_sigma_deg (fixed by
// geometry/safety), acquire_frames (fixed), and the belief clamps (their
// failure modes need adversarial captures, not clips). One-factor-at-a-
// time around the baseline rather than a cartesian product: the grid stays
// bounded (a few dozen variants), each row is attributable to one knob,
// and interactions worth chasing show up as two OFAT rows both moving a
// metric - at which point a targeted follow-up sweep is cheap.
std::vector<SweepAxis> sweep_axes() {
    return {
        {"max_interframe_deg", {25.0F, 15.0F, 35.0F}, [](TrackerConfig& c, float v) { c.max_interframe_deg = v; }},
        {"gauge_margin_deg", {15.0F, 10.0F, 20.0F}, [](TrackerConfig& c, float v) { c.gauge_margin_deg = v; }},
        {"coast_sigma_rate_deg", {4.0F, 2.0F, 6.0F}, [](TrackerConfig& c, float v) { c.coast_sigma_rate_deg = v; }},
        {"motion_sigma_gain_deg", {40.0F, 20.0F, 80.0F}, [](TrackerConfig& c, float v) { c.motion_sigma_gain_deg = v; }},
        {"coast_timeout_ms", {700.0F, 500.0F, 1000.0F},
         [](TrackerConfig& c, float v) { c.coast_timeout_ms = static_cast<std::int64_t>(v); }},
        {"outlier_omega_deg_s", {400.0F, 300.0F, 600.0F}, [](TrackerConfig& c, float v) { c.outlier_omega_deg_s = v; }},
        {"omega_blend", {0.3F, 0.2F, 0.5F}, [](TrackerConfig& c, float v) { c.omega_blend = v; }},
        {"omega_decay", {0.9F, 0.8F, 0.95F}, [](TrackerConfig& c, float v) { c.omega_decay = v; }},
        {"posterior_lock_ratio", {100.0F, 30.0F, 300.0F}, [](TrackerConfig& c, float v) { c.posterior_lock_ratio = v; }},
        {"centre_sigma_lab", {12.0F, 8.0F, 16.0F}, [](TrackerConfig& c, float v) { c.centre_sigma_lab = v; }},
        {"obs_pose_ceiling_deg", {100.0F, 85.0F, 115.0F}, [](TrackerConfig& c, float v) { c.obs_pose_ceiling_deg = v; }},
    };
}

struct SweepVariant {
    std::string axis;   // "baseline" for the untouched config
    float value = 0.0F; // the moved knob's value (baseline: 0, unused)
    TrackerConfig config;
};

// Deterministic variant enumeration: baseline first, then each axis's
// non-default values in axis order. Determinism is what makes the shard
// filter a partition rather than a lottery.
std::vector<SweepVariant> build_sweep_variants() {
    std::vector<SweepVariant> variants;
    variants.push_back({"baseline", 0.0F, TrackerConfig{}});
    for (const auto& axis : sweep_axes()) {
        for (std::size_t i = 1; i < axis.values.size(); ++i) {
            SweepVariant variant{axis.name, axis.values[i], TrackerConfig{}};
            axis.apply(variant.config, axis.values[i]);
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

// Detector stage, run ONCE per clip (see the cost model above): every
// surviving recorded pass becomes the exact TrackerPassInput the live
// handleAnalysisFinished would have built for it.
std::vector<TrackerPassInput> load_clip_passes(const std::filesystem::path& dir) {
    std::vector<TrackerPassInput> passes;
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

        TrackerPassInput input;
        input.t_ms = row.t_ms;
        input.motion_score = stability.update(frame).motion_score;

        const auto all_cells_in_frame = [&](const auto& cells) {
            return std::all_of(cells.begin(), cells.end(),
                               [&](const StickerQuad& cell) { return cell_in_frame(cell, frame); });
        };
        std::optional<FaceObservation> face;
        if (auto observed = observe_face(frame); observed.has_value() && all_cells_in_frame(observed->cells)) {
            face = std::move(*observed);
        }
        std::optional<CornerObservation> corner;
        if (auto observed = observe_corner(frame);
            observed.has_value() && all_cells_in_frame(observed->top.cells) &&
            all_cells_in_frame(observed->lower_left.cells) && all_cells_in_frame(observed->lower_right.cells)) {
            corner = std::move(*observed);
        }

        // Live seed lifecycle: the held pose expires with the overlay.
        if (seed_prior && row.t_ms - seed_t_ms > kSeedHoldMs) {
            seed_prior.reset();
        }
        const PoseSeed* seed = seed_prior ? &*seed_prior : nullptr;

        std::optional<CubePoseProjection> pose;
        bool corner_pose = false;
        if (corner.has_value()) {
            if (auto projected = project_cube_from_corner(*corner, frame.size(), seed); projected.has_value()) {
                pose = *projected;
                corner_pose = true;
            }
        } else if (face.has_value()) {
            if (auto projected = project_cube_from_face(*face, frame.size(), seed); projected.has_value()) {
                pose = *projected;
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
            } else if (!corner_pose && face) {
                input.centre_lab_count = 1;
                input.centre_lab[0] = lab_entry(face->lab[cube::model::center_facelet_index]);
            }
            seed_prior = PoseSeed{pose->rvec, pose->tvec};
            seed_t_ms = row.t_ms;
        }
        passes.push_back(input);
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
    int epochs = 0;
    double theta_sum = 0.0;
    float theta_max = 0.0F;
    double margin_sum = 0.0;
};

// Every TrackerConfig member as a comparable tuple - the drift check's
// stand-in for the reflection C++ doesn't have. memcmp over the struct
// would be simpler but reads padding bytes, whose values initialization
// does not define. If a member is added to TrackerConfig it must be added
// here too; the sweep-axis anchoring test below is what makes forgetting
// costly to miss (a new swept axis whose default can't be verified).
auto config_fields(const TrackerConfig& c) {
    return std::make_tuple(c.basin_ceiling_deg, c.max_interframe_deg, c.gauge_margin_deg, c.coast_sigma_rate_deg,
                           c.motion_sigma_gain_deg, c.max_coast_sigma_deg, c.coast_timeout_ms, c.outlier_omega_deg_s,
                           c.outlier_reanchor_count, c.outlier_consistency_deg, c.obs_pose_ceiling_deg, c.omega_blend,
                           c.omega_decay, c.acquire_frames, c.posterior_lock_ratio, c.centre_sigma_lab,
                           c.unknown_face_log_likelihood, c.max_obs_log_penalty, c.belief_clamp, c.ref_blend,
                           c.min_dt_s, c.max_dt_s);
}

SweepMetrics run_variant(const TrackerConfig& config, const std::vector<TrackerPassInput>& inputs) {
    OrientationTracker tracker(config);
    SweepMetrics m;
    for (const auto& input : inputs) {
        const auto out = tracker.on_pass(input);
        ++m.passes;
        m.observations += input.q_obs.has_value() ? 1 : 0;
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
        m.epochs = std::max(m.epochs, out.epoch);
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
    const TrackerConfig defaults;
    for (const auto& axis : sweep_axes()) {
        // Axis value 0 must BE the default: applying it to a default
        // config must change nothing.
        TrackerConfig config;
        axis.apply(config, axis.values[0]);
        CHECK(config_fields(config) == config_fields(defaults));
        // Every non-default value must actually move the config.
        for (std::size_t i = 1; i < axis.values.size(); ++i) {
            TrackerConfig moved;
            axis.apply(moved, axis.values[i]);
            CHECK(config_fields(moved) != config_fields(defaults));
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
    std::vector<std::pair<std::string, std::vector<TrackerPassInput>>> clip_passes;
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
        "locked,coasting,lost,certain,epochs,lock_rate,mean_theta,max_theta,mean_margin\n");
    const auto variants = build_sweep_variants();
    std::size_t rows = 0;
    for (std::size_t v = 0; v < variants.size(); ++v) {
        if (static_cast<int>(v) % shard.count != shard.index) {
            continue;
        }
        const auto& variant = variants[v];
        for (const auto& [clip_name, passes] : clip_passes) {
            const auto m = run_variant(variant.config, passes);
            const double lock_rate = m.passes > 0 ? static_cast<double>(m.locked) / m.passes : 0.0;
            const double mean_theta = m.accepted > 0 ? m.theta_sum / m.accepted : 0.0;
            const double mean_margin = m.accepted > 0 ? m.margin_sum / m.accepted : 0.0;
            std::printf("%zu,%s,%g,%s,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%.4f,%.2f,%.2f,%.2f\n", v,
                        variant.axis.c_str(), static_cast<double>(variant.value), clip_name.c_str(), m.passes,
                        m.observations, m.accepted, m.anchored, m.basin, m.margin, m.omega_outlier, m.implausible,
                        m.slip, m.locked, m.coasting, m.lost, m.certain, m.epochs, lock_rate, mean_theta,
                        static_cast<double>(m.theta_max), mean_margin);
            ++rows;
        }
    }
    std::printf("# %zu sweep rows (shard %d/%d, %zu variants, %zu clips)\n", rows, shard.index, shard.count,
                variants.size(), clip_passes.size());
    CHECK(rows > 0);
}
