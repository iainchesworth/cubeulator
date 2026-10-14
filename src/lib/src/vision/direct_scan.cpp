#include <algorithm>
#include <cube/vision/direct_scan.hpp>

#include <cube/model/facelet.hpp>
#include <cube/profiling.hpp>
#include <cube/solver/scan_assembly.hpp>
#include <cube/vision/color_resolution.hpp>
#include <cube/vision/corner_fit.hpp>
#include <cube/vision/sticker_colour.hpp>

#include <opencv2/imgproc.hpp>

#include <cmath>
#include <cstdint>
#include <limits>
#include <tuple>
#include <utility>

namespace cube::vision {

namespace {

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

// A side found only by the second look in read_side (see narrow_contours) is
// accepted if its gaps are plastic: the twelve points midway between
// neighbouring stickers, at their 75th percentile (so a finger or a glint may
// cover two), no brighter than this fraction of its darkest sticker. The
// second look's outlines are the noisier, and over a curtain, a hand or a
// shirt they make lattices out of what is not a cube. Measured over the
// recorded sessions, it found 143 lattices on cubes in black plastic and 137
// of them had that ratio at 0.8 or under (half at 0.45 or under); in six
// sessions of a paper cube in front of curtains it found 421, and none was
// under 0.85 (the middle one was 1.8). The first look is not held to this:
// it is what it was.
constexpr float kMaxGapOverSticker = 0.8F;

// Half-width of the sampled patch, in sticker spacings: 0.6 of a spacing
// across, three quarters of a sticker, which keeps well inside its edge for a
// side up to 15% off square-on. It is wide on purpose. A logo is printed on
// the middle of a centre sticker (a Rubik's cube's white centre has a
// wordmark in four colours across it), and a patch of 0.44 spacings had
// little else in it: measured over 84 reads of such a side, the centre read
// 13 Lab units from the white stickers beside it, almost all of it
// lightness, where this patch and sticker_lab's robust estimate read 2.
constexpr float kPatchHalfWidth = 0.30F;

// Which side a read belongs to is decided by its CENTRE sticker, because the
// centre never moves and a cube's six centres are six distinct colours.
//
// This used to be a mean over all nine stickers of colour_distance, under
// 12. Two sides of one cube can differ in nothing but which of red and
// orange sits where - a partly solved cube has that - and under a warm cast
// red and orange differ almost only in lightness, the axis colour_distance
// counts for a quarter. Each sticker with red and orange traded scored 9.5;
// the two sides of the cube this was found on differed in three, 3 to 6 over
// the nine, so they were fused into one: its medians were a blend of both,
// the colours seemed to flip as either was shown, and the scan waited for a
// sixth side it had already swallowed. Their centres were 16 to 20 Lab units
// apart in plain Lab.

// Two stickers within this of each other, in plain Lab with lightness at full
// weight (it is the lightness that separates the closest pair), are one
// colour. Below kMinCentreSeedSeparationLab on purpose: resolve_colors will
// not name two centres closer than that, and it reports it. Grouping them as
// one side down to there would hide that report behind a scan that waits for
// a sixth side it is never given; held apart to just under it, they are two
// sides and the colour naming says why they cannot be told. Replayed over
// every recorded session the grouping is identical for any value from 8 to
// 12 and from 10 to 16 for the centre alone. The closest two centres on a
// real cube read 13.6 apart under tungsten light (live-demo-p6-retry) and 16
// to 20 apart under the lamp of the cube this was found on.
constexpr float kSameColourLab = 10.0F;
static_assert(kSameColourLab < kMinCentreSeedSeparationLab,
              "two centres resolve_colors refuses as one colour must not be one side here");

// A read's centre within kSameColourLab of a side's is that side, however
// much of it a hand covers. Beyond that the centre cannot vouch - it may be
// glare on this side's centre or another side's - and the other eight
// stickers must: every one within kSameColourLab of the side's. Two
// look-alike sides can differ in nothing but the centre and one sticker, and
// allowing a sticker to differ joins them; replayed on the pair this was
// found on that left a margin of 1.3 Lab, against 7.7 holding all eight to
// the match. The cost is a stray group now and then when a finger covers a
// sticker while the centre is read through glare, and a stray is not a side
// until it is held for kMinReads.
//
// But a centre that matches is not the end of it. Two sides can look the
// same colour at the centre - orange lit to read as red - and then it is
// the rest that says they are two. A hand can leave no more than this many
// of the eight stickers other than the centre unlike the side's: the reader
// needs six of the nine in the lattice, so at most three are covered, and one
// more is noise. More than that is another side whatever its centre reads.
constexpr int kMostStickersAHandCovers = 4;

// Cost of laying a read on a side is the summed distance of the eight
// stickers other than the centre, each capped so one finger cannot decide a
// quarter-turn.
constexpr float kRollCostCap = 2.0F * kSameColourLab;

// A side keeps the quarter-turn it last had unless another explains the
// read by more than one differing sticker's worth. A held side's pattern is
// the same from frame to frame; without this its turn flapped between
// nearly-tied choices and the median fused them.
constexpr float kRollPriorMargin = kSameColourLab;

// Cubes that six flat sides make which are weighed against the corner reads.
// More than this is not chosen among (see assemble).
constexpr std::size_t kMaxCandidates = 256;

// Groups kept. A read that is no side seen so far starts a group of its own,
// and every read is compared with every group, so a scene full of lattices
// that are not a cube (a chessboard, a patterned shirt) must not grow them
// without bound. Six are sides; the rest are strays that rarely reach two
// reads. When there are more than this the least-supported stray goes.
constexpr std::size_t kMaxGroups = 24;

// Lightness and chroma carry shading; hue does not. Down-weighting the
// first two keeps a shaded sticker close to its own colour.
constexpr float kShadingWeight = 0.25F;

// (kMinCornerFound and kMinCornerAspect, the gates on which side of a corner
// view counts as a read, are in direct_scan.hpp.)

struct Blob {
    cv::Point2f centre;
    float side = 0.0F;
};

// kRoll[k][i] is the cell of a read that lands at cell i once the read is
// turned k quarter-turns.
constexpr std::array<std::array<int, 9>, 4> kRoll = {
    {
        {0, 1, 2, 3, 4, 5, 6, 7, 8},
        {2, 5, 8, 1, 4, 7, 0, 3, 6},
        {8, 7, 6, 5, 4, 3, 2, 1, 0},
        {6, 3, 0, 7, 4, 1, 8, 5, 2},
    },
};

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

// Bright, compact, squarish regions bounded by darker plastic: the outlines
// prepare_frame found, kept when they are the size and shape of a sticker.
[[nodiscard]] std::vector<Blob> sticker_blobs(const std::vector<std::vector<cv::Point>>& contours,
                                              const cv::Mat& working) {
    const double frame_area = static_cast<double>(working.rows) * static_cast<double>(working.cols);
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
        blobs.push_back(Blob{.centre = rect.center, .side = std::sqrt(shorter * longer)});
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
    // Which cells (row * 3 + column) have a blob on them.
    std::array<bool, 9> found{};

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
        design.at<double>(i, 0) = cells.at(index).x;
        design.at<double>(i, 1) = cells.at(index).y;
        design.at<double>(i, 2) = 1.0;
        target.at<double>(i, 0) = static_cast<double>(points.at(index).x);
        target.at<double>(i, 1) = static_cast<double>(points.at(index).y);
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
        const cv::Point2f residual = lattice.at(cells.at(i).x, cells.at(i).y) - points.at(i);
        squared += static_cast<double>(residual.dot(residual));
    }
    lattice.rms =
        static_cast<float>(std::sqrt(squared / static_cast<double>(points.size()))) / pitch;
    return lattice;
}

// The blobs of a size and distance near enough to `seed` to be on its side.
[[nodiscard]] std::vector<cv::Point2f> neighbours_of(const std::vector<Blob>& blobs,
                                                     const Blob& seed) {
    std::vector<cv::Point2f> points;
    for (const Blob& blob : blobs) {
        const float ratio = blob.side / seed.side;
        const cv::Point2f offset = blob.centre - seed.centre;
        if (ratio > kMinSizeRatio && ratio < kMaxSizeRatio &&
            std::hypot(offset.x, offset.y) < kReachInSides * seed.side) {
            points.push_back(blob.centre);
        }
    }
    return points;
}

// A grid's spacing and the direction of its axes.
struct GridFrame {
    float pitch = 0.0F;
    cv::Point2f axis_a;
    cv::Point2f axis_b;
};

// Spacing and direction from each member's nearest neighbour. The direction
// is folded onto a quarter-turn so that neighbours along either axis, in
// either sense, vote for the same grid angle. Nothing when the spacing is too
// small to be a side's stickers.
[[nodiscard]] std::optional<GridFrame> grid_frame(const std::vector<cv::Point2f>& points) {
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
            const cv::Point2f offset = points.at(j) - points.at(i);
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
        return std::nullopt;
    }
    const double theta = std::atan2(sum_sin, sum_cos) / 4.0;
    GridFrame frame;
    frame.pitch = pitch;
    frame.axis_a =
        cv::Point2f(static_cast<float>(std::cos(theta)), static_cast<float>(std::sin(theta)));
    frame.axis_b = cv::Point2f(-frame.axis_a.y, frame.axis_a.x);
    return frame;
}

