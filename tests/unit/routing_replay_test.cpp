// Routing replay + RoutingConfig sweep harnesses - hidden tags, never part
// of the normal suite. The Phase 0 item 6 instrument (docs/pipeline-plan.md):
// the registration layer is the measured daylight blocker, and
// RoutingConfig's epoch-registration bars were never swept because no
// routing replay harness existed (capture_routing.hpp, the oriented
// constants' evidence note). These two harnesses are that harness.
//
//   set CUBE_ROUTING_REPLAY_DIR=<recording dir, or root of recording dirs>
//   cube_unit_tests.exe "[.routing-replay]"
//
//   set CUBE_ROUTING_SWEEP_DIR=<same clip semantics>
//   cube_unit_tests.exe "[.routing-sweep]"
//
// [.routing-replay] runs the FULL live-fidelity replay loop per clip - the
// same detector cascade / tracker / identity-keyed aggregation / capture
// gates / CaptureRouter drive as [.cross-session], with the gate constants
// from the same shared header (capture_gates.hpp) the live app consumes -
// and emits the ROUTING story: one CSV row per commit ATTEMPT (outcome,
// escalation flags, and the full EpochRegistration diagnostics when the
// recovery path made a registration attempt), plus a per-clip summary that
// attributes every refusal to its gate: outcome counts, deferrals split by
// registration status (none seen / insufficient / ambiguous), quarantines
// split duplicate-centre vs verification-conflict, escalation counts,
// epochs seen, and the final store. A repeat of "0 of 6 faces, cause
// unattributed" is impossible from this output by construction, and the
// test FAILS outright on any outcome or registration status it cannot name.
//
// [.routing-sweep] re-derives the registration bars' placement evidence:
// the full-fidelity loop runs ONCE per clip while RECORDING the router's
// input event stream (epoch changes in arrival order plus every commit
// attempt's body_face / CapturedFace / t_ms, each sampled BEFORE the router
// saw it), and a grid of RoutingConfig variants then re-drives fresh
// CaptureRouter instances over that canned stream - one summary CSV row per
// (variant, clip). Detector-once/sweep-many, the same cost model as
// [.track-sweep]: the detector cascade is tens of ms per frame, a canned
// re-drive is microseconds per event.
//
// THE CANNED-STREAM APPROXIMATION, stated honestly because it bounds what
// the sweep can prove. In the live loop the router's outcomes feed BACK
// into the pipeline: a force_lost commit force-loses the tracker (changing
// every later epoch number and capture window), a layer_turn clears the
// store and the aggregators, a holdoff leaves a window accumulating that
// any other outcome consumes, and the router's own fill count gates both
// the identity waiver (< 2 slots) and the aggregation stop (6 slots). A
// canned re-drive CANNOT replay any of that feedback - the event stream is
// frozen as the DEFAULT config's run produced it - so variants that change
// escalation or fill behaviour see the attempts the default config
// generated, not the attempts they would have generated live. Variant rows
// are therefore comparative evidence about the registration bars, not
// absolute replays. The anchor: the default-config row is driven through
// the identical canned path and CHECKed field-for-field against the
// full-fidelity loop's own tallies (which are what [.routing-replay]
// reports for the same clip), and the per-clip default-agreement line in
// the output says whether that held.
//
// Deliberately self-contained rather than sharing the loop with
// cross_session_test.cpp: that harness's tee format is consumed by three
// other probes, and a refactor there to serve this file would put its
// byte-format stability at risk for no fidelity gain. The loop below is a
// line-faithful replica of its detector/tracker/capture stages (same
// seed/hint holds, same pose-source routing, same gate order); divergence
// shows up immediately as a default-agreement mismatch against the numbers
// [.cross-session] prints for the same clip.
//
// Fidelity notes inherited from that replica, so they are on the record
// here too: replay is analysis-scale (pose rotation unaffected -
// intrinsics derive from frame size); the pose seed and corner/edge hints
// hold for the same kOverlayHoldMs the live app holds them; the routed
// observation family is decided by POSE SOURCE, never by mere observation
// presence; and on_tracker_reset() is never driven, because a clip is one
// uninterrupted session - a routing force_lost calls
// OrientationTracker::force_lost(), which continues the epoch counter
// rather than restarting it, so the dead-gauge hazard on_tracker_reset()
// exists for cannot arise in replay.
//
// Skips (never fails) when no recording is present: the corpus is recorded
// on demand from live hardware and does not ship with the repo.
//
// std::getenv is fine here (single-threaded harness, read-only).

#include <catch2/catch_test_macros.hpp>

#include <cube/model/face.hpp>
#include <cube/model/facelet.hpp>
#include <cube/vision/capture_gates.hpp>
#include <cube/vision/capture_routing.hpp>
#include <cube/vision/color_resolution.hpp>
#include <cube/vision/corner_observation.hpp>
#include <cube/vision/edge_observation.hpp>
#include <cube/vision/face_observation.hpp>
#include <cube/vision/frame_recording.hpp>
#include <cube/vision/orientation_lattice.hpp>
#include <cube/vision/orientation_tracker.hpp>
#include <cube/vision/pose_estimation.hpp>
#include <cube/vision/stability.hpp>
#include <cube/vision/temporal_fusion.hpp>

#include <opencv2/imgcodecs.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
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

