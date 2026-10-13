// Sampling-purity MEASUREMENT harness - hidden tag, never part of the
// normal suite, diagnostic only.
//
// The question it exists to answer: how much of what the per-cell colour
// sampler READS is not the cell? patch_stats_for_corners took the
// AXIS-ALIGNED BOUNDING RECT of the projected sample quad and medianed
// every pixel in it. For an axis-aligned square that rect IS the quad; for
// a quad rotated in-plane by theta the rect's area grows as
// (cos|theta| + sin|theta|)^2 - 2x at 45 degrees - and the excess sits at
// the CORNERS, which under a cube face are exactly where the inter-tile
// lattice, the neighbouring stickers and (on an outer cell) the background
// live. That is the geometry this harness quantifies, per cell, over real
// recordings, so the size of the prize is measured rather than assumed.
//
//   set CUBE_PATCH_PURITY_DIR=<corpus root>   (one recording dir, or a
//                                              directory of recording dirs)
//   cube_unit_tests.exe "[.patch-purity]"
//
// One CSV row per (pass, cell) of every capture-grade face reading. The
// detector stage mirrors [.pattern-id]'s exactly - same analysis-scale
// frames, same all-cells-in-frame gate, same corner-first pose precedence
// and 450 ms seed hold - because what is measured must be what production
// could actually see.
//
// The corpus is recorded on demand from live hardware and does not ship
// with the repo, so the harness SKIPS - never fails - when it is unset.
#ifdef _MSC_VER
#pragma warning(disable : 4996)
#endif

#include <catch2/catch_test_macros.hpp>

#include <cube/vision/corner_observation.hpp>
#include <cube/vision/face_observation.hpp>
#include <cube/vision/frame_recording.hpp>
#include <cube/vision/pose_estimation.hpp>

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
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

constexpr std::int64_t kSeedHoldMs = 450;

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

// Geometry of one sampler footprint: how many pixels the bounding-rect
// sampler reads, how many of those are genuinely inside the sample quad,
// and how far the colour it returns moves when the outside ones are
// dropped. The inside test is cv::pointPolygonTest at the pixel CENTRE,
// which is the same predicate cv::fillConvexPoly rasterises, so the
// "inside" count here is the count the masked sampler will actually get.
struct FootprintStats {
    int rect_px = 0;
    int inside_px = 0;
    // Pixels of the sampled rect that escape the CELL quad - the sticker's
    // own footprint - rather than merely the shrunken sample quad. THIS is
    // the number that decides whether the bounding-rect overshoot can pick
    // up foreign material at all: overshoot that stays inside the sticker
    // reads the sticker.
    int outside_cell_px = 0;
    double quad_area = 0.0;
    double rect_area = 0.0;
    float rotation_deg = 0.0F;  // in-plane angle of the quad's top edge, folded to [0,45]
    cv::Vec3f rect_median;      // median over the whole bounding rect (the shipped statistic)
    cv::Vec3f masked_median;    // median over the rect pixels genuinely inside the quad
};

// Channel median, same nth_element form production uses.
float channel_median(std::vector<float>& values) {
    const auto mid = values.begin() + static_cast<std::ptrdiff_t>(values.size() / 2);
    std::nth_element(values.begin(), mid, values.end());
    return *mid;
}