// The points that sit on the frame's grid, with the cell each one snaps to.
struct SnappedPoints {
    std::vector<cv::Point2f> on_grid;
    std::vector<cv::Point> cells;
};

[[nodiscard]] SnappedPoints snap_to_grid(const std::vector<cv::Point2f>& points,
                                         const GridFrame& frame) {
    SnappedPoints snapped;
    for (const cv::Point2f& point : points) {
        const cv::Point2f relative = point - points.front();
        const float a = relative.dot(frame.axis_a) / frame.pitch;
        const float b = relative.dot(frame.axis_b) / frame.pitch;
        const float cell_a = std::round(a);
        const float cell_b = std::round(b);
        if (std::abs(a - cell_a) < kMaxCellResidual && std::abs(b - cell_b) < kMaxCellResidual) {
            snapped.on_grid.push_back(point);
            snapped.cells.emplace_back(static_cast<int>(cell_a), static_cast<int>(cell_b));
        }
    }
    return snapped;
}

// Moves `cells` so the lattice starts at (0, 0) and returns which of the nine
// cells are taken - or nothing unless they span exactly three by three with no
// cell used twice. Anything other than a full three-by-three span leaves the
// lattice's position on the side unknown by a row or a column.
[[nodiscard]] std::optional<std::array<bool, 9>> normalise_cells(std::vector<cv::Point>& cells) {
    const auto [min_a, max_a] =
        std::ranges::minmax_element(cells, [](const auto& l, const auto& r) { return l.x < r.x; });
    const auto [min_b, max_b] =
        std::ranges::minmax_element(cells, [](const auto& l, const auto& r) { return l.y < r.y; });
    const cv::Point low(min_a->x, min_b->y);
    if (max_a->x - low.x != 2 || max_b->y - low.y != 2) {
        return std::nullopt;
    }
    std::array<bool, 9> taken{};
    bool duplicate = false;
    for (cv::Point& cell : cells) {
        cell -= low;
        bool& slot =
            taken.at((static_cast<std::size_t>(cell.y) * 3) + static_cast<std::size_t>(cell.x));
        duplicate = duplicate || slot;
        slot = true;
    }
    if (duplicate) {
        return std::nullopt;
    }
    return taken;
}

[[nodiscard]] float length(const cv::Point2f& v) {
    return std::hypot(v.x, v.y);
}

// Whether a lattice is a side held square to the camera: its two steps
// near-equal and near-perpendicular, and the blobs sitting on it.
[[nodiscard]] bool is_square_on(const Lattice& lattice) {
    const float columns = length(lattice.column_step);
    const float rows = length(lattice.row_step);
    const float cosine = std::abs(lattice.column_step.dot(lattice.row_step)) / (columns * rows);
    return columns / rows > kMinAxisRatio && columns / rows < kMaxAxisRatio &&
           cosine <= kMaxAxisCosine && lattice.rms <= kMaxLatticeRms;
}

// The origin and steps of the affine map from cell to pixels that is the least
// squares fit through `points` at `cells`. Closed form, nothing allocated:
// this is tried for every seed of a frame that has no side square-on, where
// fit_affine's matrices would be a handful of buffers each time. The normal
// equations of integer cells have an integer determinant, so a singular
// choice of cells (all in one row) is exactly zero and no tolerance is needed.
struct Affine {
    cv::Point2f origin;
    cv::Point2f column_step;
    cv::Point2f row_step;
};

[[nodiscard]] std::optional<Affine> solve_affine(const std::vector<cv::Point2f>& points,
                                                 const std::vector<cv::Point>& cells) {
    // Sums of the design row (column, row, 1) with itself and with the point.
    double cc = 0.0;
    double cr = 0.0;
    double c1 = 0.0;
    double rr = 0.0;
    double r1 = 0.0;
    double one = 0.0;
    std::array<double, 3> bx{};
    std::array<double, 3> by{};
    for (std::size_t i = 0; i < points.size(); ++i) {
        const double c = cells.at(i).x;
        const double r = cells.at(i).y;
        const double x = static_cast<double>(points.at(i).x);
        const double y = static_cast<double>(points.at(i).y);
        cc += c * c;
        cr += c * r;
        c1 += c;
        rr += r * r;
        r1 += r;
        one += 1.0;
        bx.at(0) += c * x;
        bx.at(1) += r * x;
        bx.at(2) += x;
        by.at(0) += c * y;
        by.at(1) += r * y;
        by.at(2) += y;
    }
    using Matrix = std::array<std::array<double, 3>, 3>;
    const Matrix normal = {{
        {cc, cr, c1},
        {cr, rr, r1},
        {c1, r1, one},
    }};
    const auto determinant = [](const Matrix& m) {
        const auto entry = [&m](std::size_t row, std::size_t column) {
            return m.at(row).at(column);
        };
        return (entry(0, 0) * ((entry(1, 1) * entry(2, 2)) - (entry(1, 2) * entry(2, 1)))) -
               (entry(0, 1) * ((entry(1, 0) * entry(2, 2)) - (entry(1, 2) * entry(2, 0)))) +
               (entry(0, 2) * ((entry(1, 0) * entry(2, 1)) - (entry(1, 1) * entry(2, 0))));
    };
    const double det = determinant(normal);
    if (std::abs(det) < 0.5) {
        return std::nullopt;
    }
    // Cramer's rule: column `which` of the normal matrix replaced by `b`.
    const auto solved = [&](const std::array<double, 3>& b, std::size_t which) {
        Matrix m = normal;
        for (std::size_t row = 0; row < 3; ++row) {
            m.at(row).at(which) = b.at(row);
        }
        return determinant(m) / det;
    };
    const auto at = [&](std::size_t which) {
        return cv::Point2f(static_cast<float>(solved(bx, which)),
                           static_cast<float>(solved(by, which)));
    };
    return Affine{.origin = at(2), .column_step = at(0), .row_step = at(1)};
}

// The most cells either way that a neighbour set is placed through its own
// lattice, from the first neighbour: the span of a side is 3, and a seed sees a
// little more than one.
constexpr int kMostCellsAway = 6;

// The two steps of a fitted lattice must be this far from the same line (the
// sine of the angle between them): a fit through points in a near-line has no
// second axis to place the others by.
constexpr float kMinAxesSine = 0.5F;

// Fewest points already on the square grid from which the lattice they make
// is trusted to place the others: two steps and an offset, with one to spare.
constexpr std::size_t kMinPlacedFrom = 4;

// Fewest blobs on a lattice placed through its own fit. The square grid is a
// strong prior and six of nine is enough for it; an affine fit through six
// points has no point left over to disagree, so a seed that is not a side can
// always be fitted. With a finger over one sticker (eight of nine) it has ten
// equations for its six unknowns. Of the reads a first version of this gained
// on the recorded sessions the nine- and eight-sticker ones were all the
// cube's sides, and two of the three with six or seven were not (21 and 34 Lab
// units from every side the scan held).
constexpr std::size_t kMinPlacedBlobs = 8;

