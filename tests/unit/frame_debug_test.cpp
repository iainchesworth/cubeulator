// Offline replay harness for real captured frames - hidden tag, never part
// of the normal suite. Point CUBE_DEBUG_FRAME at a frame_analysis_N.png
// dumped by ScanSession's CUBE_VISION_DEBUG_DIR observability hook (see
// scan_session.cpp) and run:
//
//   cube_unit_tests.exe "[.frame-debug]"
//
// Prints every pipeline stage's counts/verdicts for that exact frame, so a
// real-webcam detection failure can be localized without touching the app.
// The colour-mask/corner-mask stages are replicated inline (they live in
// anonymous namespaces in face_observation.cpp / corner_observation.cpp);
// constants here MUST be kept in sync with those files when tuning.
// std::getenv is fine here (single-threaded debug harness, read-only);
// silence MSVC's CRT deprecation the same way cube_platform's CMake does
// for its own std::getenv uses.
#ifdef _MSC_VER
#pragma warning(disable : 4996)
#endif

#include <catch2/catch_test_macros.hpp>

#include <cube/vision/color_classification.hpp>
#include <cube/vision/color_resolution.hpp>
#include <cube/vision/corner_observation.hpp>
#include <cube/vision/face_observation.hpp>
#include <cube/vision/frame_recording.hpp>
#include <cube/vision/grid_detection.hpp>
#include <cube/vision/pose_estimation.hpp>

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

using namespace cube::vision;

namespace {

cube::model::FacetColor nearest_palette_color(const cv::Vec3f& lab) {
    const ColorPalette palette = default_palette();
    cube::model::FacetColor best = cube::model::FacetColor::unknown;
    float best_distance = 1e9F;
    for (const auto& entry : palette.entries) {
        const cv::Vec3f diff = lab - entry.lab;
        const float distance = diff.dot(diff);
        if (distance < best_distance) {
            best_distance = distance;
            best = entry.color;
        }
    }
    return best;
}

}  // namespace

