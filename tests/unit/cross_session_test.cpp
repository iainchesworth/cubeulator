// Cross-session capture harness - hidden tag, never part of the normal
// suite. Point CUBE_CROSS_SESSION_DIR at a directory recorded by
// ScanSession's CUBE_VISION_RECORD_DIR hook and run:
//
//   cube_unit_tests.exe "[.cross-session]"
//
// Optionally set CUBE_CROSS_SESSION_OUT to also tee the records to a file,
// and CUBE_CROSS_SESSION_CLIP to override the clip label (it defaults to
// the recording directory's leaf name).
//
// WHY THIS EXISTS, distinct from [.track-replay]. That harness measures
// the TRACKER: per-pass states, gates, and certainty counters. Every
// safety number it reports (certain_slip == 0 above all) is a
// SELF-CONSISTENCY measure - it proves the tracker never contradicted
// itself, not that the face-to-slot identity mapping was CORRECT. A
// tracker that systematically mislabels one physical face the same way
// every time scores a perfect zero there. Correctness needs an external
// referent, and the corpus now has one: all four live clips recorded THE
// SAME physical cube in THE SAME configuration, so
//   (T1) the same body face must yield the same nine facelet colours in
//        every clip that captured it, and
//   (T2) the union of the per-face captures must assemble into a
//        physically valid, solvable cube.
// Neither test can run without the actual sampled colours per body face,
// which nothing currently emits. This harness emits them.
//
// FIDELITY IS THE WHOLE POINT. The records must be what PRODUCTION would
// have captured, so the capture layer here is not a re-implementation: it
// drives the real FaceAggregator windows, the real CaptureRouter, the real
// kFaceObsToBody/kCornerObsToBody routing tables, and the real gate
// constants, in the same order handleAnalysisFinished /
// tryCommitConverged apply them (scan_session.cpp):
//   Gate 1  locked track + kMinCaptureAccepts accept streak + resolved
//           lattice element, and per-body-face commit-grade identity
//           (waived below two filled slots, exactly as live).
//   Gate 4  fully-in-frame quads (ALL FOUR corners, not just the centre)
//           enforced per observation at aggregation time by refusing the
//           add - so a converged window is fully-visible by construction.
//   Gates 2/3/5 at commit time: frontal-ness (kMaxCornerFlankDeg once the
//           window took corner samples, else kMaxFaceCaptureDeg), the
//           per-pass rotation and stability motion bounds, and the
//           unknown-cell rule.
// Deliberately NOT loosened anywhere: a looser capture rule would measure
// a pipeline that does not ship.
//
// The detector/pose/tracker stage is identical to [.track-replay]'s,
// including its fidelity notes (analysis-scale replay leaves orientation
// unchanged, the 450 ms seed/hint hold, ring-wrap exclusion). Two
// deliberate refinements over that harness's bookkeeping, both needed
// because routing - not just pose - is now being measured:
//  - The observation family that routes is decided by the POSE SOURCE, as
//    production decides it (AnalysisResult::pose_source), never by "a
//    corner observation exists": a corner whose own solve failed leaves
//    the pose to the edge fallback, and aggregating that corner's cells
//    under an edge pose's gauge would scramble stickers.
//  - Both the centre-in-frame gate (detector acceptance) and the strict
//    all-four-corners gate (capture acceptance) are computed, because
//    production applies exactly those two different predicates at those
//    two different stages.
//
// Skips (never fails) when no recording is present: the corpus is recorded
// on demand from live hardware and does not ship with the repo.
//
// std::getenv is fine here (single-threaded harness, read-only); silence
// MSVC's CRT deprecation the way the other offline harnesses do.
#ifdef _MSC_VER
#pragma warning(disable : 4996)
#endif

#include <catch2/catch_test_macros.hpp>

#include <cube/model/face.hpp>
#include <cube/model/facelet.hpp>
#include <cube/solver/scan_assembly.hpp>
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
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

