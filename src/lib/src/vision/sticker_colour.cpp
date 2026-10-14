#include <cube/vision/sticker_colour.hpp>

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace cube::vision {

namespace {

// Pixels tried as the centre of the densest cluster: a lattice this many on a
// side laid across the patch. A lattice and not every n-th pixel in raster
// order, which with a stride a row long plus one is the patch's diagonal -
// exactly where a wordmark runs, so every candidate would be a letter.
constexpr int kCandidatesPerSide = 5;

// A pixel shares a candidate's colour when it lies within this many Lab units
// of it. Webcam noise on a sticker is 2 to 4 and shading across one a few
// more; a logo's letters and the dark plastic of a gap are 20 or more away
// from white.
constexpr float kBallRadius = 8.0F;

// Once the sticker's colour is known, a pixel is the sticker's own within this
// of it. A little wider than the ball: noise in a dim room spreads the
// plastic out, and the median of what is kept is steady where a count is not.
constexpr float kKeepRadius = 12.0F;

// Rounds of keeping what is near the estimate and taking the median again. A
// clean patch settles on the first.
constexpr int kMaxRounds = 3;

// The patch has a colour of its own only if at least this share of it (and
// this many pixels) sit in the densest cluster. Below that it is a gradient or
// a scatter, and the plain per-channel median is the honest answer.
constexpr float kMinClusterShare = 0.25F;
constexpr std::size_t kMinClusterPixels = 9;

// The estimate has settled when it moves by less than this, in Lab units.
constexpr float kSettled = 0.1F;

[[nodiscard]] float median_of(std::vector<float>& values) {
    const auto mid = values.begin() + static_cast<std::ptrdiff_t>(values.size() / 2);
    std::nth_element(values.begin(), mid, values.end());
    if (values.size() % 2 == 1) {
        return *mid;
    }
    const float lower = *std::max_element(values.begin(), mid);
    return (lower + *mid) / 2.0F;
}

// Per-channel median. `scratch` is the caller's, to spare an allocation per
// call.
[[nodiscard]] cv::Vec3f channel_medians(const std::vector<cv::Vec3f>& pixels,
                                        std::vector<float>& scratch) {
    cv::Vec3f median;
    for (int channel = 0; channel < 3; ++channel) {
        scratch.clear();
        for (const cv::Vec3f& pixel : pixels) {
            scratch.push_back(pixel(channel));
        }
        median(channel) = median_of(scratch);
    }
    return median;
}

[[nodiscard]] float squared_distance(const cv::Vec3f& a, const cv::Vec3f& b) {
    const cv::Vec3f d = a - b;
    return d.dot(d);
}

// The pixels within `radius` of `centre`.
void within(const std::vector<cv::Vec3f>& pixels, const cv::Vec3f& centre, float radius,
            std::vector<cv::Vec3f>& out) {
    out.clear();
    const float limit = radius * radius;
    for (const cv::Vec3f& pixel : pixels) {
        if (squared_distance(pixel, centre) <= limit) {
            out.push_back(pixel);
        }
    }
}

// The candidate with the most pixels within kBallRadius of it: the colour the
// largest part of the patch shares, which is the plastic's whenever the plastic
// is the largest part - and that holds well past half the patch being logo,
// because a logo is several colours and the plastic is one. `pixels` is the
// patch row by row, `cols` wide.
[[nodiscard]] const cv::Vec3f& densest_candidate(const std::vector<cv::Vec3f>& pixels, int rows,
                                                 int cols) {
    const float limit = kBallRadius * kBallRadius;
    std::size_t best = 0;
    std::size_t best_count = 0;
    for (int grid_row = 0; grid_row < kCandidatesPerSide; ++grid_row) {
        for (int grid_col = 0; grid_col < kCandidatesPerSide; ++grid_col) {
            const int row = ((2 * grid_row + 1) * rows) / (2 * kCandidatesPerSide);
            const int col = ((2 * grid_col + 1) * cols) / (2 * kCandidatesPerSide);
            const auto candidate = static_cast<std::size_t>((row * cols) + col);
            std::size_t count = 0;
            for (const cv::Vec3f& pixel : pixels) {
                if (squared_distance(pixel, pixels.at(candidate)) <= limit) {
                    ++count;
                }
            }
            if (count > best_count) {
                best_count = count;
                best = candidate;
            }
        }
    }
    return pixels.at(best);
}

}  // namespace

std::optional<cv::Vec3f> sticker_lab(const cv::Mat& bgr, const cv::Point2f& centre,
                                     int half_width) {
    const int x = static_cast<int>(std::lround(centre.x));
    const int y = static_cast<int>(std::lround(centre.y));
    const cv::Rect patch =
        cv::Rect(x - half_width, y - half_width, (2 * half_width) + 1, (2 * half_width) + 1) &
        cv::Rect(0, 0, bgr.cols, bgr.rows);
    if (patch.area() < static_cast<int>(kMinClusterPixels)) {
        return std::nullopt;
    }
    cv::Mat as_float;
    bgr(patch).convertTo(as_float, CV_32FC3, 1.0 / 255.0);
    cv::Mat lab;
    cv::cvtColor(as_float, lab, cv::COLOR_BGR2Lab);

    std::vector<cv::Vec3f> pixels;
    pixels.reserve(static_cast<std::size_t>(patch.area()));
    for (int row = 0; row < lab.rows; ++row) {
        for (int col = 0; col < lab.cols; ++col) {
            pixels.push_back(lab.at<cv::Vec3f>(row, col));
        }
    }

    std::vector<float> scratch;
    scratch.reserve(pixels.size());
    const auto min_cluster =
        std::max(kMinClusterPixels,
                 static_cast<std::size_t>(kMinClusterShare * static_cast<float>(pixels.size())));

    std::vector<cv::Vec3f> kept;
    kept.reserve(pixels.size());
    within(pixels, densest_candidate(pixels, lab.rows, lab.cols), kBallRadius, kept);
    if (kept.size() < min_cluster) {
        return channel_medians(pixels, scratch);
    }

    cv::Vec3f estimate = channel_medians(kept, scratch);
    for (int round = 0; round < kMaxRounds; ++round) {
        within(pixels, estimate, kKeepRadius, kept);
        if (kept.size() < min_cluster) {
            break;
        }
        const cv::Vec3f next = channel_medians(kept, scratch);
        const bool settled = std::sqrt(squared_distance(next, estimate)) < kSettled;
        estimate = next;
        if (settled) {
            break;
        }
    }
    return estimate;
}

}  // namespace cube::vision