// The 3x3 windows, of the lattice that the neighbours already on the square
// grid make, that hold at least kMinPlacedBlobs of all the neighbours.
//
// snap_to_grid lays a square grid at the nearest-neighbour spacing. A side
// turned about one axis is not square: its steps along the turned axis are
// shorter, and a cell two steps from the first neighbour sits 2/ratio spacings
// out, past the 0.3 a cell may be off at a ratio of 0.87 - inside the 0.85 the
// reader is documented to take, and beyond that earlier still once the steps
// narrow with perspective, or the side is sheared (a cosine of 0.15 is inside
// the gate and a third of a cell off by the third column). Placed through the
// lattice the points themselves make, a cell is where the lattice puts it,
// whatever the ratio and the shear.
//
// A blob beside the side that does sit on the lattice, a sticker of the side
// next to it or a stray in the room, lands in a cell outside the window and is
// left out; as part of the set it made the span more than three and the whole
// seed was dropped. Two sides that make nearly one lattice have a window on
// each, and some across their edge: they are all returned, for the caller to
// choose among.
//
// Nothing when there is too little on the grid to fit, or the points it has
// lie in a line.
[[nodiscard]] std::vector<SnappedPoints> windows_through_fit(const std::vector<cv::Point2f>& points,
                                                             const SnappedPoints& on_grid) {
    if (on_grid.on_grid.size() < kMinPlacedFrom) {
        return {};
    }
    const std::optional<Affine> fit = solve_affine(on_grid.on_grid, on_grid.cells);
    if (!fit) {
        return {};
    }
    const float det =
        (fit->column_step.x * fit->row_step.y) - (fit->column_step.y * fit->row_step.x);
    const float columns = std::hypot(fit->column_step.x, fit->column_step.y);
    const float rows = std::hypot(fit->row_step.x, fit->row_step.y);
    if (columns < 1.0F || rows < 1.0F || std::abs(det) < kMinAxesSine * columns * rows) {
        return {};
    }

    // Every neighbour at the cell the fit puts it, if it is on one.
    std::vector<cv::Point2f> placed_points;
    std::vector<cv::Point> placed_cells;
    for (const cv::Point2f& point : points) {
        const cv::Point2f offset = point - fit->origin;
        const float u = ((offset.x * fit->row_step.y) - (offset.y * fit->row_step.x)) / det;
        const float v = ((fit->column_step.x * offset.y) - (fit->column_step.y * offset.x)) / det;
        const float cell_u = std::round(u);
        const float cell_v = std::round(v);
        if (std::abs(u - cell_u) < kMaxCellResidual && std::abs(v - cell_v) < kMaxCellResidual &&
            std::abs(cell_u) <= static_cast<float>(kMostCellsAway) &&
            std::abs(cell_v) <= static_cast<float>(kMostCellsAway)) {
            placed_points.push_back(point);
            placed_cells.emplace_back(static_cast<int>(cell_u), static_cast<int>(cell_v));
        }
    }
    if (placed_points.size() < kMinPlacedBlobs) {
        return {};
    }

    const auto [min_u, max_u] = std::ranges::minmax_element(
        placed_cells, [](const auto& l, const auto& r) { return l.x < r.x; });
    const auto [min_v, max_v] = std::ranges::minmax_element(
        placed_cells, [](const auto& l, const auto& r) { return l.y < r.y; });
    std::vector<SnappedPoints> windows;
    for (int u0 = min_u->x; u0 <= max_u->x - 2; ++u0) {
        for (int v0 = min_v->y; v0 <= max_v->y - 2; ++v0) {
            SnappedPoints window;
            for (std::size_t i = 0; i < placed_points.size(); ++i) {
                const cv::Point& cell = placed_cells.at(i);
                if (cell.x >= u0 && cell.x < u0 + 3 && cell.y >= v0 && cell.y < v0 + 3) {
                    window.on_grid.push_back(placed_points.at(i));
                    window.cells.push_back(cell);
                }
            }
            if (window.on_grid.size() >= kMinPlacedBlobs) {
                windows.push_back(std::move(window));
            }
        }
    }
    return windows;
}

// The best 3x3 lattice through the blobs: most blobs on it, then tightest.
// Each blob in turn seeds a group of similar-sized neighbours; the group's
// nearest-neighbour spacing and direction give a grid to snap its members to.
[[nodiscard]] std::optional<Lattice> fit_lattice(const std::vector<Blob>& blobs) {
    std::optional<Lattice> best;
    for (const Blob& seed : blobs) {
        const std::vector<cv::Point2f> points = neighbours_of(blobs, seed);
        if (points.size() < kMinLatticeBlobs) {
            continue;
        }
        const std::optional<GridFrame> frame = grid_frame(points);
        if (!frame) {
            continue;
        }
        SnappedPoints snapped = snap_to_grid(points, *frame);
        if (snapped.on_grid.size() < kMinLatticeBlobs) {
            continue;
        }
        const std::optional<std::array<bool, 9>> taken = normalise_cells(snapped.cells);
        if (!taken) {
            continue;
        }

        Lattice candidate = fit_affine(snapped.on_grid, snapped.cells, frame->pitch);
        candidate.found = *taken;
        if (!best.has_value() ||
            std::tuple(candidate.blobs, -candidate.rms) > std::tuple(best->blobs, -best->rms)) {
            best = candidate;
        }
    }
    return best;
}
// A lattice, and the blobs that make it.
struct Candidate {
    Lattice lattice;
    std::vector<cv::Point2f> members;
};

[[nodiscard]] bool shares_a_blob(const std::vector<cv::Point2f>& a,
                                 const std::vector<cv::Point2f>& b) {
    return std::ranges::any_of(
        a, [&b](const cv::Point2f& point) { return std::ranges::find(b, point) != b.end(); });
}

// The lattice for a frame in which fit_lattice found no side held square: the
// best that the neighbours of any seed make when they are placed through the
// lattice they themselves make (windows_through_fit) as well as on the square
// grid, and that passes is_square_on.
//
// Candidates are weighed as fit_lattice weighs them, the most blobs and then
// the tightest, and a candidate is looked at only if it shares no blob with
// one that was weighed before it and was refused. That is the point of it: a
// lattice that fits better than this one, from the same blobs, and is not a
// side held square, says these blobs are not one. A window of two columns of
// the near side and the merged column of a much thinner side beside it passed
// the gate on its own (ratio 0.852); the near side it was part of, at 0.80,
// did not, and fitted tighter.
[[nodiscard]] std::optional<Lattice> fit_lattice_through_fit(const std::vector<Blob>& blobs) {
    std::vector<Candidate> candidates;
    const auto consider = [&candidates](SnappedPoints set, float pitch) {
        if (set.on_grid.size() < kMinLatticeBlobs) {
            return;
        }
        // The same blobs as a lattice already weighed: seeds on one side
        // find it again, each from its own neighbours.
        const auto same = [&set](const Candidate& known) {
            return known.members.size() == set.on_grid.size() &&
                   std::ranges::all_of(set.on_grid, [&known](const cv::Point2f& point) {
                       return std::ranges::find(known.members, point) != known.members.end();
                   });
        };
        if (std::ranges::any_of(candidates, same)) {
            return;
        }
        const std::vector<cv::Point2f> members = set.on_grid;
        const std::optional<std::array<bool, 9>> taken = normalise_cells(set.cells);
        if (!taken) {
            return;
        }
        Lattice lattice = fit_affine(set.on_grid, set.cells, pitch);
        lattice.found = *taken;
        candidates.push_back(Candidate{.lattice = lattice, .members = members});
    };

    for (const Blob& seed : blobs) {
        const std::vector<cv::Point2f> points = neighbours_of(blobs, seed);
        if (points.size() < kMinLatticeBlobs) {
            continue;
        }
        const std::optional<GridFrame> frame = grid_frame(points);
        if (!frame) {
            continue;
        }
        const SnappedPoints snapped = snap_to_grid(points, *frame);
        for (SnappedPoints& window : windows_through_fit(points, snapped)) {
            consider(std::move(window), frame->pitch);
        }
        consider(snapped, frame->pitch);
    }

    std::ranges::sort(candidates, [](const Candidate& l, const Candidate& r) {
        return std::tuple(r.lattice.blobs, l.lattice.rms) <
               std::tuple(l.lattice.blobs, r.lattice.rms);
    });
    std::vector<cv::Point2f> weighed;
    for (const Candidate& candidate : candidates) {
        if (shares_a_blob(candidate.members, weighed)) {
            continue;
        }
        weighed.insert(weighed.end(), candidate.members.begin(), candidate.members.end());
        if (is_square_on(candidate.lattice)) {
            return candidate.lattice;
        }
    }
    return std::nullopt;
}

// The nine centres row-major with cell 0 nearest the image's top-left: of
// the lattice's four quarter-turn labellings, the one whose rows run most
// nearly rightwards and whose columns run most nearly downwards.
struct Upright {
    std::array<cv::Point2f, 9> centres{};
    // Whether each of those centres had a blob on it.
    std::array<bool, 9> found{};
};

[[nodiscard]] Upright upright_centres(const Lattice& lattice) {
    Upright best;
    float best_score = -std::numeric_limits<float>::max();
    for (const auto& roll : kRoll) {
        Upright upright;
        for (std::size_t i = 0; i < 9; ++i) {
            const int cell = roll.at(i);
            upright.centres.at(i) = lattice.at(cell % 3, cell / 3);
            upright.found.at(i) = lattice.found.at(static_cast<std::size_t>(cell));
        }
        const float score = (upright.centres.at(2) - upright.centres.at(0)).x +
                            (upright.centres.at(6) - upright.centres.at(0)).y;
        if (score > best_score) {
            best_score = score;
            best = upright;
        }
    }
    return best;
}

[[nodiscard]] std::array<cv::Vec3f, 9> rolled(const std::array<cv::Vec3f, 9>& read, int roll) {
    std::array<cv::Vec3f, 9> out{};
    for (std::size_t i = 0; i < 9; ++i) {
        out.at(i) =
            read.at(static_cast<std::size_t>(kRoll.at(static_cast<std::size_t>(roll)).at(i)));
    }
    return out;
}