namespace {

// Capture-gate constants, transcribed from scan_session.cpp's anonymous
// namespace (they are file-local there). Any tuning of those MUST be
// mirrored here or this harness stops measuring the shipping policy.
constexpr int kMinCaptureAccepts = 3;
constexpr std::int64_t kIdentityAggStaleMs = 5000;
constexpr float kMaxFaceCaptureDeg = 35.0F;
constexpr float kMaxCornerFlankDeg = 70.0F;
constexpr float kMaxCaptureRotDeg = 8.0F;
constexpr float kMaxCaptureMotionScore = 0.05F;
// Overlay/seed hold window (kOverlayHoldMs), same constant the other
// replay harnesses keep.
constexpr std::int64_t kSeedHoldMs = 450;

// Per-face aggregator bookkeeping, mirroring ScanSession::AggregatorMeta.
struct AggregatorMeta {
    bool has_window = false;
    std::array<int, 9> window_map{};
    std::int64_t last_add_ms = 0;
    bool corner_fed = false;
};

const char* commit_outcome_name(cube::vision::CommitOutcome outcome) {
    switch (outcome) {
        case cube::vision::CommitOutcome::filled: return "filled";
        case cube::vision::CommitOutcome::updated: return "updated";
        case cube::vision::CommitOutcome::quarantined: return "quarantined";
        case cube::vision::CommitOutcome::deferred: return "deferred";
        case cube::vision::CommitOutcome::holdoff: return "holdoff";
    }
    return "?";
}

char color_letter(cube::model::FacetColor color) {
    switch (color) {
        case cube::model::FacetColor::white: return 'W';
        case cube::model::FacetColor::yellow: return 'Y';
        case cube::model::FacetColor::red: return 'R';
        case cube::model::FacetColor::orange: return 'O';
        case cube::model::FacetColor::green: return 'G';
        case cube::model::FacetColor::blue: return 'B';
        case cube::model::FacetColor::unknown: return '?';
    }
    return '?';
}

// Records go to stdout always, and to CUBE_CROSS_SESSION_OUT as well when
// that names a writable file - a tee, so an interactive run and a scripted
// one see byte-identical records.
class RecordSink {
public:
    void open(const char* path) {
        if (path != nullptr) {
            file_.open(path, std::ios::out | std::ios::trunc);
        }
    }
    void line(const std::string& text) {
        std::printf("%s\n", text.c_str());
        if (file_.is_open()) {
            file_ << text << '\n';
        }
    }
    [[nodiscard]] bool teed() const { return file_.is_open(); }

private:
    std::ofstream file_;
};

// One face record: nine classified colours, nine mean Labs, and the
// provenance a cross-session comparison needs to weigh a disagreement.
std::string face_record(const std::string& kind, const std::string& clip, int pass, std::int64_t t_ms, int epoch,
                        char epoch_face, char store_face, int slot, const std::string& outcome, int contrib_obs,
                        bool corner_fed, float frontal_deg, float pass_rot_deg, float motion_score,
                        float belief_margin, bool certain, bool certain_face, float convergence,
                        const cube::vision::CapturedFace& captured) {
    std::string colours;
    for (const auto& lab : captured.lab) {
        colours += color_letter(cube::vision::classify_lab(lab).color);
    }
    char head[512];
    std::snprintf(head, sizeof(head), "%s,%s,%d,%lld,%d,%c,%c,%d,%s,%d,%d,%.2f,%.2f,%.4f,%.3f,%d,%d,%.3f,%s",
                  kind.c_str(), clip.c_str(), pass, static_cast<long long>(t_ms), epoch, epoch_face, store_face, slot,
                  outcome.c_str(), contrib_obs, corner_fed ? 1 : 0, frontal_deg, pass_rot_deg, motion_score,
                  belief_margin, certain ? 1 : 0, certain_face ? 1 : 0, convergence, colours.c_str());
    std::string row(head);
    for (const auto& lab : captured.lab) {
        char cell[64];
        std::snprintf(cell, sizeof(cell), ",%.2f,%.2f,%.2f", lab[0], lab[1], lab[2]);
        row += cell;
    }
    return row;
}

// Leaf name of a path, for the default clip label (both separators, since
// the corpus lives on a Windows drive but the paths get typed either way).
std::string leaf_name(const std::string& path) {
    const auto cut = path.find_last_of("/\\");
    std::string leaf = cut == std::string::npos ? path : path.substr(cut + 1);
    return leaf.empty() ? path : leaf;
}

}  // namespace

