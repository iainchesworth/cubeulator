// The offline pipeline runner (docs/pipeline-plan.md, Phase 0 item 5):
// replays a recording through the pipeline (legacy adapter for now), and
// emits the project's eyes and instruments:
//   - trace.jsonl      one line per frame (docs/pipeline-trace-schema.md)
//   - overlay.avi      annotated video (quads, cells, colours, wireframe,
//                      tracker state, gate verdicts, stage timings)
//   - metrics.json     per-clip summary
// plus a `corpus` mode that runs every recording under a root and prints
// one table, and a `synth` mode that generates a small synthetic
// recording so the runner is exercisable while the real corpus is being
// re-recorded (it is currently missing - see the plan).
//
// The runner is the ACCEPTANCE surface: it fails (non-zero exit) if any
// frame's trace is incomplete (validate_trace) - "trace everything" is
// enforced here, not hoped for.

#include <cube/model/facelet.hpp>
#include <cube/solver/scan_assembly.hpp>
#include <cube/vision/camera_intrinsics.hpp>
#include <cube/vision/color_resolution.hpp>
#include <cube/vision/temporal_fusion.hpp>
#include <cube/vision/track_fusion.hpp>
#include <cube/vision/corner_detector.hpp>
#include <cube/vision/frame_recording.hpp>
#include <cube/vision/groundtruth.hpp>
#include <cube/track_m3t/m3t_pose_tracker.hpp>
#include <cube/vision/pipeline/legacy_pipeline.hpp>
#include <cube/vision/pose_from_corners.hpp>
#include <cube/vision/recording_v2.hpp>
#include <cube/vision/pipeline/stage_types.hpp>
#include <cube/vision/pipeline/trace.hpp>

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/videoio.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <thread>
#include <fstream>
#include <iterator>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace fs = std::filesystem;
namespace pipeline = cube::vision::pipeline;
namespace recording = cube::vision::recording;
using cube::model::FacetColor;

namespace {

// The palette-seed BGR values (same seeds the detectors' tests draw with)
// for painting classified cells into the overlay.
cv::Scalar facet_bgr(FacetColor color) {
    switch (color) {
        case FacetColor::white: return {255, 255, 255};
        case FacetColor::yellow: return {0, 213, 255};
        case FacetColor::red: return {0, 0, 196};
        case FacetColor::orange: return {0, 106, 255};
        case FacetColor::green: return {72, 155, 0};
        case FacetColor::blue: return {173, 70, 0};
        case FacetColor::unknown: break;
    }
    return {128, 128, 128};
}

void draw_quad(cv::Mat& canvas, const std::array<cv::Point2f, 4>& quad, const cv::Scalar& color, int thickness) {
    for (int i = 0; i < 4; ++i) {
        cv::line(canvas, quad[static_cast<std::size_t>(i)], quad[static_cast<std::size_t>((i + 1) % 4)], color,
                 thickness, cv::LINE_AA);
    }
}

// Burn one frame's pipeline result into a copy of the frame. This is the
// "eyes" half of the observability spine: everything a stage produced is
// visible, nothing is smoothed or held (unlike the app overlay).
cv::Mat render_overlay(const cv::Mat& frame, const pipeline::LegacyPipelineResult& result) {
    cv::Mat canvas = frame.clone();

    if (result.detection.found && result.detection.roi.area() > 0) {
        cv::rectangle(canvas, result.detection.roi, cv::Scalar(80, 80, 80), 1, cv::LINE_AA);
    }

    // Cell quads + classified colours, then face quads on top.
    for (std::size_t f = 0; f < result.faces.faces.size(); ++f) {
        const auto& view = result.faces.faces[f];
        if (f < result.cells.faces.size()) {
            for (std::size_t c = 0; c < 9; ++c) {
                const auto& cell = result.cells.faces[f].cells[c];
                cv::Point2f center(0.0F, 0.0F);
                bool has_cell = false;
                for (const auto& corner : cell.corners) {
                    center += corner;
                    has_cell = has_cell || corner.x != 0.0F || corner.y != 0.0F;
                }
                if (!has_cell) {
                    continue;
                }
                center *= 0.25F;
                draw_quad(canvas, cell.corners, cv::Scalar(160, 160, 160), 1);
                if (f < result.reads.faces.size() && view.capture_grade) {
                    const auto facelet = result.reads.faces[f].facelets[c];
                    cv::circle(canvas, center, 5, facet_bgr(facelet.color), cv::FILLED, cv::LINE_AA);
                    cv::circle(canvas, center, 5, cv::Scalar(0, 0, 0), 1, cv::LINE_AA);
                }
            }
        }
        draw_quad(canvas, view.quad, view.capture_grade ? cv::Scalar(0, 200, 0) : cv::Scalar(0, 200, 200), 2);
        if (view.body_face) {
            const char label[2] = {"URFDLB"[static_cast<std::size_t>(*view.body_face)], '\0'};
            cv::putText(canvas, label, view.quad[0] + cv::Point2f(2.0F, 14.0F), cv::FONT_HERSHEY_SIMPLEX, 0.5,
                        cv::Scalar(0, 200, 0), 1, cv::LINE_AA);
        }
    }

    // Derived-face identity labels (P3 gate criterion 4): model-frame
    // face id + frontality at each tracked face's centre. Epoch-local
    // identity by design - stable within a gauge epoch, renamed only by
    // an anchor or a declared slip.
    if (!result.track_faces.empty()) {
        static const std::array<const char*, 6> kFaceNames{"+x", "-x", "+y", "-y", "+z", "-z"};
        for (const auto& face : result.track_faces) {
            cv::Point2f centre(0.0F, 0.0F);
            for (const auto& corner : face.quad) {
                centre += corner;
            }
            centre *= 0.25F;
            std::array<char, 32> label{};
            std::snprintf(label.data(), label.size(), "%s %.2f",
                          kFaceNames[static_cast<std::size_t>(face.face)], face.frontality);
            cv::putText(canvas, label.data(), centre + cv::Point2f(-18.0F, 4.0F), cv::FONT_HERSHEY_SIMPLEX, 0.45,
                        cv::Scalar(0, 255, 255), 1, cv::LINE_AA);
        }
    }

    // Learned corner candidates (stage B primary): magenta, Singmaster id.
    for (const auto& corner : result.detection.corners) {
        cv::circle(canvas, corner.px, 4, cv::Scalar(255, 0, 255), cv::FILLED, cv::LINE_AA);
        cv::circle(canvas, corner.px, 4, cv::Scalar(0, 0, 0), 1, cv::LINE_AA);
        if (corner.model_vertex >= 0) {
            cv::putText(canvas, std::to_string(corner.model_vertex), corner.px + cv::Point2f(6.0F, -4.0F),
                        cv::FONT_HERSHEY_SIMPLEX, 0.4, cv::Scalar(255, 160, 255), 1, cv::LINE_AA);
        }
    }

    // Pose wireframe: near face 0-1-2-3, far face 4-5-6-7, connecting ribs.
    if (result.track.pose_valid) {
        const auto& v = result.track.projected_vertices;
        const cv::Scalar wire(255, 200, 0);
        for (int i = 0; i < 4; ++i) {
            const auto a = static_cast<std::size_t>(i);
            const auto b = static_cast<std::size_t>((i + 1) % 4);
            cv::line(canvas, v[a], v[b], wire, 2, cv::LINE_AA);
            cv::line(canvas, v[a + 4], v[b + 4], wire, 1, cv::LINE_AA);
            cv::line(canvas, v[a], v[a + 4], wire, 1, cv::LINE_AA);
        }
    }

    // HUD: raw verdicts, no smoothing. Dark banner for legibility.
    const auto& track = result.track;
    std::array<char, 160> line1{};
    std::array<char, 160> line2{};
    std::snprintf(line1.data(), line1.size(), "#%d  %s/%s  epoch %d  front %c%s", result.trace.seq,
                  track.phase == pipeline::TrackPhase::lost        ? "lost"
                  : track.phase == pipeline::TrackPhase::acquiring ? "acquiring"
                  : track.phase == pipeline::TrackPhase::locked    ? "locked"
                                                                   : "coasting",
                  track.gate.c_str(), track.epoch,
                  track.front_face ? "URFDLB"[static_cast<std::size_t>(*track.front_face)] : '-',
                  track.identity_certain_front ? "*" : "");
    double total_ms = 0.0;
    for (const auto& stage : result.trace.stages) {
        total_ms += stage.duration_ms;
    }
    std::snprintf(line2.data(), line2.size(), "src %s  conf %.2f  reproj %.3f  faces %zu  %5.1f ms",
                  track.pose_source.c_str(), result.detection.confidence, track.reproj_fraction,
                  result.faces.faces.size(), total_ms);
    cv::rectangle(canvas, cv::Rect(0, 0, canvas.cols, 38), cv::Scalar(20, 20, 20), cv::FILLED);
    cv::putText(canvas, line1.data(), {6, 15}, cv::FONT_HERSHEY_SIMPLEX, 0.42, cv::Scalar(240, 240, 240), 1,
                cv::LINE_AA);
    cv::putText(canvas, line2.data(), {6, 32}, cv::FONT_HERSHEY_SIMPLEX, 0.42, cv::Scalar(180, 220, 180), 1,
                cv::LINE_AA);
    return canvas;
}

struct ClipMetrics {
    std::string clip;
    int frames_indexed = 0;
    int frames_replayed = 0;
    int frames_detected = 0;
    int frames_pose_valid = 0;
    int frames_locked = 0;
    int max_epoch = 0;
    int capture_grade_faces = 0;
    std::int64_t cells_classified = 0;
    std::int64_t cells_unknown = 0;
    std::map<std::string, int> state_counts;
    std::map<std::string, int> gate_counts;
    std::map<std::string, double> stage_ms_total;
    std::map<std::string, double> stage_ms_max;
    int trace_violations = 0;

    // Ground-truth-scored metrics (docs/pipeline-metrics.md), computed
    // only when the clip carries sidecars. -1 = not computable (no
    // labels, or no frames in the denominator's segment class).
    bool has_ground_truth = false;
    int gt_inframe_frames = 0;      // in_frame, not occluded
    int gt_inframe_detected = 0;
    int gt_occluded_frames = 0;
    int gt_occluded_detected = 0;
    int gt_absent_frames = 0;
    int gt_fp_sustained_runs = 0;   // >=5 consecutive found-frames inside absent segments
    int gt_keyframes_scored = 0;    // hand-clicked keyframes with a matched detected face
    std::vector<double> gt_centre_err_pitch;  // per labelled sticker centre, err / pitch
    int gt_whole_cell_misassignments = 0;     // centre error > 50% of pitch

    // Learned-corner localisation vs labelled corners
    // (docs/pipeline-metrics.md "corner localisation error"), scored UP TO
    // THE BEST CUBE ROTATION: detector ids are view-canonical (see
    // corner_detector.hpp), so each keyframe is matched under whichever of
    // the 24 rotation permutations minimises the mean error, and the
    // stability of that choice across keyframes is reported separately
    // (it is the gauge the Phase 2 tracker will pin).
    std::vector<double> gt_corner_err_extent;
    int gt_corners_labelled = 0;   // on keyframes where the detector fired
    int gt_corners_matched = 0;
    int gt_corner_keyframes = 0;       // keyframes entering the corner metric
    int gt_corner_perm_changes = 0;    // best-rotation flips between consecutive scored keyframes
    int gt_corner_last_perm = -1;