namespace {

// Capture-gate constants: the shipping policy, from the same shared header
// the live app consumes (capture_gates.hpp) - nothing re-transcribed.
using cube::vision::gates::kIdentityAggStaleMs;
using cube::vision::gates::kMaxCaptureMotionScore;
using cube::vision::gates::kMaxCaptureRotDeg;
using cube::vision::gates::kMaxCornerFlankDeg;
using cube::vision::gates::kMaxFaceCaptureDeg;
using cube::vision::gates::kMinCaptureAccepts;
constexpr std::int64_t kSeedHoldMs = cube::vision::gates::kOverlayHoldMs;

// Per-face aggregator bookkeeping, mirroring ScanSession::AggregatorMeta
// (and cross_session_test's replica of it).
struct AggregatorMeta {
    bool has_window = false;
    std::array<int, 9> window_map{};
    std::int64_t last_add_ms = 0;
    bool corner_fed = false;
};

// Name lookups return nullptr on an enum value they do not know, and the
// harnesses FAIL on nullptr: an outcome or registration status this
// instrument cannot attribute must never be silently binned as "other" -
// unattributed refusals are the exact blindness it exists to end.
[[nodiscard]] const char* outcome_name(cube::vision::CommitOutcome outcome) {
    switch (outcome) {
        case cube::vision::CommitOutcome::filled:
            return "filled";
        case cube::vision::CommitOutcome::updated:
            return "updated";
        case cube::vision::CommitOutcome::quarantined:
            return "quarantined";
        case cube::vision::CommitOutcome::deferred:
            return "deferred";
        case cube::vision::CommitOutcome::holdoff:
            return "holdoff";
    }
    return nullptr;
}

[[nodiscard]] const char* reg_status_name(cube::vision::EpochRegistration::Status status) {
    switch (status) {
        case cube::vision::EpochRegistration::Status::resolved:
            return "resolved";
        case cube::vision::EpochRegistration::Status::ambiguous:
            return "ambiguous";
        case cube::vision::EpochRegistration::Status::insufficient:
            return "insufficient";
    }
    return nullptr;
}

[[nodiscard]] const char* reg_basis_name(cube::vision::EpochRegistration::Basis basis) {
    switch (basis) {
        case cube::vision::EpochRegistration::Basis::unique:
            return "unique";
        case cube::vision::EpochRegistration::Basis::oriented:
            return "oriented";
        case cube::vision::EpochRegistration::Basis::content_equivalent:
            return "content-equiv";
    }
    return nullptr;
}

[[nodiscard]] char color_letter(cube::model::FacetColor color) {
    switch (color) {
        case cube::model::FacetColor::white:
            return 'W';
        case cube::model::FacetColor::yellow:
            return 'Y';
        case cube::model::FacetColor::red:
            return 'R';
        case cube::model::FacetColor::orange:
            return 'O';
        case cube::model::FacetColor::green:
            return 'G';
        case cube::model::FacetColor::blue:
            return 'B';
        case cube::model::FacetColor::unknown:
            return '?';
    }
    return '?';
}

// Per-clip refusal attribution: every commit attempt lands in exactly one
// outcome bucket, every deferral in exactly one registration-status bucket,
// every quarantine in exactly one kind bucket. `unattributed` counts enum
// values the name lookups above refused - the tests REQUIRE it zero.
struct RoutingTally {
    int attempts = 0;
    std::array<int, 5> outcomes{};  // indexed by CommitOutcome
    int deferred_no_registration = 0;
    int deferred_insufficient = 0;
    int deferred_ambiguous = 0;
    int deferred_resolved = 0;  // never expected; counted, not hidden
    int quarantine_duplicate_centre = 0;
    int quarantine_verify_conflict = 0;
    int force_lost = 0;
    int layer_turn = 0;
    std::array<int, 3> reg_status{};  // every registration attempt seen
    std::array<int, 3> reg_basis{};   // resolved registrations only
    int epochs_seen = 0;
    int filled_slots = 0;
    std::string slot_centres;  // "U:W;R:G;..." store-face:centre-class, fill order
    int unattributed = 0;

