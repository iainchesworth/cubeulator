// Replay harness for the stateless simple face pipeline - hidden tag, never
// part of the normal suite. Point CUBE_SIMPLE_DIR at a recording directory
// (one written by ScanSession's CUBE_VISION_RECORD_DIR hook) and run:
//
//   cube_unit_tests.exe "[.simple-pipeline]"
//
// CUBE_SIMPLE_OUT tees the per-frame records to a file, CUBE_SIMPLE_CLIP
// overrides the clip label (defaults to the directory's leaf name), and
// CUBE_SIMPLE_QUIET=1 suppresses the per-frame lines when only the summary
// is wanted.
//
// WHAT IT MEASURES, and why those and not others.
//
// The decisive metric is VALIDITY: do the captured faces assemble into a
// physically valid, solvable cube via the production assembler? Nothing
// less counts. A 9-per-colour census proves nothing on its own - the
// production stack's resolve_colors() is a constrained assignment that
// FORCES a perfect census out of any 54 samples whatsoever, and a perfect
// census from a scan that cannot assemble has already been mistaken for
// progress twice on this corpus. So the census is printed beside the
// verdict as a description, never as a pass condition.
//
// Secondary, and the reason the pipeline is stateless: READING STABILITY.
// Replaying many frames of one physical face should yield ONE nine-sticker
// string. The production stack produced dozens (one face gave 57 distinct
// strings from 67 readings), which is what makes its per-frame readings
// unusable however they are fused. Distinct-string counts per centre colour
// are reported directly.
//
// Third, and reported FIRST because everything downstream is conditional on
// it: the fraction of frames from which a face quad is recovered at all,
// per route (LSD vs Canny) and combined. If the outline route does not
// recover quads on real footage then the design does not get to be
// evaluated on colour at all, and that is the headline.
//
// Both classifier modes are run over the same frames in the same process,
// so the lab-vs-hsv comparison is not confounded by anything else.
//
// This harness reads ONLY cube::vision::simple and the solver's assembler;
// it does not touch the production detector, tracker, router, or
// aggregators. Skips (never fails) without a recording, since the corpus is
// captured from live hardware and does not ship with the repo.
//
// std::getenv is fine here (single-threaded harness, read-only); silence
// MSVC's CRT deprecation the way the other offline harnesses do.
#ifdef _MSC_VER
#pragma warning(disable : 4996)
#endif

#include <catch2/catch_test_macros.hpp>

#include <cube/model/facelet.hpp>
#include <cube/solver/cube.hpp>
#include <cube/solver/scan_assembly.hpp>
#include <cube/vision/frame_recording.hpp>
#include <cube/vision/simple_face_pipeline.hpp>

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace {

namespace simple = cube::vision::simple;
namespace recording = cube::vision::recording;

using cube::model::FacetColor;

char color_letter(FacetColor color) {
    switch (color) {
        case FacetColor::white: return 'W';
        case FacetColor::yellow: return 'Y';
        case FacetColor::red: return 'R';
        case FacetColor::orange: return 'O';
        case FacetColor::green: return 'G';
        case FacetColor::blue: return 'B';
        case FacetColor::unknown: return '?';
    }
    return '?';
}

std::string cells_string(const simple::FaceReading& reading) {
    std::string text;
    for (const auto& cell : reading.cells) {
        text += color_letter(cell.color);
    }
    return text;
}

std::string cells_string(const std::array<FacetColor, simple::kCellCount>& cells) {
    std::string text;
    for (const auto& color : cells) {
        text += color_letter(color);
    }
    return text;
}

// Records go to stdout always, and to CUBE_SIMPLE_OUT as well when that
// names a writable file - a tee, so an interactive run and a scripted one
// see byte-identical records.
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

std::string leaf_name(const std::string& path) {
    const auto cut = path.find_last_of("/\\");
    std::string leaf = cut == std::string::npos ? path : path.substr(cut + 1);
    return leaf.empty() ? path : leaf;
}

// Per-mode accumulators. Everything reported is counted here rather than
// derived later, so a number in the summary can always be traced to the
// frame that produced it.
struct ModeStats {
    int frames = 0;
    long long lsd_candidates = 0;  // admissible 4-gons offered, not chosen
    int lsd_quads = 0;      // standalone recovery, LSD route only
    int canny_quads = 0;    // standalone recovery, Canny route only
    int hull_quads = 0;     // standalone recovery, sticker-hull route only
    int any_quads = 0;      // the combined path read_face() actually takes
    int complete = 0;       // quad found AND all nine cells committed
    int centre_unknown = 0; // quad found but the centre abstained
    int identified_with_holes = 0;  // centre named, but not all nine cells
    long long unknown_cells = 0;
    long long cells_with_quad = 0;
    // Distinct nine-sticker strings per centre colour, over COMPLETE
    // readings - the stability metric.
    std::map<char, std::set<std::string>> strings_by_centre;
    std::map<char, int> readings_by_centre;
};

// Census of the 54 stored stickers. Printed as description, never as a
// pass condition - see the file header.
std::string census_string(const std::array<std::array<cube::model::Facelet, 9>, 6>& faces) {
    std::map<char, int> counts;
    for (const auto& face : faces) {
        for (const auto& facelet : face) {
            ++counts[color_letter(facelet.color)];
        }
    }
    std::string text;
    for (const auto& [letter, count] : counts) {
        char cell[16];
        std::snprintf(cell, sizeof(cell), "%c=%d ", letter, count);
        text += cell;
    }
    return text;
}

}  // namespace