// Plain Lab distance, lightness at full weight. colour_distance is for
// sticker colours that shading has moved; here two colours that differ only
// in lightness must still be two.
[[nodiscard]] float lab_gap(const cv::Vec3f& a, const cv::Vec3f& b) {
    const cv::Vec3f difference = a - b;
    return std::sqrt(difference.dot(difference));
}

// How a read lays on a side, and whether it is that side.
struct SideFit {
    int roll = 0;
    // Summed distance of the eight stickers other than the centre.
    float cost = 0.0F;
    bool same = false;
};

// Lays `read` on `side` under whichever quarter-turn fits best, `prior_roll`
// winning any near-tie, and judges whether the two are one side.
//
// The quarter-turn is chosen from the eight stickers other than the centre,
// which every turn leaves where it was, in plain Lab with each capped: a
// finger over one sticker must not turn the side. It was chosen by
// colour_distance, which barely sees red against orange, so a red and orange
// pattern was laid down at whichever turn the noise preferred.
//
// Then the judgement. The centre is the side's identity (see
// kSameColourLab). When it agrees the read is the side, however many
// stickers a hand covers (up to kMostStickersAHandCovers). When it does not,
// it may be glare on the centre or another side; the other eight say which.
struct FitInputs {
    std::array<cv::Vec3f, 9> read;
    std::array<cv::Vec3f, 9> side;
    int prior_roll = 0;
};

[[nodiscard]] SideFit fit_to_side(const FitInputs& inputs) {
    const std::array<cv::Vec3f, 9>& read = inputs.read;
    const std::array<cv::Vec3f, 9>& side = inputs.side;
    const int prior_roll = inputs.prior_roll;
    const auto cost_of = [&](int roll) {
        float cost = 0.0F;
        const std::array<cv::Vec3f, 9> turned = rolled(read, roll);
        for (std::size_t i = 0; i < 9; ++i) {
            if (i != 4) {
                cost += std::min(lab_gap(turned.at(i), side.at(i)), kRollCostCap);
            }
        }
        return cost;
    };

    SideFit fit{.roll = 0, .cost = std::numeric_limits<float>::max()};
    for (int roll = 0; roll < 4; ++roll) {
        const float cost = cost_of(roll);
        if (cost < fit.cost) {
            fit.roll = roll;
            fit.cost = cost;
        }
    }
    if (const float prior_cost = cost_of(prior_roll); prior_cost <= fit.cost + kRollPriorMargin) {
        fit.roll = prior_roll;
        fit.cost = prior_cost;
    }

    const std::array<cv::Vec3f, 9> turned = rolled(read, fit.roll);
    int differing = 0;
    for (std::size_t i = 0; i < 9; ++i) {
        if (i != 4 && lab_gap(turned.at(i), side.at(i)) > kSameColourLab) {
            ++differing;
        }
    }
    if (differing > kMostStickersAHandCovers) {
        fit.same = false;
    } else if (lab_gap(turned.at(4), side.at(4)) <= kSameColourLab) {
        fit.same = true;
    } else {
        fit.same = differing == 0;
    }
    return fit;
}

// Per-cell, per-channel median over every read of a side.
[[nodiscard]] std::array<cv::Vec3f, 9> fuse(const std::vector<std::array<cv::Vec3f, 9>>& reads) {
    std::array<cv::Vec3f, 9> fused{};
    std::vector<float> values(reads.size());
    for (std::size_t cell = 0; cell < 9; ++cell) {
        for (int channel = 0; channel < 3; ++channel) {
            for (std::size_t i = 0; i < reads.size(); ++i) {
                values.at(i) = reads.at(i).at(cell)(channel);
            }
            fused.at(cell)(channel) = median_of(values);
        }
    }
    return fused;
}

// Whether a read taken at an angle is one of `groups` lit differently: its nine
// colours are those of the side under some quarter-turn, as colour_distance
// counts them - lightness and chroma, which shading moves, for a quarter of what
// hue is. That is deliberately the loose measure: it is asked only whether a
// read that fits no side is nevertheless one of them, and when it is not sure it
// says yes (two look-alike sides, red and orange under a warm lamp, are one
// here); the read is then not counted, and the side is begun by a read
// square-on, where the strict fit_to_side tells them apart. A template, for the
// group type is the scan's own and private.
template <typename Groups>
[[nodiscard]] bool is_shaded_view_of_any(const std::array<cv::Vec3f, 9>& read,
                                         const Groups& groups) {
    return std::ranges::any_of(groups, [&read](const auto& group) {
        for (int roll = 0; roll < 4; ++roll) {
            const std::array<cv::Vec3f, 9> turned = rolled(read, roll);
            float total = 0.0F;
            for (std::size_t i = 0; i < 9; ++i) {
                total += colour_distance(turned.at(i), group.fused.at(i));
            }
            if (total / 9.0F <= kCornerFitDistance) {
                return true;
            }
        }
        return false;
    });
}

using NamedSides = std::array<std::array<model::Facelet, 9>, 6>;

[[nodiscard]] std::optional<solver::Color> solver_colour(model::FacetColor colour) {
    switch (colour) {
        case model::FacetColor::white:
            return solver::Color::White;
        case model::FacetColor::yellow:
            return solver::Color::Yellow;
        case model::FacetColor::red:
            return solver::Color::Red;
        case model::FacetColor::orange:
            return solver::Color::Orange;
        case model::FacetColor::green:
            return solver::Color::Green;
        case model::FacetColor::blue:
            return solver::Color::Blue;
        case model::FacetColor::unknown:
            break;
    }
    return std::nullopt;
}

// What each colour measured, given the naming that made the cube: the
// per-channel median over every sticker given that name.
[[nodiscard]] SessionColours colours_of(const NamedSides& named,
                                        const std::array<CapturedFace, 6>& faces) {
    std::array<std::array<std::vector<float>, 3>, 6> samples;
    for (std::size_t side = 0; side < 6; ++side) {
        for (std::size_t cell = 0; cell < 9; ++cell) {
            const auto colour = solver_colour(named.at(side).at(cell).color);
            if (!colour.has_value()) {
                continue;
            }
            for (std::size_t channel = 0; channel < 3; ++channel) {
                samples.at(static_cast<std::size_t>(*colour))
                    .at(channel)
                    .push_back(faces.at(side).lab.at(cell)(static_cast<int>(channel)));
            }
        }
    }
    SessionColours colours{};
    for (std::size_t colour = 0; colour < 6; ++colour) {
        for (std::size_t channel = 0; channel < 3; ++channel) {
            if (!samples.at(colour).at(channel).empty()) {
                colours.at(colour)(static_cast<int>(channel)) =
                    median_of(samples.at(colour).at(channel));
            }
        }
    }
    return colours;
}

// How the corner reads of a scan vote among the cubes six sides make.
struct CornerVotes {
    // How many reads fit each candidate.
    std::vector<std::size_t> fits;
    // For each candidate, the weight of the reads that fit some candidate and
    // not this one. A read that fits none is a blurred frame, a lattice laid
    // across two sides or a side lit past the colours of the scan: it votes for
    // no one and against no one. A read of three sides tests the three edges
    // between them at once, and weighs as many as it has.
    std::vector<std::size_t> misses;
};

[[nodiscard]] CornerVotes corner_votes(const std::vector<solver::Cube>& candidates,
                                       const std::vector<CornerRead>& corners,
                                       const SessionColours& colours) {
    CornerVotes votes{
        .fits = std::vector<std::size_t>(candidates.size(), 0),
        .misses = std::vector<std::size_t>(candidates.size(), 0),
    };
    std::vector<bool> fitted(candidates.size(), false);
    for (const CornerRead& read : corners) {
        std::size_t any = 0;
        for (std::size_t i = 0; i < candidates.size(); ++i) {
            fitted.at(i) = fit_corner(read, candidates.at(i), colours).fits;
            any += fitted.at(i) ? 1U : 0U;
            votes.fits.at(i) += fitted.at(i) ? 1U : 0U;
        }
        if (any == 0) {
            continue;
        }
        const std::size_t weight = std::max<std::size_t>(1, read.edges.size());
        for (std::size_t i = 0; i < candidates.size(); ++i) {
            votes.misses.at(i) += fitted.at(i) ? 0U : weight;
        }
    }
    return votes;
}

// The candidate among several that the corner reads say it is, if they do: the
// one that fits every read that fits any cube, or all but one edge's worth of
// them, and that the next misses at least two edges more of. Every cube the
// sides make agrees with the true one on the sides a read shows unless the read
// is of the stickers they differ on, so most reads fit most candidates; it is
// the few that differ that decide, and one of them is not enough to decide on:
// by the time the sixth side was in, seven views and a candidate that fitted
// three of them was the whole of the evidence on one recorded scan, and it was
// not the cube. A tie says nothing.
constexpr std::size_t kMostMissesOfTheTrueCube = 1;
constexpr std::size_t kMarginOfMisses = 2;

