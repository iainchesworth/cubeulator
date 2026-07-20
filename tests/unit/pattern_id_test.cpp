// Ordered-sticker-pattern face identification MEASUREMENT harness - hidden
// tag, never part of the normal suite, and diagnostic only: nothing here
// changes production behaviour, and nothing in production consumes it.
//
// The question it exists to answer: on a SCRAMBLED cube, is a face's
// ordered nine-sticker pattern distinctive enough to identify that face
// from a SINGLE frame, with no tracking continuity at all? Today identity
// comes only from continuous orientation tracking, and the ordered pattern
// (oriented_cell_lab_distance) is consulted only as a roll tie-break AFTER
// tracking has already supplied a candidate. If the pattern separates
// same-face from different-face readings on its own, re-anchoring could
// recover identity photometrically instead of losing it.
//
//   set CUBE_PATTERN_ID_DIR=<corpus root>   (one recording dir, or a
//                                            directory of recording dirs,
//                                            one per clip)
//   cube_unit_tests.exe "[.pattern-id]"
//
// The corpus is recorded on demand from live hardware (see
// CUBE_VISION_RECORD_DIR in scan_session.cpp) and does not ship with the
// repo, so the harness SKIPS - never fails - when it is not set.
//
// Output is one CSV row per accepted pass on stdout: the nine Lab triples
// in the detector's cell order, the raw pose quaternion, and - FOR
// CROSS-CHECK ONLY, never as an input to any matching - the tracker's
// state, front face and epoch. The analysis (pairing, distances,
// clustering, baselines) lives in tools/pattern_id_analysis.py, which
// consumes this CSV.
//
// Detector-stage fidelity mirrors [.track-sweep] (tracker_sweep_test.cpp):
// same analysis-scale frames, same fully-visible-cells gate, same
// corner-first pose precedence, same pose-seed chain and corner hint. What
// is measured must be what production could actually see; a looser reader
// would answer a question nobody asked.
//
// Cell order. FaceObservation cells are screen row-major, and
// FaceAggregator::capture() preserves that index, so the emitted array IS
// the array production aggregates. Two screen readings of the SAME
// physical face differ by a pure in-plane grid ROTATION (never a
// reflection - both are views of the same face from the same side), so the
// four-hypothesis search the analysis runs over raw screen arrays is the
// same search oriented_cell_lab_distance runs over solver-order arrays.
// PatternIdRollsMatchBodyRelabel below PROVES that equality against the
// production tables rather than asserting it in a comment.
#ifdef _MSC_VER
#pragma warning(disable : 4996)
#endif

#include <catch2/catch_test_macros.hpp>

#include <cube/model/face.hpp>
#include <cube/vision/capture_routing.hpp>
#include <cube/vision/color_classification.hpp>
#include <cube/vision/corner_observation.hpp>
#include <cube/vision/face_observation.hpp>
#include <cube/vision/frame_recording.hpp>
#include <cube/vision/orientation_lattice.hpp>
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
#include <vector>

using namespace cube::vision;

namespace {

// Matches the live overlay/seed hold window (kOverlayHoldMs in
// scan_session.cpp), as [.track-sweep] does.
constexpr std::int64_t kSeedHoldMs = 450;

const char* state_name(TrackState state) {
    switch (state) {
        case TrackState::lost: return "lost";
        case TrackState::acquiring: return "acquiring";
        case TrackState::locked: return "locked";
        case TrackState::coasting: return "coasting";
    }
    return "?";
}

const char* face_name(cube::model::Face face) {
    static constexpr std::array<const char*, 6> kNames{"U", "R", "F", "D", "L", "B"};
    return kNames[static_cast<std::size_t>(face)];
}

// Same corpus-root convention as [.track-sweep]: a single recording dir,
// or a directory of them.
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

// Replay one clip through the real detector cascade and the real tracker,
// emitting a CSV row for every pass that produced a CAPTURE-GRADE face
// observation (the same bar production requires before a reading may reach
// the aggregator: a non-pose_only face fit with all nine cells in frame).
void emit_clip(const std::filesystem::path& dir, const std::string& clip, std::FILE* out) {
    std::ifstream index_stream(dir / std::string(recording::kIndexCsvName));
    if (!index_stream) {
        return;
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
    OrientationTracker tracker;
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

        if (seed_prior && row.t_ms - seed_t_ms > kSeedHoldMs) {
            seed_prior.reset();
        }
        const PoseSeed* seed = seed_prior ? &*seed_prior : nullptr;

        const auto all_cells_in_frame = [&](const auto& cells) {
            return std::all_of(cells.begin(), cells.end(),
                               [&](const StickerQuad& cell) { return cell_in_frame(cell, frame); });
        };

        std::optional<FaceObservation> face;
        std::optional<FaceObservation> pose_only_face;
        if (auto observed = observe_face(frame); observed.has_value() && all_cells_in_frame(observed->cells)) {
            if (observed->pose_only) {
                pose_only_face = std::move(*observed);
            } else {
                face = std::move(*observed);
            }
        }
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
                input.centre_lab_count = 1;
                input.centre_lab[0] = lab_entry(face->lab[cube::model::center_facelet_index]);
            }
            seed_prior = PoseSeed{pose->rvec, pose->tvec};
            seed_t_ms = row.t_ms;
        }

        // The tracker runs on EVERY pass (its pass cadence is its clock),
        // but its output is written to the CSV purely as a cross-check
        // column - the pattern matching under test never reads it.
        const TrackerPassOutput track = tracker.on_pass(input);

