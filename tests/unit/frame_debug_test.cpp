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
#include <cube/vision/grid_detection.hpp>

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <cstdio>
#include <cstdlib>
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
    cv::Mat mask = hsv_channels[1] >= 60;
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
    cv::bitwise_and(hsv_channels[2] >= 150, hsv_channels[1] < 60, mask);
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

    // Stage 4: corner-path candidate replica, then the real verdict.
    replica_corner_candidates(frame);
    const auto corner = observe_corner(frame);
    const std::string corner_status =
        corner.has_value() ? "OK conf=" + std::to_string(corner->geometry_confidence) : std::string("FAILED");
    std::printf("observe_corner: %s\n", corner_status.c_str());

    SUCCEED();
}