    // P2.7 tracked-pose metrics (docs/pipeline-metrics.md, Tracking
    // section): the CONTINUOUS track's projected model corners, scored
    // only on frames the tracker itself claims (phase locked,
    // pose_source "m3t"). Unlike the detector metric above, the
    // view->body gauge is pinned once per locked run
    // (score_tracked_frame) - re-picking per frame would let a mid-run
    // gauge flip masquerade as a small error instead of the pose jump
    // it is.
    std::vector<double> gt_track_corner_err_extent;  // per labelled corner, err / extent
    int gt_track_scored_frames = 0;
    int gt_track_gauge_runs = 0;              // locked runs that entered scoring
    // Registration diagnostics: geometric error above is scored up to
    // the best cube rotation per frame (a cube silhouette is invariant
    // under quarter turns, so the tracker CAN slip a 90-degree
    // registration while staying geometrically glued to the cube - and
    // the loop's own health check, also best-of-24, cannot see it).
    // These count how often the run's pinned registration survived: a
    // re-pin means the best gauge moved while the old one went out of
    // bound - a quarter-turn slip (or a face-on pin whose tie broke
    // wrong, indistinguishable by construction).
    int gt_track_reg_frames = 0;              // scored frames carrying a prior pin
    int gt_track_reg_held = 0;                // ... whose pinned gauge stayed within bound
    int gt_track_repins = 0;                  // registration slips (re-pins)
    int gt_inframe_any_frames = 0;            // in_frame incl. occluded - the lock-rate denominator
    int gt_lock_correct_frames = 0;           // locked and (unlabelled or within bound)
    int gt_lock_incorrect_frames = 0;         // locked but beyond bound on a labelled frame
    std::array<cv::Point2d, 8> gt_track_bias_sum{};  // residual-vector sums per BODY corner (of extent)
    std::array<int, 8> gt_track_bias_count{};
    // P3.1 pose-derived face geometry (face_projection.hpp) vs labelled
    // face quads/centres, scored on tracked frames. Quad corners match
    // under the best of the 8 dihedral alignments (the face-local frame
    // is a convention, not an observable, until P3.3 identity lands);
    // centres match nearest-neighbour within the matched face - honest
    // caveat: nearest matching cannot flag a whole-grid alias by itself,
    // the quad metric is what catches gross misplacement.
    std::vector<double> gt_face_quad_err_extent;   // per corner of each matched face
    int gt_faces_labelled = 0;
    int gt_faces_matched = 0;
    std::vector<double> gt_face_centre_err_pitch;  // per labelled centre of matched faces
    int gt_face_centre_misassigned = 0;            // centre err > 50% of pitch

    // Self-relative jitter evidence per steady segment (keyed by segment
    // start pass): projected corner positions on locked frames plus the
    // per-frame projected extent. Needs no labels - jitter is deviation
    // around the segment's own mean (docs/pipeline-metrics.md).
    struct SteadyJitterAccum {
        std::array<std::vector<cv::Point2f>, 8> corners;
        std::vector<double> extents;  // per-frame projected-vertex bbox longest side
    };
    std::map<int, SteadyJitterAccum> gt_track_steady;
    std::vector<double> gt_reacq_ms;          // per reentry event; -1 = never re-locked

    // P3 gate criterion 2, "no simmering": phase-correlation shift
    // magnitude between CONSECUTIVE steady frames' rectified images of
    // the same (most frontal) face - per-frame fitting could never hold
    // this; a projection of a stable pose must.
    std::vector<double> gt_rect_shift_px;

    // P3.3: quarter-turn slips the loop's ONLINE gauge detector declared
    // (trace field gauge_slip) - compare against gt_track_repins, the
    // ground-truth count of registration slips, on the console line.
    int gauge_slips_detected = 0;