namespace {

// Mirrors face_observation.cpp's separate saturation / white candidate
// sources (post mask-separation fix - masks are contoured independently).
std::vector<detail::StickerCandidate> replica_saturation_candidates(const cv::Mat& bgr_frame) {
    cv::Mat hsv;
    cv::cvtColor(bgr_frame, hsv, cv::COLOR_BGR2HSV);
    std::vector<cv::Mat> hsv_channels;
    cv::split(hsv, hsv_channels);
    cv::Mat mask = hsv_channels[1] >= 40;
    const auto kernel = cv::getStructuringElement(cv::MORPH_RECT, cv::Size(3, 3));
    cv::morphologyEx(mask, mask, cv::MORPH_OPEN, kernel);
    return detail::candidates_from_binary_mask(mask);
}

std::vector<detail::StickerCandidate> replica_white_candidates(const cv::Mat& bgr_frame) {
    cv::Mat hsv;
    cv::cvtColor(bgr_frame, hsv, cv::COLOR_BGR2HSV);
    std::vector<cv::Mat> hsv_channels;
    cv::split(hsv, hsv_channels);
    cv::Mat mask;
    cv::bitwise_and(hsv_channels[2] >= 150, hsv_channels[1] < 40, mask);
    const auto kernel = cv::getStructuringElement(cv::MORPH_RECT, cv::Size(3, 3));
    cv::morphologyEx(mask, mask, cv::MORPH_OPEN, kernel);
    return detail::candidates_from_binary_mask(mask);
}

// Mirrors corner_observation.cpp's candidate mask (post value-floor fix).
int replica_corner_candidates(const cv::Mat& bgr_frame) {
    cv::Mat blurred;
    cv::GaussianBlur(bgr_frame, blurred, cv::Size(5, 5), 0);
    cv::Mat blurred_hsv;
    cv::cvtColor(blurred, blurred_hsv, cv::COLOR_BGR2HSV);
    std::vector<cv::Mat> blurred_channels;
    cv::split(blurred_hsv, blurred_channels);
    cv::Mat hsv;
    cv::cvtColor(bgr_frame, hsv, cv::COLOR_BGR2HSV);
    std::vector<cv::Mat> hsv_channels;
    cv::split(hsv, hsv_channels);

    cv::Mat v_eq;
    cv::createCLAHE(2.0, cv::Size(8, 8))->apply(blurred_channels[2], v_eq);
    int block_size = std::min(bgr_frame.cols, bgr_frame.rows) / 4;
    if (block_size % 2 == 0) {
        ++block_size;
    }
    block_size = std::max(block_size, 7);
    cv::Mat brightness_mask;
    cv::adaptiveThreshold(v_eq, brightness_mask, 255, cv::ADAPTIVE_THRESH_GAUSSIAN_C, cv::THRESH_BINARY, block_size,
                          5.0);
    cv::Mat value_floor;
    cv::threshold(hsv_channels[2], value_floor, 44, 255, cv::THRESH_BINARY);
    cv::bitwise_and(brightness_mask, value_floor, brightness_mask);
    cv::Mat saturated_mask;
    cv::threshold(hsv_channels[1], saturated_mask, 59, 255, cv::THRESH_BINARY);
    cv::Mat white_mask;
    cv::threshold(hsv_channels[2], white_mask, 179, 255, cv::THRESH_BINARY);
    cv::Mat mask = brightness_mask | saturated_mask | white_mask;
    const auto kernel = cv::getStructuringElement(cv::MORPH_RECT, cv::Size(3, 3));
    cv::morphologyEx(mask, mask, cv::MORPH_OPEN, kernel);

    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(mask, contours, cv::RETR_LIST, cv::CHAIN_APPROX_SIMPLE);
    const double frame_area = static_cast<double>(bgr_frame.cols) * bgr_frame.rows;

    int pass_area = 0;
    int pass_poly = 0;
    int final_count = 0;
    for (const auto& contour : contours) {
        const double area = cv::contourArea(contour);
        if (area < frame_area * 0.0005 || area > frame_area * 0.05) {
            continue;
        }
        ++pass_area;
        std::vector<cv::Point> approx;
        cv::approxPolyDP(contour, approx, 0.04 * cv::arcLength(contour, true), true);
        if (approx.size() != 4 || !cv::isContourConvex(approx)) {
            std::printf("  corner-candidate reject poly: area=%.0f vertices=%zu\n", area, approx.size());
            continue;
        }
        ++pass_poly;
        const cv::RotatedRect min_rect = cv::minAreaRect(approx);
        if (min_rect.size.width <= 0.0F || min_rect.size.height <= 0.0F) {
            continue;
        }
        const float aspect = std::max(min_rect.size.width, min_rect.size.height) /
                             std::min(min_rect.size.width, min_rect.size.height);
        if (aspect > 3.0F) {
            std::printf("  corner-candidate reject aspect: area=%.0f aspect=%.2f\n", area, aspect);
            continue;
        }
        ++final_count;
    }
    std::printf("corner replica: contours=%zu pass_area=%d pass_poly=%d final=%d (floor 18)\n", contours.size(),
                pass_area, pass_poly, final_count);
    return final_count;
}

}  // namespace

