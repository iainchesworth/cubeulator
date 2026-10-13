#include <cube/vision/direct_scan.hpp>

#include <cube/model/facelet.hpp>
#include <cube/solver/scan_assembly.hpp>
#include <cube/vision/color_resolution.hpp>
#include <cube/vision/temporal_fusion.hpp>

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <tuple>
#include <utility>

namespace cube::vision {

namespace {

// The reader works at this width: blob filters and the blur are sized for
// it, and a side read at a quarter of this still resolves (measured on
// recorded scans down to 320 wide).
constexpr int kWorkingWidth = 640;

// Sticker blob limits, as fractions of the frame area. At the working width
// they admit stickers from about 9 to 65 pixels across.
constexpr double kMinBlobArea = 0.0006;
constexpr double kMaxBlobArea = 0.03;
constexpr float kMaxBlobAspect = 1.45F;
constexpr float kMinBlobFill = 0.72F;

// Blobs that may belong to one lattice: similar size, within reach of the
// seed. Six of nine is enough to span three rows and three columns.
constexpr float kMinSizeRatio = 0.72F;
constexpr float kMaxSizeRatio = 1.4F;
constexpr float kReachInSides = 3.4F;
constexpr std::size_t kMinLatticeBlobs = 6;
constexpr float kMaxCellResidual = 0.3F;

// Square-on test: the lattice's two axes must be near-equal and near
// perpendicular, and the blobs must sit on it.
constexpr float kMinAxisRatio = 0.85F;
constexpr float kMaxAxisRatio = 1.18F;
constexpr float kMaxAxisCosine = 0.17F;
constexpr float kMaxLatticeRms = 0.12F;

// Half-width of the sampled patch, in sticker spacings.
constexpr float kPatchHalfWidth = 0.22F;

// A read counts only if the lattice centre moved less than this fraction of
// a sticker spacing since the previous frame.
constexpr float kSteadyFraction = 0.12F;

// Two reads are the same side when their nine colours agree this closely
// under some quarter-turn. Recorded scans give the same cube anywhere from
// 4 to 26, so the value is not delicate.
constexpr float kSameSideDistance = 12.0F;

// Lightness and chroma carry shading; hue does not. Down-weighting the
// first two keeps a shaded sticker close to its own colour.
constexpr float kShadingWeight = 0.25F;

struct Blob {
    cv::Point2f centre;
    float side = 0.0F;
};

// kRoll[k][i] is the cell of a read that lands at cell i once the read is
// turned k quarter-turns.
constexpr std::array<std::array<int, 9>, 4> kRoll = {{
    {0, 1, 2, 3, 4, 5, 6, 7, 8},
    {2, 5, 8, 1, 4, 7, 0, 3, 6},
    {8, 7, 6, 5, 4, 3, 2, 1, 0},
    {6, 3, 0, 7, 4, 1, 8, 5, 2},
}};

template <typename T>
[[nodiscard]] T median_of(std::vector<T> values) {
    const auto mid = values.begin() + static_cast<std::ptrdiff_t>(values.size() / 2);
    std::nth_element(values.begin(), mid, values.end());
    if (values.size() % 2 == 1) {
        return *mid;
    }
    const T lower = *std::max_element(values.begin(), mid);
    return (lower + *mid) / T{2};
}

// Bright, compact, squarish regions bounded by darker plastic. Value (the
// brightest channel) is used so a saturated blue or red sticker still
// stands clear of black plastic.
[[nodiscard]] std::vector<Blob> sticker_blobs(const cv::Mat& bgr) {
    std::array<cv::Mat, 3> channels;
    cv::split(bgr, channels.data());
    cv::Mat value = cv::max(cv::max(channels[0], channels[1]), channels[2]);
    cv::GaussianBlur(value, value, cv::Size(5, 5), 0.0);

    const int block = static_cast<int>(std::min(bgr.rows, bgr.cols) * 0.12) | 1;
    cv::Mat binary;
    cv::adaptiveThreshold(value, binary, 255.0, cv::ADAPTIVE_THRESH_MEAN_C, cv::THRESH_BINARY,
                          std::max(block, 3), 8.0);
    cv::morphologyEx(binary, binary, cv::MORPH_OPEN, cv::Mat::ones(3, 3, CV_8U));

    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(binary, contours, cv::RETR_LIST, cv::CHAIN_APPROX_SIMPLE);

    const double frame_area = static_cast<double>(bgr.rows) * bgr.cols;
    std::vector<Blob> blobs;
    for (const auto& contour : contours) {
        const double area = cv::contourArea(contour);
        if (area <= frame_area * kMinBlobArea || area >= frame_area * kMaxBlobArea) {
            continue;
        }
        const cv::RotatedRect rect = cv::minAreaRect(contour);
        const float shorter = std::min(rect.size.width, rect.size.height);
        const float longer = std::max(rect.size.width, rect.size.height);
        if (shorter < 1.0F || longer / shorter > kMaxBlobAspect) {
            continue;
        }
        if (static_cast<float>(area) / (shorter * longer) < kMinBlobFill) {
            continue;
        }
        blobs.push_back(Blob{rect.center, std::sqrt(shorter * longer)});
    }
    return blobs;
}

// The lattice as an affine map from cell (column, row) to pixels.
struct Lattice {
    cv::Point2f origin;
    cv::Point2f column_step;
    cv::Point2f row_step;
    std::size_t blobs = 0;
    float rms = 0.0F;  // residual of the blobs about the lattice, in spacings