TEST_CASE("replay a recorded clip through the stateless simple pipeline", "[.simple-pipeline]") {
    const char* dir = std::getenv("CUBE_SIMPLE_DIR");
    if (dir == nullptr) {
        SKIP("CUBE_SIMPLE_DIR not set - point it at a CUBE_VISION_RECORD_DIR recording");
    }
    const std::string dir_path(dir);
    std::ifstream index_stream(dir_path + "/" + std::string(recording::kIndexCsvName));
    if (!index_stream) {
        SKIP("no " << recording::kIndexCsvName << " under " << dir_path
                   << " - not a recording directory");
    }
    const char* clip_override = std::getenv("CUBE_SIMPLE_CLIP");
    const std::string clip = clip_override != nullptr ? std::string(clip_override) : leaf_name(dir_path);
    const char* quiet_env = std::getenv("CUBE_SIMPLE_QUIET");
    const bool quiet = quiet_env != nullptr && std::string(quiet_env) != "0";
    // CUBE_SIMPLE_DUMP names an EXISTING directory to write per-pass quad
    // overlays and edge masks into (every 40th pass, LSD route only).
    const char* dump_dir = std::getenv("CUBE_SIMPLE_DUMP");

    RecordSink sink;
    sink.open(std::getenv("CUBE_SIMPLE_OUT"));

    const auto rows = recording::read_index(index_stream);
    std::printf("simple-pipeline: %s clip=%s (%zu passes)%s\n", dir_path.c_str(), clip.c_str(),
                rows.size(), sink.teed() ? " [teed to CUBE_SIMPLE_OUT]" : "");
    sink.line("kind,clip,mode,pass,t_ms,quad,lsd,canny,area_frac,side_ratio,centre,unknown,colors");

    int max_pass = 0;
    for (const auto& row : rows) {
        max_pass = std::max(max_pass, row.pass);
    }

    // Frames are decoded once per mode rather than once overall. Slower, but
    // it keeps each mode's replay a clean single pass with no shared state
    // to reason about, and the corpus clips are a few hundred frames.
    const std::array<simple::ClassifierMode, 2> modes{simple::ClassifierMode::lab_hue_bands,
                                                      simple::ClassifierMode::hsv_white_axis};

    for (const auto mode : modes) {
        simple::PipelineParams params;
        params.classifier = mode;
        const std::string mode_name(simple::to_string(mode));

        ModeStats stats;
        simple::FaceStore store;

        for (const auto& row : rows) {
            if (recording::frame_overwritten(row.pass, max_pass)) {
                continue;
            }
            const cv::Mat frame =
                cv::imread(dir_path + "/" + recording::frame_filename(row.pass), cv::IMREAD_COLOR);
            if (frame.empty() || frame.cols != row.w || frame.rows != row.h) {
                continue;
            }
            ++stats.frames;

            // The two routes are ALSO run standalone, because read_face()
            // takes LSD-then-Canny and would otherwise report only "some
            // route worked". Which route works on which footage is the
            // question the design brief asks first.
            const simple::QuadCandidates lsd_candidates =
                simple::quad_candidates_line_segments(frame, params);
            const simple::QuadCandidates canny_candidates =
                simple::quad_candidates_canny(frame, params);
            const bool lsd_ok = !lsd_candidates.quads.empty();
            const bool canny_ok = !canny_candidates.quads.empty();
            const auto hull_quad = simple::detect_quad_sticker_hull(frame, params);
            stats.lsd_quads += lsd_ok ? 1 : 0;
            stats.canny_quads += canny_ok ? 1 : 0;
            stats.hull_quads += hull_quad ? 1 : 0;
            stats.lsd_candidates += static_cast<long long>(lsd_candidates.quads.size());

            // Overlay dump for a handful of passes, so a wrong ACCEPTANCE is
            // visible rather than inferred. The first runD replay reported
            // an 87.3% quad rate that was 87.3% single stickers; nothing in
            // a rate can show that, and an image shows it immediately.
            if (dump_dir != nullptr && (row.pass % 40) == 0 && mode == modes.front()) {
                cv::Mat overlay = frame.clone();
                for (const auto& quad : lsd_candidates.quads) {
                    std::vector<cv::Point> poly;
                    for (const auto& corner : quad.corners) {
                        poly.emplace_back(cvRound(corner.x), cvRound(corner.y));
                    }
                    cv::polylines(overlay, poly, true, cv::Scalar(0, 255, 0), 1);
                }
                if (hull_quad) {
                    std::vector<cv::Point> poly;
                    for (const auto& corner : hull_quad->corners) {
                        poly.emplace_back(cvRound(corner.x), cvRound(corner.y));
                    }
                    cv::polylines(overlay, poly, true, cv::Scalar(0, 0, 255), 2);
                }
                const simple::FaceReading dump_reading = simple::read_face(frame, params);
                if (dump_reading.quad) {
                    for (const auto& cell : dump_reading.cells) {
                        cv::circle(overlay, cv::Point(cvRound(cell.centre.x), cvRound(cell.centre.y)),
                                   3, cv::Scalar(255, 0, 255), -1);
                    }
                }
                char name[512];
                std::snprintf(name, sizeof(name), "%s/%s_%06d_quads.png", dump_dir, clip.c_str(),
                              row.pass);
                cv::imwrite(name, overlay);
                std::snprintf(name, sizeof(name), "%s/%s_%06d_lsdmask.png", dump_dir, clip.c_str(),
                              row.pass);
                cv::imwrite(name, lsd_candidates.edge_mask);
            }

            const simple::FaceReading reading = simple::read_face(frame, params);
            const bool has_quad = reading.quad.has_value();
            stats.any_quads += has_quad ? 1 : 0;
            if (has_quad) {
                stats.cells_with_quad += static_cast<long long>(simple::kCellCount);
                stats.unknown_cells += reading.unknown_count;
                if (reading.centre_color() == FacetColor::unknown) {
                    ++stats.centre_unknown;
                } else if (reading.unknown_count > 0) {
                    // Identified but holed. Counted because complete() is a
                    // deliberately strict rule with nothing downstream yet
                    // able to use holes, and this is exactly the headroom
                    // that a census-constrained resolver treating unknowns
                    // as free variables would be reaching for.
                    ++stats.identified_with_holes;
                }
            }
            if (reading.complete()) {
                ++stats.complete;
                const char centre = color_letter(reading.centre_color());
                stats.strings_by_centre[centre].insert(cells_string(reading));
                ++stats.readings_by_centre[centre];
            }
            store.offer(reading);

            if (!quiet) {
                char line[512];
                std::snprintf(line, sizeof(line), "frame,%s,%s,%d,%lld,%s,%d,%d,%.4f,%.3f,%c,%d,%s",
                              clip.c_str(), mode_name.c_str(), row.pass,
                              static_cast<long long>(row.t_ms),
                              has_quad ? std::string(simple::to_string(reading.quad->source)).c_str()
                                       : "none",
                              lsd_ok ? 1 : 0, canny_ok ? 1 : 0,
                              has_quad ? static_cast<double>(reading.quad->area_fraction) : 0.0,
                              has_quad ? static_cast<double>(reading.quad->side_ratio) : 0.0,
                              color_letter(reading.centre_color()),
                              has_quad ? reading.unknown_count : -1,
                              has_quad ? cells_string(reading).c_str() : "");
                sink.line(line);
            }
        }

        // --- summary ---------------------------------------------------
        const auto pct = [&stats](int count) {
            return stats.frames > 0 ? 100.0 * static_cast<double>(count) / stats.frames : 0.0;
        };
        std::printf(
            "\n=== %s / %s ===\n"
            "frames replayed        %d\n"
            "LSD 4-gon candidates   %lld total (%.1f per frame)\n"
            "quad via LSD           %d (%.1f%%)\n"
            "quad via Canny         %d (%.1f%%)\n"
            "quad via sticker hull  %d (%.1f%%)\n"
            "quad via any route     %d (%.1f%%)\n"
            "complete readings      %d (%.1f%% of frames)\n"
            "centre named, holed    %d (rejected only by the all-nine rule)\n"
            "centre abstained       %d frames with a quad\n"
            "unknown cells          %lld of %lld sampled (%.1f%%)\n",
            clip.c_str(), mode_name.c_str(), stats.frames, stats.lsd_candidates,
            stats.frames > 0 ? static_cast<double>(stats.lsd_candidates) / stats.frames : 0.0,
            stats.lsd_quads, pct(stats.lsd_quads),
            stats.canny_quads, pct(stats.canny_quads), stats.hull_quads, pct(stats.hull_quads),
            stats.any_quads, pct(stats.any_quads),
            stats.complete, pct(stats.complete), stats.identified_with_holes, stats.centre_unknown,
            stats.unknown_cells,
            stats.cells_with_quad,
            stats.cells_with_quad > 0
                ? 100.0 * static_cast<double>(stats.unknown_cells) /
                      static_cast<double>(stats.cells_with_quad)
                : 0.0);

        std::printf("per-centre stability (distinct 9-sticker strings / complete readings):\n");
        for (const auto& [centre, strings] : stats.strings_by_centre) {
            std::printf("  %c  %zu distinct / %d readings\n", centre, strings.size(),
                        stats.readings_by_centre[centre]);
        }

        std::printf("face store (%zu of 6 filled):\n", store.filled_count());
        for (const auto centre : {FacetColor::white, FacetColor::yellow, FacetColor::red,
                                  FacetColor::orange, FacetColor::green, FacetColor::blue}) {
            const auto& slot = store.slot(centre);
            std::string modal = "---------";
            int modal_count = 0;
            for (const auto& [cells, count] : slot.tally) {
                if (count > modal_count) {
                    modal_count = count;
                    modal = cells_string(cells);
                }
            }
            std::printf("  %c  first=%s modal=%s(%d/%d)  readings=%d conflicts=%d\n",
                        color_letter(centre),
                        slot.filled ? cells_string(slot.cells).c_str() : "---------", modal.c_str(),
                        modal_count, slot.readings, slot.readings, slot.conflicts);
        }

        // --- the decisive metric ---------------------------------------
        //
        // Reported under BOTH slot-selection rules on the same evidence.
        // first_complete is the strict claim the design makes (one frame is
        // enough); modal says whether the readings were mostly right and
        // unluckily ordered. Reporting only the first would hide a pipeline
        // that works, reporting only the second would overstate one that
        // does not, and the gap between them is itself the measurement.
        for (const auto selection : {simple::FaceStore::Selection::first_complete,
                                     simple::FaceStore::Selection::modal}) {
            const char* rule =
                selection == simple::FaceStore::Selection::first_complete ? "first" : "modal";
            const auto faces = store.as_faces(selection);
            if (!faces) {
                std::printf("VALIDITY[%s]: NOT ASSEMBLED - only %zu of 6 faces captured\n", rule,
                            store.filled_count());
                continue;
            }
            std::printf("census[%s]: %s\n", rule, census_string(*faces).c_str());
            const auto assembled = cube::solver::assemble_scanned_faces_with_mapping(*faces);
            if (!assembled) {
                std::printf("VALIDITY[%s]: INVALID - assembler rejected (%s)\n", rule,
                            std::string(cube::to_string(assembled.error())).c_str());
                continue;
            }
            std::printf("VALIDITY[%s]: VALID and SOLVABLE (physically_valid=%d solvable=%d)\n", rule,
                        assembled->cube.is_physically_valid() ? 1 : 0,
                        assembled->cube.is_solvable() ? 1 : 0);
        }
    }

    // The harness always passes: it is a measurement instrument, and a clip
    // that fails to assemble is a RESULT to read off the report, not a
    // broken build. The numbers above are the output.
    SUCCEED("replay complete");
}