    // P6.1 end-to-end: pose-derived reads fused per (epoch, face id),
    // best epoch's six faces resolved, assembled, and compared to the
    // manifest's oracle state. Both sides are centre-anchored (the
    // manifest format fixes centre letters positionally; the assembler
    // pins orientation by centre colours through the same scheme), so
    // exact-state match is plain Cube equality - whole-cube rotation is
    // already factored out of both representations.
    int fusion_best_epoch = -1;
    int fusion_epoch_faces = 0;      // face ids with enough observations in the best epoch
    int fusion_epoch_converged = 0;  // ... that also passed the aggregator's convergence gate
    int fusion_epoch_distinct = 0;   // ... counting one physical colour once (P6.15)
    bool exact_state_match = false;
    std::string e2e_outcome = "not-attempted";  // the per-clip refusal attribution
    // P6.15: what the chain actually built, in the manifest's own 54-char
    // encoding - empty unless assembly succeeded. On an UNLABELLED clip
    // this is the whole report (outcome "assembled-no-oracle"); on a
    // labelled one it is the mismatch's evidence.
    std::string assembled_state;
    // P6.3 content registration across epochs: verified merges and
    // refused ones (a shared face beyond the verify radius).
    int register_merges = 0;
    int register_refusals = 0;
    // P6.6 content-keyed sub-epochs: times the feed-time takeover
    // detector froze a generation's aggregators because an incoming
    // centre jumped implausibly far from a settled slot's fused centre
    // (a tracker slip without an epoch bump).
    int fusion_generation_splits = 0;
};

// Locked-and-correct bound (docs/pipeline-plan.md Phase 2 exit gate): a
// self-declared locked frame whose tracked-corner reprojection exceeds
// this fraction of the labelled extent counts AGAINST the lock rate.
// The value is the gate's own p95 corner bound, so "correct" means
// "inside the accuracy the gate already demands of the pose".
constexpr double kLockedCorrectBound = 0.03;

// The 24 corner-id permutations of the cube's rotation group, built by
// closure over two quarter-turn generators acting on the Singmaster
// model coordinates. perm[view_id] = body_id candidate alignment.
// Runner-local for now; promotes into the lattice module when Phase 3
// grows a production consumer.
const std::vector<std::array<int, 8>>& corner_rotation_perms() {
    static const std::vector<std::array<int, 8>> perms = [] {
        using cube::vision::kSingmasterCornerModel;
        const auto perm_of = [](const cv::Matx33f& rotation) {
            std::array<int, 8> perm{};
            for (int i = 0; i < 8; ++i) {
                const cv::Point3f p = kSingmasterCornerModel[static_cast<std::size_t>(i)];
                const cv::Matx31f rotated = rotation * cv::Matx31f(p.x, p.y, p.z);
                for (int j = 0; j < 8; ++j) {
                    const cv::Point3f q = kSingmasterCornerModel[static_cast<std::size_t>(j)];
                    if (cv::norm(cv::Point3f(rotated(0), rotated(1), rotated(2)) - q) < 0.1F) {
                        perm[static_cast<std::size_t>(i)] = j;
                        break;
                    }
                }
            }
            return perm;
        };
        const cv::Matx33f rot_y(0, 0, 1, 0, 1, 0, -1, 0, 0);   // +90 about y
        const cv::Matx33f rot_z(0, -1, 0, 1, 0, 0, 0, 0, 1);   // +90 about z
        std::vector<cv::Matx33f> rotations{cv::Matx33f::eye()};
        for (std::size_t i = 0; i < rotations.size(); ++i) {
            for (const auto& generator : {rot_y, rot_z}) {
                const cv::Matx33f candidate = generator * rotations[i];
                const bool known = std::any_of(rotations.begin(), rotations.end(), [&](const cv::Matx33f& r) {
                    return cv::norm(r - candidate) < 0.1;
                });
                if (!known) {
                    rotations.push_back(candidate);
                }
            }
        }
        std::vector<std::array<int, 8>> out;
        out.reserve(rotations.size());
        for (const auto& rotation : rotations) {
            out.push_back(perm_of(rotation));
        }
        return out;
    }();
    return perms;
}

// Median/p95 of an unsorted sample vector (copies; fine at these sizes).
std::pair<double, double> median_p95(std::vector<double> values) {
    if (values.empty()) {
        return {-1.0, -1.0};
    }
    std::sort(values.begin(), values.end());
    const auto at = [&values](double q) {
        const auto index = std::min(values.size() - 1, static_cast<std::size_t>(q * static_cast<double>(values.size())));
        return values[index];
    };
    return {at(0.5), at(0.95)};
}

// One steady segment's jitter (docs/pipeline-metrics.md "pose jitter"):
// per projected model corner, the sample standard deviation of its
// position across the segment's locked frames (2-D: RMS distance from
// the mean point, n-1 denominator), as a fraction of the segment's
// median projected extent; the segment reports its WORST corner. Self-
// relative - no labels involved - so it also runs on real clips whose
// keyframes are sparse. -1 = fewer than 2 locked frames.
double segment_jitter(const ClipMetrics::SteadyJitterAccum& accum) {
    if (accum.corners[0].size() < 2 || accum.extents.empty()) {
        return -1.0;
    }
    std::vector<double> extents = accum.extents;
    std::sort(extents.begin(), extents.end());
    const double extent = extents[extents.size() / 2];
    if (extent <= 1.0) {
        return -1.0;
    }
    double worst = 0.0;
    for (const auto& samples : accum.corners) {
        cv::Point2d mean(0.0, 0.0);
        for (const auto& p : samples) {
            mean += cv::Point2d(p.x, p.y);
        }
        mean /= static_cast<double>(samples.size());
        double sum_squares = 0.0;
        for (const auto& p : samples) {
            const cv::Point2d d = cv::Point2d(p.x, p.y) - mean;
            sum_squares += d.dot(d);
        }
        worst = std::max(worst, std::sqrt(sum_squares / static_cast<double>(samples.size() - 1)));
    }
    return worst / extent;
}

// Clip-level jitter: median over steady segments (per the metrics doc);
// -1 = no scorable segment.
double clip_jitter(const std::map<int, ClipMetrics::SteadyJitterAccum>& segments) {
    std::vector<double> values;
    for (const auto& [start, accum] : segments) {
        const double jitter = segment_jitter(accum);
        if (jitter >= 0.0) {
            values.push_back(jitter);
        }
    }
    if (values.empty()) {
        return -1.0;
    }
    std::sort(values.begin(), values.end());
    return values[values.size() / 2];
}

// Median re-acquisition latency over ALL events, failures (-1 = never
// re-locked before the cube left again or the clip ended) ranked worst:
// a corpus where the median event failed reports -1 rather than the
// median of the survivors - the gate must not reward giving up.
double reacq_median_ms(std::vector<double> latencies) {
    if (latencies.empty()) {
        return -1.0;
    }
    std::sort(latencies.begin(), latencies.end(),
              [](double a, double b) { return (a < 0.0 ? std::numeric_limits<double>::max() : a) <
                                              (b < 0.0 ? std::numeric_limits<double>::max() : b); });
    return latencies[latencies.size() / 2];
}

// Per-BODY-corner bias magnitudes (mean residual vector per corner, as a
// fraction of extent; -1 = corner never scored) and their maximum - the
// plan's anti-live5 criterion applied to the tracked pose.
std::pair<std::array<double, 8>, double> track_bias(const ClipMetrics& m) {
    std::array<double, 8> per_corner{};
    double worst = -1.0;
    for (std::size_t i = 0; i < 8; ++i) {
        if (m.gt_track_bias_count[i] > 0) {
            per_corner[i] = cv::norm(m.gt_track_bias_sum[i] / static_cast<double>(m.gt_track_bias_count[i]));
            worst = std::max(worst, per_corner[i]);
        } else {
            per_corner[i] = -1.0;
        }
    }
    return {per_corner, worst};
}

// P2.7: score the TRACKED pose's projected model corners against one
// labelled frame. Returns the frame's GEOMETRIC mean corner error -
// minimised over the 24 cube rotations, the same convention as the
// detector metric and the plan's reprojection bound (a quarter-turn-
// slipped pose still projects onto the cube's corners; whether the
// REGISTRATION survived is scored separately) - as a fraction of the
// labelled extent, -1 = unscorable. pinned_perm carries the run's
// registration: pinned on the first scored frame, held while it stays
// in bound, re-pinned (counted as a slip) when the best gauge moved
// and the old one went out of bound. The caller resets it to -1
// whenever the track is not live. Err samples accumulate under the
// best gauge, bias residuals under the pinned registration (residuals
// are attributed to the LABELLED body id - physical corners - either
// way).
double score_tracked_frame(const cube::vision::groundtruth::Keyframe& keyframe,
                           const std::array<cv::Point2f, 8>& projected, int& pinned_perm, ClipMetrics& metrics) {
    if (keyframe.corners.size() < 4) {
        return -1.0;
    }
    float min_x = keyframe.corners.front().px.x;
    float max_x = min_x;
    float min_y = keyframe.corners.front().px.y;
    float max_y = min_y;
    for (const auto& corner : keyframe.corners) {
        min_x = std::min(min_x, corner.px.x);
        max_x = std::max(max_x, corner.px.x);
        min_y = std::min(min_y, corner.px.y);
        max_y = std::max(max_y, corner.px.y);
    }
    const float extent = std::max(max_x - min_x, max_y - min_y);
    if (extent <= 1.0F) {
        return -1.0;
    }

    const auto mean_under = [&](const std::array<int, 8>& perm, bool accumulate_errs, bool accumulate_bias) {
        double total = 0.0;
        int count = 0;
        for (const auto& labelled : keyframe.corners) {
            for (int view_id = 0; view_id < 8; ++view_id) {
                if (perm[static_cast<std::size_t>(view_id)] == labelled.model_id) {
                    const cv::Point2f residual = projected[static_cast<std::size_t>(view_id)] - labelled.px;
                    const double err = cv::norm(residual) / extent;
                    total += err;
                    ++count;
                    if (labelled.model_id >= 0 && labelled.model_id < 8) {
                        if (accumulate_errs) {
                            metrics.gt_track_corner_err_extent.push_back(err);
                        }
                        if (accumulate_bias) {
                            metrics.gt_track_bias_sum[static_cast<std::size_t>(labelled.model_id)] +=
                                cv::Point2d(residual.x / extent, residual.y / extent);
                            ++metrics.gt_track_bias_count[static_cast<std::size_t>(labelled.model_id)];
                        }
                    }
                    break;
                }
            }
        }
        return count >= 4 ? total / count : -1.0;
    };

    int best_perm = -1;
    double best_mean = -1.0;
    for (std::size_t p = 0; p < corner_rotation_perms().size(); ++p) {
        const double mean = mean_under(corner_rotation_perms()[p], false, false);
        if (mean >= 0.0 && (best_perm < 0 || mean < best_mean)) {
            best_mean = mean;
            best_perm = static_cast<int>(p);
        }
    }
    if (best_perm < 0) {
        return -1.0;
    }

    if (pinned_perm < 0) {
        pinned_perm = best_perm;
        ++metrics.gt_track_gauge_runs;
    } else {
        const double pinned_mean =
            pinned_perm == best_perm
                ? best_mean
                : mean_under(corner_rotation_perms()[static_cast<std::size_t>(pinned_perm)], false, false);
        ++metrics.gt_track_reg_frames;
        if (pinned_mean >= 0.0 && pinned_mean <= kLockedCorrectBound) {
            ++metrics.gt_track_reg_held;
        } else if (best_perm != pinned_perm && best_mean <= kLockedCorrectBound) {
            ++metrics.gt_track_repins;  // the registration slipped a cube symmetry
            pinned_perm = best_perm;
        }
    }

    mean_under(corner_rotation_perms()[static_cast<std::size_t>(best_perm)], true, best_perm == pinned_perm);
    if (best_perm != pinned_perm) {
        mean_under(corner_rotation_perms()[static_cast<std::size_t>(pinned_perm)], false, true);
    }
    ++metrics.gt_track_scored_frames;
    return best_mean;
}

// P3.1: score the pose-DERIVED faces (face_projection.hpp) against one
// labelled frame. Each labelled face matches the nearest derived quad
// by centre (within half the labelled diagonal); quad corners score
// under the best of the 8 dihedral alignments as a fraction of the
// labelled corner extent; sticker centres score nearest-neighbour
// within the matched face as a fraction of the labelled pitch.
void score_tracked_faces(const cube::vision::groundtruth::Keyframe& keyframe,
                         const std::vector<cube::vision::pipeline::ProjectedFace>& derived, ClipMetrics& metrics) {
    if (derived.empty() || keyframe.faces.empty() || keyframe.corners.size() < 2) {
        return;
    }
    float min_x = keyframe.corners.front().px.x;
    float max_x = min_x;
    float min_y = keyframe.corners.front().px.y;
    float max_y = min_y;
    for (const auto& corner : keyframe.corners) {
        min_x = std::min(min_x, corner.px.x);
        max_x = std::max(max_x, corner.px.x);
        min_y = std::min(min_y, corner.px.y);
        max_y = std::max(max_y, corner.px.y);
    }
    const double extent = std::max(max_x - min_x, max_y - min_y);
    if (extent <= 1.0) {
        return;
    }

    static const std::array<std::array<int, 4>, 8> kDihedral{{{0, 1, 2, 3},
                                                              {1, 2, 3, 0},
                                                              {2, 3, 0, 1},
                                                              {3, 0, 1, 2},
                                                              {3, 2, 1, 0},
                                                              {2, 1, 0, 3},
                                                              {1, 0, 3, 2},
                                                              {0, 3, 2, 1}}};

    for (const auto& labelled : keyframe.faces) {
        ++metrics.gt_faces_labelled;
        cv::Point2f label_centre(0.0F, 0.0F);
        for (const auto& corner : labelled.quad) {
            label_centre += corner;
        }
        label_centre *= 0.25F;
        const double diagonal = cv::norm(labelled.quad[0] - labelled.quad[2]);

        const cube::vision::pipeline::ProjectedFace* best_face = nullptr;
        double best_distance = diagonal * 0.5;
        for (const auto& candidate : derived) {
            cv::Point2f candidate_centre(0.0F, 0.0F);
            for (const auto& corner : candidate.quad) {
                candidate_centre += corner;
            }
            candidate_centre *= 0.25F;
            const double distance = cv::norm(candidate_centre - label_centre);
            if (distance < best_distance) {
                best_distance = distance;
                best_face = &candidate;
            }
        }
        if (best_face == nullptr) {
            continue;
        }
        ++metrics.gt_faces_matched;

        double best_mean = -1.0;
        std::array<double, 4> best_errors{};
        for (const auto& alignment : kDihedral) {
            double total = 0.0;
            std::array<double, 4> errors{};
            for (int i = 0; i < 4; ++i) {
                errors[static_cast<std::size_t>(i)] =
                    cv::norm(best_face->quad[static_cast<std::size_t>(alignment[static_cast<std::size_t>(i)])] -
                             labelled.quad[static_cast<std::size_t>(i)]) /
                    extent;
                total += errors[static_cast<std::size_t>(i)];
            }
            if (best_mean < 0.0 || total / 4.0 < best_mean) {
                best_mean = total / 4.0;
                best_errors = errors;
            }
        }
        metrics.gt_face_quad_err_extent.insert(metrics.gt_face_quad_err_extent.end(), best_errors.begin(),
                                               best_errors.end());

        // Labelled sticker pitch: median adjacent-centre distance (same
        // recipe as the legacy cell metric).
        std::vector<double> gaps;
        for (int row = 0; row < 3; ++row) {
            for (int col = 0; col < 2; ++col) {
                gaps.push_back(cv::norm(labelled.centres[static_cast<std::size_t>(row * 3 + col + 1)] -
                                        labelled.centres[static_cast<std::size_t>(row * 3 + col)]));
                gaps.push_back(cv::norm(labelled.centres[static_cast<std::size_t>((col + 1) * 3 + row)] -
                                        labelled.centres[static_cast<std::size_t>(col * 3 + row)]));
            }
        }
        std::sort(gaps.begin(), gaps.end());
        const double pitch = gaps[gaps.size() / 2];
        if (pitch <= 0.0) {
            continue;
        }
        for (const auto& centre : labelled.centres) {
            double nearest = -1.0;
            for (const auto& candidate : best_face->centres) {
                const double distance = cv::norm(candidate - centre);
                if (nearest < 0.0 || distance < nearest) {
                    nearest = distance;
                }
            }
            metrics.gt_face_centre_err_pitch.push_back(nearest / pitch);
            metrics.gt_face_centre_misassigned += nearest / pitch > 0.5 ? 1 : 0;
        }
    }
}

// Score one hand-clicked keyframe against this frame's detected faces:
// match each labelled face to the nearest detected face view by quad
// centre (within half the labelled quad diagonal), then accumulate cell
// centre errors as fractions of the labelled sticker pitch
// (docs/pipeline-metrics.md "cell-centre error"). Content-agnostic - no
// face identity needed, so it works under the legacy adapter too.
// The 8 dihedral index permutations of the 3x3 cell grid (rotations +
// reflection), for scoring track-sourced cells whose declared face-local
// frame is a convention away from the labels' body-frame grid order -
// the assembler's roll search absorbs exactly this (plan Phase 3 item
// 4). perm[label_index] = candidate cell index under the alignment.
const std::array<std::array<int, 9>, 8>& cell_grid_alignments() {
    static const std::array<std::array<int, 9>, 8> alignments = [] {
        std::array<std::array<int, 9>, 8> out{};
        for (int flip = 0; flip < 2; ++flip) {
            for (int rot = 0; rot < 4; ++rot) {
                auto& perm = out[static_cast<std::size_t>(flip * 4 + rot)];
                for (int index = 0; index < 9; ++index) {
                    int row = index / 3;
                    int col = index % 3;
                    for (int turn = 0; turn < rot; ++turn) {
                        const int next_row = col;
                        const int next_col = 2 - row;
                        row = next_row;
                        col = next_col;
                    }
                    if (flip == 1) {
                        col = 2 - col;
                    }
                    perm[static_cast<std::size_t>(index)] = row * 3 + col;
                }
            }
        }
        return out;
    }();
    return alignments;
}

void score_keyframe(const cube::vision::groundtruth::Keyframe& keyframe,
                    const cube::vision::pipeline::LegacyPipelineResult& result, bool alignment_invariant,
                    ClipMetrics& metrics) {
    // Learned-corner localisation, scored up to the best cube rotation
    // (view-canonical detector ids vs body-frame labels - see the
    // ClipMetrics comment). Extent = longest side of the labelled
    // visible-corner bbox, per the metrics doc.
    if (result.detection.corners.size() >= 4 && !keyframe.corners.empty()) {
        float min_x = keyframe.corners.front().px.x;
        float max_x = min_x;
        float min_y = keyframe.corners.front().px.y;
        float max_y = min_y;
        for (const auto& corner : keyframe.corners) {
            min_x = std::min(min_x, corner.px.x);
            max_x = std::max(max_x, corner.px.x);
            min_y = std::min(min_y, corner.px.y);
            max_y = std::max(max_y, corner.px.y);
        }
        const float extent = std::max(max_x - min_x, max_y - min_y);
        if (extent > 1.0F) {
            const cv::Point2f* detected_by_id[8] = {};
            for (const auto& detected : result.detection.corners) {
                if (detected.model_vertex >= 0 && detected.model_vertex < 8) {
                    detected_by_id[detected.model_vertex] = &detected.px;
                }
            }
            int best_perm = -1;
            double best_mean = 0.0;
            std::vector<double> best_errors;
            for (std::size_t p = 0; p < corner_rotation_perms().size(); ++p) {
                const auto& perm = corner_rotation_perms()[p];
                std::vector<double> errors;
                for (const auto& labelled : keyframe.corners) {
                    // Which view-canonical id does this body corner get
                    // under alignment p? perm maps view->body, so search
                    // the preimage.
                    for (int view_id = 0; view_id < 8; ++view_id) {
                        if (perm[static_cast<std::size_t>(view_id)] == labelled.model_id) {
                            if (detected_by_id[view_id] != nullptr) {
                                errors.push_back(cv::norm(*detected_by_id[view_id] - labelled.px) / extent);
                            }
                            break;
                        }
                    }
                }
                if (errors.size() < 4) {
                    continue;
                }
                double total = 0.0;
                for (const double err : errors) {
                    total += err;
                }
                const double mean = total / static_cast<double>(errors.size());
                if (best_perm < 0 || mean < best_mean) {
                    best_perm = static_cast<int>(p);
                    best_mean = mean;
                    best_errors = std::move(errors);
                }
            }
            if (best_perm >= 0) {
                ++metrics.gt_corner_keyframes;
                metrics.gt_corners_labelled += static_cast<int>(keyframe.corners.size());
                metrics.gt_corners_matched += static_cast<int>(best_errors.size());
                metrics.gt_corner_err_extent.insert(metrics.gt_corner_err_extent.end(), best_errors.begin(),
                                                    best_errors.end());
                if (metrics.gt_corner_last_perm >= 0 && best_perm != metrics.gt_corner_last_perm) {
                    ++metrics.gt_corner_perm_changes;
                }
                metrics.gt_corner_last_perm = best_perm;
            }
        }
    }

    bool scored_any = false;
    for (const auto& labelled : keyframe.faces) {
        cv::Point2f label_centre(0.0F, 0.0F);
        for (const auto& corner : labelled.quad) {
            label_centre += corner;
        }
        label_centre *= 0.25F;
        const float diagonal = static_cast<float>(cv::norm(labelled.quad[0] - labelled.quad[2]));

        // Labelled sticker pitch: median adjacent-centre distance.
        std::vector<double> gaps;
        for (int row = 0; row < 3; ++row) {
            for (int col = 0; col < 2; ++col) {
                gaps.push_back(cv::norm(labelled.centres[static_cast<std::size_t>(row * 3 + col + 1)] -
                                        labelled.centres[static_cast<std::size_t>(row * 3 + col)]));
                gaps.push_back(cv::norm(labelled.centres[static_cast<std::size_t>((col + 1) * 3 + row)] -
                                        labelled.centres[static_cast<std::size_t>(col * 3 + row)]));
            }
        }
        std::sort(gaps.begin(), gaps.end());
        const double pitch = gaps[gaps.size() / 2];
        if (pitch <= 0.0) {
            continue;
        }

        // Nearest detected capture-grade face view with cells.
        int best = -1;
        double best_distance = diagonal * 0.5;
        for (std::size_t f = 0; f < result.faces.faces.size(); ++f) {
            if (f >= result.cells.faces.size()) {
                continue;
            }
            cv::Point2f detected_centre(0.0F, 0.0F);
            for (const auto& corner : result.faces.faces[f].quad) {
                detected_centre += corner;
            }
            detected_centre *= 0.25F;
            const double distance = cv::norm(detected_centre - label_centre);
            if (distance < best_distance) {
                best_distance = distance;
                best = static_cast<int>(f);
            }
        }
        if (best < 0) {
            continue;
        }
        scored_any = true;
        std::array<cv::Point2f, 9> cell_centres{};
        for (std::size_t c = 0; c < 9; ++c) {
            const auto& cell = result.cells.faces[static_cast<std::size_t>(best)].cells[c];
            cv::Point2f cell_centre(0.0F, 0.0F);
            for (const auto& corner : cell.corners) {
                cell_centre += corner;
            }
            cell_centre *= 0.25F;
            cell_centres[c] = cell_centre;
        }
        // Track-sourced cells live in the declared face-local frame, a
        // dihedral alignment away from the labels' grid order; legacy
        // cells claim the labels' own order and stay strictly indexed
        // (an off-by-one there is a real permutation bug, not a
        // convention).
        std::array<int, 9> chosen{0, 1, 2, 3, 4, 5, 6, 7, 8};
        if (alignment_invariant) {
            double best_total = -1.0;
            for (const auto& perm : cell_grid_alignments()) {
                double total = 0.0;
                for (std::size_t c = 0; c < 9; ++c) {
                    total += cv::norm(cell_centres[static_cast<std::size_t>(perm[c])] - labelled.centres[c]);
                }
                if (best_total < 0.0 || total < best_total) {
                    best_total = total;
                    chosen = perm;
                }
            }
        }
        for (std::size_t c = 0; c < 9; ++c) {
            const double err =
                cv::norm(cell_centres[static_cast<std::size_t>(chosen[c])] - labelled.centres[c]) / pitch;
            metrics.gt_centre_err_pitch.push_back(err);
            metrics.gt_whole_cell_misassignments += err > 0.5 ? 1 : 0;
        }
    }
    metrics.gt_keyframes_scored += scored_any ? 1 : 0;
}

void write_metrics_json(const fs::path& path, const ClipMetrics& m) {
    std::ofstream out(path, std::ios::trunc);
    out << "{\n";
    out << "  \"clip\": \"" << m.clip << "\",\n";
    out << "  \"frames_indexed\": " << m.frames_indexed << ",\n";
    out << "  \"frames_replayed\": " << m.frames_replayed << ",\n";
    out << "  \"frames_detected\": " << m.frames_detected << ",\n";
    out << "  \"frames_pose_valid\": " << m.frames_pose_valid << ",\n";
    out << "  \"frames_locked\": " << m.frames_locked << ",\n";
    out << "  \"epochs\": " << m.max_epoch << ",\n";
    out << "  \"capture_grade_faces\": " << m.capture_grade_faces << ",\n";
    out << "  \"cells_classified\": " << m.cells_classified << ",\n";
    out << "  \"cells_unknown\": " << m.cells_unknown << ",\n";
    out << "  \"trace_violations\": " << m.trace_violations << ",\n";
    out << "  \"e2e_outcome\": \"" << m.e2e_outcome << "\",\n";
    out << "  \"assembled_state\": \"" << m.assembled_state << "\",\n";
    out << "  \"exact_state_match\": " << (m.exact_state_match ? "true" : "false") << ",\n";
    out << "  \"fusion_best_epoch\": " << m.fusion_best_epoch << ",\n";
    out << "  \"fusion_epoch_faces\": " << m.fusion_epoch_faces << ",\n";
    out << "  \"fusion_epoch_converged\": " << m.fusion_epoch_converged << ",\n";
    out << "  \"fusion_epoch_distinct\": " << m.fusion_epoch_distinct << ",\n";
    const auto write_map = [&out](const char* key, const auto& map, auto&& value_writer, bool trailing_comma) {
        out << "  \"" << key << "\": {";
        bool first = true;
        for (const auto& [k, v] : map) {
            out << (first ? "" : ", ") << "\"" << k << "\": ";
            value_writer(v);
            first = false;
        }
        out << "}" << (trailing_comma ? "," : "") << "\n";
    };
    write_map("state_counts", m.state_counts, [&out](int v) { out << v; }, true);
    write_map("gate_counts", m.gate_counts, [&out](int v) { out << v; }, true);
    write_map("stage_ms_total", m.stage_ms_total, [&out](double v) { out << v; }, true);
    write_map("stage_ms_max", m.stage_ms_max, [&out](double v) { out << v; }, m.has_ground_truth);
    if (m.has_ground_truth) {
        const auto [centre_median, centre_p95] = median_p95(m.gt_centre_err_pitch);
        const auto [corner_median, corner_p95] = median_p95(m.gt_corner_err_extent);
        out << "  \"ground_truth\": {\n";
        out << "    \"corners_labelled\": " << m.gt_corners_labelled << ",\n";
        out << "    \"corners_matched\": " << m.gt_corners_matched << ",\n";
        out << "    \"corner_err_extent_median\": " << corner_median << ",\n";
        out << "    \"corner_err_extent_p95\": " << corner_p95 << ",\n";
        const auto [track_median, track_p95] = median_p95(m.gt_track_corner_err_extent);
        const auto [bias_per_corner, bias_max] = track_bias(m);
        int reacq_failed = 0;
        for (const double latency : m.gt_reacq_ms) {
            reacq_failed += latency < 0.0 ? 1 : 0;
        }
        out << "    \"track_scored_frames\": " << m.gt_track_scored_frames << ",\n";
        out << "    \"track_err_extent_median\": " << track_median << ",\n";
        out << "    \"track_err_extent_p95\": " << track_p95 << ",\n";
        out << "    \"track_gauge_runs\": " << m.gt_track_gauge_runs << ",\n";
        out << "    \"track_reg_frames\": " << m.gt_track_reg_frames << ",\n";
        out << "    \"track_reg_held\": " << m.gt_track_reg_held << ",\n";
        out << "    \"track_repins\": " << m.gt_track_repins << ",\n";
        out << "    \"inframe_any_frames\": " << m.gt_inframe_any_frames << ",\n";
        out << "    \"locked_correct_frames\": " << m.gt_lock_correct_frames << ",\n";
        out << "    \"locked_incorrect_frames\": " << m.gt_lock_incorrect_frames << ",\n";
        out << "    \"track_bias_extent_per_corner\": [";
        for (std::size_t i = 0; i < bias_per_corner.size(); ++i) {
            out << (i == 0 ? "" : ", ") << bias_per_corner[i];
        }
        out << "],\n";
        out << "    \"track_bias_extent_max\": " << bias_max << ",\n";
        const auto [face_quad_median, face_quad_p95] = median_p95(m.gt_face_quad_err_extent);
        const auto [face_centre_median, face_centre_p95] = median_p95(m.gt_face_centre_err_pitch);
        out << "    \"faces_labelled\": " << m.gt_faces_labelled << ",\n";
        out << "    \"faces_matched\": " << m.gt_faces_matched << ",\n";
        out << "    \"face_quad_err_extent_median\": " << face_quad_median << ",\n";
        out << "    \"face_quad_err_extent_p95\": " << face_quad_p95 << ",\n";
        out << "    \"face_centre_err_pitch_median\": " << face_centre_median << ",\n";
        out << "    \"face_centre_err_pitch_p95\": " << face_centre_p95 << ",\n";
        out << "    \"face_centre_misassigned\": " << m.gt_face_centre_misassigned << ",\n";
        const auto [rect_shift_median, rect_shift_p95] = median_p95(m.gt_rect_shift_px);
        out << "    \"rect_shift_px_median\": " << rect_shift_median << ",\n";
        out << "    \"rect_shift_px_p95\": " << rect_shift_p95 << ",\n";
        out << "    \"rect_shift_pairs\": " << m.gt_rect_shift_px.size() << ",\n";
        out << "    \"track_jitter_extent_median\": " << clip_jitter(m.gt_track_steady) << ",\n";
        out << "    \"track_jitter_segments\": " << m.gt_track_steady.size() << ",\n";
        out << "    \"reacq_events\": " << m.gt_reacq_ms.size() << ",\n";
        out << "    \"reacq_failed\": " << reacq_failed << ",\n";
        out << "    \"reacq_median_ms\": " << reacq_median_ms(m.gt_reacq_ms) << ",\n";
        out << "    \"reacq_latency_ms\": [";
        for (std::size_t i = 0; i < m.gt_reacq_ms.size(); ++i) {
            out << (i == 0 ? "" : ", ") << m.gt_reacq_ms[i];
        }
        out << "],\n";
        out << "    \"inframe_frames\": " << m.gt_inframe_frames << ",\n";
        out << "    \"inframe_detected\": " << m.gt_inframe_detected << ",\n";
        out << "    \"occluded_frames\": " << m.gt_occluded_frames << ",\n";
        out << "    \"occluded_detected\": " << m.gt_occluded_detected << ",\n";
        out << "    \"absent_frames\": " << m.gt_absent_frames << ",\n";
        out << "    \"fp_sustained_runs\": " << m.gt_fp_sustained_runs << ",\n";
        out << "    \"keyframes_scored\": " << m.gt_keyframes_scored << ",\n";
        out << "    \"centre_err_pitch_median\": " << centre_median << ",\n";
        out << "    \"centre_err_pitch_p95\": " << centre_p95 << ",\n";
        out << "    \"centre_err_samples\": " << m.gt_centre_err_pitch.size() << ",\n";
        out << "    \"whole_cell_misassignments\": " << m.gt_whole_cell_misassignments << "\n";
        out << "  }\n";
    }
    out << "}\n";
}

// The manifest's face order at offsets 0,9,..,45 - shared by the state
// string's reader and its writer below, which must agree or the round
// trip silently transposes faces.
constexpr std::array<cube::solver::Face, 6> kManifestFaceOrder{
    cube::solver::Face::U, cube::solver::Face::R, cube::solver::Face::F,
    cube::solver::Face::D, cube::solver::Face::L, cube::solver::Face::B,
};

// The manifest's oracle state as a solver Cube (P6.1). Mirrors the
// recorder's validated parsing: centres positionally fixed, letters ->
// canonical colours. nullopt for clips without a state or with an
// unparseable one (the recorder's oracle should have refused those at
// write time).
std::optional<cube::solver::Cube> manifest_cube(const std::string& state) {
    if (state.size() != 54) {
        return std::nullopt;
    }
    std::array<std::array<cube::solver::Color, 9>, 6> facelets{};
    for (std::size_t i = 0; i < 54; ++i) {
        cube::solver::Face face{};
        switch (state[i]) {
            case 'U': face = cube::solver::Face::U; break;
            case 'R': face = cube::solver::Face::R; break;
            case 'F': face = cube::solver::Face::F; break;
            case 'D': face = cube::solver::Face::D; break;
            case 'L': face = cube::solver::Face::L; break;
            case 'B': face = cube::solver::Face::B; break;
            default: return std::nullopt;
        }
        facelets[static_cast<std::size_t>(kManifestFaceOrder[i / 9])][i % 9] = cube::solver::canonical_color(face);
    }
    auto cube = cube::solver::Cube::from_facelets(facelets);
    if (!cube.has_value()) {
        return std::nullopt;
    }
    return *cube;
}

// manifest_cube's inverse (P6.15): the 54-character state string an
// assembled cube would have carried in a manifest. An UNLABELLED clip has
// nothing to compare against, so the only honest report of what the
// pipeline built is the state itself - printed for a human to eyeball
// against the recording (and to paste back as the oracle once confirmed).
std::string manifest_state_string(const cube::solver::Cube& cube) {
    std::string state;
    state.reserve(54);
    for (const auto face : kManifestFaceOrder) {
        for (int index = 0; index < 9; ++index) {
            const cube::solver::Color color = cube.at(face, index);
            char letter = '?';
            for (const auto candidate : cube::solver::kAllFaces) {
                if (cube::solver::canonical_color(candidate) == color) {
                    letter = "UDLRFB"[static_cast<std::size_t>(candidate)];
                }
            }
            state.push_back(letter);
        }
    }
    return state;
}

// Per-clip camera intrinsics (P2.9): intrinsics.txt is the calibration
// tool's flat format (read_intrinsics); intrinsics.json is the synth
// generator's sidecar, parsed by field scan - the synthetic clips render
// at fx=700 while the f=width approximation assumes 640, so scoring them
// under the approximation was a measured distortion of every
// PnP-derived number.
std::optional<cube::vision::CameraIntrinsics> load_clip_intrinsics(const fs::path& dir) {
    if (auto loaded = cube::vision::read_intrinsics((dir / "intrinsics.txt").string()); loaded.has_value()) {
        return *loaded;
    }
    std::ifstream json_stream(dir / "intrinsics.json");
    if (!json_stream) {
        return std::nullopt;
    }
    const std::string text((std::istreambuf_iterator<char>(json_stream)), std::istreambuf_iterator<char>());
    const auto field = [&text](const char* name) -> double {
        const auto at = text.find('"' + std::string(name) + '"');
        if (at == std::string::npos) {
            return 0.0;
        }
        const auto colon = text.find(':', at);
        return colon == std::string::npos ? 0.0 : std::atof(text.c_str() + colon + 1);
    };
    cube::vision::CameraIntrinsics value;
    value.fx = field("fx");
    value.fy = field("fy");
    value.cx = field("cx");
    value.cy = field("cy");
    value.calibrated_size = cv::Size(static_cast<int>(field("width")), static_cast<int>(field("height")));
    if (value.fx <= 0.0 || value.fy <= 0.0 || value.calibrated_size.width <= 0) {
        return std::nullopt;
    }
    return value;
}

int replay_clip(const fs::path& dir, const fs::path& out_dir, bool write_video, int max_frames,
                cube::vision::OnnxCornerDetector* detector, cube::vision::pipeline::PoseTracker* tracker,
                bool refine_enabled, ClipMetrics& metrics) {
    std::ifstream index_stream(dir / std::string(recording::kIndexCsvName));
    if (!index_stream) {
        std::fprintf(stderr, "error: no %s under %s - not a recording directory\n",
                     std::string(recording::kIndexCsvName).c_str(), dir.string().c_str());
        return 2;
    }
    const auto rows = recording::read_index(index_stream);
    metrics.clip = dir.filename().string();
    metrics.frames_indexed = static_cast<int>(rows.size());

    // Ground truth, when the clip carries it (docs/ground-truth-format.md).
    namespace gt = cube::vision::groundtruth;
    gt::ClipGroundTruth truth;
    if (auto loaded = gt::load_clip(dir.string())) {
        truth = std::move(*loaded);
        metrics.has_ground_truth = truth.segments.has_value() || !truth.keyframes.empty();
    } else {
        std::fprintf(stderr, "warning: ground truth unreadable, scoring without it: %s\n", loaded.error().c_str());
    }
    int fp_run = 0;  // consecutive found-frames inside absent segments

    int max_pass = 0;
    for (const auto& row : rows) {
        max_pass = std::max(max_pass, row.pass);
    }

    fs::create_directories(out_dir);
    std::ofstream trace_out(out_dir / "trace.jsonl", std::ios::trunc);

    // Overlay video: fps from the median inter-frame delta so the clip
    // plays back at roughly recorded speed. MJPG/AVI writes with OpenCV's
    // built-in encoder - no external codec dependency.
    std::vector<std::int64_t> deltas;
    for (std::size_t i = 1; i < rows.size(); ++i) {
        const auto d = rows[i].t_ms - rows[i - 1].t_ms;
        if (d > 0) {
            deltas.push_back(d);
        }
    }
    double fps = 30.0;
    if (!deltas.empty()) {
        std::nth_element(deltas.begin(), deltas.begin() + static_cast<std::ptrdiff_t>(deltas.size() / 2), deltas.end());
        const double median_dt = static_cast<double>(deltas[deltas.size() / 2]);
        fps = std::clamp(1000.0 / median_dt, 5.0, 60.0);
    }
    cv::VideoWriter video;

    pipeline::LegacyPipeline pipe;
    pipe.set_corner_detector(detector);
    if (tracker != nullptr) {
        tracker->reset();  // fresh track per clip; the region-model cache persists
        pipe.set_pose_tracker(tracker);
    }
    pipe.set_refine_enabled(refine_enabled);
    if (const auto clip_intrinsics = load_clip_intrinsics(dir)) {
        pipe.set_camera_intrinsics(*clip_intrinsics);
        std::printf("%-12s using clip intrinsics fx %.1f fy %.1f (approximation would be fx %d)\n",
                    metrics.clip.c_str(), clip_intrinsics->fx, clip_intrinsics->fy,
                    clip_intrinsics->calibrated_size.width);
    }

    // P2.7 tracked-pose scoring state. The gauge pin lives across frames
    // of one locked run; re-acquisition latency walks the sidecar's
    // reentry events in pass order, timing each until the first
    // locked-and-correct frame - or marking it failed (-1) if the cube
    // goes absent again (or the clip ends) first.
    int track_pinned_perm = -1;
    // P6.1/P6.6 fusion store + per-epoch generation counters. The store's
    // shape, the content-keyed sub-epoch scheme, the takeover detector
    // and the registration pass all live in cube/vision/track_fusion.hpp
    // since P6.12 - ONE definition shared with the live app's track-mode
    // capture path (scan_session.cpp); the rationale comments moved with
    // the code.
    cube::vision::fusion::FusionStore fusion;
    // Current generation per gauge_epoch (starts at 0).
    std::map<int, int> fusion_generation;
    const auto manifest = recording::read_manifest(dir.string());
    // Rectified-stability state: previous steady frame's most-frontal
    // rectified face (float grey for phaseCorrelate), its model face id
    // and pass number - only same-face, consecutive-pass pairs score.
    cv::Mat rect_prev;
    int rect_prev_face = -1;
    int rect_prev_pass = -10;
    std::vector<int> reentry_events;
    if (truth.segments) {
        reentry_events = truth.segments->reentry_events;
        std::sort(reentry_events.begin(), reentry_events.end());
    }
    std::size_t next_reentry = 0;
    std::int64_t reentry_pending_t = -1;

    int replayed = 0;
    for (const auto& row : rows) {
        if (max_frames > 0 && replayed >= max_frames) {
            break;
        }
        if (recording::frame_overwritten(row.pass, max_pass)) {
            continue;
        }
        const cv::Mat frame = cv::imread((dir / recording::frame_filename(row.pass)).string(), cv::IMREAD_COLOR);
        if (frame.empty() || frame.cols != row.w || frame.rows != row.h) {
            continue;
        }

        pipeline::FramePacket packet;
        packet.bgr = frame;
        packet.seq = row.pass;
        packet.t_ms = row.t_ms;
        packet.full_size = cv::Size(row.full_w, row.full_h);
        const auto result = pipe.process(packet);
        ++replayed;

        if (const auto problem = pipeline::validate_trace(result.trace)) {
            std::fprintf(stderr, "TRACE VIOLATION: %s\n", problem->c_str());
            ++metrics.trace_violations;
        }
        trace_out << pipeline::to_jsonl(result.trace) << "\n";

        if (truth.segments) {
            const gt::TagSet tags = truth.segments->tags_at(row.pass);
            if ((tags & gt::kAbsent) != 0) {
                ++metrics.gt_absent_frames;
                fp_run = result.detection.found ? fp_run + 1 : 0;
                if (fp_run == gt::kSustainedFrames) {
                    ++metrics.gt_fp_sustained_runs;  // counted once per run, at threshold
                }
            } else {
                fp_run = 0;
                if ((tags & gt::kInFrame) != 0) {
                    if ((tags & gt::kOccluded) != 0) {
                        ++metrics.gt_occluded_frames;
                        metrics.gt_occluded_detected += result.detection.found ? 1 : 0;
                    } else {
                        ++metrics.gt_inframe_frames;
                        metrics.gt_inframe_detected += result.detection.found ? 1 : 0;
                    }
                }
            }
        }
        if (const auto keyframe = truth.keyframes.find(row.pass);
            keyframe != truth.keyframes.end() && !keyframe->second.interpolated) {
            const bool cells_from_track =
                result.track.phase == pipeline::TrackPhase::locked && result.track.pose_source == "m3t";
            score_keyframe(keyframe->second, result, cells_from_track, metrics);
        }

        // --- P6.1: fusion feed -----------------------------------------
        // Capture-grade tracked faces' per-cell reads accumulate under
        // their epoch-local identity; a sampled centre (purity > 0) is
        // the "this face was really read" proxy. The content-keyed
        // sub-epoch and its feed-time takeover detector are the shared
        // implementation (track_fusion.hpp) - the app's live feed applies
        // the identical conditions and the identical add.
        if (result.track.pose_source == "m3t" && result.track.phase == pipeline::TrackPhase::locked) {
            for (std::size_t f = 0; f < result.track_faces.size() && f < result.faces.faces.size(); ++f) {
                if (!result.faces.faces[f].capture_grade || result.samples.faces[f].purity[4] <= 0.0F) {
                    continue;
                }
                cube::vision::FaceObservation observation;
                observation.lab = result.samples.faces[f].lab;
                observation.cell_purity = result.samples.faces[f].purity;
                const auto fed = cube::vision::fusion::add_observation(
                    fusion, fusion_generation, result.gauge_epoch, result.track_faces[f].face, observation);
                metrics.fusion_generation_splits += fed.generation_split ? 1 : 0;
            }
        }

        // --- P2.7: tracked-pose scoring --------------------------------
        // Locked means the CONTINUOUS tracker owns TrackState this frame
        // (pose_source "m3t") - the legacy tracker's locked is a
        // different animal and never enters these metrics.
        const bool m3t_locked =
            result.track.phase == pipeline::TrackPhase::locked && result.track.pose_source == "m3t";
        double track_frame_err = -1.0;
        if (m3t_locked) {
            if (const auto keyframe = truth.keyframes.find(row.pass);
                keyframe != truth.keyframes.end() && !keyframe->second.interpolated) {
                track_frame_err =
                    score_tracked_frame(keyframe->second, result.track.projected_vertices, track_pinned_perm, metrics);
                score_tracked_faces(keyframe->second, result.track_faces, metrics);  // P3.1
            }
        } else {
            track_pinned_perm = -1;  // gauge re-pins at the next anchor
        }
        // Correct = within bound where checkable; an unlabelled locked
        // frame counts as correct (the plan checks correctness on
        // labelled frames), a labelled one beyond bound counts AGAINST.
        const bool lock_correct = m3t_locked && (track_frame_err < 0.0 || track_frame_err <= kLockedCorrectBound);
        if (truth.segments) {
            const gt::TagSet tags = truth.segments->tags_at(row.pass);
            if ((tags & gt::kInFrame) != 0) {
                ++metrics.gt_inframe_any_frames;
                metrics.gt_lock_correct_frames += lock_correct ? 1 : 0;
                metrics.gt_lock_incorrect_frames += (m3t_locked && !lock_correct) ? 1 : 0;
                // Rectified-face stability (P3 gate criterion 2): shift
                // between consecutive steady frames' rectified images of
                // the same most-frontal face, via phase correlation.
                if ((tags & gt::kSteady) != 0 && m3t_locked && !result.track_faces.empty() &&
                    !result.faces.faces.empty() && !result.faces.faces.front().rectified.empty()) {
                    cv::Mat grey;
                    cv::cvtColor(result.faces.faces.front().rectified, grey, cv::COLOR_BGR2GRAY);
                    cv::Mat grey_float;
                    grey.convertTo(grey_float, CV_32F);
                    const int face_id = result.track_faces.front().face;
                    if (!rect_prev.empty() && rect_prev_face == face_id && row.pass == rect_prev_pass + 1) {
                        const cv::Point2d shift = cv::phaseCorrelate(rect_prev, grey_float);
                        metrics.gt_rect_shift_px.push_back(cv::norm(shift));
                    }
                    rect_prev = grey_float;
                    rect_prev_face = face_id;
                    rect_prev_pass = row.pass;
                }

                // Jitter evidence: self-relative, so every locked frame
                // in a steady segment contributes, labelled or not.
                if ((tags & gt::kSteady) != 0 && m3t_locked) {
                    for (const auto& segment : truth.segments->segments) {
                        if (segment.start <= row.pass && row.pass <= segment.end &&
                            (segment.tags & gt::kSteady) != 0) {
                            auto& accum = metrics.gt_track_steady[segment.start];
                            float min_x = result.track.projected_vertices[0].x;
                            float max_x = min_x;
                            float min_y = result.track.projected_vertices[0].y;
                            float max_y = min_y;
                            for (std::size_t i = 0; i < 8; ++i) {
                                const auto& p = result.track.projected_vertices[i];
                                accum.corners[i].push_back(p);
                                min_x = std::min(min_x, p.x);
                                max_x = std::max(max_x, p.x);
                                min_y = std::min(min_y, p.y);
                                max_y = std::max(max_y, p.y);
                            }
                            accum.extents.push_back(static_cast<double>(std::max(max_x - min_x, max_y - min_y)));
                            break;
                        }
                    }
                }
            }
            // Re-acquisition latency. A new event while one is pending
            // means the pending one never re-locked before the cube left
            // again - failed; so does going absent with one pending.
            while (next_reentry < reentry_events.size() && row.pass >= reentry_events[next_reentry]) {
                if (reentry_pending_t >= 0) {
                    metrics.gt_reacq_ms.push_back(-1.0);
                }
                reentry_pending_t = row.t_ms;
                ++next_reentry;
            }
            if (reentry_pending_t >= 0) {
                if ((tags & gt::kAbsent) != 0) {
                    metrics.gt_reacq_ms.push_back(-1.0);
                    reentry_pending_t = -1;
                } else if (lock_correct) {
                    metrics.gt_reacq_ms.push_back(static_cast<double>(row.t_ms - reentry_pending_t));
                    reentry_pending_t = -1;
                }
            }
        }

        metrics.frames_detected += result.detection.found ? 1 : 0;
        metrics.frames_pose_valid += result.track.pose_valid ? 1 : 0;
        metrics.frames_locked += result.track.phase == pipeline::TrackPhase::locked ? 1 : 0;
        metrics.max_epoch = std::max(metrics.max_epoch, result.track.epoch);
        for (const auto& view : result.faces.faces) {
            metrics.capture_grade_faces += view.capture_grade ? 1 : 0;
        }
        for (std::size_t f = 0; f < result.reads.faces.size(); ++f) {
            if (!result.faces.faces[f].capture_grade) {
                continue;
            }
            for (const auto& facelet : result.reads.faces[f].facelets) {
                ++metrics.cells_classified;
                metrics.cells_unknown += facelet.color == FacetColor::unknown ? 1 : 0;
            }
        }
        for (const auto& stage : result.trace.stages) {
            const std::string key = stage.stage;
            metrics.stage_ms_total[key] += stage.duration_ms;
            metrics.stage_ms_max[key] = std::max(metrics.stage_ms_max[key], stage.duration_ms);
            if (key == "track") {
                for (const auto& [field, value] : stage.fields) {
                    if (field == "state" && std::holds_alternative<std::string>(value)) {
                        ++metrics.state_counts[std::get<std::string>(value)];
                    } else if (field == "gate" && std::holds_alternative<std::string>(value)) {
                        ++metrics.gate_counts[std::get<std::string>(value)];
                    } else if (field == "gauge_slip" && std::holds_alternative<bool>(value) &&
                               std::get<bool>(value)) {
                        ++metrics.gauge_slips_detected;  // P3.3 online detector
                    }
                }
            }
        }

        if (write_video) {
            const cv::Mat overlay = render_overlay(frame, result);
            if (!video.isOpened()) {
                // Explicit backend: the built-in MJPEG/AVI writer needs no
                // external codec, and naming it skips the CV_IMAGES probe
                // (which raises a noisy exception on this filename).
                video.open((out_dir / "overlay.avi").string(), cv::CAP_OPENCV_MJPEG,
                           cv::VideoWriter::fourcc('M', 'J', 'P', 'G'), fps, overlay.size());
                if (!video.isOpened()) {
                    std::fprintf(stderr, "warning: could not open overlay.avi for writing - continuing without video\n");
                    write_video = false;
                }
            }
            if (video.isOpened() && overlay.size() == cv::Size(frame.cols, frame.rows)) {
                video.write(overlay);
            }
        }
    }
    metrics.frames_replayed = replayed;
    if (reentry_pending_t >= 0) {
        metrics.gt_reacq_ms.push_back(-1.0);  // clip ended with the event still unresolved
    }

    // --- P6.3: content-based cross-epoch registration ------------------
    // The whole pass - candidate scoring in the hue-aware decomposed cell
    // cost, the P6.11 radius/margin bars, drift-tolerant verify,
    // never-overwrite with incumbent-yield, and cell re-indexing on
    // transplant - is the shared implementation in track_fusion.cpp
    // (P6.12), where its measurement rationale moved with it. The app's
    // live completion path runs the identical pass.
    {
        const auto registration = cube::vision::fusion::register_epochs(fusion);
        metrics.register_merges += registration.merges;
        metrics.register_refusals += registration.refusals;
    }

    // --- P6.1 finalize: best sub-epoch -> resolve -> assemble -> oracle ---
    // "Best" = most DISTINCT converged faces (P6.15), ties to most
    // converged aggregators (the photometric gate), then to most
    // sufficiently-observed - one sub-epoch must carry all six faces, the
    // plan's identity-lifetime bar (shared selection, track_fusion.hpp).
    {
        const auto best = cube::vision::fusion::best_sub_epoch(fusion);
        metrics.fusion_best_epoch = best.sub_epoch;
        metrics.fusion_epoch_faces = best.observed;
        metrics.fusion_epoch_converged = best.converged;
        metrics.fusion_epoch_distinct = best.distinct;
    }
    // P6.15: the chain runs to completion for EVERY clip; the oracle only
    // decides how the finished result is reported. It used to short-circuit
    // to "no-oracle-state" whenever the manifest carried no true state,
    // which meant resolve_colors and the assembler were never called on an
    // unlabelled clip - and both live recordings are unlabelled, so a
    // genuine refusal deep in the chain (P6.15's duplicated face) read as
    // "nothing to check" for as long as it existed. What CANNOT be checked
    // is the comparison, not the pipeline.
    const std::optional<cube::solver::Cube> oracle =
        (manifest.has_value() && manifest->state.has_value()) ? manifest_cube(*manifest->state) : std::nullopt;
    if (metrics.fusion_best_epoch < 0) {
        metrics.e2e_outcome = "no-tracked-reads";
    } else if (metrics.fusion_epoch_converged < 6) {
        metrics.e2e_outcome =
            fusion[metrics.fusion_best_epoch].size() < 6 ? "insufficient-faces" : "unconverged-faces";
    } else if (metrics.fusion_epoch_distinct < 6) {
        // P6.15: six converged slots, fewer than six physical faces. Named
        // apart from resolve-failed because it is a different fact about
        // the scan - resolve_colors refuses the same store, but as "these
        // captures are not a cube", which reads like a colour problem. The
        // scan is a face short and cannot finish until the missing one is
        // shown, and that is what a user (and the app's progress ring)
        // must be told.
        metrics.e2e_outcome = "duplicate-faces";
    } else {
        std::array<cube::vision::CapturedFace, 6> captured{};
        std::array<int, 6> body_face_ids{};
        std::size_t slot = 0;
        for (auto& [face_id, aggregator] : fusion[metrics.fusion_best_epoch]) {
            if (aggregator.converged() && slot < 6) {
                body_face_ids[slot] = face_id;
                captured[slot++] = aggregator.capture();
            }
        }
        const auto resolved = cube::vision::resolve_colors(captured);
        if (!resolved.has_value()) {
            metrics.e2e_outcome = "resolve-failed";
        } else {
            // P6.4: a tracked scan's captures carry their body-frame
            // orientation, so the assembler gets the quarter-turn hint -
            // tie-break advice only, the search and its bars are unchanged.
            const cube::solver::RotationHint hint =
                cube::solver::rotation_hint_for_tracked_scan(*resolved, body_face_ids);
            const auto assembled = cube::solver::assemble_scanned_faces_with_mapping(*resolved, &hint);
            if (!assembled.has_value()) {
                metrics.e2e_outcome = "assembly-failed";
            } else {
                metrics.assembled_state = manifest_state_string(assembled->cube);
                if (!oracle.has_value()) {
                    metrics.e2e_outcome = "assembled-no-oracle";
                } else if (assembled->cube == *oracle) {
                    metrics.exact_state_match = true;
                    metrics.e2e_outcome = "exact-match";
                } else {
                    metrics.e2e_outcome = "state-mismatch";
                }
            }
        }
    }

    write_metrics_json(out_dir / "metrics.json", metrics);

    std::printf("  end-to-end: %s  (sub-epoch %d: %d/6 observed, %d/6 converged, %d/6 distinct; "
                "%d registered merges, %d refusals, %d generation splits)\n",
                metrics.e2e_outcome.c_str(), metrics.fusion_best_epoch, metrics.fusion_epoch_faces,
                metrics.fusion_epoch_converged, metrics.fusion_epoch_distinct, metrics.register_merges,
                metrics.register_refusals, metrics.fusion_generation_splits);
    if (!metrics.assembled_state.empty() && !oracle.has_value()) {
        std::printf("  assembled state (no oracle to check it against): %s\n", metrics.assembled_state.c_str());
    }
    std::printf("%-12s frames %4d/%4d  det %5.1f%%  pose %5.1f%%  locked %5.1f%%  epochs %2d  unknown-cells %5.1f%%%s\n",
                metrics.clip.c_str(), metrics.frames_replayed, metrics.frames_indexed,
                100.0 * metrics.frames_detected / std::max(1, metrics.frames_replayed),
                100.0 * metrics.frames_pose_valid / std::max(1, metrics.frames_replayed),
                100.0 * metrics.frames_locked / std::max(1, metrics.frames_replayed), metrics.max_epoch,
                100.0 * static_cast<double>(metrics.cells_unknown) /
                    std::max<std::int64_t>(1, metrics.cells_classified),
                metrics.trace_violations > 0 ? "  TRACE-VIOLATIONS" : "");
    if (metrics.has_ground_truth) {
        if (!metrics.gt_corner_err_extent.empty()) {
            const auto [corner_median, corner_p95] = median_p95(metrics.gt_corner_err_extent);
            std::printf("  corners vs labels (best-rotation): median %.3f p95 %.3f (of extent)  matched %d/%d  "
                        "gauge flips %d/%d keyframes\n",
                        corner_median, corner_p95, metrics.gt_corners_matched, metrics.gt_corners_labelled,
                        metrics.gt_corner_perm_changes, metrics.gt_corner_keyframes);
        }
        if (metrics.gt_track_scored_frames > 0 || !metrics.gt_reacq_ms.empty()) {
            const auto [track_median, track_p95] = median_p95(metrics.gt_track_corner_err_extent);
            const double bias_max = track_bias(metrics).second;
            int reacq_failed = 0;
            for (const double latency : metrics.gt_reacq_ms) {
                reacq_failed += latency < 0.0 ? 1 : 0;
            }
            std::printf("  tracked pose: err median %.4f p95 %.4f (of extent, %d frames, %d gauge runs)  "
                        "locked-correct %d/%d (%.1f%%)  locked-incorrect %d\n",
                        track_median, track_p95, metrics.gt_track_scored_frames, metrics.gt_track_gauge_runs,
                        metrics.gt_lock_correct_frames, metrics.gt_inframe_any_frames,
                        100.0 * metrics.gt_lock_correct_frames / std::max(1, metrics.gt_inframe_any_frames),
                        metrics.gt_lock_incorrect_frames);
            std::printf("  tracked pose: bias max %.4f  jitter median %.4f (%zu steady segments)  "
                        "reacq median %.0f ms over %zu events (%d failed)\n",
                        bias_max, clip_jitter(metrics.gt_track_steady), metrics.gt_track_steady.size(),
                        reacq_median_ms(metrics.gt_reacq_ms), metrics.gt_reacq_ms.size(), reacq_failed);
            std::printf("  tracked pose: registration held %d/%d frames, %d slips (quarter-turn re-pins)  "
                        "online gauge detector: %d slips declared\n",
                        metrics.gt_track_reg_held, metrics.gt_track_reg_frames, metrics.gt_track_repins,
                        metrics.gauge_slips_detected);
            if (metrics.gt_faces_labelled > 0) {
                const auto [face_quad_median, face_quad_p95] = median_p95(metrics.gt_face_quad_err_extent);
                const auto [face_centre_median, face_centre_p95] = median_p95(metrics.gt_face_centre_err_pitch);
                std::printf("  derived faces: matched %d/%d  quad err median %.4f p95 %.4f (of extent)  "
                            "centre err median %.3f p95 %.3f (of pitch)  misassigned %d\n",
                            metrics.gt_faces_matched, metrics.gt_faces_labelled, face_quad_median, face_quad_p95,
                            face_centre_median, face_centre_p95, metrics.gt_face_centre_misassigned);
            }
            if (!metrics.gt_rect_shift_px.empty()) {
                const auto [rect_median, rect_p95] = median_p95(metrics.gt_rect_shift_px);
                std::printf("  rectified stability: shift median %.3f px p95 %.3f px over %zu steady pairs\n",
                            rect_median, rect_p95, metrics.gt_rect_shift_px.size());
            }
        }
        const auto [centre_median, centre_p95] = median_p95(metrics.gt_centre_err_pitch);
        std::printf("  ground truth: det(in_frame) %s  det(occluded) %s  fp-sustained %d  "
                    "centre-err median %.3f p95 %.3f (of pitch, %zu samples)  misassigned %d  keyframes %d\n",
                    metrics.gt_inframe_frames > 0
                        ? (std::to_string(100 * metrics.gt_inframe_detected / metrics.gt_inframe_frames) + "%").c_str()
                        : "n/a",
                    metrics.gt_occluded_frames > 0
                        ? (std::to_string(100 * metrics.gt_occluded_detected / metrics.gt_occluded_frames) + "%").c_str()
                        : "n/a",
                    metrics.gt_fp_sustained_runs, centre_median, centre_p95, metrics.gt_centre_err_pitch.size(),
                    metrics.gt_whole_cell_misassignments, metrics.gt_keyframes_scored);
    }
    return metrics.trace_violations == 0 ? 0 : 3;
}

// A small synthetic recording (the detectors' own synthetic-face recipe,
// gently translated frame to frame) so the runner and its consumers can be
// exercised end to end while the real corpus is re-recorded.
int write_synth_recording(const fs::path& dir, int frames) {
    static const std::array<cv::Scalar, 9> layout{{
        {255, 255, 255},
        {0, 213, 255},
        {0, 0, 196},
        {0, 106, 255},
        {72, 155, 0},
        {173, 70, 0},
        {255, 255, 255},
        {0, 213, 255},
        {0, 0, 196},
    }};
    fs::create_directories(dir);
    std::ofstream index(dir / std::string(recording::kIndexCsvName), std::ios::trunc);
    index << recording::kIndexCsvHeader << "\n";
    constexpr int kW = 640;
    constexpr int kH = 480;
    constexpr int kCell = 60;
    constexpr int kGap = 10;
    for (int i = 0; i < frames; ++i) {
        cv::Mat frame(kH, kW, CV_8UC3, cv::Scalar(30, 30, 30));
        // Slow sub-pixel drift: enough motion to exercise the temporal
        // paths, small enough to stay "steady".
        const int ox = 215 + static_cast<int>(2.0 * std::sin(i * 0.15));
        const int oy = 140 + static_cast<int>(2.0 * std::cos(i * 0.11));
        int cell_index = 0;
        for (int row = 0; row < 3; ++row) {
            for (int col = 0; col < 3; ++col, ++cell_index) {
                const int x = ox + col * (kCell + kGap);
                const int y = oy + row * (kCell + kGap);
                cv::rectangle(frame, cv::Rect(x, y, kCell, kCell),
                              layout[static_cast<std::size_t>(cell_index)], cv::FILLED);
            }
        }
        cv::imwrite((dir / recording::frame_filename(i)).string(), frame,
                    {cv::IMWRITE_JPEG_QUALITY, 90});
        index << i << "," << 33 * i << "," << kW << "," << kH << "," << kW << "," << kH << "\n";
    }
    std::printf("synth recording: %d frames at %s\n", frames, dir.string().c_str());
    return 0;
}

// Recording integrity audit + manifest summary (recorder v2's exit-gate
// check: zero dropped frames, contiguous index, files present, state
// valid).
int check_clip(const fs::path& dir) {
    namespace v2 = cube::vision::recording;
    if (const auto manifest = v2::read_manifest(dir.string())) {
        std::printf("manifest: state=%s  lighting=\"%s\"  motion=\"%s\"  camera=\"%s\"  cube=\"%s\"\n",
                    manifest->state ? manifest->state->c_str() : "(none)", manifest->lighting.c_str(),
                    manifest->motion_script.c_str(), manifest->camera.c_str(), manifest->cube.c_str());
    } else {
        std::printf("manifest: none (pre-v2 recording)\n");
    }
    if (const auto problem = v2::check_recording(dir.string())) {
        std::printf("check: FAIL - %s\n", problem->c_str());
        return 1;
    }
    std::printf("check: OK\n");
    return 0;
}

// Throughput bench for the async recorder (docs/pipeline-plan.md P0 item
// 2: measure BEFORE the user recording session). Random-noise frames are
// the JPEG encoder's worst case, so a clean bench here is a safe margin.
int bench_recorder(const fs::path& dir, int frames, int width, int height, int fps) {
    namespace v2 = cube::vision::recording;
    fs::remove_all(dir);
    v2::Manifest manifest;
    manifest.camera = "bench";
    manifest.motion_script = "synthetic noise bench";
    v2::AsyncRecorder recorder;
    if (const auto problem = recorder.start(dir.string(), manifest, true)) {
        std::fprintf(stderr, "error: %s\n", problem->c_str());
        return 2;
    }
    std::vector<cv::Mat> pool(8);
    for (auto& m : pool) {
        m.create(height, width, CV_8UC3);
        cv::randu(m, cv::Scalar(0, 0, 0), cv::Scalar(255, 255, 255));
    }
    const auto period = std::chrono::microseconds(1000000 / std::max(1, fps));
    const auto start = std::chrono::steady_clock::now();
    auto next = start;
    for (int i = 0; i < frames; ++i) {
        recorder.push(pool[static_cast<std::size_t>(i) % pool.size()],
                      std::chrono::duration_cast<std::chrono::milliseconds>(next - start).count());
        next += period;
        std::this_thread::sleep_until(next);
    }
    const auto stats = recorder.stop();
    const double wall_s =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    std::uintmax_t bytes = 0;
    for (const auto& entry : fs::recursive_directory_iterator(dir)) {
        if (entry.is_regular_file()) {
            bytes += entry.file_size();
        }
    }
    std::printf("bench: %dx%d @ %d fps, %d frames in %.1f s\n", width, height, fps, frames, wall_s);
    std::printf("  pushed %d  written %d  dropped %d  queue peak %d/%d\n", stats.pushed, stats.written,
                stats.dropped, stats.queue_peak, 256);
    std::printf("  disk %.1f MB (%.1f MB/s)\n", static_cast<double>(bytes) / 1e6,
                static_cast<double>(bytes) / 1e6 / wall_s);
    std::printf("  verdict: %s\n", stats.dropped == 0 ? "OK - no drops" : "FAIL - recorder cannot keep up");
    return stats.dropped == 0 ? 0 : 1;
}

// One-time per-camera chessboard calibration (docs/pipeline-plan.md
// Phase 2 item 1): point it at a directory of full-board views captured
// with the target camera; writes the intrinsics file the recorder
// manifest references.
int calibrate_camera(const fs::path& images_dir, cv::Size pattern, float square, const fs::path& out_path) {
    std::vector<cv::Mat> views;
    for (const auto& entry : fs::directory_iterator(images_dir)) {
        const auto ext = entry.path().extension().string();
        if (ext == ".jpg" || ext == ".jpeg" || ext == ".png") {
            views.push_back(cv::imread(entry.path().string(), cv::IMREAD_COLOR));
        }
    }
    int used = 0;
    const auto result = cube::vision::calibrate_from_views(views, pattern, square, &used);
    if (!result.has_value()) {
        std::fprintf(stderr, "error: calibration failed (%d/%zu views usable, need >=4 full-board views)\n", used,
                     views.size());
        return 1;
    }
    std::printf("calibrated from %d/%zu views at %dx%d\n", used, views.size(), result->calibrated_size.width,
                result->calibrated_size.height);
    std::printf("  fx %.2f  fy %.2f  cx %.2f  cy %.2f  rms %.3f px\n", result->fx, result->fy, result->cx,
                result->cy, result->rms_reprojection);
    const cv::Matx33d approx = cube::vision::approximate_camera_matrix(result->calibrated_size);
    std::printf("  (the f=width approximation would be fx %.0f - measured error of the fallback: %+.1f%%)\n",
                approx(0, 0), 100.0 * (result->fx - approx(0, 0)) / approx(0, 0));
    if (const auto problem = cube::vision::write_intrinsics(out_path.string(), *result)) {
        std::fprintf(stderr, "error: %s\n", problem->c_str());
        return 1;
    }
    std::printf("wrote %s\n", out_path.string().c_str());
    return 0;
}

int usage() {
    std::fprintf(stderr,
                 "usage:\n"
                 "  cube_pipeline_runner replay <recording-dir> [--detector <model.onnx>] [--tracker m3t] "
                 "[--out <dir>] [--no-video] [--no-refine] [--max-frames N]\n"
                 "  cube_pipeline_runner corpus <root-dir> [--detector <model.onnx>] [--tracker m3t] "
                 "[--no-refine] [--out <dir>]\n"
                 "  cube_pipeline_runner calibrate <images-dir> [--pattern WxH] [--square MM] [--out <file>]\n"
                 "  cube_pipeline_runner check <recording-dir>\n"
                 "  cube_pipeline_runner bench-recorder <out-dir> [--frames N] [--size WxH] [--fps N]\n"
                 "  cube_pipeline_runner synth <out-dir> [--frames N]\n");
    return 2;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        return usage();
    }
    const std::string mode = argv[1];
    const fs::path target = argv[2];