        // Rows are emitted for every pass with a VALID POSE, not only the
        // capture-grade ones. The analysis reconstructs face identity by
        // integrating the frame-to-frame relabelling along the pose
        // sequence - that is its ground truth - and the chain is only
        // trustworthy while consecutive links sit a few tens of ms apart.
        // Restricting rows to capture-grade readings would punch
        // multi-hundred-ms holes in it and break the chain far more often
        // than the physics demands. face_good says which rows carry a
        // pattern to match; the rest exist purely to carry continuity.
        const bool face_good = face.has_value();
        if (!face_good && !(pose && pose->orientation_valid)) {
            continue;
        }

        const Quat q = pose && pose->orientation_valid ? *input.q_obs : Quat{1.0F, 0.0F, 0.0F, 0.0F};
        std::fprintf(out, "%s,%d,%lld,%d,%d,%.6f,%.6f,%.6f,%.6f,%d,%s,%s,%d,%.4f", clip.c_str(), row.pass,
                     static_cast<long long>(row.t_ms), pose && pose->orientation_valid ? 1 : 0,
                     face_good ? 1 : 0, q[0], q[1], q[2], q[3], corner_pose ? 1 : 0, state_name(track.state),
                     track.front_face ? face_name(*track.front_face) : "-", track.epoch,
                     face_good ? static_cast<double>(face->geometry_confidence) : 0.0);
        // The two rotation-invariant BASELINES the ordered pattern must
        // beat, computed by production's own classifier rather than
        // re-derived in the analysis script: the census the router uses to
        // verify, and the centre's classified colour.
        if (face_good) {
            CapturedFace captured;
            captured.lab = face->lab;
            const ColorCensus census = color_census(captured);
            std::fprintf(out, ",%d",
                         static_cast<int>(classify_lab(face->lab[cube::model::center_facelet_index]).color));
            for (const int count : census) {
                std::fprintf(out, ",%d", count);
            }
            for (const auto& lab : face->lab) {
                std::fprintf(out, ",%.3f,%.3f,%.3f", static_cast<double>(lab[0]), static_cast<double>(lab[1]),
                             static_cast<double>(lab[2]));
            }
        } else {
            std::fprintf(out, ",-1");
            for (int blank = 0; blank < 7 + 27; ++blank) {
                std::fprintf(out, ",");
            }
        }
        std::fprintf(out, "\n");
    }
}

}  // namespace

// Proves the analysis script's four-hypothesis search IS production's.
// oriented_cell_lab_distance compares two solver-order faces under the
// lattice elements that stabilise the face; the analysis compares two
// SCREEN-order faces under the four plain 3x3 grid rotations. These are
// the same set of cell permutations, so the measured distance is the
// production distance - established here against the shipped kBodyRelabel
// and kFacePermutation tables, not asserted in prose.
TEST_CASE("PatternIdRollsMatchBodyRelabel", "[vision][capture-routing]") {
    // Clockwise 90-degree rotation of a 3x3 grid, as a cell permutation:
    // out[rot90[k]] = in[k].
    static constexpr std::array<int, 9> kRot90{2, 5, 8, 1, 4, 7, 0, 3, 6};

    for (std::size_t f = 0; f < 6; ++f) {
        const auto face = static_cast<cube::model::Face>(f);
        // The four rolls of `face`: lattice elements that keep it in place.
        std::vector<std::array<int, 9>> rolls;
        for (int g = 0; g < kLatticeSize; ++g) {
            if (kFacePermutation[static_cast<std::size_t>(g)][f] != face) {
                continue;
            }
            const auto& map = kBodyRelabel[static_cast<std::size_t>(g)][f];
            REQUIRE(map.body_face == face);
            rolls.push_back(map.cell);
        }
        REQUIRE(rolls.size() == 4);

        // The four grid rotations, generated by composing kRot90.
        std::vector<std::array<int, 9>> grid;
        std::array<int, 9> current{0, 1, 2, 3, 4, 5, 6, 7, 8};
        for (int turn = 0; turn < 4; ++turn) {
            grid.push_back(current);
            std::array<int, 9> next{};
            for (std::size_t k = 0; k < 9; ++k) {
                next[k] = kRot90[static_cast<std::size_t>(current[k])];
            }
            current = next;
        }

        std::sort(rolls.begin(), rolls.end());
        std::sort(grid.begin(), grid.end());
        REQUIRE(rolls == grid);
    }
}

TEST_CASE("PatternIdExtract", "[.pattern-id]") {
    const char* root_env = std::getenv("CUBE_PATTERN_ID_DIR");
    if (root_env == nullptr || *root_env == '\0') {
        SKIP("CUBE_PATTERN_ID_DIR not set - record clips via CUBE_VISION_RECORD_DIR first");
    }
    const std::filesystem::path root(root_env);
    const auto clips = collect_clip_dirs(root);
    if (clips.empty()) {
        SKIP("no recording directories (index.csv) under " << root.string() << " - corpus not recorded yet");
    }

    std::FILE* out = stdout;
    std::fprintf(out,
                 "clip,pass,t_ms,pose_valid,face_good,qw,qx,qy,qz,corner_view,trk_state,trk_front,trk_epoch,"
                 "geom_conf,"
                 "centre_class,cen0,cen1,cen2,cen3,cen4,cen5,cen6");
    for (int cell = 0; cell < 9; ++cell) {
        std::fprintf(out, ",L%d,a%d,b%d", cell, cell, cell);
    }
    std::fprintf(out, "\n");

    for (const auto& clip : clips) {
        emit_clip(clip, clip.filename().string(), out);
        std::fflush(out);
    }
    SUCCEED();
}