FootprintStats footprint_stats(const cv::Mat& bgr_frame, const std::array<cv::Point2f, 4>& corners,
                               const std::array<cv::Point2f, 4>& cell_corners) {
    const cv::Size frame_size = bgr_frame.size();
    FootprintStats stats;
    float min_x = corners[0].x;
    float max_x = corners[0].x;
    float min_y = corners[0].y;
    float max_y = corners[0].y;
    for (const auto& corner : corners) {
        min_x = std::min(min_x, corner.x);
        max_x = std::max(max_x, corner.x);
        min_y = std::min(min_y, corner.y);
        max_y = std::max(max_y, corner.y);
    }
    const int x0 = std::clamp(static_cast<int>(std::floor(min_x)), 0, frame_size.width - 1);
    const int y0 = std::clamp(static_cast<int>(std::floor(min_y)), 0, frame_size.height - 1);
    const int x1 = std::clamp(static_cast<int>(std::ceil(max_x)), x0 + 1, frame_size.width);
    const int y1 = std::clamp(static_cast<int>(std::ceil(max_y)), y0 + 1, frame_size.height);

    const std::vector<cv::Point2f> polygon(corners.begin(), corners.end());
    const std::vector<cv::Point2f> cell_polygon(cell_corners.begin(), cell_corners.end());

    cv::Mat rect_bgr = bgr_frame(cv::Rect(x0, y0, x1 - x0, y1 - y0));
    cv::Mat rect_float;
    rect_bgr.convertTo(rect_float, CV_32FC3, 1.0 / 255.0);
    cv::Mat lab;
    cv::cvtColor(rect_float, lab, cv::COLOR_BGR2Lab);

    std::array<std::vector<float>, 3> rect_channels;
    std::array<std::vector<float>, 3> masked_channels;
    for (int y = y0; y < y1; ++y) {
        for (int x = x0; x < x1; ++x) {
            ++stats.rect_px;
            const cv::Point2f probe(static_cast<float>(x) + 0.5F, static_cast<float>(y) + 0.5F);
            const bool inside = cv::pointPolygonTest(polygon, probe, false) >= 0.0;
            if (inside) {
                ++stats.inside_px;
            }
            if (cv::pointPolygonTest(cell_polygon, probe, false) < 0.0) {
                ++stats.outside_cell_px;
            }
            const auto& pixel = lab.at<cv::Vec3f>(y - y0, x - x0);
            for (int c = 0; c < 3; ++c) {
                rect_channels[static_cast<std::size_t>(c)].push_back(pixel[c]);
                if (inside) {
                    masked_channels[static_cast<std::size_t>(c)].push_back(pixel[c]);
                }
            }
        }
    }
    for (int c = 0; c < 3; ++c) {
        const auto channel = static_cast<std::size_t>(c);
        stats.rect_median[c] = channel_median(rect_channels[channel]);
        stats.masked_median[c] = masked_channels[channel].empty()
                                     ? stats.rect_median[c]
                                     : channel_median(masked_channels[channel]);
    }
    stats.quad_area = std::abs(cv::contourArea(polygon));
    stats.rect_area = static_cast<double>(x1 - x0) * static_cast<double>(y1 - y0);

    const cv::Point2f top_edge = corners[1] - corners[0];
    float degrees =
        std::abs(static_cast<float>(std::atan2(top_edge.y, top_edge.x) * 180.0 / CV_PI));
    degrees = std::fmod(degrees, 90.0F);
    stats.rotation_deg = degrees > 45.0F ? 90.0F - degrees : degrees;
    return stats;
}

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

    std::optional<PoseSeed> seed_prior;
    std::int64_t seed_t_ms = 0;

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
        const PoseSeed* seed = seed_prior ? &*seed_prior : nullptr;

        const auto all_cells_in_frame = [&](const auto& cells) {
            return std::all_of(cells.begin(), cells.end(),
                               [&](const StickerQuad& cell) { return cell_in_frame(cell, frame); });
        };

        std::optional<FaceObservation> face;
        if (auto observed = observe_face(frame);
            observed.has_value() && !observed->pose_only && all_cells_in_frame(observed->cells)) {
            face = std::move(*observed);
        }
        if (face.has_value()) {
            if (auto projected = project_cube_from_face(*face, frame.size(), seed);
                projected.has_value()) {
                seed_prior = PoseSeed{projected->rvec, projected->tvec};
                seed_t_ms = row.t_ms;
            }
        }
        if (!face.has_value()) {
            continue;
        }

        for (int index = 0; index < 9; ++index) {
            const auto& cell = face->cells[static_cast<std::size_t>(index)];
            const auto sample = detail::cell_patch_sample(frame, cell);
            const auto geometry = footprint_stats(frame, sample.sample_quad, cell.corners);
            // Position class: 4 is the centre, the odd indices are edges,
            // the rest corners - the split the defect predicts will matter,
            // since only a corner cell's overshoot can reach off-face.
            const char* position = index == 4 ? "centre" : (index % 2 == 1 ? "edge" : "corner");
            std::fprintf(
                out,
                "%s,%d,%d,%s,%d,%d,%d,%.2f,%.2f,%.2f,%.1f,%.4f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f\n",
                clip.c_str(), row.pass, index, position, geometry.rect_px, geometry.inside_px,
                geometry.outside_cell_px, geometry.quad_area, geometry.rect_area,
                static_cast<double>(geometry.rotation_deg),
                static_cast<double>(sample.cell_quad_area_px),
                static_cast<double>(sample.weighted_mad),
                static_cast<double>(geometry.rect_median[0]),
                static_cast<double>(geometry.rect_median[1]),
                static_cast<double>(geometry.rect_median[2]),
                static_cast<double>(geometry.masked_median[0]),
                static_cast<double>(geometry.masked_median[1]),
                static_cast<double>(geometry.masked_median[2]));
        }
    }
}

}  // namespace