TEST_CASE("replay a dumped analysis frame through every pipeline stage", "[.frame-debug]") {
    const char* path = std::getenv("CUBE_DEBUG_FRAME");
    REQUIRE(path != nullptr);

    const cv::Mat frame = cv::imread(path, cv::IMREAD_COLOR);
    REQUIRE_FALSE(frame.empty());
    std::printf("frame: %s (%dx%d)\n", path, frame.cols, frame.rows);

    // Stage 1a: brightness-path candidates (the shared detail:: seam).
    const auto bright = detail::find_square_candidates(frame);
    std::printf("bright-path candidates: %zu\n", bright.size());
    for (const auto& candidate : bright) {
        std::printf("  bright cand: center=(%.0f,%.0f) area=%.0f bounds=%dx%d\n", candidate.center.x,
                    candidate.center.y, candidate.area, candidate.bounds.width, candidate.bounds.height);
    }

    // Stage 1a': inverted-brightness candidates (light-lattice cubes).
    const auto dark = detail::find_dark_square_candidates(frame);
    std::printf("dark-path candidates: %zu\n", dark.size());
    for (const auto& candidate : dark) {
        std::printf("  dark cand: center=(%.0f,%.0f) area=%.0f bounds=%dx%d\n", candidate.center.x,
                    candidate.center.y, candidate.area, candidate.bounds.width, candidate.bounds.height);
    }

    // Stage 1b/1c: saturation and white candidate sources (replicas).
    const auto saturated = replica_saturation_candidates(frame);
    std::printf("saturation candidates: %zu\n", saturated.size());
    for (const auto& candidate : saturated) {
        std::printf("  sat cand: center=(%.0f,%.0f) area=%.0f\n", candidate.center.x, candidate.center.y,
                    candidate.area);
    }
    const auto white = replica_white_candidates(frame);
    std::printf("white candidates: %zu\n", white.size());
    for (const auto& candidate : white) {
        std::printf("  white cand: center=(%.0f,%.0f) area=%.0f\n", candidate.center.x, candidate.center.y,
                    candidate.area);
    }

    // Stage 2: lattice fit over the bright path alone, then bright+dark.
    const auto fit = detail::fit_lattice_detailed(bright, frame.size());
    const std::string fit_status = fit ? "inliers=" + std::to_string(fit->inlier_count) : std::string("none");
    std::printf("lattice(bright only): %s\n", fit_status.c_str());

    auto combined = bright;
    combined.insert(combined.end(), dark.begin(), dark.end());
    const auto fit2 = detail::fit_lattice_detailed(combined, frame.size());
    const std::string fit2_status = fit2 ? "inliers=" + std::to_string(fit2->inlier_count) : std::string("none");
    std::printf("lattice(bright+dark): %s\n", fit2_status.c_str());

    // Stage 3: the real observe_face verdict, with per-cell samples - the
    // ground truth for diagnosing wrong captured colours (offset lattice
    // vs glare vs classification drift).
    const auto face = observe_face(frame);
    if (face.has_value()) {
        std::printf("observe_face: OK conf=%.2f face_quad=(%.0f,%.0f)-(%.0f,%.0f)\n", face->geometry_confidence,
                    face->face_quad[0].x, face->face_quad[0].y, face->face_quad[2].x, face->face_quad[2].y);
        const char* names[] = {"unknown", "white", "yellow", "red", "orange", "green", "blue"};
        for (int row = 0; row < 3; ++row) {
            for (int col = 0; col < 3; ++col) {
                const auto index = static_cast<std::size_t>(row * 3 + col);
                cv::Point2f center(0, 0);
                for (const auto& corner : face->cells[index].corners) {
                    center += corner;
                }
                center *= 0.25F;
                const auto& lab = face->lab[index];
                // classify_lab is the production (hue-based) classifier;
                // nearest_palette_color shows what naive distance would say.
                std::printf("  cell[%d,%d] center=(%.0f,%.0f) lab=(%.0f,%.0f,%.0f) -> %s (naive: %s)\n", row, col,
                            center.x, center.y, lab[0], lab[1], lab[2],
                            names[static_cast<int>(classify_lab(lab).color)],
                            names[static_cast<int>(nearest_palette_color(lab))]);
            }
        }
    } else {
        std::printf("observe_face: FAILED\n");
    }

    // Stage 3b: partial-lattice completion diagnostics - every scored
    // placement exactly as the production fallback ranked it.
    const auto placements = detail::score_partial_placements(frame);
    std::printf("completion placements: %zu\n", placements.size());
    for (const auto& placement : placements) {
        const auto center = [&](float u, float v) {
            const double x = placement.transform.at<double>(0, 0) * u + placement.transform.at<double>(0, 1) * v +
                             placement.transform.at<double>(0, 2);
            const double y = placement.transform.at<double>(1, 0) * u + placement.transform.at<double>(1, 1) * v +
                             placement.transform.at<double>(1, 2);
            return cv::Point2f(static_cast<float>(x), static_cast<float>(y));
        };
        const cv::Point2f anchor = center(0.0F, 0.0F);
        const cv::Point2f col_end = center(1.0F, 0.0F);
        std::printf(
            "  placement: anchor=(%.0f,%.0f) pitch=%.1f inliers=%d gap_mean=%.1f gap_peak=%.1f contrast=%.1f "
            "cell_mad=%.2f min_l=%.2f edge=%.1f%s\n",
            anchor.x, anchor.y, std::hypot(col_end.x - anchor.x, col_end.y - anchor.y), placement.inlier_count,
            placement.mean_gap_deviation, placement.peak_gap_deviation, placement.anchor_contrast, placement.cell_mad,
            placement.min_cell_lightness, placement.boundary_contrast, placement.accepted ? " ACCEPTED" : "");
    }

    // Optional overlay dump: CUBE_DEBUG_OVERLAY=<path.png> draws the
    // accepted grid (face quad + cell quads) over the frame for visual
    // placement verification.
    if (const char* overlay_path = std::getenv("CUBE_DEBUG_OVERLAY"); overlay_path != nullptr && face.has_value()) {
        cv::Mat overlay = frame.clone();
        const auto draw_quad = [&](const std::array<cv::Point2f, 4>& corners, const cv::Scalar& colour) {
            std::vector<cv::Point> points;
            points.reserve(corners.size());
            for (const auto& corner : corners) {
                points.emplace_back(static_cast<int>(std::lround(corner.x)), static_cast<int>(std::lround(corner.y)));
            }
            cv::polylines(overlay, points, true, colour, 1, cv::LINE_AA);
        };
        for (const auto& cell : face->cells) {
            draw_quad(cell.corners, cv::Scalar(0, 255, 255));
        }
        draw_quad(face->face_quad, cv::Scalar(0, 255, 0));
        cv::imwrite(overlay_path, overlay);
        std::printf("overlay written: %s\n", overlay_path);
    }

    // Stage 4: corner-path candidate replica, then the real verdict.
    replica_corner_candidates(frame);
    const auto corner = observe_corner(frame);
    const std::string corner_status =
        corner.has_value() ? "OK conf=" + std::to_string(corner->geometry_confidence) : std::string("FAILED");
    std::printf("observe_corner: %s\n", corner_status.c_str());

    SUCCEED();
}