[[nodiscard]] std::optional<std::size_t> choose_by_corners(const std::vector<std::size_t>& misses) {
    if (misses.empty()) {
        return std::nullopt;
    }
    const auto best = std::ranges::min_element(misses);
    if (*best > kMostMissesOfTheTrueCube) {
        return std::nullopt;
    }
    const auto chosen = static_cast<std::size_t>(std::distance(misses.begin(), best));
    std::size_t runner_up = std::numeric_limits<std::size_t>::max();
    for (std::size_t i = 0; i < misses.size(); ++i) {
        if (i != chosen) {
            runner_up = std::min(runner_up, misses.at(i));
        }
    }
    if (runner_up == std::numeric_limits<std::size_t>::max() ||
        runner_up < *best + kMarginOfMisses) {
        return std::nullopt;
    }
    return chosen;
}

// The candidates the corner reads miss least: every one when none is missed
// more than another (or there is nothing to go by).
[[nodiscard]] std::vector<const solver::Cube*> best_fitting(
    const std::vector<solver::Cube>& candidates, const std::vector<std::size_t>& misses) {
    std::size_t fewest = std::numeric_limits<std::size_t>::max();
    for (const std::size_t count : misses) {
        fewest = std::min(fewest, count);
    }
    std::vector<const solver::Cube*> remaining;
    for (std::size_t i = 0; i < candidates.size(); ++i) {
        if (i >= misses.size() || misses.at(i) == fewest) {
            remaining.push_back(&candidates.at(i));
        }
    }
    return remaining;
}

// How many pairs of `cubes` differ on a side of `corner`.
[[nodiscard]] std::size_t pairs_told_apart(const std::vector<const solver::Cube*>& cubes,
                                           const std::array<solver::Face, 3>& corner) {
    std::size_t told = 0;
    for (std::size_t a = 0; a < cubes.size(); ++a) {
        for (std::size_t b = a + 1; b < cubes.size(); ++b) {
            const bool differ = std::ranges::any_of(corner, [&](solver::Face face) {
                return cubes.at(a)->face(face) != cubes.at(b)->face(face);
            });
            told += differ ? 1U : 0U;
        }
    }
    return told;
}

// The corner of the cube whose view tells the most of the cubes still possible
// apart: of the candidates, those the corner reads miss least (all of them when
// none fits), and of the eight corners the one at which the most pairs of them
// differ on a side the corner shows. A corner shows all nine stickers of each of
// its three sides, so two cubes are told apart there when any of those three
// sides is not the same on both. The centres are the same on every candidate
// (they are what names the sides), so the corner is said by their colours.
[[nodiscard]] std::optional<std::array<solver::Color, 3>> settling_corner_of(
    const std::vector<solver::Cube>& candidates, const std::vector<std::size_t>& misses) {
    const std::vector<const solver::Cube*> remaining = best_fitting(candidates, misses);
    if (remaining.size() < 2) {
        return std::nullopt;
    }

    using solver::Face;
    std::size_t best_told = 0;
    std::optional<std::array<Face, 3>> best;
    for (const Face vertical : {Face::U, Face::D}) {
        for (const Face depth : {Face::F, Face::B}) {
            for (const Face side : {Face::L, Face::R}) {
                const std::array<Face, 3> corner = {vertical, depth, side};
                const std::size_t told = pairs_told_apart(remaining, corner);
                if (told > best_told) {
                    best_told = told;
                    best = corner;
                }
            }
        }
    }
    if (!best.has_value()) {
        return std::nullopt;
    }
    std::array<solver::Color, 3> colours{};
    for (std::size_t i = 0; i < 3; ++i) {
        colours.at(i) = remaining.front()->face(best->at(i)).at(4);
    }
    std::ranges::sort(colours);
    return colours;
}

// `named` with two colour names traded wherever they appear.
[[nodiscard]] NamedSides with_names_exchanged(NamedSides named, model::FacetColor first,
                                              model::FacetColor second) {
    for (auto& side : named) {
        for (model::Facelet& facelet : side) {
            if (facelet.color == first) {
                facelet.color = second;
            } else if (facelet.color == second) {
                facelet.color = first;
            }
        }
    }
    return named;
}

// The cube that trading the names of the two closest-looking centres makes,
// when any trade does - else `failure` - and which trade it was.
struct Exchanged {
    Result<ScannedCube> result;
    std::optional<NameExchange> exchange;
};

[[nodiscard]] Exchanged assemble_with_closest_exchange(const NamedSides& named,
                                                       const std::array<CapturedFace, 6>& faces,
                                                       CubeError failure) {
    Exchanged best{.result = std::unexpected(failure), .exchange = std::nullopt};
    float best_distance = std::numeric_limits<float>::max();
    for (std::size_t a = 0; a < 6; ++a) {
        for (std::size_t b = a + 1; b < 6; ++b) {
            const float distance = colour_distance(faces.at(a).lab.at(4), faces.at(b).lab.at(4));
            if (distance >= best_distance) {
                continue;
            }
            // Side a's centre carries one name and side b's the other.
            const NamedSides exchanged =
                with_names_exchanged(named, named.at(a).at(4).color, named.at(b).at(4).color);
            if (const Result<solver::Cube> cube = solver::assemble_scanned_faces(exchanged);
                cube.has_value()) {
                best.result = ScannedCube{.cube = *cube, .colours = colours_of(exchanged, faces)};
                best.exchange = NameExchange{
                    .first_side = a,
                    .second_side = b,
                    .first_name = named.at(a).at(4).color,
                    .second_name = named.at(b).at(4).color,
                    .centre_distance = distance,
                };
                best_distance = distance;
            }
        }
    }
    return best;
}

// The cube, and how it was put together.
struct Assembled {
    Result<ScannedCube> result;
    AssemblyTrace trace;
};

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
//
// What happened on the way is returned beside the cube, in an AssemblyTrace:
// values this function holds anyway, handed back instead of dropped.
[[nodiscard]] Assembled assemble(const NamedSides& named, const std::array<CapturedFace, 6>& faces,
                                 const std::vector<CornerRead>& corners) {
    Assembled out{.result = std::unexpected(CubeError::invalid_cube_state), .trace = {}};
    AssemblyTrace& trace = out.trace;
    trace.corner_reads = corners.size();

    // A naming that makes one cube is done. One that makes several is a
    // valid naming, so exchanging names would only invent a different cube:
    // the sides themselves are ambiguous, and only more than six flat
    // views can say which.
    // One more than is looked at, to know that there are more. A patterned
    // cube makes twenty to thirty cubes from six flat sides (the two recorded
    // ones make 20 and 32; two half-turns of a solved cube make 112), and this
    // was sixteen: with the true cube one of them at random it was outside the
    // sixteen one time in five for the 20 and one in two for the 32, and then no
    // corner read could fit it.
    std::vector<solver::Cube> candidates =
        solver::assemble_all_scanned_faces(named, kMaxCandidates + 1);
    trace.candidates = candidates.size();
    if (candidates.size() == 1) {
        trace.path = AssemblyPath::single_candidate;
        out.result = ScannedCube{.cube = candidates.front(), .colours = colours_of(named, faces)};
        return out;
    }
    if (candidates.size() > 1) {
        // More than can be told apart here is not chosen among: the true cube
        // may be one of those not looked at.
        const bool all_looked_at = candidates.size() <= kMaxCandidates;
        if (!all_looked_at) {
            candidates.resize(kMaxCandidates);
        }
        const SessionColours colours = colours_of(named, faces);
        CornerVotes votes = corner_votes(candidates, corners, colours);
        trace.corner_fits = std::move(votes.fits);
        trace.chosen = all_looked_at ? choose_by_corners(votes.misses) : std::nullopt;
        if (trace.chosen.has_value()) {
            trace.path = AssemblyPath::chosen_by_corners;
            out.result = ScannedCube{.cube = candidates.at(*trace.chosen), .colours = colours};
            return out;
        }
        trace.path = AssemblyPath::ambiguous;
        trace.error = CubeError::ambiguous_scan;
        trace.settling_corner = settling_corner_of(candidates, votes.misses);
        out.result = std::unexpected(CubeError::ambiguous_scan);
        return out;
    }
    const Result<solver::Cube> direct = solver::assemble_scanned_faces(named);
    if (direct.has_value()) {
        trace.path = AssemblyPath::direct;
        out.result = ScannedCube{.cube = *direct, .colours = colours_of(named, faces)};
        return out;
    }
    trace.direct_error = direct.error();

    Exchanged exchanged = assemble_with_closest_exchange(named, faces, direct.error());
    out.result = std::move(exchanged.result);
    trace.exchange = exchanged.exchange;
    if (out.result.has_value()) {
        trace.path = AssemblyPath::names_exchanged;
    } else {
        trace.path = AssemblyPath::failed;
        trace.error = out.result.error();
    }
    return out;
}