    // PRE-ROUTER starvation attribution: converged aggregator windows that
    // never reached router.commit, counted at the gate that refused them.
    // A clip with zero attempts is explained by THESE numbers (the
    // daylight-wall lesson: "no attempts" must be attributable too).
    // Live-loop only - the canned sweep starts from offered commits, so
    // these are deliberately excluded from tally_fields' agreement check.
    int starved_pass_gate = 0;  // window converged but track not locked / streak short
    int starved_identity = 0;   // per-face identity not commit-grade (and not waived)
    int starved_frontal = 0;    // face too oblique (kMaxFaceCaptureDeg / corner flank)
    int starved_motion = 0;     // pass rotation or motion score over bounds
    int starved_unknown = 0;    // > FaceAggregator::kMaxUnstableCells unknown cells
};

// Field-for-field comparison surface for the default-agreement check. All
// members on purpose: a canned re-drive that diverges from the live loop in
// ANY tallied dimension is a fidelity bug, not a rounding difference.
[[nodiscard]] auto tally_fields(const RoutingTally& t) {
    return std::tie(t.attempts, t.outcomes, t.deferred_no_registration, t.deferred_insufficient,
                    t.deferred_ambiguous, t.deferred_resolved, t.quarantine_duplicate_centre,
                    t.quarantine_verify_conflict, t.force_lost, t.layer_turn, t.reg_status,
                    t.reg_basis, t.epochs_seen, t.filled_slots, t.slot_centres, t.unattributed);
}

// One commit result into the tally - shared verbatim by the live loop and
// the canned re-drive so the two countings can never diverge.
void tally_commit(RoutingTally& tally, const cube::vision::CommitResult& result) {
    using Status = cube::vision::EpochRegistration::Status;
    ++tally.attempts;
    if (outcome_name(result.outcome) == nullptr) {
        ++tally.unattributed;
    } else {
        ++tally.outcomes[static_cast<std::size_t>(result.outcome)];
    }
    if (result.registration.has_value()) {
        const auto& reg = *result.registration;
        if (reg_status_name(reg.status) == nullptr) {
            ++tally.unattributed;
        } else {
            ++tally.reg_status[static_cast<std::size_t>(reg.status)];
            if (reg.status == Status::resolved) {
                if (reg_basis_name(reg.basis) == nullptr) {
                    ++tally.unattributed;
                } else {
                    ++tally.reg_basis[static_cast<std::size_t>(reg.basis)];
                }
            }
        }
    }
    if (result.outcome == cube::vision::CommitOutcome::deferred) {
        if (!result.registration.has_value()) {
            ++tally.deferred_no_registration;
        } else if (result.registration->status == Status::insufficient) {
            ++tally.deferred_insufficient;
        } else if (result.registration->status == Status::ambiguous) {
            ++tally.deferred_ambiguous;
        } else if (result.registration->status == Status::resolved) {
            ++tally.deferred_resolved;
        } else {
            ++tally.unattributed;
        }
    }
    if (result.outcome == cube::vision::CommitOutcome::quarantined) {
        if (result.duplicate_centre) {
            ++tally.quarantine_duplicate_centre;
        } else {
            ++tally.quarantine_verify_conflict;
        }
    }
    tally.force_lost += result.force_lost ? 1 : 0;
    tally.layer_turn += result.layer_turn ? 1 : 0;
}

// Final store snapshot into the tally: filled count plus each slot's store
// face and centre class (the "what did the scan actually end with" line).
void tally_final_store(RoutingTally& tally, const cube::vision::CaptureRouter& router) {
    tally.filled_slots = router.filled_count();
    tally.slot_centres.clear();
    for (int fill_index = 0; fill_index < router.filled_count(); ++fill_index) {
        const cube::vision::CapturedFace* content = router.slot_at(fill_index);
        const auto store_face = router.face_at(fill_index);
        if (content == nullptr || !store_face.has_value()) {
            continue;
        }
        if (!tally.slot_centres.empty()) {
            tally.slot_centres += ';';
        }
        tally.slot_centres += "URFDLB"[static_cast<std::size_t>(*store_face)];
        tally.slot_centres += ':';
        tally.slot_centres += color_letter(
            cube::vision::classify_lab(content->lab[cube::model::center_facelet_index]).color);
    }
    if (tally.slot_centres.empty()) {
        tally.slot_centres = "-";
    }
}

// One commit attempt as the live loop made it, keys sampled BEFORE the
// router saw the capture, result recorded after.
struct AttemptRecord {
    int pass = 0;
    std::int64_t t_ms = 0;
    int epoch = 0;
    cube::model::Face body_face{};
    cube::vision::CapturedFace captured;
    cube::vision::CommitResult result;
};

// The canned router-input stream: epoch changes and commit attempts,
// interleaved in the exact order the live loop drove the router. Epoch
// events are recorded only at CHANGES - CaptureRouter::on_epoch early-
// returns on an unchanged number, so per-pass repeats are no-ops by
// contract and replaying only the changes is exact, not approximate.
struct CannedEvent {
    bool is_epoch = false;
    int epoch = 0;                  // is_epoch only
    std::size_t attempt_index = 0;  // !is_epoch only, into LiveLoopResult::attempts
};

struct LiveLoopResult {
    std::vector<CannedEvent> events;
    std::vector<AttemptRecord> attempts;
    RoutingTally tally;
    std::size_t replayed = 0;
};

// The corpus root is either a single recording dir (has index.csv) or a
// directory of them - the same clip semantics as [.track-sweep].
std::vector<std::filesystem::path> collect_clip_dirs(const std::filesystem::path& root) {
    std::vector<std::filesystem::path> clips;
    std::error_code ec;
    const auto index_name = std::string(cube::vision::recording::kIndexCsvName);
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

// The full live-fidelity loop over one recorded clip: a line-faithful
// replica of cross_session_test's detector/tracker/capture stages (see the
// file header for why it is replicated rather than shared), recording the
// router-input event stream and tallying every commit attempt.
LiveLoopResult run_live_fidelity_loop(const std::filesystem::path& dir) {
    namespace recording = cube::vision::recording;
    using cube::vision::observe_corner;
    using cube::vision::observe_edge;
    using cube::vision::observe_face;
    using cube::vision::project_cube_from_corner;
    using cube::vision::project_cube_from_edge;
    using cube::vision::project_cube_from_face;

    LiveLoopResult result;
    std::ifstream index_stream(dir / std::string(recording::kIndexCsvName));
    if (!index_stream) {
        return result;
    }
    const auto rows = recording::read_index(index_stream);
    int max_pass = 0;
    for (const auto& row : rows) {
        max_pass = std::max(max_pass, row.pass);
    }

    // Detector acceptance gate: cell CENTRE in frame (what analyze() uses
    // to accept an observation at all).
    const auto cell_in_frame = [](const cube::vision::StickerQuad& cell, const cv::Mat& bounds) {
        cv::Point2f center(0.0F, 0.0F);
        for (const auto& corner : cell.corners) {
            center += corner;
        }
        center *= 0.25F;
        return center.x >= 0.0F && center.y >= 0.0F && center.x < static_cast<float>(bounds.cols) &&
               center.y < static_cast<float>(bounds.rows);
    };
    // Capture acceptance gate: ALL FOUR corners in frame (the strict
    // full-visibility flag production computes for the routed family).
    const auto quad_fully_in_frame = [](const cube::vision::StickerQuad& cell,
                                        const cv::Mat& bounds) {
        return std::all_of(cell.corners.begin(), cell.corners.end(),
                           [&bounds](const cv::Point2f& corner) {
                               return corner.x >= 0.0F && corner.y >= 0.0F &&
                                      corner.x < static_cast<float>(bounds.cols) &&
                                      corner.y < static_cast<float>(bounds.rows);
                           });
    };

    std::optional<cube::vision::PoseSeed> seed_prior;
    std::int64_t seed_t_ms = 0;

    cube::vision::StabilityTracker stability;
    cube::vision::OrientationTracker tracker;

    // The real capture layer: real aggregator windows, real router.
    std::array<cube::vision::FaceAggregator, cube::model::face_count> aggregators{};
    std::array<AggregatorMeta, cube::model::face_count> metas{};
    cube::vision::CaptureRouter router;
    std::array<bool, cube::model::face_count> capture_identity_ok{};
    int agg_epoch = -1;
    int consec_accepts = 0;
    std::array<float, 4> last_track_q{1.0F, 0.0F, 0.0F, 0.0F};
    bool has_last_track_q = false;
    int last_epoch = -1;

    for (const auto& row : rows) {
        if (recording::frame_overwritten(row.pass, max_pass)) {
            continue;
        }
        const cv::Mat frame =
            cv::imread((dir / recording::frame_filename(row.pass)).string(), cv::IMREAD_COLOR);
        if (frame.empty() || frame.cols != row.w || frame.rows != row.h) {
            continue;
        }

        if (seed_prior && row.t_ms - seed_t_ms > kSeedHoldMs) {
            seed_prior.reset();
        }
        const cube::vision::PoseSeed* seed = seed_prior ? &*seed_prior : nullptr;

        const auto all_cells_in_frame = [&](const auto& cells) {
            return std::all_of(
                cells.begin(), cells.end(),
                [&](const cube::vision::StickerQuad& cell) { return cell_in_frame(cell, frame); });
        };
        const auto all_quads_full = [&](const auto& cells) {
            return std::all_of(cells.begin(), cells.end(),
                               [&](const cube::vision::StickerQuad& cell) {
                                   return quad_fully_in_frame(cell, frame);
                               });
        };

        const auto observed = observe_face(frame);
        const bool face_gated = observed.has_value() && !all_cells_in_frame(observed->cells);
        std::optional<cube::vision::FaceObservation> face;
        std::optional<cube::vision::FaceObservation> pose_only_face;
        if (observed.has_value() && !face_gated) {
            if (observed->pose_only) {
                pose_only_face = *observed;
            } else {
                face = *observed;
            }
        }
        std::optional<cube::vision::CornerHint> corner_hint;
        if (seed_prior) {
            corner_hint = cube::vision::compute_corner_hint(*seed_prior);
        }
        const auto observed_corner = observe_corner(frame, corner_hint ? &*corner_hint : nullptr);
        const bool corner_gated = observed_corner.has_value() &&
                                  !(all_cells_in_frame(observed_corner->top.cells) &&
                                    all_cells_in_frame(observed_corner->lower_left.cells) &&
                                    all_cells_in_frame(observed_corner->lower_right.cells));
        std::optional<cube::vision::CornerObservation> corner;
        if (observed_corner.has_value() && !corner_gated) {
            corner = *observed_corner;
        }

        // Pose, corner-first, exactly as analyze() orders it. The routed
        // family is whichever estimator ACTUALLY produced the pose - the
        // pose_source distinction, not mere observation presence.
        std::optional<cube::vision::CubePoseProjection> pose;
        bool corner_pose = false;
        bool face_pose = false;
        if (corner.has_value()) {
            if (const auto projected = project_cube_from_corner(*corner, frame.size(), seed);
                projected.has_value()) {
                pose = *projected;
                corner_pose = true;
            }
        } else if (face.has_value()) {
            if (const auto projected = project_cube_from_face(*face, frame.size(), seed);
                projected.has_value()) {
                pose = *projected;
                face_pose = true;
            }
        } else if (pose_only_face.has_value()) {
            if (const auto projected = project_cube_from_face(*pose_only_face, frame.size(), seed);
                projected.has_value()) {
                pose = *projected;
            }
        }
        if (!pose.has_value()) {
            std::optional<cube::vision::EdgeHint> edge_hint;
            if (seed_prior) {
                edge_hint = cube::vision::compute_edge_hint(*seed_prior);
            }
            if (const auto edge = observe_edge(frame, edge_hint ? &*edge_hint : nullptr);
                edge.has_value()) {
                if (const auto projected = project_cube_from_edge(*edge, frame.size(), seed);
                    projected.has_value()) {
                    pose = *projected;
                }
            }
        }
        if (pose.has_value() && pose->orientation_valid) {
            seed_prior = cube::vision::PoseSeed{pose->rvec, pose->tvec};
            seed_t_ms = row.t_ms;
        }

        // Per-family strict full-visibility, computed on the routed
        // family's own cells (production keeps these per family so a
        // non-routed family's clipping cannot veto the routed one).
        const bool face_full_quads = face.has_value() && all_quads_full(face->cells);
        const bool corner_full_quads = corner.has_value() && all_quads_full(corner->top.cells) &&
                                       all_quads_full(corner->lower_left.cells) &&
                                       all_quads_full(corner->lower_right.cells);

        cube::vision::TrackerPassInput track_in;
        track_in.t_ms = row.t_ms;
        const float motion_score = stability.update(frame).motion_score;
        track_in.motion_score = motion_score;
        if (pose.has_value() && pose->orientation_valid) {
            track_in.q_obs = pose->orientation;
            track_in.corner_view = corner_pose;
            const auto lab_entry = [](const cv::Vec3f& lab) {
                return std::array<float, 3>{lab[0], lab[1], lab[2]};
            };
            if (corner_pose && corner.has_value()) {
                track_in.centre_lab_count = 3;
                track_in.centre_lab[0] =
                    lab_entry(corner->top.lab[cube::model::center_facelet_index]);
                track_in.centre_lab[1] =
                    lab_entry(corner->lower_left.lab[cube::model::center_facelet_index]);
                track_in.centre_lab[2] =
                    lab_entry(corner->lower_right.lab[cube::model::center_facelet_index]);
            } else if (face_pose && face.has_value()) {
                track_in.centre_lab_count = 1;
                track_in.centre_lab[0] = lab_entry(face->lab[cube::model::center_facelet_index]);
            }
        }
        const auto track = tracker.on_pass(track_in);
        ++result.replayed;

        // --- Capture layer, in handleAnalysisFinished's order --------------
        if (track.epoch != last_epoch) {
            last_epoch = track.epoch;
            ++result.tally.epochs_seen;
            result.events.push_back(CannedEvent{true, track.epoch, 0});
        }
        router.on_epoch(track.epoch);
        if (track.epoch != agg_epoch) {
            agg_epoch = track.epoch;
            for (auto& aggregator : aggregators) {
                aggregator.reset();
            }
            metas.fill(AggregatorMeta{});
            consec_accepts = 0;
            has_last_track_q = false;
        }
        const bool pass_accepted = track.gate == cube::vision::TrackGate::accepted ||
                                   track.gate == cube::vision::TrackGate::anchored;
        const float pass_rotation_deg =
            pass_accepted && has_last_track_q
                ? cube::vision::quat_angle_deg(track.q_track, last_track_q)
                : 0.0F;
        if (pass_accepted) {
            last_track_q = track.q_track;
            has_last_track_q = true;
            ++consec_accepts;
        } else {
            consec_accepts = 0;
        }

        const bool identity_waived = router.filled_count() < 2;
        for (std::size_t f = 0; f < capture_identity_ok.size(); ++f) {
            capture_identity_ok[f] = track.identity_certain_face[f] || identity_waived;
        }
        const bool capture_gate_ok = track.state == cube::vision::TrackState::locked &&
                                     consec_accepts >= kMinCaptureAccepts && pass_accepted &&
                                     track.lattice_index >= 0;

        if (capture_gate_ok && router.filled_count() < static_cast<int>(cube::model::face_count)) {
            const auto lattice = static_cast<std::size_t>(track.lattice_index);
            const auto add_keyed = [&](const cube::vision::FaceCellMap& map,
                                       const cube::vision::FaceObservation& observation,
                                       bool from_corner, bool full_quads) {
                if (!full_quads) {
                    return;
                }
                const auto face_index = static_cast<std::size_t>(map.body_face);
                if (!capture_identity_ok[face_index]) {
                    return;
                }
                auto& aggregator = aggregators[face_index];
                auto& meta = metas[face_index];
                if (meta.has_window && (row.t_ms - meta.last_add_ms > kIdentityAggStaleMs ||
                                        meta.window_map != map.cell)) {
                    aggregator.reset();
                    meta = AggregatorMeta{};
                }
                cube::vision::FaceObservation permuted = observation;
                for (std::size_t obs = 0; obs < permuted.lab.size(); ++obs) {
                    const auto solver_index = static_cast<std::size_t>(map.cell[obs]);
                    permuted.lab[solver_index] = observation.lab[obs];
                    permuted.cells[solver_index] = observation.cells[obs];
                }
                aggregator.add(permuted);
                meta.has_window = true;
                meta.window_map = map.cell;
                meta.last_add_ms = row.t_ms;
                meta.corner_fed = meta.corner_fed || from_corner;
            };
            if (corner_pose && corner.has_value()) {
                const std::array<const cube::vision::CornerFaceView*, 3> views{
                    &corner->top, &corner->lower_left, &corner->lower_right};
                for (std::size_t view = 0; view < views.size(); ++view) {
                    cube::vision::FaceObservation slot_obs;
                    slot_obs.cells = views[view]->cells;
                    slot_obs.lab = views[view]->lab;
                    slot_obs.face_quad = views[view]->quad;
                    slot_obs.geometry_confidence = corner->geometry_confidence;
                    add_keyed(cube::vision::kCornerObsToBody[lattice][view], slot_obs,
                              /*from_corner=*/true, corner_full_quads);
                }
            } else if (face_pose && face.has_value()) {
                add_keyed(cube::vision::kFaceObsToBody[lattice], *face, /*from_corner=*/false,
                          face_full_quads);
            }
        }

        // --- tryCommitConverged, one attempt record per router commit ------
        if (!capture_gate_ok) {
            // A converged window refused here is starvation, and it must be
            // attributed just like a router refusal (a clip can produce
            // ZERO attempts - cardboard steady-02 does - and the summary
            // still has to say why).
            for (auto& aggregator : aggregators) {
                result.tally.starved_pass_gate += aggregator.converged() ? 1 : 0;
            }
            continue;
        }
        for (std::size_t f = 0; f < aggregators.size(); ++f) {
            auto& aggregator = aggregators[f];
            auto& meta = metas[f];
            if (!aggregator.converged()) {
                continue;
            }
            const auto body_face = static_cast<cube::model::Face>(f);
            if (!capture_identity_ok[f]) {
                ++result.tally.starved_identity;
                continue;
            }
            const float frontal = cube::vision::face_frontal_deg(track.q_track, body_face);
            if (frontal > (meta.corner_fed ? kMaxCornerFlankDeg : kMaxFaceCaptureDeg)) {
                ++result.tally.starved_frontal;
                continue;
            }
            if (pass_rotation_deg > kMaxCaptureRotDeg || motion_score >= kMaxCaptureMotionScore) {
                ++result.tally.starved_motion;
                continue;
            }
            const auto captured = aggregator.capture();
            // Live bound, exactly (scan_session.cpp tryCommitConverged): up
            // to kMaxUnstableCells unknown cells pass - a glare highlight
            // or a fingertip - and only MORE than that blocks. The first
            // cut of this harness blocked on ANY unknown cell, which on
            // real cardboard footage (~11% unknown) starved clips the live
            // path would have committed.
            const auto unknown_cells = static_cast<std::size_t>(
                std::count_if(captured.lab.begin(), captured.lab.end(), [](const cv::Vec3f& lab) {
                    return cube::vision::classify_lab(lab).color ==
                           cube::model::FacetColor::unknown;
                }));
            if (unknown_cells > cube::vision::FaceAggregator::kMaxUnstableCells) {
                ++result.tally.starved_unknown;
                continue;
            }

            // The attempt's router inputs, sampled BEFORE the router sees
            // them - the canned stream must carry what was OFFERED, not
            // anything the outcome could have influenced.
            AttemptRecord attempt;
            attempt.pass = row.pass;
            attempt.t_ms = row.t_ms;
            attempt.epoch = track.epoch;
            attempt.body_face = body_face;
            attempt.captured = captured;

            const auto commit = router.commit(body_face, captured, row.t_ms);
            attempt.result = commit;
            tally_commit(result.tally, commit);
            result.events.push_back(CannedEvent{false, 0, result.attempts.size()});
            result.attempts.push_back(std::move(attempt));

            if (commit.outcome == cube::vision::CommitOutcome::holdoff) {
                continue;
            }
            aggregator.reset();
            meta = AggregatorMeta{};

            if (commit.layer_turn) {
                for (auto& reset_me : aggregators) {
                    reset_me.reset();
                }
                metas.fill(AggregatorMeta{});
                break;
            }
            if (commit.force_lost) {
                tracker.force_lost();
                for (auto& reset_me : aggregators) {
                    reset_me.reset();
                }
                metas.fill(AggregatorMeta{});
                break;
            }
        }
    }

    tally_final_store(result.tally, router);
    return result;
}

// Re-drive one RoutingConfig variant over a canned event stream: a fresh
// router, the recorded epoch changes and commit attempts, nothing else.
// See the file header for exactly what this does and does not replay.
RoutingTally drive_canned(const std::vector<CannedEvent>& events,
                          const std::vector<AttemptRecord>& attempts,
                          const cube::vision::RoutingConfig& config) {
    cube::vision::CaptureRouter router(config);
    RoutingTally tally;
    for (const auto& event : events) {
        if (event.is_epoch) {
            router.on_epoch(event.epoch);
            ++tally.epochs_seen;
        } else {
            const auto& attempt = attempts[event.attempt_index];
            const auto commit = router.commit(attempt.body_face, attempt.captured, attempt.t_ms);
            tally_commit(tally, commit);
        }
    }
    tally_final_store(tally, router);
    return tally;
}

// The per-clip refusal-attribution summary - the block whose existence the
// pipeline plan requires (Phase 0 exit gate: "routing replay harness
// produces a per-gate refusal breakdown for a replayed clip").
void print_summary(const char* tag, const std::string& clip, const RoutingTally& t,
                   std::size_t replayed) {
    std::printf(
        "# %s clip=%s attempts=%d outcomes: filled=%d updated=%d quarantined=%d deferred=%d "
        "holdoff=%d\n",
        tag, clip.c_str(), t.attempts, t.outcomes[0], t.outcomes[1], t.outcomes[2], t.outcomes[3],
        t.outcomes[4]);
    std::printf(
        "# %s clip=%s deferrals: no-registration=%d insufficient=%d ambiguous=%d "
        "resolved-flush=%d\n",
        tag, clip.c_str(), t.deferred_no_registration, t.deferred_insufficient,
        t.deferred_ambiguous, t.deferred_resolved);
    std::printf(
        "# %s clip=%s quarantines: duplicate-centre=%d verification-conflict=%d | escalations: "
        "force-lost=%d layer-turn=%d\n",
        tag, clip.c_str(), t.quarantine_duplicate_centre, t.quarantine_verify_conflict,
        t.force_lost, t.layer_turn);
    std::printf(
        "# %s clip=%s registrations: resolved=%d ambiguous=%d insufficient=%d | resolved-by: "
        "unique=%d "
        "oriented=%d content-equiv=%d\n",
        tag, clip.c_str(), t.reg_status[0], t.reg_status[1], t.reg_status[2], t.reg_basis[0],
        t.reg_basis[1], t.reg_basis[2]);
    std::printf(
        "# %s clip=%s starvation (converged windows refused pre-router): pass-gate=%d identity=%d "
        "frontal=%d motion=%d unknown-cells=%d\n",
        tag, clip.c_str(), t.starved_pass_gate, t.starved_identity, t.starved_frontal,
        t.starved_motion, t.starved_unknown);
    std::printf("# %s clip=%s epochs=%d replayed-passes=%zu filled-slots=%d slots=%s\n", tag,
                clip.c_str(), t.epochs_seen, replayed, t.filled_slots, t.slot_centres.c_str());
}

// One swept RoutingConfig knob: a name for the CSV, the candidate values
// (FIRST value = the shipped default, checked at runtime so the grid can
// never silently drift from the struct), and how to apply a value.
struct RoutingAxis {
    const char* name;
    std::vector<float> values;
    void (*apply)(cube::vision::RoutingConfig&, float);
};

// The registration-bar knobs the daylight replay implicated, one-factor-
// at-a-time around the shipped defaults (the OFAT rationale is
// tracker_sweep_test's: bounded grid, one-row-one-knob attribution).
// reassoc_census joins the four bars because tight re-association is what
// PINS registration hypotheses - it moves the same refusal counts.
std::vector<RoutingAxis> routing_axes() {
    return {
        {"epoch_register_max_score",
         {6.0F, 4.0F, 8.0F, 10.0F, 12.0F},
         [](cube::vision::RoutingConfig& c, float v) { c.epoch_register_max_score = v; }},
        {"epoch_register_margin",
         {4.0F, 2.0F, 6.0F},
         [](cube::vision::RoutingConfig& c, float v) { c.epoch_register_margin = v; }},
        {"epoch_register_oriented_max_lab",
         {20.0F, 12.0F, 28.0F},
         [](cube::vision::RoutingConfig& c, float v) { c.epoch_register_oriented_max_lab = v; }},
        {"epoch_register_oriented_margin_lab",
         {6.0F, 3.0F, 9.0F},
         [](cube::vision::RoutingConfig& c, float v) { c.epoch_register_oriented_margin_lab = v; }},
        {"reassoc_census",
         {4.0F, 2.0F, 6.0F},
         [](cube::vision::RoutingConfig& c, float v) { c.reassoc_census = static_cast<int>(v); }},
    };
}

// Every RoutingConfig member as a comparable tuple - the grid-anchoring
// check's stand-in for reflection, same rationale as tracker_sweep's
// settings_fields (memcmp would read padding).
[[nodiscard]] auto routing_config_fields(const cube::vision::RoutingConfig& c) {
    return std::make_tuple(c.same_face_centre_lab, c.verify_census_drift, c.duplicate_centre_radius,
                           c.self_duplication_centre_lab, c.conflict_strikes,
                           c.layer_turn_conflict_faces, c.layer_turn_window_ms,
                           c.recommit_holdoff_ms, c.reassoc_census, c.content_equiv_lab,
                           c.epoch_register_max_score, c.epoch_register_margin,
                           c.epoch_register_oriented_max_lab, c.epoch_register_oriented_margin_lab);
}

struct RoutingVariant {
    std::string axis;    // "default" for the untouched config
    float value = 0.0F;  // the moved knob's value (default row: 0, unused)
    cube::vision::RoutingConfig config;
};

// Deterministic enumeration: default first, then each axis's non-default
// values in axis order (~13 variants).
std::vector<RoutingVariant> build_routing_variants() {
    std::vector<RoutingVariant> variants;
    variants.push_back({"default", 0.0F, cube::vision::RoutingConfig{}});
    for (const auto& axis : routing_axes()) {
        for (std::size_t i = 1; i < axis.values.size(); ++i) {
            RoutingVariant variant{axis.name, axis.values[i], cube::vision::RoutingConfig{}};
            axis.apply(variant.config, axis.values[i]);
            variants.push_back(std::move(variant));
        }
    }
    return variants;
}

}  // namespace

TEST_CASE("emit the routing story for recorded clips", "[.routing-replay]") {
    const char* root_env = std::getenv("CUBE_ROUTING_REPLAY_DIR");
    if (root_env == nullptr) {
        SKIP("CUBE_ROUTING_REPLAY_DIR not set - record clips via CUBE_VISION_RECORD_DIR first");
    }
    const std::filesystem::path root(root_env);
    const auto clips = collect_clip_dirs(root);
    if (clips.empty()) {
        SKIP("no recording directories (index.csv) under " << root.string()
                                                           << " - corpus not recorded yet");
    }

    // One row per commit ATTEMPT (cross_session's records show only what
    // LANDED; the routing story is above all what was refused, and why).
    std::printf(
        "clip,pass,t_ms,epoch,body_face,outcome,confirmed,dup_centre,force_lost,layer_turn,"
        "reg_status,reg_basis,reg_lattice,reg_cand,reg_tied,reg_best,reg_runner_up,"
        "reg_best_lab,reg_runner_up_lab\n");

    std::size_t clips_replayed = 0;
    int total_attempts = 0;
    for (const auto& clip_dir : clips) {
        const std::string clip = clip_dir.filename().string();
        const auto live = run_live_fidelity_loop(clip_dir);
        if (live.replayed == 0) {
            std::printf("# routing-replay clip=%s has no replayable passes - skipped\n",
                        clip.c_str());
            continue;
        }
        ++clips_replayed;

        for (const auto& attempt : live.attempts) {
            const auto& commit = attempt.result;
            const char* outcome = outcome_name(commit.outcome);
            char reg_text[160];
            if (commit.registration.has_value()) {
                const auto& reg = *commit.registration;
                const char* status = reg_status_name(reg.status);
                const bool resolved =
                    reg.status == cube::vision::EpochRegistration::Status::resolved;
                const char* basis = resolved ? reg_basis_name(reg.basis) : "-";
                char lattice_text[16];
                if (resolved) {
                    std::snprintf(lattice_text, sizeof(lattice_text), "%d", reg.lattice_index);
                } else {
                    std::snprintf(lattice_text, sizeof(lattice_text), "-");
                }
                std::snprintf(reg_text, sizeof(reg_text), "%s,%s,%s,%d,%d,%.2f,%.2f,%.2f,%.2f",
                              status != nullptr ? status : "?", basis != nullptr ? basis : "?",
                              lattice_text, reg.candidates, reg.tied,
                              static_cast<double>(reg.best_score),
                              static_cast<double>(reg.runner_up_score),
                              static_cast<double>(reg.best_oriented_lab),
                              static_cast<double>(reg.runner_up_oriented_lab));
            } else {
                std::snprintf(reg_text, sizeof(reg_text), "-,-,-,-,-,-,-,-,-");
            }
            std::printf("%s,%d,%lld,%d,%c,%s,%d,%d,%d,%d,%s\n", clip.c_str(), attempt.pass,
                        static_cast<long long>(attempt.t_ms), attempt.epoch,
                        "URFDLB"[static_cast<std::size_t>(attempt.body_face)],
                        outcome != nullptr ? outcome : "?",
                        static_cast<int>(commit.confirmed_slots.size()),
                        commit.duplicate_centre ? 1 : 0, commit.force_lost ? 1 : 0,
                        commit.layer_turn ? 1 : 0, reg_text);
        }

        print_summary("routing-replay", clip, live.tally, live.replayed);

        // The instrument's own contract: every attempt attributed, no enum
        // value it cannot name (see tally_commit).
        REQUIRE(live.tally.unattributed == 0);
        // Zero attempts is a legal, ATTRIBUTED outcome (the starvation line
        // above says which pre-router gate refused every converged window -
        // cardboard steady-02 measures exactly this). Plumbing fidelity is
        // asserted where it can be: the sweep's default-config agreement
        // check, and the synthetic corpus which produces real traffic.
        total_attempts += live.tally.attempts;
    }
    std::printf("# routing-replay TOTAL clips=%zu attempts=%d\n", clips_replayed, total_attempts);
    CHECK(clips_replayed > 0);
}

TEST_CASE("sweep RoutingConfig registration bars over canned router-input streams",
          "[.routing-sweep]") {
    const char* root_env = std::getenv("CUBE_ROUTING_SWEEP_DIR");
    if (root_env == nullptr) {
        SKIP("CUBE_ROUTING_SWEEP_DIR not set - record clips via CUBE_VISION_RECORD_DIR first");
    }
    const std::filesystem::path root(root_env);
    const auto clips = collect_clip_dirs(root);
    if (clips.empty()) {
        SKIP("no recording directories (index.csv) under " << root.string()
                                                           << " - corpus not recorded yet");
    }

    // Grid anchoring (tracker_sweep's rule): each axis's first value must
    // BE the shipped default, and every other value must move the config.
    const cube::vision::RoutingConfig defaults;
    for (const auto& axis : routing_axes()) {
        cube::vision::RoutingConfig anchored;
        axis.apply(anchored, axis.values[0]);
        CHECK(routing_config_fields(anchored) == routing_config_fields(defaults));
        for (std::size_t i = 1; i < axis.values.size(); ++i) {
            cube::vision::RoutingConfig moved;
            axis.apply(moved, axis.values[i]);
            CHECK(routing_config_fields(moved) != routing_config_fields(defaults));
        }
    }

    // Full-fidelity loop ONCE per clip, recording the router-input stream.
    struct ClipStream {
        std::string name;
        LiveLoopResult live;
    };
    std::vector<ClipStream> streams;
    for (const auto& clip_dir : clips) {
        auto live = run_live_fidelity_loop(clip_dir);
        std::printf("# clip %s: %zu passes, %d attempts, %d epochs (full-fidelity reference)\n",
                    clip_dir.filename().string().c_str(), live.replayed, live.tally.attempts,
                    live.tally.epochs_seen);
        if (live.replayed > 0) {
            streams.push_back(ClipStream{clip_dir.filename().string(), std::move(live)});
        }
    }
    if (streams.empty()) {
        SKIP("corpus directories contain no replayable passes");
    }

    std::printf(
        "variant,axis,value,clip,attempts,filled,updated,quarantined,deferred,holdoff,"
        "def_noreg,def_insufficient,def_ambiguous,def_resolved,quar_dup,quar_verify,"
        "force_lost,layer_turn,reg_resolved,reg_ambiguous,reg_insufficient,"
        "by_unique,by_oriented,by_content_equiv,epochs,filled_slots,slots\n");
    const auto variants = build_routing_variants();
    std::size_t rows = 0;
    for (std::size_t v = 0; v < variants.size(); ++v) {
        const auto& variant = variants[v];
        for (const auto& stream : streams) {
            const auto t = drive_canned(stream.live.events, stream.live.attempts, variant.config);
            REQUIRE(t.unattributed == 0);
            std::printf(
                "%zu,%s,%g,%s,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%"
                "s\n",
                v, variant.axis.c_str(), static_cast<double>(variant.value), stream.name.c_str(),
                t.attempts, t.outcomes[0], t.outcomes[1], t.outcomes[2], t.outcomes[3],
                t.outcomes[4], t.deferred_no_registration, t.deferred_insufficient,
                t.deferred_ambiguous, t.deferred_resolved, t.quarantine_duplicate_centre,
                t.quarantine_verify_conflict, t.force_lost, t.layer_turn, t.reg_status[0],
                t.reg_status[1], t.reg_status[2], t.reg_basis[0], t.reg_basis[1], t.reg_basis[2],
                t.epochs_seen, t.filled_slots, t.slot_centres.c_str());
            ++rows;

            // The fidelity anchor (see the file header): the default-config
            // re-drive must reproduce the full-fidelity loop's own tallies
            // exactly - this is the check that the canned stream carries
            // everything the router's decisions depend on.
            if (v == 0) {
                const bool agrees = tally_fields(t) == tally_fields(stream.live.tally);
                std::printf(
                    "# routing-sweep clip=%s default-agreement=%s (canned default vs "
                    "full-fidelity: "
                    "attempts %d/%d filled %d/%d deferred %d/%d quarantined %d/%d)\n",
                    stream.name.c_str(), agrees ? "OK" : "MISMATCH", t.attempts,
                    stream.live.tally.attempts, t.outcomes[0], stream.live.tally.outcomes[0],
                    t.outcomes[3], stream.live.tally.outcomes[3], t.outcomes[2],
                    stream.live.tally.outcomes[2]);
                CHECK(agrees);
                print_summary("routing-sweep-reference", stream.name, stream.live.tally,
                              stream.live.replayed);
            }
        }
    }
    std::printf("# %zu sweep rows (%zu variants, %zu clips)\n", rows, variants.size(),
                streams.size());
    CHECK(rows > 0);
}