// Sequence replay harness for whole recorded clips - hidden tag, never part
// of the normal suite. Point CUBE_TRACK_REPLAY_DIR at a directory recorded
// by ScanSession's CUBE_VISION_RECORD_DIR hook (see record_vision_frame in
// scan_session.cpp; on-disk format shared via cube/vision/frame_recording.hpp)
// and run:
//
//   cube_unit_tests.exe "[.track-replay]"
//
// Every recorded pass is driven through the same detector cascade the live
// worker runs - observe_face and observe_corner each behind the live
// fully-visible-cells gate, then the corner-first pose projection over
// whatever survived - and emitted as one CSV row per pass on stdout, ready
// for offline metric extraction (theta histograms, gap lengths, identity
// stability) as the tracker milestones land. Detections the gate discards
// are reported in the face_gated/corner_gated columns so a metric can
// distinguish "detector missed" from "detected but partially off-frame" -
// conflating them would misattribute gate losses to detector losses.
// Skips (never fails) when no recording is present: the corpus is
// recorded on demand from live hardware and does not ship with the repo.
//
// Fidelity notes, so numbers from this harness can be trusted against live
// behavior:
//  - Frames are replayed at the recorded analysis scale rather than
//    re-upscaled to full-frame coordinates. Orientation quaternions are
//    IDENTICAL either way: the pose projectors derive their approximate
//    intrinsics from the frame size (fx=fy=width, centred principal point,
//    pose_estimation.cpp), so uniformly scaling both the geometry and the
//    frame size leaves the normalized camera rays - and hence the rotation
//    - unchanged. Only the translation magnitude differs, and nothing here
//    consumes it.
//  - The live cell_in_frame gate compares full-frame geometry against
//    full-frame bounds; comparing analysis-scale geometry against
//    analysis-scale bounds is the same predicate under that uniform scale,
//    so gating decisions match too. It is applied to BOTH detector paths
//    exactly as analyze() applies it: a face is accepted only when all 9
//    cells are in frame, a corner only when all 27 cells across its three
//    views are, and only a surviving corner (falling back to a surviving
//    face) feeds pose projection.
//  - Pose continuity is mirrored too: the previous pass's accepted pose is
//    held under the live 450 ms overlay/seed window (kOverlayHoldMs,
//    scan_session.cpp), and while live it both seeds the projectors' solves
//    (M3 arbitration) and hints observe_corner via compute_corner_hint
//    (M7's pose-derived sectors/bases). Without the hint, corner detections
//    stop firing off the isometric pose and the replay understates exactly
//    the detection cadence these CSV rows exist to measure. The hint is
//    derived from the seed's ROTATION only, so replaying at analysis scale
//    (which shrinks the held translation along with the frame) cannot skew
//    it; the seeded solves are self-consistent for the same reason the
//    first bullet gives. Null seed -> null hint -> the historical
//    fixed-pose detector, exactly as live.
//  - Index rows at least one full ring lap older than the newest row are
//    excluded as overwritten (frame_overwritten in frame_recording.hpp):
//    their filename slot provably holds a later pass's pixels, and within
//    one run the resolution never changes, so the size cross-check alone
//    would silently pair those rows' timestamps with late-clip images.
//  - JPEG q=90 recompression perturbs Lab samples slightly relative to the
//    live pass; geometry/pose are unaffected and colour deltas are far
//    below classification margins.
TEST_CASE("replay a recorded analysis sequence pass by pass", "[.track-replay]") {
    namespace recording = cube::vision::recording;

    const char* dir = std::getenv("CUBE_TRACK_REPLAY_DIR");
    if (dir == nullptr) {
        SKIP("CUBE_TRACK_REPLAY_DIR not set - record a clip via CUBE_VISION_RECORD_DIR first");
    }
    const std::string dir_path(dir);
    std::ifstream index_stream(dir_path + "/" + std::string(recording::kIndexCsvName));
    if (!index_stream) {
        SKIP("no " << recording::kIndexCsvName << " under " << dir_path << " - not a recording directory");
    }

    const auto rows = recording::read_index(index_stream);
    std::printf("replay: %s (%zu passes)\n", dir_path.c_str(), rows.size());
    std::printf("pass,t_ms,dt_ms,face,face_conf,corner,corner_conf,face_gated,corner_gated,pose,qw,qx,qy,qz\n");

    // The newest pass determines which older rows' ring slots have provably
    // been rewritten on disk (see frame_overwritten) - must be known before
    // the replay loop touches any file.
    int max_pass = 0;
    for (const auto& row : rows) {
        max_pass = std::max(max_pass, row.pass);
    }

    // Same fully-visible gate as the live worker (scan_session.cpp), at
    // analysis scale per the fidelity note above.
    const auto cell_in_frame = [](const cube::vision::StickerQuad& cell, const cv::Mat& bounds) {
        cv::Point2f center(0.0F, 0.0F);
        for (const auto& corner : cell.corners) {
            center += corner;
        }
        center *= 0.25F;
        return center.x >= 0.0F && center.y >= 0.0F && center.x < static_cast<float>(bounds.cols) &&
               center.y < static_cast<float>(bounds.rows);
    };

    // Matches the live overlay/seed hold window (kOverlayHoldMs in
    // scan_session.cpp), same constant tracker_sweep_test.cpp keeps: a pose
    // the overlay already dropped is too stale to hint or seed with.
    constexpr std::int64_t kSeedHoldMs = 450;
    std::optional<cube::vision::PoseSeed> seed_prior;
    std::int64_t seed_t_ms = 0;

    std::int64_t previous_t_ms = 0;
    bool have_previous = false;
    std::size_t replayed = 0;
    for (const auto& row : rows) {
        if (recording::frame_overwritten(row.pass, max_pass)) {
            // Ring wrap: a recording longer than the file ring overwrites
            // its oldest frames, and within one run they are the SAME
            // resolution as their replacements - only the index reveals
            // the wrap. Report and carry on; the surviving final lap is
            // still a valid contiguous clip.
            std::printf("%d,%lld,,overwritten,,,,,,,,,,\n", row.pass, static_cast<long long>(row.t_ms));
            continue;
        }
        const cv::Mat frame = cv::imread(dir_path + "/" + recording::frame_filename(row.pass), cv::IMREAD_COLOR);
        if (frame.empty() || frame.cols != row.w || frame.rows != row.h) {
            // Secondary guard for a file that is missing outright or was
            // left behind by an earlier run at a different resolution
            // (the fresh-truncated index doesn't describe those frames).
            std::printf("%d,%lld,,overwritten,,,,,,,,,,\n", row.pass, static_cast<long long>(row.t_ms));
            continue;
        }

        const std::int64_t dt_ms = have_previous ? row.t_ms - previous_t_ms : 0;
        previous_t_ms = row.t_ms;
        have_previous = true;

        // Expired BEFORE the detector stage: the live pass samples ONE
        // prior at queue time and derives both the corner hint and the
        // solve seed from it, so a pose the live worker would already have
        // dropped must not hint observe_corner here either.
        if (seed_prior && row.t_ms - seed_t_ms > kSeedHoldMs) {
            seed_prior.reset();
        }
        const cube::vision::PoseSeed* seed = seed_prior ? &*seed_prior : nullptr;

        const auto all_cells_in_frame = [&](const auto& cells) {
            return std::all_of(cells.begin(), cells.end(),
                               [&](const cube::vision::StickerQuad& cell) { return cell_in_frame(cell, frame); });
        };

        const auto observed = observe_face(frame);
        const bool face_gated = observed.has_value() && !all_cells_in_frame(observed->cells);
        std::optional<cube::vision::FaceObservation> face;
        if (observed.has_value() && !face_gated) {
            face = *observed;
        }
        // Same all-27-cells gate as the live worker: a corner view with any
        // cell off-frame samples fabricated colour and never reaches the
        // live pipeline's aggregation OR pose projection, so it must not
        // count as a detection (or seed a pose) here either. Hinted from
        // the held pose exactly as analyze() hints it (see the fidelity
        // note above): a frontal/edge-on prediction yields no hint and the
        // detector runs the historical fixed-pose way.
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

        // Corner-first, exactly like the live pass: three pinned quads
        // constrain the pose far better than one. Both solves take the held
        // seed the way analyze() passes it.
        std::optional<cube::vision::CubePoseProjection> pose;
        if (corner.has_value()) {
            if (const auto projected = project_cube_from_corner(*corner, frame.size(), seed); projected.has_value()) {
                pose = *projected;
            }
        } else if (face.has_value()) {
            if (const auto projected = project_cube_from_face(*face, frame.size(), seed); projected.has_value()) {
                pose = *projected;
            }
        }
        if (pose.has_value() && pose->orientation_valid) {
            seed_prior = cube::vision::PoseSeed{pose->rvec, pose->tvec};
            seed_t_ms = row.t_ms;
        }

        std::printf("%d,%lld,%lld,%d,%.3f,%d,%.3f,%d,%d,%d,", row.pass, static_cast<long long>(row.t_ms),
                    static_cast<long long>(dt_ms), face.has_value() ? 1 : 0,
                    face.has_value() ? face->geometry_confidence : 0.0F, corner.has_value() ? 1 : 0,
                    corner.has_value() ? corner->geometry_confidence : 0.0F, face_gated ? 1 : 0, corner_gated ? 1 : 0,
                    pose.has_value() && pose->orientation_valid ? 1 : 0);
        if (pose.has_value() && pose->orientation_valid) {
            std::printf("%.5f,%.5f,%.5f,%.5f\n", pose->orientation[0], pose->orientation[1], pose->orientation[2],
                        pose->orientation[3]);
        } else {
            std::printf(",,,\n");
        }
        ++replayed;
    }

    std::printf("replayed %zu of %zu passes\n", replayed, rows.size());
    CHECK(replayed > 0);
}
