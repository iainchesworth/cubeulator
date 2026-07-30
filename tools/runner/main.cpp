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
#include <cube/vision/frame_recording.hpp>
#include <cube/vision/pipeline/legacy_pipeline.hpp>
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
};

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
    write_map("stage_ms_max", m.stage_ms_max, [&out](double v) { out << v; }, false);
    out << "}\n";
}

int replay_clip(const fs::path& dir, const fs::path& out_dir, bool write_video, int max_frames, ClipMetrics& metrics) {
    std::ifstream index_stream(dir / std::string(recording::kIndexCsvName));
    if (!index_stream) {
        std::fprintf(stderr, "error: no %s under %s - not a recording directory\n",
                     std::string(recording::kIndexCsvName).c_str(), dir.string().c_str());
        return 2;
    }
    const auto rows = recording::read_index(index_stream);
    metrics.clip = dir.filename().string();
    metrics.frames_indexed = static_cast<int>(rows.size());

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

int usage() {
    std::fprintf(stderr,
                 "usage:\n"
                 "  cube_pipeline_runner replay <recording-dir> [--out <dir>] [--no-video] [--max-frames N]\n"
                 "  cube_pipeline_runner corpus <root-dir> [--out <dir>]\n"
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
    bool write_video = true;
    int max_frames = 0;
    int synth_frames = 120;
    int bench_w = 1280;
    int bench_h = 720;
    int bench_fps = 30;
    for (int i = 3; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--out" && i + 1 < argc) {
            out_dir = argv[++i];
        } else if (arg == "--no-video") {
            write_video = false;
        } else if (arg == "--max-frames" && i + 1 < argc) {
            max_frames = std::atoi(argv[++i]);
        } else if (arg == "--frames" && i + 1 < argc) {
            synth_frames = std::atoi(argv[++i]);
        } else if (arg == "--fps" && i + 1 < argc) {
            bench_fps = std::atoi(argv[++i]);
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
    if (mode == "bench-recorder") {
        return bench_recorder(target, synth_frames, bench_w, bench_h, bench_fps);
    }
    if (mode == "replay") {
        ClipMetrics metrics;
        return replay_clip(target, out_dir.empty() ? target / "pipeline-out" : out_dir, write_video, max_frames,
                           metrics);
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
            worst = std::max(worst, replay_clip(entry.path(), clip_out, write_video, max_frames, metrics));
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