// The name each of the 54 stickers was given, side by side.
[[nodiscard]] std::array<std::array<model::FacetColor, 9>, 6> names_of(const NamedSides& named) {
    std::array<std::array<model::FacetColor, 9>, 6> names{};
    for (std::size_t side = 0; side < 6; ++side) {
        for (std::size_t cell = 0; cell < 9; ++cell) {
            names.at(side).at(cell) = named.at(side).at(cell).color;
        }
    }
    return names;
}

// Whether any of the six colours measures further than kRejudgeLabDistance from
// where it did.
[[nodiscard]] bool colours_moved(const SessionColours& now, const SessionColours& before) {
    constexpr float kLimitSquared = kRejudgeLabDistance * kRejudgeLabDistance;
    for (std::size_t colour = 0; colour < 6; ++colour) {
        const cv::Vec3f gap = now.at(colour) - before.at(colour);
        if (gap.dot(gap) > kLimitSquared) {
            return true;
        }
    }
    return false;
}

// Which of read_side's two looks a set of blobs came from.
enum class Look : std::uint8_t {
    // The outlines prepare_frame took.
    first,
    // narrow_contours: more of the frame is an outline, so more is held to.
    second,
};

// Whether the plastic between the lattice's stickers is darker than the
// stickers: see kMaxGapOverSticker.
[[nodiscard]] bool gaps_are_plastic(const cv::Mat& value, const Lattice& lattice) {
    const auto at = [&value](const cv::Point2f& point) {
        const int x = std::clamp(static_cast<int>(std::lround(point.x)), 0, value.cols - 1);
        const int y = std::clamp(static_cast<int>(std::lround(point.y)), 0, value.rows - 1);
        return static_cast<float>(value.at<std::uint8_t>(y, x));
    };
    float darkest = std::numeric_limits<float>::max();
    std::vector<float> gaps;
    for (int row = 0; row < 3; ++row) {
        for (int column = 0; column < 3; ++column) {
            const cv::Point2f centre = lattice.at(column, row);
            darkest = std::min(darkest, at(centre));
            if (column < 2) {
                gaps.push_back(at((centre + lattice.at(column + 1, row)) * 0.5F));
            }
            if (row < 2) {
                gaps.push_back(at((centre + lattice.at(column, row + 1)) * 0.5F));
            }
        }
    }
    const auto quartile = gaps.begin() + static_cast<std::ptrdiff_t>(gaps.size() * 3 / 4);
    std::nth_element(gaps.begin(), quartile, gaps.end());
    return *quartile <= kMaxGapOverSticker * std::max(darkest, 1.0F);
}

// The side the blobs make, if they make one: the lattice through them, judged
// square-on, and the nine colours sampled at its cells.
[[nodiscard]] std::optional<SideRead> read_blobs(const PreparedFrame& frame,
                                                 const std::vector<Blob>& blobs, Look look) {
    if (blobs.size() < kMinLatticeBlobs) {
        return std::nullopt;
    }
    // The first try is a square grid, which is all a side held square needs.
    // Only when it leaves no side square-on are the blobs placed again,
    // through the lattice they make: a side near the edge of the gate, or one
    // with a blob beside it that is not part of it. A set of blobs that read
    // before reads as it did, and one with a side held square pays nothing
    // more.
    std::optional<Lattice> lattice = fit_lattice(blobs);
    if (!lattice.has_value() || !is_square_on(*lattice)) {
        lattice = fit_lattice_through_fit(blobs);
    }
    if (!lattice.has_value() || !is_square_on(*lattice)) {
        return std::nullopt;
    }
    const float columns = length(lattice->column_step);
    const float rows = length(lattice->row_step);
    if (look == Look::second && !gaps_are_plastic(frame.brightness, *lattice)) {
        return std::nullopt;
    }

    const float pitch = (columns + rows) / 2.0F;
    const int half_width = std::max(2, static_cast<int>(kPatchHalfWidth * pitch));
    const Upright upright = upright_centres(*lattice);
    const std::array<cv::Point2f, 9>& centres = upright.centres;

    SideRead read;
    read.pitch = pitch * frame.scale;
    read.stickers_found = static_cast<int>(lattice->blobs);
    read.found = upright.found;
    for (std::size_t i = 0; i < 9; ++i) {
        const std::optional<cv::Vec3f> lab = sticker_lab(frame.working, centres.at(i), half_width);
        if (!lab.has_value()) {
            return std::nullopt;
        }
        read.lab.at(i) = *lab;
        read.centres.at(i) = centres.at(i) * frame.scale;
    }
    return read;
}

}  // namespace

std::optional<SideRead> read_side(const cv::Mat& bgr) {
    const std::optional<PreparedFrame> frame = prepare_frame(bgr);
    return frame.has_value() ? read_side(*frame) : std::nullopt;
}

std::optional<SideRead> read_side(const PreparedFrame& frame) {
    CUBE_ZONE_SCOPED_N("vision.direct_scan.read_side");
    const std::vector<Blob> blobs = sticker_blobs(frame.contours, frame.working);
    if (std::optional<SideRead> read = read_blobs(frame, blobs, Look::first); read.has_value()) {
        return read;
    }
    // Most frames of a scan have nothing in them near a side: no six
    // sticker-sized outlines at all. The second look is for the frame that
    // came close, six or more that did not make a side.
    if (blobs.size() < kMinLatticeBlobs) {
        return std::nullopt;
    }
    // A dark sticker beside a bright one is clipped by the mean the outlines
    // were taken against, and a row of them is lost; the outlines taken
    // against a narrower block keep it (see narrow_contours). A frame the
    // first look read is read as it was: only one it read nothing of is
    // looked at again.
    CUBE_ZONE_SCOPED_N("vision.direct_scan.read_side_second_look");
    return read_blobs(frame, sticker_blobs(narrow_contours(frame.brightness), frame.working),
                      Look::second);
}

float colour_distance(const cv::Vec3f& a, const cv::Vec3f& b) {
    const float d_lightness = a(0) - b(0);
    const float d_chroma = std::hypot(a(1), a(2)) - std::hypot(b(1), b(2));
    const float d_a = a(1) - b(1);
    const float d_b = a(2) - b(2);
    const float hue_squared = std::max((d_a * d_a) + (d_b * d_b) - (d_chroma * d_chroma), 0.0F);
    return std::sqrt((kShadingWeight * ((d_lightness * d_lightness) + (d_chroma * d_chroma))) +
                     hue_squared);
}

bool held_still(const SideRead& read, const cv::Point2f& previous_centre) {
    return length(read.centres.at(4) - previous_centre) < kSteadyFraction * read.pitch;
}

float interval_scale(float interval_ms) {
    if (interval_ms <= kNominalFrameMs) {
        return 1.0F;
    }
    return std::min(interval_ms / kNominalFrameMs, kMaxIntervalScale);
}

cv::Vec3b lab_to_bgr(const cv::Vec3f& lab) {
    const cv::Mat3f source(1, 1, lab);
    cv::Mat3f bgr;
    cv::cvtColor(source, bgr, cv::COLOR_Lab2BGR);
    const cv::Vec3f value = bgr(0, 0) * 255.0F;
    return {
        cv::saturate_cast<uchar>(value(0)),
        cv::saturate_cast<uchar>(value(1)),
        cv::saturate_cast<uchar>(value(2)),
    };
}

void SideScan::reset() {
    groups_.clear();
    corners_.clear();
    ++corner_revision_;
    failed_.reset();
    assemblies_ = 0;
    previous_centre_.reset();
    previous_corner_centres_.clear();
    in_view_.clear();
}