    fs::path out_dir;
    std::string detector_path;
    std::string tracker_name;
    bool write_video = true;
    bool refine_enabled = true;
    int max_frames = 0;
    int synth_frames = 120;
    int bench_w = 1280;
    int bench_h = 720;
    int bench_fps = 30;
    int pattern_w = 9;
    int pattern_h = 6;
    float square_size = 24.0F;
    for (int i = 3; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--out" && i + 1 < argc) {
            out_dir = argv[++i];
        } else if (arg == "--detector" && i + 1 < argc) {
            detector_path = argv[++i];
        } else if (arg == "--tracker" && i + 1 < argc) {
            tracker_name = argv[++i];
        } else if (arg == "--no-video") {
            write_video = false;
        } else if (arg == "--no-refine") {
            refine_enabled = false;
        } else if (arg == "--max-frames" && i + 1 < argc) {
            max_frames = std::atoi(argv[++i]);
        } else if (arg == "--frames" && i + 1 < argc) {
            synth_frames = std::atoi(argv[++i]);
        } else if (arg == "--fps" && i + 1 < argc) {
            bench_fps = std::atoi(argv[++i]);
        } else if (arg == "--square" && i + 1 < argc) {
            square_size = static_cast<float>(std::atof(argv[++i]));
        } else if (arg == "--pattern" && i + 1 < argc) {
            const std::string pattern = argv[++i];
            const auto x = pattern.find('x');
            if (x == std::string::npos) {
                return usage();
            }
            pattern_w = std::atoi(pattern.c_str());
            pattern_h = std::atoi(pattern.c_str() + x + 1);
        } else if (arg == "--size" && i + 1 < argc) {
            const std::string size = argv[++i];
            const auto x = size.find('x');
            if (x == std::string::npos) {
                return usage();
            }
            bench_w = std::atoi(size.c_str());
            bench_h = std::atoi(size.c_str() + x + 1);
        } else {
            return usage();
        }
    }

    if (mode == "synth") {
        return write_synth_recording(target, synth_frames);
    }
    if (mode == "check") {
        return check_clip(target);
    }
    if (mode == "calibrate") {
        return calibrate_camera(target, cv::Size(pattern_w, pattern_h), square_size,
                                out_dir.empty() ? target / "intrinsics.txt" : out_dir);
    }
    if (mode == "bench-recorder") {
        return bench_recorder(target, synth_frames, bench_w, bench_h, bench_fps);
    }
    // Phase 1 learned detector: attached when --detector points at the
    // ONNX model (assets/models/cube_corner_pose.onnx, deployed beside
    // the binaries under models/). Absent flag = legacy-only replay.
    std::unique_ptr<cube::vision::OnnxCornerDetector> detector;
    if (!detector_path.empty()) {
        auto loaded = cube::vision::OnnxCornerDetector::create(detector_path);
        if (!loaded.has_value()) {
            std::fprintf(stderr, "error: could not load corner detector model at %s\n", detector_path.c_str());
            return 2;
        }
        detector = std::move(*loaded);
    }
    // --tracker m3t: continuous 6-DoF region tracking (Phase 2). Requires
    // the detector (init and health both consume its corners).
    std::unique_ptr<cube::track_m3t::M3tPoseTracker> pose_tracker;
    if (tracker_name == "m3t") {
        if (detector == nullptr) {
            std::fprintf(stderr, "error: --tracker m3t requires --detector\n");
            return 2;
        }
        pose_tracker = std::make_unique<cube::track_m3t::M3tPoseTracker>();
    } else if (!tracker_name.empty()) {
        std::fprintf(stderr, "error: unknown tracker '%s' (supported: m3t)\n", tracker_name.c_str());
        return 2;
    }

    if (mode == "replay") {
        ClipMetrics metrics;
        return replay_clip(target, out_dir.empty() ? target / "pipeline-out" : out_dir, write_video, max_frames,
                           detector.get(), pose_tracker.get(), refine_enabled, metrics);
    }
    if (mode == "corpus") {
        int worst = 0;
        int clips = 0;
        std::ofstream table;
        const fs::path table_dir = out_dir.empty() ? target : out_dir;
        fs::create_directories(table_dir);
        table.open(table_dir / "corpus_table.csv", std::ios::trunc);
        table << "clip,frames_indexed,frames_replayed,detected,pose_valid,locked,epochs,capture_grade_faces,"
                 "cells_classified,cells_unknown,trace_violations,"
                 "locked_correct,inframe_any,track_err_median,track_bias_max,track_jitter_median,"
                 "reacq_median_ms,reacq_events,reacq_failed,track_repins\n";
        for (const auto& entry : fs::directory_iterator(target)) {
            if (!entry.is_directory() || !fs::exists(entry.path() / std::string(recording::kIndexCsvName))) {
                continue;
            }
            ++clips;
            ClipMetrics metrics;
            const fs::path clip_out =
                (out_dir.empty() ? entry.path() : out_dir / entry.path().filename()) / "pipeline-out";
            worst = std::max(worst, replay_clip(entry.path(), clip_out, write_video, max_frames, detector.get(),
                                                pose_tracker.get(), refine_enabled, metrics));
            int table_reacq_failed = 0;
            for (const double latency : metrics.gt_reacq_ms) {
                table_reacq_failed += latency < 0.0 ? 1 : 0;
            }
            table << metrics.clip << "," << metrics.frames_indexed << "," << metrics.frames_replayed << ","
                  << metrics.frames_detected << "," << metrics.frames_pose_valid << "," << metrics.frames_locked << ","
                  << metrics.max_epoch << "," << metrics.capture_grade_faces << "," << metrics.cells_classified << ","
                  << metrics.cells_unknown << "," << metrics.trace_violations << ","
                  << metrics.gt_lock_correct_frames << "," << metrics.gt_inframe_any_frames << ","
                  << median_p95(metrics.gt_track_corner_err_extent).first << "," << track_bias(metrics).second << ","
                  << clip_jitter(metrics.gt_track_steady) << "," << reacq_median_ms(metrics.gt_reacq_ms)
                  << "," << metrics.gt_reacq_ms.size() << "," << table_reacq_failed << ","
                  << metrics.gt_track_repins << "\n";
        }
        if (clips == 0) {
            std::fprintf(stderr, "error: no recording directories under %s\n", target.string().c_str());
            return 2;
        }
        return worst;
    }
    return usage();
}