    [[nodiscard]] cv::Point2f at(int column, int row) const {
        return origin + column_step * static_cast<float>(column) +
               row_step * static_cast<float>(row);
    }
};

// Least-squares affine through blob centres at known integer cells.
[[nodiscard]] Lattice fit_affine(const std::vector<cv::Point2f>& points,
                                 const std::vector<cv::Point>& cells, float pitch) {
    const int n = static_cast<int>(points.size());
    cv::Mat design(n, 3, CV_64F);
    cv::Mat target(n, 2, CV_64F);
    for (int i = 0; i < n; ++i) {
        const auto index = static_cast<std::size_t>(i);
        design.at<double>(i, 0) = cells[index].x;
        design.at<double>(i, 1) = cells[index].y;
        design.at<double>(i, 2) = 1.0;
        target.at<double>(i, 0) = points[index].x;
        target.at<double>(i, 1) = points[index].y;
    }
    cv::Mat solution;
    cv::solve(design, target, solution, cv::DECOMP_SVD);

    Lattice lattice;
    lattice.column_step = {static_cast<float>(solution.at<double>(0, 0)),
                           static_cast<float>(solution.at<double>(0, 1))};
    lattice.row_step = {static_cast<float>(solution.at<double>(1, 0)),
                        static_cast<float>(solution.at<double>(1, 1))};
    lattice.origin = {static_cast<float>(solution.at<double>(2, 0)),
                      static_cast<float>(solution.at<double>(2, 1))};
    lattice.blobs = points.size();

    double squared = 0.0;
    for (std::size_t i = 0; i < points.size(); ++i) {
        const cv::Point2f residual = lattice.at(cells[i].x, cells[i].y) - points[i];
        squared += static_cast<double>(residual.dot(residual));
    }
    lattice.rms =
        static_cast<float>(std::sqrt(squared / static_cast<double>(points.size()))) / pitch;
    return lattice;
}

// The best 3x3 lattice through the blobs: most blobs on it, then tightest.
// Each blob in turn seeds a group of similar-sized neighbours; the group's
// nearest-neighbour spacing and direction give a grid to snap its members to.
[[nodiscard]] std::optional<Lattice> fit_lattice(const std::vector<Blob>& blobs) {
    std::optional<Lattice> best;
    for (const Blob& seed : blobs) {
        std::vector<cv::Point2f> points;
        for (const Blob& blob : blobs) {
            const float ratio = blob.side / seed.side;
            const cv::Point2f offset = blob.centre - seed.centre;
            if (ratio > kMinSizeRatio && ratio < kMaxSizeRatio &&
                std::hypot(offset.x, offset.y) < kReachInSides * seed.side) {
                points.push_back(blob.centre);
            }
        }
        if (points.size() < kMinLatticeBlobs) {
            continue;
        }

        // Spacing and direction from each member's nearest neighbour. The
        // direction is folded onto a quarter-turn so that neighbours along
        // either axis, in either sense, vote for the same grid angle.
        std::vector<float> nearest_distance;
        double sum_sin = 0.0;
        double sum_cos = 0.0;
        for (std::size_t i = 0; i < points.size(); ++i) {
            float nearest = std::numeric_limits<float>::max();
            cv::Point2f step;
            for (std::size_t j = 0; j < points.size(); ++j) {
                if (i == j) {
                    continue;
                }
                const cv::Point2f offset = points[j] - points[i];
                const float distance = std::hypot(offset.x, offset.y);
                if (distance < nearest) {
                    nearest = distance;
                    step = offset;
                }
            }
            nearest_distance.push_back(nearest);
            const double folded =
                std::atan2(static_cast<double>(step.y), static_cast<double>(step.x)) * 4.0;
            sum_sin += std::sin(folded);
            sum_cos += std::cos(folded);
        }
        const float pitch = median_of(nearest_distance);
        if (pitch < 4.0F) {
            continue;
        }
        const double theta = std::atan2(sum_sin, sum_cos) / 4.0;
        const cv::Point2f axis_a(static_cast<float>(std::cos(theta)),
                                 static_cast<float>(std::sin(theta)));
        const cv::Point2f axis_b(-axis_a.y, axis_a.x);

        std::vector<cv::Point2f> on_grid;
        std::vector<cv::Point> cells;
        for (const cv::Point2f& point : points) {
            const cv::Point2f relative = point - points.front();
            const float a = relative.dot(axis_a) / pitch;
            const float b = relative.dot(axis_b) / pitch;
            const float cell_a = std::round(a);
            const float cell_b = std::round(b);
            if (std::abs(a - cell_a) < kMaxCellResidual &&
                std::abs(b - cell_b) < kMaxCellResidual) {
                on_grid.push_back(point);
                cells.emplace_back(static_cast<int>(cell_a), static_cast<int>(cell_b));
            }
        }
        if (on_grid.size() < kMinLatticeBlobs) {
            continue;
        }

        const auto [min_a, max_a] = std::minmax_element(
            cells.begin(), cells.end(), [](const auto& l, const auto& r) { return l.x < r.x; });
        const auto [min_b, max_b] = std::minmax_element(
            cells.begin(), cells.end(), [](const auto& l, const auto& r) { return l.y < r.y; });
        const cv::Point low(min_a->x, min_b->y);
        // Anything other than a full three-by-three span leaves the lattice's
        // position on the side unknown by a row or a column.
        if (max_a->x - low.x != 2 || max_b->y - low.y != 2) {
            continue;
        }
        std::array<bool, 9> taken{};
        bool duplicate = false;
        for (cv::Point& cell : cells) {
            cell -= low;
            bool& slot = taken[static_cast<std::size_t>(cell.y * 3 + cell.x)];
            duplicate = duplicate || slot;
            slot = true;
        }
        if (duplicate) {
            continue;
        }

        const Lattice candidate = fit_affine(on_grid, cells, pitch);
        if (!best.has_value() ||
            std::tuple(candidate.blobs, -candidate.rms) > std::tuple(best->blobs, -best->rms)) {
            best = candidate;
        }
    }
    return best;
}

[[nodiscard]] float length(const cv::Point2f& v) {
    return std::hypot(v.x, v.y);
}

// The nine centres row-major with cell 0 nearest the image's top-left: of
// the lattice's four quarter-turn labellings, the one whose rows run most
// nearly rightwards and whose columns run most nearly downwards.
[[nodiscard]] std::array<cv::Point2f, 9> upright_centres(const Lattice& lattice) {
    std::array<cv::Point2f, 9> best{};
    float best_score = -std::numeric_limits<float>::max();
    for (const auto& roll : kRoll) {
        std::array<cv::Point2f, 9> centres{};
        for (std::size_t i = 0; i < 9; ++i) {
            const int cell = roll[i];
            centres[i] = lattice.at(cell % 3, cell / 3);
        }
        const float score = (centres[2] - centres[0]).x + (centres[6] - centres[0]).y;
        if (score > best_score) {
            best_score = score;
            best = centres;
        }
    }
    return best;
}

// Median Lab over a square patch. Nothing if the patch falls off the frame.
[[nodiscard]] std::optional<cv::Vec3f> patch_lab(const cv::Mat& bgr, const cv::Point2f& centre,
                                                 int half_width) {
    const int x = static_cast<int>(std::lround(centre.x));
    const int y = static_cast<int>(std::lround(centre.y));
    const cv::Rect patch =
        cv::Rect(x - half_width, y - half_width, 2 * half_width + 1, 2 * half_width + 1) &
        cv::Rect(0, 0, bgr.cols, bgr.rows);
    if (patch.area() < 9) {
        return std::nullopt;
    }
    cv::Mat as_float;
    bgr(patch).convertTo(as_float, CV_32FC3, 1.0 / 255.0);
    cv::Mat lab;
    cv::cvtColor(as_float, lab, cv::COLOR_BGR2Lab);

    cv::Vec3f median;
    for (int channel = 0; channel < 3; ++channel) {
        std::vector<float> values;
        values.reserve(static_cast<std::size_t>(patch.area()));
        for (int row = 0; row < lab.rows; ++row) {
            for (int col = 0; col < lab.cols; ++col) {
                values.push_back(lab.at<cv::Vec3f>(row, col)[channel]);
            }
        }
        median[channel] = median_of(std::move(values));
    }
    return median;
}

// Lab distance with the shading-carried axes (lightness, chroma) weighted
// down and the hue residual kept whole.
[[nodiscard]] float colour_distance(const cv::Vec3f& p, const cv::Vec3f& q) {
    const float d_lightness = p[0] - q[0];
    const float d_chroma = std::hypot(p[1], p[2]) - std::hypot(q[1], q[2]);
    const float d_a = p[1] - q[1];
    const float d_b = p[2] - q[2];
    const float hue_squared = std::max(d_a * d_a + d_b * d_b - d_chroma * d_chroma, 0.0F);
    return std::sqrt(kShadingWeight * (d_lightness * d_lightness + d_chroma * d_chroma) +
                     hue_squared);
}

// Mean cell distance between a read and a side under the read's best
// quarter-turn, and that turn.
[[nodiscard]] std::pair<float, int> side_distance(const std::array<cv::Vec3f, 9>& read,
                                                  const std::array<cv::Vec3f, 9>& side) {
    float best = std::numeric_limits<float>::max();
    int best_roll = 0;
    for (int roll = 0; roll < 4; ++roll) {
        float total = 0.0F;
        for (std::size_t i = 0; i < 9; ++i) {
            total += colour_distance(
                read[static_cast<std::size_t>(kRoll[static_cast<std::size_t>(roll)][i])], side[i]);
        }
        if (total / 9.0F < best) {
            best = total / 9.0F;
            best_roll = roll;
        }
    }
    return {best, best_roll};
}

[[nodiscard]] std::array<cv::Vec3f, 9> rolled(const std::array<cv::Vec3f, 9>& read, int roll) {
    std::array<cv::Vec3f, 9> out{};
    for (std::size_t i = 0; i < 9; ++i) {
        out[i] = read[static_cast<std::size_t>(kRoll[static_cast<std::size_t>(roll)][i])];
    }
    return out;
}

// Per-cell, per-channel median over every read of a side.
[[nodiscard]] std::array<cv::Vec3f, 9> fuse(const std::vector<std::array<cv::Vec3f, 9>>& reads) {
    std::array<cv::Vec3f, 9> fused{};
    std::vector<float> values(reads.size());
    for (std::size_t cell = 0; cell < 9; ++cell) {
        for (int channel = 0; channel < 3; ++channel) {
            for (std::size_t i = 0; i < reads.size(); ++i) {
                values[i] = reads[i][cell][channel];
            }
            fused[cell][channel] = median_of(values);
        }
    }
    return fused;
}

using NamedSides = std::array<std::array<model::Facelet, 9>, 6>;

// Assembles the cube, letting physical validity settle which colour is which
// where colour alone cannot.
//
// resolve_colors sorts the 54 stickers into six groups reliably, but naming
// the groups rests on colour, and under a strong cast two of them can trade
// names: by tungsten light red and orange share a hue and differ mostly in
// lightness. A cube with two colours exchanged is never a valid cube - the
// pieces come out mirrored or do not exist - so a naming that fails to
// assemble is retried with each pair of names exchanged.
//
// Exchanging one opposite pair and exchanging a different opposite pair
// differ by a half-turn of the whole cube, so when the true error is an
// opposite pair, three exchanges assemble: the true one, and two that name a
// valid cube wrongly. The pair that really was confused is the pair that
// looks alike, so the exchange between the closest two colours wins.
[[nodiscard]] Result<solver::Cube> assemble(const NamedSides& named,
                                            const std::array<CapturedFace, 6>& faces) {
    const Result<solver::Cube> direct = solver::assemble_scanned_faces(named);
    if (direct.has_value()) {
        return direct;
    }

    Result<solver::Cube> best = direct;
    float best_distance = std::numeric_limits<float>::max();
    for (std::size_t a = 0; a < 6; ++a) {
        for (std::size_t b = a + 1; b < 6; ++b) {
            const float distance = colour_distance(faces[a].lab[4], faces[b].lab[4]);
            if (distance >= best_distance) {
                continue;
            }
            // Side a's centre carries one name and side b's the other.
            const model::FacetColor first = named[a][4].color;
            const model::FacetColor second = named[b][4].color;
            NamedSides exchanged = named;
            for (auto& side : exchanged) {
                for (model::Facelet& facelet : side) {
                    if (facelet.color == first) {
                        facelet.color = second;
                    } else if (facelet.color == second) {
                        facelet.color = first;
                    }
                }
            }
            if (Result<solver::Cube> cube = solver::assemble_scanned_faces(exchanged);
                cube.has_value()) {
                best = std::move(cube);
                best_distance = distance;
            }
        }
    }
    return best;
}

}  // namespace

std::optional<SideRead> read_side(const cv::Mat& bgr) {
    if (bgr.empty() || bgr.type() != CV_8UC3) {
        return std::nullopt;
    }
    cv::Mat working = bgr;
    float scale = 1.0F;
    if (bgr.cols > kWorkingWidth) {
        scale = static_cast<float>(bgr.cols) / static_cast<float>(kWorkingWidth);
        const int height = static_cast<int>(std::lround(static_cast<float>(bgr.rows) / scale));
        cv::resize(bgr, working, cv::Size(kWorkingWidth, height), 0.0, 0.0, cv::INTER_AREA);
    }

    const std::vector<Blob> blobs = sticker_blobs(working);
    if (blobs.size() < kMinLatticeBlobs) {
        return std::nullopt;
    }
    const std::optional<Lattice> lattice = fit_lattice(blobs);
    if (!lattice.has_value()) {
        return std::nullopt;
    }
    const float columns = length(lattice->column_step);
    const float rows = length(lattice->row_step);
    const float cosine = std::abs(lattice->column_step.dot(lattice->row_step)) / (columns * rows);
    if (columns / rows <= kMinAxisRatio || columns / rows >= kMaxAxisRatio ||
        cosine > kMaxAxisCosine || lattice->rms > kMaxLatticeRms) {
        return std::nullopt;
    }

    const float pitch = (columns + rows) / 2.0F;
    const int half_width = std::max(2, static_cast<int>(kPatchHalfWidth * pitch));
    const std::array<cv::Point2f, 9> centres = upright_centres(*lattice);

    SideRead read;
    read.pitch = pitch * scale;
    read.stickers_found = static_cast<int>(lattice->blobs);
    for (std::size_t i = 0; i < 9; ++i) {
        const std::optional<cv::Vec3f> lab = patch_lab(working, centres[i], half_width);
        if (!lab.has_value()) {
            return std::nullopt;
        }
        read.lab[i] = *lab;
        read.centres[i] = centres[i] * scale;
    }
    return read;
}

cv::Vec3b lab_to_bgr(const cv::Vec3f& lab) {
    const cv::Mat3f source(1, 1, lab);
    cv::Mat3f bgr;
    cv::cvtColor(source, bgr, cv::COLOR_Lab2BGR);
    const cv::Vec3f value = bgr(0, 0) * 255.0F;
    return {cv::saturate_cast<uchar>(value[0]), cv::saturate_cast<uchar>(value[1]),
            cv::saturate_cast<uchar>(value[2])};
}

void SideScan::reset() {
    groups_.clear();
    previous_centre_.reset();
    in_view_.reset();
}

void SideScan::add(const std::optional<SideRead>& read) {
    in_view_.reset();
    if (!read.has_value()) {
        previous_centre_.reset();
        return;
    }
    const cv::Point2f centre = read->centres[4];
    const bool steady = previous_centre_.has_value() &&
                        length(centre - *previous_centre_) < kSteadyFraction * read->pitch;
    previous_centre_ = centre;
    if (!steady) {
        return;
    }

    std::optional<std::size_t> nearest;
    float nearest_distance = std::numeric_limits<float>::max();
    int nearest_roll = 0;
    for (std::size_t i = 0; i < groups_.size(); ++i) {
        const auto [distance, roll] = side_distance(read->lab, groups_[i].fused);
        if (distance < nearest_distance) {
            nearest = i;
            nearest_distance = distance;
            nearest_roll = roll;
        }
    }
    if (nearest.has_value() && nearest_distance < kSameSideDistance) {
        Group& group = groups_[*nearest];
        group.reads.push_back(rolled(read->lab, nearest_roll));
        group.fused = fuse(group.reads);
        group.roll = nearest_roll;
        in_view_ = nearest;
    } else {
        groups_.push_back(Group{.fused = read->lab, .reads = {read->lab}, .roll = 0});
        in_view_ = groups_.size() - 1;
    }
}

std::vector<SideScan::Side> SideScan::sides() const {
    // The same six the cube is built from, so the display never shows a
    // stray capture in place of a side that is actually being used.
    std::vector<std::size_t> shown = captured_by_support();
    shown.resize(std::min<std::size_t>(shown.size(), 6));
    std::sort(shown.begin(), shown.end());

    std::vector<Side> out;
    for (const std::size_t i : shown) {
        const Group& group = groups_[i];
        // The fused read is stored in the orientation the side was first
        // seen in; turn it back to how it was last shown.
        Side side{.reads = group.reads.size(), .in_view = in_view_ == i};
        for (std::size_t cell = 0; cell < 9; ++cell) {
            side.lab[static_cast<std::size_t>(kRoll[static_cast<std::size_t>(group.roll)][cell])] =
                group.fused[cell];
        }
        out.push_back(side);
    }
    return out;
}

std::vector<std::size_t> SideScan::captured_by_support() const {
    std::vector<std::size_t> captured;
    for (std::size_t i = 0; i < groups_.size(); ++i) {
        if (groups_[i].reads.size() >= kMinReads) {
            captured.push_back(i);
        }
    }
    std::stable_sort(captured.begin(), captured.end(), [this](std::size_t l, std::size_t r) {
        return groups_[l].reads.size() > groups_[r].reads.size();
    });
    return captured;
}

Result<solver::Cube> SideScan::cube() const {
    const std::vector<std::size_t> captured = captured_by_support();
    if (captured.size() < 6) {
        return std::unexpected(CubeError::face_capture_incomplete);
    }
    // A stray group (a hand, a lattice fitted across two sides) has far
    // fewer reads than a side that was deliberately shown, so the six
    // best-supported groups are the six sides.
    std::array<CapturedFace, 6> faces{};
    for (std::size_t i = 0; i < 6; ++i) {
        faces[i].lab = groups_[captured[i]].fused;
    }
    return resolve_colors(faces).and_then(
        [&faces](const NamedSides& named) { return assemble(named, faces); });
}

}  // namespace cube::vision