bool SideScan::count_read(const std::array<cv::Vec3f, 9>& lab, bool square_on) {
    // Which sides this read is: those it fits (fit_to_side), the cheapest
    // laying winning.
    std::optional<std::size_t> nearest;
    SideFit nearest_fit;
    std::size_t claiming = 0;
    for (std::size_t i = 0; i < groups_.size(); ++i) {
        const SideFit fit = fit_to_side(
            {.read = lab, .side = groups_.at(i).fused, .prior_roll = groups_.at(i).roll});
        if (!fit.same) {
            continue;
        }
        ++claiming;
        if (!nearest.has_value() || fit.cost < nearest_fit.cost) {
            nearest = i;
            nearest_fit = fit;
        }
    }
    // A side seen at an angle sits several Lab units from its own colours, so
    // one that two sides each claim has not said which it is, and counting it
    // for the cheaper is a coin toss. A square-on read is held to no such
    // standard: its colours are the side's own.
    if (!square_on && claiming > 1) {
        return false;
    }

    // A side seen at an angle that fits no side, but is one of them lit
    // differently, does not start a side of its own. It is lit a few to a dozen
    // Lab units differently from the same side seen square-on (the same orange
    // read at lightness 14, 22 and 31 in one recorded scan, and a drawn cube
    // turned slowly gave a second group for each of three sides), and a group of
    // its own is not told from a side of its own: it reaches kMinReads, counts as
    // one of the six, and the scan is declared six sides that make no cube - a
    // verdict that takes the better part of a tenth of a second to reach, on
    // every frame, with the screen saying the sides do not fit. A side that is
    // not like any seen so far is new, as before.
    if (!nearest.has_value() && !square_on && is_shaded_view_of_any(lab, groups_)) {
        return false;
    }

    std::size_t index = 0;
    if (nearest.has_value()) {
        index = *nearest;
        Group& group = groups_.at(index);
        const std::array<cv::Vec3f, 9> aligned = rolled(lab, nearest_fit.roll);
        group.reads.push_back(aligned);
        if (square_on) {
            group.flat_reads.push_back(aligned);
        }
        // A side seen at an angle is lit a little differently, so once a side
        // has been read square-on as often as it takes to count, those reads
        // alone give its colours.
        group.fused = fuse(group.flat_reads.size() >= kMinReads ? group.flat_reads : group.reads);
        group.roll = nearest_fit.roll;
    } else {
        Group group{.fused = lab, .reads = {lab}, .flat_reads = {}, .roll = 0};
        if (square_on) {
            group.flat_reads.push_back(lab);
        }
        groups_.push_back(std::move(group));
        index = groups_.size() - 1;
    }
    if (std::ranges::find(in_view_, index) == in_view_.end()) {
        in_view_.push_back(index);
    }
    // These renumber the groups and, with them, the sides in view.
    merge_alike_groups();
    drop_weakest_stray();
    return true;
}

void SideScan::add(const std::optional<SideRead>& read, float interval_ms) {
    CUBE_ZONE_SCOPED_N("vision.direct_scan.add");
    in_view_.clear();
    if (!read.has_value()) {
        previous_centre_.reset();
        return;
    }
    // A side square to the camera: whatever corner view came before is over.
    previous_corner_centres_.clear();
    const bool steady = previous_centre_.has_value() &&
                        length(read->centres.at(4) - *previous_centre_) <
                            kSteadyFraction * interval_scale(interval_ms) * read->pitch;
    previous_centre_ = read->centres.at(4);
    if (!steady) {
        return;
    }
    count_read(read->lab, true);
}

// Keeps the groups to kMaxGroups by dropping the one with the fewest reads,
// the oldest of them, but never the side in view.
void SideScan::drop_weakest_stray() {
    if (groups_.size() <= kMaxGroups) {
        return;
    }
    std::optional<std::size_t> weakest;
    for (std::size_t i = 0; i < groups_.size(); ++i) {
        if (std::ranges::find(in_view_, i) != in_view_.end()) {
            continue;
        }
        if (!weakest.has_value() ||
            groups_.at(i).reads.size() < groups_.at(*weakest).reads.size()) {
            weakest = i;
        }
    }
    if (!weakest.has_value()) {
        return;
    }
    groups_.erase(groups_.begin() + static_cast<std::ptrdiff_t>(*weakest));
    for (std::size_t& viewed : in_view_) {
        if (viewed > *weakest) {
            --viewed;
        }
    }
}

// A group begun while its side was only a read or two old is judged on a
// noisy colour, and a side whose light changed part way through can start a
// second. Neither is a different side, and two groups of one side leave the
// scan a side short. Joined here, the earlier group surviving so the sides
// keep the order they were first seen in.
std::optional<SideScan::AlikePair> SideScan::first_alike_pair() const {
    for (std::size_t keep = 0; keep < groups_.size(); ++keep) {
        for (std::size_t other = keep + 1; other < groups_.size(); ++other) {
            // Two groups are each in the turn their side was first seen in, so
            // no turn is more likely than another.
            const SideFit fit = fit_to_side(
                {.read = groups_.at(other).fused, .side = groups_.at(keep).fused, .prior_roll = 0});
            if (fit.same) {
                return AlikePair{.keep = keep, .other = other, .roll = fit.roll};
            }
        }
    }
    return std::nullopt;
}

void SideScan::absorb(const AlikePair& pair) {
    Group& kept = groups_.at(pair.keep);
    for (const auto& read : groups_.at(pair.other).reads) {
        kept.reads.push_back(rolled(read, pair.roll));
    }
    for (const auto& read : groups_.at(pair.other).flat_reads) {
        kept.flat_reads.push_back(rolled(read, pair.roll));
    }
    kept.fused = fuse(kept.flat_reads.size() >= kMinReads ? kept.flat_reads : kept.reads);
    groups_.erase(groups_.begin() + static_cast<std::ptrdiff_t>(pair.other));
    for (std::size_t& viewed : in_view_) {
        if (viewed == pair.other) {
            viewed = pair.keep;
        } else if (viewed > pair.other) {
            --viewed;
        }
    }
    std::ranges::sort(in_view_);
    const auto duplicates = std::ranges::unique(in_view_);
    in_view_.erase(duplicates.begin(), duplicates.end());
}

void SideScan::merge_alike_groups() {
    while (const std::optional<AlikePair> pair = first_alike_pair()) {
        absorb(*pair);
    }
}

namespace {

// Whether a side of a corner view was seen well enough to count: most of its
// stickers found, and not so foreshortened that it is a strip.
[[nodiscard]] bool seen_whole_enough(const ObliqueSide& side) {
    const auto found = std::ranges::count(side.found, true);
    if (found < kMinCornerFound) {
        return false;
    }
    const float along_columns = length((side.centres.at(2) - side.centres.at(0)) * 0.5F);
    const float along_rows = length((side.centres.at(6) - side.centres.at(0)) * 0.5F);
    const float longer = std::max(along_columns, along_rows);
    return longer > 0.0F && std::min(along_columns, along_rows) >= kMinCornerAspect * longer;
}

// Two reads of one side of a cube, from slightly different angles: the centres
// alike and the nine colours alike under some quarter-turn (the lattice's first
// cell is not always the same corner of the side). The same cells under a
// different light or a little blur sit a few Lab units apart; two sides of one
// colour that differ in a sticker or more sit further.
constexpr float kSameViewLab = 8.0F;

[[nodiscard]] bool same_oblique_side(const ObliqueSide& a, const ObliqueSide& b) {
    if (lab_gap(a.lab.at(4), b.lab.at(4)) > kSameColourLab) {
        return false;
    }
    for (int roll = 0; roll < 4; ++roll) {
        const std::array<cv::Vec3f, 9> turned = rolled(b.lab, roll);
        float total = 0.0F;
        for (std::size_t i = 0; i < 9; ++i) {
            total += lab_gap(a.lab.at(i), turned.at(i));
        }
        if (total / 9.0F <= kSameViewLab) {
            return true;
        }
    }
    return false;
}

// Whether two corner reads show the same sides. Two sides of a cube meet along
// one edge, always the same one, so the same pair seen again from another angle
// - as a cube turned slowly through the part of a quarter-turn in which both
// can be read shows it dozens of times - says nothing the first read did not:
// which stickers of one side touch which of the other is fixed by the cube and
// by which sides they are.
[[nodiscard]] bool same_view(const CornerRead& a, const CornerRead& b) {
    if (a.sides.size() != b.sides.size()) {
        return false;
    }
    std::array<bool, 3> taken{};
    for (const ObliqueSide& side : a.sides) {
        bool matched = false;
        for (std::size_t j = 0; j < b.sides.size() && !matched; ++j) {
            if (!taken.at(j) && same_oblique_side(side, b.sides.at(j))) {
                taken.at(j) = true;
                matched = true;
            }
        }
        if (!matched) {
            return false;
        }
    }
    return true;
}

// Which of two reads of the same view to keep: the one with more stickers found
// as blobs, and for as many the one whose worse-seen side is least foreshortened.
[[nodiscard]] float view_quality(const CornerRead& read) {
    float found = 0.0F;
    float least_aspect = 1.0F;
    for (const ObliqueSide& side : read.sides) {
        found += static_cast<float>(std::ranges::count(side.found, true));
        const float along_columns = length(side.centres.at(2) - side.centres.at(0));
        const float along_rows = length(side.centres.at(6) - side.centres.at(0));
        const float longer = std::max(along_columns, along_rows);
        if (longer > 0.0F) {
            least_aspect = std::min(least_aspect, std::min(along_columns, along_rows) / longer);
        }
    }
    return found + least_aspect;
}

}  // namespace