TEST_CASE("emit per-body-face captured colour for a recorded clip", "[.cross-session]") {
    namespace recording = cube::vision::recording;
    using cube::vision::observe_corner;
    using cube::vision::observe_edge;
    using cube::vision::observe_face;
    using cube::vision::project_cube_from_corner;
    using cube::vision::project_cube_from_edge;
    using cube::vision::project_cube_from_face;

    const char* dir = std::getenv("CUBE_CROSS_SESSION_DIR");
    if (dir == nullptr) {
        SKIP("CUBE_CROSS_SESSION_DIR not set - record a clip via CUBE_VISION_RECORD_DIR first");
    }
    const std::string dir_path(dir);
    std::ifstream index_stream(dir_path + "/" + std::string(recording::kIndexCsvName));
    if (!index_stream) {
        SKIP("no " << recording::kIndexCsvName << " under " << dir_path << " - not a recording directory");
    }
    const char* clip_override = std::getenv("CUBE_CROSS_SESSION_CLIP");
    const std::string clip = clip_override != nullptr ? std::string(clip_override) : leaf_name(dir_path);

    RecordSink sink;
    sink.open(std::getenv("CUBE_CROSS_SESSION_OUT"));

    const auto rows = recording::read_index(index_stream);
    std::printf("cross-session: %s clip=%s (%zu passes)%s\n", dir_path.c_str(), clip.c_str(), rows.size(),
                sink.teed() ? " [teed to CUBE_CROSS_SESSION_OUT]" : "");
    // Schema line, emitted into the record stream so a parser reading only
    // the tee'd file is self-describing. "kind" is `face` for a capture
    // that landed and `slot` for the end-of-clip store dump.
    sink.line("kind,clip,pass,t_ms,epoch,epoch_face,store_face,slot,outcome,contrib_obs,corner_fed,frontal_deg,"
              "pass_rot_deg,motion,belief_margin,certain,certain_face,convergence,colors,"
              "L0,a0,b0,L1,a1,b1,L2,a2,b2,L3,a3,b3,L4,a4,b4,L5,a5,b5,L6,a6,b6,L7,a7,b7,L8,a8,b8");

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
    const auto quad_fully_in_frame = [](const cube::vision::StickerQuad& cell, const cv::Mat& bounds) {
        return std::all_of(cell.corners.begin(), cell.corners.end(), [&bounds](const cv::Point2f& corner) {
            return corner.x >= 0.0F && corner.y >= 0.0F && corner.x < static_cast<float>(bounds.cols) &&
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

    int records = 0;
    int commits_attempted = 0;
    std::array<int, cube::model::face_count> records_per_face{};
    // Outcome and registration histograms. Records alone show only what
    // LANDED; a scan that cannot complete is characterised entirely by
    // what it refused and why, and "deferred" without its registration
    // status is the specific blindness that hid the four-fold roll tie.
    std::array<int, 5> outcome_counts{};  // indexed by CommitOutcome
    std::array<int, 3> registration_counts{};  // indexed by EpochRegistration::Status
    std::array<int, 3> registration_basis{};   // indexed by EpochRegistration::Basis
    int epochs_seen = 0;
    int last_epoch = -1;
    std::size_t replayed = 0;
    std::int64_t previous_t_ms = 0;
    bool have_previous = false;

    for (const auto& row : rows) {
        if (recording::frame_overwritten(row.pass, max_pass)) {
            continue;
        }
        const cv::Mat frame = cv::imread(dir_path + "/" + recording::frame_filename(row.pass), cv::IMREAD_COLOR);
        if (frame.empty() || frame.cols != row.w || frame.rows != row.h) {
            continue;
        }
        const std::int64_t dt_ms = have_previous ? row.t_ms - previous_t_ms : 0;
        static_cast<void>(dt_ms);
        previous_t_ms = row.t_ms;
        have_previous = true;

        if (seed_prior && row.t_ms - seed_t_ms > kSeedHoldMs) {
            seed_prior.reset();
        }
        const cube::vision::PoseSeed* seed = seed_prior ? &*seed_prior : nullptr;

        const auto all_cells_in_frame = [&](const auto& cells) {
            return std::all_of(cells.begin(), cells.end(),
                               [&](const cube::vision::StickerQuad& cell) { return cell_in_frame(cell, frame); });
        };
        const auto all_quads_full = [&](const auto& cells) {
            return std::all_of(cells.begin(), cells.end(),
                               [&](const cube::vision::StickerQuad& cell) { return quad_fully_in_frame(cell, frame); });
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
        const bool corner_gated =
            observed_corner.has_value() &&
            !(all_cells_in_frame(observed_corner->top.cells) && all_cells_in_frame(observed_corner->lower_left.cells) &&
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
            if (const auto projected = project_cube_from_corner(*corner, frame.size(), seed); projected.has_value()) {
                pose = *projected;
                corner_pose = true;
            }
        } else if (face.has_value()) {
            if (const auto projected = project_cube_from_face(*face, frame.size(), seed); projected.has_value()) {
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
            if (const auto edge = observe_edge(frame, edge_hint ? &*edge_hint : nullptr); edge.has_value()) {
                if (const auto projected = project_cube_from_edge(*edge, frame.size(), seed); projected.has_value()) {
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
                track_in.centre_lab[0] = lab_entry(corner->top.lab[cube::model::center_facelet_index]);
                track_in.centre_lab[1] = lab_entry(corner->lower_left.lab[cube::model::center_facelet_index]);
                track_in.centre_lab[2] = lab_entry(corner->lower_right.lab[cube::model::center_facelet_index]);
            } else if (face_pose && face.has_value()) {
                track_in.centre_lab_count = 1;
                track_in.centre_lab[0] = lab_entry(face->lab[cube::model::center_facelet_index]);
            }
        }
        const auto track = tracker.on_pass(track_in);
        ++replayed;

        // --- Capture layer, in handleAnalysisFinished's order --------------
        if (track.epoch != last_epoch) {
            last_epoch = track.epoch;
            ++epochs_seen;
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
        const bool pass_accepted =
            track.gate == cube::vision::TrackGate::accepted || track.gate == cube::vision::TrackGate::anchored;
        const float pass_rotation_deg =
            pass_accepted && has_last_track_q ? cube::vision::quat_angle_deg(track.q_track, last_track_q) : 0.0F;
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
                                       const cube::vision::FaceObservation& observation, bool from_corner,
                                       bool full_quads) {
                if (!full_quads) {
                    return;
                }
                const auto face_index = static_cast<std::size_t>(map.body_face);
                if (!capture_identity_ok[face_index]) {
                    return;
                }
                auto& aggregator = aggregators[face_index];
                auto& meta = metas[face_index];
                if (meta.has_window &&
                    (row.t_ms - meta.last_add_ms > kIdentityAggStaleMs || meta.window_map != map.cell)) {
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
                    add_keyed(cube::vision::kCornerObsToBody[lattice][view], slot_obs, /*from_corner=*/true,
                              corner_full_quads);
                }
            } else if (face_pose && face.has_value()) {
                add_keyed(cube::vision::kFaceObsToBody[lattice], *face, /*from_corner=*/false, face_full_quads);
            }
        }

        // --- tryCommitConverged, with a record per landed capture ----------
        if (!capture_gate_ok) {
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
                continue;
            }
            const float frontal = cube::vision::face_frontal_deg(track.q_track, body_face);
            if (frontal > (meta.corner_fed ? kMaxCornerFlankDeg : kMaxFaceCaptureDeg)) {
                continue;
            }
            if (pass_rotation_deg > kMaxCaptureRotDeg || motion_score >= kMaxCaptureMotionScore) {
                continue;
            }
            const auto captured = aggregator.capture();
            const bool has_unknown_cell = std::any_of(captured.lab.begin(), captured.lab.end(),
                                                      [](const cv::Vec3f& lab) {
                                                          return cube::vision::classify_lab(lab).color ==
                                                                 cube::model::FacetColor::unknown;
                                                      });
            if (has_unknown_cell) {
                continue;
            }

            // Sampled BEFORE commit(): the window is consumed by every
            // outcome except holdoff.
            const int contrib_obs = static_cast<int>(aggregator.observation_count());
            const float convergence = aggregator.convergence();
            const bool corner_fed = meta.corner_fed;
            ++commits_attempted;
            const auto commit = router.commit(body_face, captured, row.t_ms);
            ++outcome_counts[static_cast<std::size_t>(commit.outcome)];
            if (commit.registration) {
                const auto& reg = *commit.registration;
                ++registration_counts[static_cast<std::size_t>(reg.status)];
                if (reg.status == cube::vision::EpochRegistration::Status::resolved) {
                    ++registration_basis[static_cast<std::size_t>(reg.basis)];
                }
                std::printf(
                    "# reg clip=%s pass=%d epoch=%d face=%c status=%d cand=%d tied=%d score=%.2f/%.2f "
                    "roll=%.2f/%.2f filled=%d\n",
                    clip.c_str(), row.pass, track.epoch, "URFDLB"[f], static_cast<int>(reg.status), reg.candidates,
                    reg.tied, static_cast<double>(reg.best_score), static_cast<double>(reg.runner_up_score),
                    static_cast<double>(reg.best_oriented_lab), static_cast<double>(reg.runner_up_oriented_lab),
                    router.filled_count());
            }

            if (commit.outcome == cube::vision::CommitOutcome::holdoff) {
                continue;
            }
            aggregator.reset();
            meta = AggregatorMeta{};

            // One record per slot the commit CONFIRMED, read back from the
            // store so the colours emitted are literally the bytes
            // production would assemble from. A recovery resolution flushes
            // several buffered epoch faces at once; only a single-slot
            // confirmation can be attributed to THIS epoch face, so a
            // multi-slot flush emits its per-pass metrics as the same
            // values with the epoch_face column set to '?' rather than
            // inventing an attribution.
            const bool single = commit.confirmed_slots.size() == 1;
            for (const int slot_index : commit.confirmed_slots) {
                const cube::vision::CapturedFace* content = router.slot_at(slot_index);
                const auto store_face = router.face_at(slot_index);
                if (content == nullptr || !store_face.has_value()) {
                    continue;
                }
                const char store_letter = "URFDLB"[static_cast<std::size_t>(*store_face)];
                sink.line(face_record("face", clip, row.pass, row.t_ms, track.epoch,
                                      single ? "URFDLB"[f] : '?', store_letter, slot_index,
                                      commit_outcome_name(commit.outcome), single ? contrib_obs : -1, corner_fed,
                                      frontal, pass_rotation_deg, motion_score, track.belief_margin,
                                      track.identity_certain, capture_identity_ok[f], convergence, *content));
                ++records;
                ++records_per_face[static_cast<std::size_t>(*store_face)];
            }

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

    // End-of-clip store dump: the six (or fewer) slots the production
    // capture layer would hand to assembly. These are the rows T1 compares
    // across clips and T2 pools - the per-commit `face` rows above are the
    // history that produced them.
    for (int slot_index = 0; slot_index < router.filled_count(); ++slot_index) {
        const cube::vision::CapturedFace* content = router.slot_at(slot_index);
        const auto store_face = router.face_at(slot_index);
        if (content == nullptr || !store_face.has_value()) {
            continue;
        }
        sink.line(face_record("slot", clip, -1, 0, agg_epoch, '-',
                              "URFDLB"[static_cast<std::size_t>(*store_face)], slot_index, "final", -1, false, 0.0F,
                              0.0F, 0.0F, 0.0F, false, false, 0.0F, *content));
    }

    // --- The verdict that actually counts ---------------------------------
    //
    // A nine-per-colour census proves NOTHING on its own: resolve_colors()
    // forces exactly nine of each by construction, so a census of all-nines
    // is a property of the resolver, not evidence about the scan. Cross-clip
    // consistency proves nothing either - a systematically wrong reader is
    // perfectly consistent with itself. The only external referent is
    // PHYSICAL VALIDITY, so the oracle here is production's own completion
    // path (ScanSession::finalizeIfComplete): resolve_colors() over the
    // stored slots, then assemble_scanned_faces_with_mapping(), which
    // succeeds only when some rotation combination builds a cube that
    // Cube::from_facelets() accepts AND is_solvable() confirms. Deliberately
    // called, never reimplemented - a private re-derivation of validity
    // would just be a second thing to be wrong.
    const int filled = router.filled_count();

    // Provisional (classify_lab) census over the filled slots. Emitted as a
    // DIAGNOSTIC beside the verdict - it says where the live classifier is
    // over- and under-counting, which is what a failed assembly needs to be
    // debugged against - and never as a pass criterion.
    std::array<int, 7> live_census{};
    for (int slot_index = 0; slot_index < filled; ++slot_index) {
        const cube::vision::CapturedFace* content = router.slot_at(slot_index);
        if (content == nullptr) {
            continue;
        }
        for (const auto& lab : content->lab) {
            ++live_census[static_cast<std::size_t>(cube::vision::classify_lab(lab).color)];
        }
    }
    constexpr std::array<cube::model::FacetColor, 7> kCensusOrder{
        cube::model::FacetColor::white,  cube::model::FacetColor::yellow,
        cube::model::FacetColor::red,    cube::model::FacetColor::orange,
        cube::model::FacetColor::green,  cube::model::FacetColor::blue,
        cube::model::FacetColor::unknown};
    std::string census_text;
    for (const auto color : kCensusOrder) {
        char cell[16];
        std::snprintf(cell, sizeof(cell), "%c%d", color_letter(color),
                      live_census[static_cast<std::size_t>(color)]);
        census_text += cell;
    }

    // Distinct-centre count under the resolver's OWN seed floor
    // (kMinCentreSeedSeparationLab), not an independently restated radius:
    // this is precisely the predicate resolve_colors will apply, so a clip
    // that fails it here is one resolve_colors is about to reject, and the
    // report says so rather than leaving the rejection unexplained.
    int distinct_centres = 0;
    for (int a = 0; a < filled; ++a) {
        const cube::vision::CapturedFace* first = router.slot_at(a);
        if (first == nullptr) {
            continue;
        }
        bool distinct = true;
        for (int b = 0; b < a; ++b) {
            const cube::vision::CapturedFace* second = router.slot_at(b);
            if (second == nullptr) {
                continue;
            }
            const cv::Vec3f diff = first->lab[cube::model::center_facelet_index] -
                                   second->lab[cube::model::center_facelet_index];
            if (std::sqrt(diff.dot(diff)) < cube::vision::kMinCentreSeedSeparationLab) {
                distinct = false;
            }
        }
        distinct_centres += distinct ? 1 : 0;
    }

    std::string resolve_text = "n/a";
    std::string assemble_text = "n/a";
    std::string resolved_census_text = "n/a";
    if (filled == static_cast<int>(cube::model::face_count)) {
        std::array<cube::vision::CapturedFace, cube::model::face_count> captured{};
        for (std::size_t i = 0; i < captured.size(); ++i) {
            captured[i] = *router.slot_at(static_cast<int>(i));
        }
        const auto resolved = cube::vision::resolve_colors(captured);
        if (!resolved.has_value()) {
            resolve_text = "FAIL";
            assemble_text = "not-reached";
        } else {
            resolve_text = "ok";
            std::array<int, 7> resolved_census{};
            for (const auto& face : *resolved) {
                for (const auto& facelet : face) {
                    ++resolved_census[static_cast<std::size_t>(facelet.color)];
                }
            }
            resolved_census_text.clear();
            for (const auto color : kCensusOrder) {
                char cell[16];
                std::snprintf(cell, sizeof(cell), "%c%d", color_letter(color),
                              resolved_census[static_cast<std::size_t>(color)]);
                resolved_census_text += cell;
            }
            const auto assembled = cube::solver::assemble_scanned_faces_with_mapping(*resolved);
            assemble_text = assembled.has_value() ? "VALID" : "INVALID";

            // The resolved faces themselves, one nine-letter string each.
            //
            // Worth emitting because of what the oracle does NOT depend on:
            // assemble_scanned_faces_with_mapping keys each face to a cube
            // position by CENTRE COLOUR alone and then tries all 4096
            // per-face rotation combinations. Slot identity and roll are
            // therefore both invisible to it - a capture filed under the
            // wrong slot, or rotated by a guessed gauge, still assembles if
            // its nine colours are right. An INVALID verdict consequently
            // localises the fault precisely: the 54 resolved COLOURS are
            // wrong, and no amount of routing or roll correction can move
            // it. These strings are what a colour fix has to change.
            for (std::size_t f = 0; f < resolved->size(); ++f) {
                std::string letters;
                for (const auto& facelet : (*resolved)[f]) {
                    letters += color_letter(facelet.color);
                }
                std::printf("# resolved: clip=%s slot=%zu %s\n", clip.c_str(), f, letters.c_str());
            }
        }
    }
    std::printf("# verdict: clip=%s filled=%d distinct-centres=%d live-census=%s resolve=%s "
                "resolved-census=%s assemble=%s\n",
                clip.c_str(), filled, distinct_centres, census_text.c_str(), resolve_text.c_str(),
                resolved_census_text.c_str(), assemble_text.c_str());

    std::string face_letters;
    for (std::size_t f = 0; f < records_per_face.size(); ++f) {
        if (records_per_face[f] > 0) {
            face_letters += "URFDLB"[f];
        }
    }
    std::printf("# cross-session: clip=%s replayed=%zu commits-attempted=%d records=%d faces=%s filled-slots=%d\n",
                clip.c_str(), replayed, commits_attempted, records,
                face_letters.empty() ? "-" : face_letters.c_str(), router.filled_count());
    std::printf(
        "# cross-session: clip=%s epochs=%d outcomes filled=%d updated=%d quarantined=%d deferred=%d holdoff=%d"
        " | registration resolved=%d ambiguous=%d insufficient=%d"
        " | resolved-by unique=%d oriented=%d content-equiv=%d\n",
        clip.c_str(), epochs_seen, outcome_counts[0], outcome_counts[1], outcome_counts[2], outcome_counts[3],
        outcome_counts[4], registration_counts[0], registration_counts[1], registration_counts[2],
        registration_basis[0], registration_basis[1], registration_basis[2]);

    // T2's verdict, computed here rather than left to a downstream script,
    // because the three numbers that decide whether a clip COULD have
    // assembled are all derived from the same store dump and are all easy
    // to get subtly wrong by hand:
    //
    //   census          the tally of classify_lab names over every cell of
    //                   every filled slot. A correct six-slot scan reads
    //                   9/9/9/9/9/9 and '?'=0; anything else names which
    //                   colour is being over- or under-called.
    //   centre NAMES    the set of DISTINCT classify_lab names over the six
    //                   centres. Reported as names, not as a Lab-distance
    //                   count: two centres 30 Lab apart that classify_lab
    //                   nonetheless calls the same colour are a fatal
    //                   collision for the assembler, and a distance-based
    //                   "distinct centres" figure scores them as distinct.
    //   assemble        the production chain verbatim - resolve_colors()
    //                   then assemble_scanned_faces_with_mapping(), the
    //                   two calls finalizeIfComplete() makes in that order
    //                   (scan_session.cpp) - so a failure here is the
    //                   failure the user would have seen, with the error
    //                   code that produced it.
    //
    // Instrumentation only: no CHECK is attached to any of it, because a
    // corpus clip failing to assemble is the state of the product, not a
    // regression in this harness.
    {
        std::array<int, 7> census{};
        std::string centre_names;
        std::array<cube::vision::CapturedFace, cube::model::face_count> slots{};
        const int filled = router.filled_count();
        for (int slot_index = 0; slot_index < filled; ++slot_index) {
            const cube::vision::CapturedFace* content = router.slot_at(slot_index);
            if (content == nullptr) {
                continue;
            }
            if (slot_index < static_cast<int>(slots.size())) {
                slots[static_cast<std::size_t>(slot_index)] = *content;
            }
            for (const auto& lab : content->lab) {
                ++census[static_cast<std::size_t>(cube::vision::classify_lab(lab).color)];
            }
            const char centre =
                color_letter(cube::vision::classify_lab(content->lab[cube::model::center_facelet_index]).color);
            if (centre_names.find(centre) == std::string::npos) {
                centre_names += centre;
            }
        }
        std::printf("# cross-session: clip=%s census W=%d Y=%d R=%d O=%d G=%d B=%d ?=%d"
                    " | centre-names=%s distinct-centre-names=%zu\n",
                    clip.c_str(), census[static_cast<std::size_t>(cube::model::FacetColor::white)],
                    census[static_cast<std::size_t>(cube::model::FacetColor::yellow)],
                    census[static_cast<std::size_t>(cube::model::FacetColor::red)],
                    census[static_cast<std::size_t>(cube::model::FacetColor::orange)],
                    census[static_cast<std::size_t>(cube::model::FacetColor::green)],
                    census[static_cast<std::size_t>(cube::model::FacetColor::blue)],
                    census[static_cast<std::size_t>(cube::model::FacetColor::unknown)],
                    centre_names.empty() ? "-" : centre_names.c_str(), centre_names.size());

        if (filled < static_cast<int>(cube::model::face_count)) {
            std::printf("# cross-session: clip=%s assemble=not-attempted (filled-slots=%d of 6)\n", clip.c_str(),
                        filled);
        } else {
            const auto resolved = cube::vision::resolve_colors(slots);
            if (!resolved.has_value()) {
                std::printf("# cross-session: clip=%s assemble=NO resolve_colors=%s\n", clip.c_str(),
                            std::string(cube::to_string(resolved.error())).c_str());
            } else {
                const auto assembled = cube::solver::assemble_scanned_faces_with_mapping(*resolved);
                std::printf("# cross-session: clip=%s assemble=%s\n", clip.c_str(),
                            assembled.has_value()
                                ? "YES"
                                : ("NO " + std::string(cube::to_string(assembled.error()))).c_str());
            }
        }
    }
    CHECK(replayed > 0);
}
