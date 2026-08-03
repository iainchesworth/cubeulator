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
#include <cube/vision/camera_intrinsics.hpp>
#include <cube/vision/corner_detector.hpp>
#include <cube/vision/frame_recording.hpp>
#include <cube/vision/groundtruth.hpp>
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
};

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

// Score one hand-clicked keyframe against this frame's detected faces:
// match each labelled face to the nearest detected face view by quad
// centre (within half the labelled quad diagonal), then accumulate cell
// centre errors as fractions of the labelled sticker pitch
// (docs/pipeline-metrics.md "cell-centre error"). Content-agnostic - no
// face identity needed, so it works under the legacy adapter too.
void score_keyframe(const cube::vision::groundtruth::Keyframe& keyframe,
                    const cube::vision::pipeline::LegacyPipelineResult& result, ClipMetrics& metrics) {
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
        for (std::size_t c = 0; c < 9; ++c) {
            const auto& cell = result.cells.faces[static_cast<std::size_t>(best)].cells[c];
            cv::Point2f cell_centre(0.0F, 0.0F);
            for (const auto& corner : cell.corners) {
                cell_centre += corner;
            }
            cell_centre *= 0.25F;
            const double err = cv::norm(cell_centre - labelled.centres[c]) / pitch;
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

int replay_clip(const fs::path& dir, const fs::path& out_dir, bool write_video, int max_frames,
                cube::vision::OnnxCornerDetector* detector, ClipMetrics& metrics) {
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
            score_keyframe(keyframe->second, result, metrics);
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
    write_metrics_json(out_dir / "metrics.json", metrics);

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
                 "  cube_pipeline_runner replay <recording-dir> [--detector <model.onnx>] [--out <dir>] "
                 "[--no-video] [--max-frames N]\n"
                 "  cube_pipeline_runner corpus <root-dir> [--detector <model.onnx>] [--out <dir>]\n"
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
    bool write_video = true;
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
        } else if (arg == "--no-video") {
            write_video = false;
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

    if (mode == "replay") {
        ClipMetrics metrics;
        return replay_clip(target, out_dir.empty() ? target / "pipeline-out" : out_dir, write_video, max_frames,
                           detector.get(), metrics);
    }
    if (mode == "corpus") {
        int worst = 0;
        int clips = 0;
        std::ofstream table;
        const fs::path table_dir = out_dir.empty() ? target : out_dir;
        fs::create_directories(table_dir);
        table.open(table_dir / "corpus_table.csv", std::ios::trunc);
        table << "clip,frames_indexed,frames_replayed,detected,pose_valid,locked,epochs,capture_grade_faces,"
                 "cells_classified,cells_unknown,trace_violations\n";
        for (const auto& entry : fs::directory_iterator(target)) {
            if (!entry.is_directory() || !fs::exists(entry.path() / std::string(recording::kIndexCsvName))) {
                continue;
            }
            ++clips;
            ClipMetrics metrics;
            const fs::path clip_out =
                (out_dir.empty() ? entry.path() : out_dir / entry.path().filename()) / "pipeline-out";
            worst = std::max(worst,
                             replay_clip(entry.path(), clip_out, write_video, max_frames, detector.get(), metrics));
            table << metrics.clip << "," << metrics.frames_indexed << "," << metrics.frames_replayed << ","
                  << metrics.frames_detected << "," << metrics.frames_pose_valid << "," << metrics.frames_locked << ","
                  << metrics.max_epoch << "," << metrics.capture_grade_faces << "," << metrics.cells_classified << ","
                  << metrics.cells_unknown << "," << metrics.trace_violations << "\n";
        }
        if (clips == 0) {
            std::fprintf(stderr, "error: no recording directories under %s\n", target.string().c_str());
            return 2;
        }
        return worst;
    }
    return usage();
}