void SideScan::add_corner(const std::optional<CornerRead>& read, float interval_ms) {
    in_view_.clear();
    if (!read.has_value()) {
        previous_corner_centres_.clear();
        return;
    }
    // One read of each view, the best seen. A tumble that shows the same two
    // sides for a second gives dozens of reads, and a count of reads that fit
    // is only a count of views if they are not the same view again and again:
    // kept as the newest few dozen, the early views of a slow tumble were
    // pushed out by the later ones and the cubes they had told apart were tied.
    // Bounded all the same: a few hundred bytes each, and a view seen two ways
    // (a side lit differently) is kept twice.
    constexpr std::size_t kKeptCornerReads = 64;
    const auto same = std::ranges::find_if(
        corners_, [&](const CornerRead& kept) { return same_view(kept, *read); });
    if (same != corners_.end()) {
        if (view_quality(*read) > view_quality(*same)) {
            *same = *read;
            ++corner_revision_;
        }
    } else {
        if (corners_.size() >= kKeptCornerReads) {
            corners_.erase(corners_.begin());
        }
        corners_.push_back(*read);
        ++corner_revision_;
    }

    // Each side that was seen whole and held still is a read of its side.
    std::vector<cv::Point2f> centres;
    for (const ObliqueSide& side : read->sides) {
        const cv::Point2f centre = side.centres.at(4);
        centres.push_back(centre);
        if (!seen_whole_enough(side)) {
            continue;
        }
        const float reach = kSteadyFraction * interval_scale(interval_ms) * side.pitch;
        const bool steady = std::ranges::any_of(
            previous_corner_centres_,
            [&](const cv::Point2f& before) { return length(centre - before) < reach; });
        if (!steady) {
            continue;
        }
        count_read(side.lab, false);
    }
    previous_corner_centres_ = std::move(centres);
}

std::size_t SideScan::reads_in_view() const noexcept {
    if (in_view_.empty()) {
        return 0;
    }
    std::size_t fewest = std::numeric_limits<std::size_t>::max();
    for (const std::size_t i : in_view_) {
        fewest = std::min(fewest, groups_.at(i).reads.size());
    }
    return fewest;
}

std::vector<SideScan::Side> SideScan::sides() const {
    // The same six the cube is built from, so the display never shows a
    // stray capture in place of a side that is actually being used.
    std::vector<std::size_t> shown = captured_by_support();
    shown.resize(std::min<std::size_t>(shown.size(), 6));
    std::ranges::sort(shown);

    std::vector<Side> out;
    for (const std::size_t i : shown) {
        const Group& group = groups_.at(i);
        // The fused read is stored in the orientation the side was first
        // seen in; turn it back to how it was last shown.
        Side side{
            .reads = group.reads.size(),
            .in_view = std::ranges::find(in_view_, i) != in_view_.end(),
        };
        for (std::size_t cell = 0; cell < 9; ++cell) {
            side.lab.at(static_cast<std::size_t>(
                kRoll.at(static_cast<std::size_t>(group.roll)).at(cell))) = group.fused.at(cell);
        }
        out.push_back(side);
    }
    return out;
}

std::vector<std::size_t> SideScan::captured_by_support() const {
    std::vector<std::size_t> captured;
    for (std::size_t i = 0; i < groups_.size(); ++i) {
        if (groups_.at(i).reads.size() >= kMinReads) {
            captured.push_back(i);
        }
    }
    // A side that was read square-on is ahead of one that only a corner view
    // has shown: a side at an angle can start a group of its own when its
    // colours sit far from its side's, and that stray must not take the place
    // of a real side however many frames it was held for.
    std::ranges::stable_sort(captured, [this](std::size_t l, std::size_t r) {
        const Group& left = groups_.at(l);
        const Group& right = groups_.at(r);
        if (left.flat_reads.empty() != right.flat_reads.empty()) {
            return !left.flat_reads.empty();
        }
        return left.reads.size() > right.reads.size();
    });
    return captured;
}

Result<solver::Cube> SideScan::cube() const {
    // By hand, not Result::transform: see scan_assembly.cpp.
    const auto whole = scanned();
    if (!whole) {
        return std::unexpected(whole.error());
    }
    return whole->cube;
}

Result<ScannedCube> SideScan::scanned() const {
    CUBE_ZONE_SCOPED_N("vision.direct_scan.scanned");
    const std::vector<std::size_t> captured = captured_by_support();
    if (captured.size() < 6) {
        return std::unexpected(CubeError::face_capture_incomplete);
    }
    // A stray group (a hand, a lattice fitted across two sides) has far
    // fewer reads than a side that was deliberately shown, so the six
    // best-supported groups are the six sides.
    std::array<CapturedFace, 6> faces{};
    for (std::size_t i = 0; i < 6; ++i) {
        faces.at(i).lab = groups_.at(captured.at(i)).fused;
    }
    return judge(faces).result;
}

ScanDiagnosis SideScan::judge(const std::array<CapturedFace, 6>& faces) const {
    ScanDiagnosis judged;
    // Named in the colours the light would have given under neutral light,
    // once the cube's own white says what the light did. The sides are only
    // ever named here, after all six are in, so nothing seen earlier has to
    // be taken back. The session's colours stay as measured (assemble gets
    // `faces`, not `seen`): a corner or a held read later is measured the
    // same way.
    //
    // Naming is done every time; what costs is putting the names together
    // (kRejudgeLabDistance).
    judged.white = find_white_reference(faces);
    std::array<CapturedFace, 6> seen = faces;
    if (judged.white.has_value()) {
        for (CapturedFace& face : seen) {
            for (cv::Vec3f& lab : face.lab) {
                lab = neutralised(*judged.white, lab);
            }
        }
    }
    Result<ColorResolution> naming = resolve_colors_explained(
        seen, judged.white.has_value() ? std::optional(judged.white->side) : std::nullopt);
    if (!naming) {
        judged.result = std::unexpected(naming.error());
        return judged;
    }
    const NamedSides& named = naming->faces;
    const auto names = names_of(named);

    // The assembly that failed is the answer again when what it rested on is as
    // it was. Where no cube can be made that is the names alone: assembling,
    // and trading the names of two centres, look at nothing else. Choosing
    // among several cubes rests on the corner reads and on what each colour
    // measured, the colours the reads are fitted in - and a hand showing the
    // cube about adds a corner read a frame.
    if (failed_.has_value() && failed_->names == names) {
        const bool several = failed_->trace.candidates > 1;
        if (!several || (failed_->corner_revision == corner_revision_ &&
                         !colours_moved(colours_of(named, faces), failed_->colours))) {
            judged.assembly = failed_->trace;
            // What the trace says it was given, as of now.
            judged.assembly->corner_reads = corners_.size();
            judged.result = std::unexpected(failed_->error);
            judged.naming = std::move(*naming);
            return judged;
        }
    }

    ++assemblies_;
    Assembled assembled = assemble(named, faces, corners_);
    // Only a failure is kept: a cube ends the scan.
    if (assembled.result.has_value()) {
        failed_.reset();
    } else {
        failed_ = FailedAssembly{
            .names = names,
            .colours = colours_of(named, faces),
            .corner_revision = corner_revision_,
            .trace = assembled.trace,
            .error = assembled.result.error(),
        };
    }
    judged.assembly = std::move(assembled.trace);
    judged.result = std::move(assembled.result);
    if (judged.result.has_value() && judged.white.has_value()) {
        judged.result->cast = judged.white->cast;
    }
    judged.naming = std::move(*naming);
    return judged;
}

ScanDiagnosis SideScan::diagnose() const {
    CUBE_ZONE_SCOPED_N("vision.direct_scan.diagnose");
    const std::vector<std::size_t> captured = captured_by_support();

    ScanDiagnosis diagnosis;
    diagnosis.groups = groups_.size();
    diagnosis.captured = captured.size();
    diagnosis.corner_reads = corners_.size();
    for (std::size_t i = 0; i < std::min<std::size_t>(captured.size(), 6); ++i) {
        const Group& group = groups_.at(captured.at(i));
        diagnosis.sides.push_back(ScanDiagnosis::Side{
            .first_seen = captured.at(i),
            .reads = group.reads.size(),
            .lab = group.fused,
            .roll = group.roll,
            .in_view = std::ranges::find(in_view_, captured.at(i)) != in_view_.end(),
        });
    }
    if (captured.size() < 6) {
        return diagnosis;
    }

    std::array<CapturedFace, 6> faces{};
    for (std::size_t i = 0; i < 6; ++i) {
        faces.at(i).lab = diagnosis.sides.at(i).lab;
    }
    ScanDiagnosis judged = judge(faces);
    diagnosis.white = judged.white;
    diagnosis.naming = std::move(judged.naming);
    diagnosis.assembly = std::move(judged.assembly);
    diagnosis.result = std::move(judged.result);
    return diagnosis;
}

}  // namespace cube::vision