TEST_CASE("PatchPurityExtract", "[.patch-purity]") {
    const char* root_env = std::getenv("CUBE_PATCH_PURITY_DIR");
    if (root_env == nullptr || *root_env == '\0') {
        SKIP("CUBE_PATCH_PURITY_DIR not set - record clips via CUBE_VISION_RECORD_DIR first");
    }
    const std::filesystem::path root(root_env);
    const auto clips = collect_clip_dirs(root);
    if (clips.empty()) {
        SKIP("no recording directories (index.csv) under " << root.string());
    }

    std::FILE* out = stdout;
    std::fprintf(out,
                 "clip,pass,cell,position,rect_px,inside_px,outside_cell_px,quad_area,rect_area,"
                 "rot_deg,cell_area,"
                 "mad,rL,ra,rb,mL,ma,mb\n");
    for (const auto& clip : clips) {
        emit_clip(clip, clip.filename().string(), out);
        std::fflush(out);
    }
    SUCCEED();
}

// Per-cell sampler COST, hidden tag. The masked, mode-seeking sampler runs
// 9 times per face per frame on a phone, so the question "what did purity
// cost" has to be answered in nanoseconds per cell, not inferred from the
// wall time of a whole replay (where detection dominates and the sampler's
// share is lost in the noise). Times detail::cell_patch_sample - the same
// patch_stats_for_corners body production's per-cell sampler runs - over a
// synthetic patch of the size the corpus actually produces (the median
// in-quad patch there is 488 px, i.e. a ~22x22 sample square).
//
//   cube_unit_tests.exe "[.patch-cost]"
TEST_CASE("PatchSampleCost", "[.patch-cost]") {
    cv::Mat frame(120, 120, CV_8UC3, cv::Scalar(60, 90, 140));
    cv::rectangle(frame, cv::Rect(40, 40, 40, 40), cv::Scalar(70, 100, 150), cv::FILLED);
    StickerQuad cell;
    cell.corners = {cv::Point2f(32.0F, 32.0F), cv::Point2f(88.0F, 32.0F), cv::Point2f(88.0F, 88.0F),
                    cv::Point2f(32.0F, 88.0F)};

    constexpr int kIterations = 200000;
    float sink = 0.0F;
    const auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < kIterations; ++i) {
        sink += detail::cell_patch_sample(frame, cell).median[0];
    }
    const auto elapsed = std::chrono::steady_clock::now() - start;
    const double ns_per_cell =
        static_cast<double>(std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count()) /
        kIterations;
    std::printf("# patch-cost: %.0f ns per cell, %.1f us per 9-cell face pass (sink %.3f)\n",
                ns_per_cell, ns_per_cell * 9.0 / 1000.0, static_cast<double>(sink));
    SUCCEED();
}
