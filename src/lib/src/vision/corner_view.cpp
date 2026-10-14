#include <cube/vision/corner_view.hpp>

#include <cube/profiling.hpp>
#include <cube/solver/hold.hpp>
#include <cube/solver/outside_view.hpp>
#include <cube/vision/corner_fit.hpp>
#include <cube/vision/prepared_frame.hpp>
#include <cube/vision/sticker_colour.hpp>

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <ranges>
#include <tuple>
#include <utility>
#include <vector>

namespace cube::vision {

namespace {

// Sticker blobs. A side at an angle is a parallelogram, not a square, so the
// shape test is the looser "compact quadrilateral": the contour fills its
// hull, the hull is four-cornered, and it is not a sliver.
constexpr double kMinBlobArea = 0.0003;
constexpr double kMaxBlobArea = 0.03;
constexpr float kMinHullFill = 0.85F;
constexpr float kMaxBlobAspect = 2.4F;
// A cube in a hand has a few dozen blobs. More is a busy room, and the
// search below grows with the square of the count.
constexpr std::size_t kMaxBlobs = 48;
// Blobs that may be stickers of one cube: within this of the median size.
constexpr float kMinSizeRatio = 0.55F;
constexpr float kMaxSizeRatio = 1.8F;

// Lattice search.
constexpr float kNeighbourReach = 1.9F;  // in blob sides
// A cell has at most four close neighbours and a few diagonal ones; more
// than this nearest are only a crowd, and the triples grow with its square.
constexpr std::size_t kMaxNeighbours = 8;
constexpr float kMaxCellResidual = 0.3F;  // in lattice steps
constexpr std::size_t kMinLatticeBlobs = 6;
constexpr float kMaxLatticeRms = 0.14F;  // in lattice steps
constexpr float kMinSkew = 0.35F;        // |det| / (|column| |row|)
constexpr std::size_t kMaxSides = 3;

// Two lattice corners are the same corner when this close, in sticker
// spacings.
constexpr float kCornerReach = 0.45F;

// Half-width of the sampled patch, in the narrower of the two steps.
constexpr float kPatchHalfWidth = 0.2F;

struct Blob {
    cv::Point2f centre;
    float side = 0.0F;
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

[[nodiscard]] float length(const cv::Point2f& p) {
    return std::hypot(p.x, p.y);
}

[[nodiscard]] std::vector<Blob> oblique_blobs(const PreparedFrame& frame) {
    const std::vector<std::vector<cv::Point>>& contours = frame.contours;

    const double frame_area =
        static_cast<double>(frame.working.rows) * static_cast<double>(frame.working.cols);
    std::vector<Blob> blobs;
    for (const auto& contour : contours) {
        const double area = cv::contourArea(contour);
        if (area <= frame_area * kMinBlobArea || area >= frame_area * kMaxBlobArea) {
            continue;
        }
        std::vector<cv::Point> hull;
        cv::convexHull(contour, hull);
        const double hull_area = cv::contourArea(hull);
        if (hull_area <= 0.0 || area / hull_area < static_cast<double>(kMinHullFill)) {
            continue;
        }
        std::vector<cv::Point> corners;
        cv::approxPolyDP(hull, corners, 0.08 * cv::arcLength(hull, true), true);
        if (corners.size() != 4) {
            continue;
        }
        const cv::Moments moments = cv::moments(hull);
        if (moments.m00 <= 0.0) {
            continue;
        }
        const cv::RotatedRect rect = cv::minAreaRect(hull);
        const float shorter = std::min(rect.size.width, rect.size.height);
        const float longer = std::max(rect.size.width, rect.size.height);
        if (shorter < 1.0F || longer / shorter > kMaxBlobAspect) {
            continue;
        }
        blobs.push_back(Blob{
            .centre = cv::Point2f(static_cast<float>(moments.m10 / moments.m00),
                                  static_cast<float>(moments.m01 / moments.m00)),
            .side = static_cast<float>(std::sqrt(area)),
        });
    }

    // Keep what could be stickers of one cube.
    if (blobs.size() < kMinLatticeBlobs) {
        return blobs;
    }
    std::vector<float> sides;
    sides.reserve(blobs.size());
    for (const Blob& blob : blobs) {
        sides.push_back(blob.side);
    }
    const float typical = median_of(sides);
    std::erase_if(blobs, [typical](const Blob& blob) {
        return blob.side < kMinSizeRatio * typical || blob.side > kMaxSizeRatio * typical;
    });
    return blobs;
}

// A 3x3 lattice laid through blobs, as an affine map from cell (column, row)
// to pixels, its two steps turning clockwise so the side reads unmirrored.
struct Lattice {
    cv::Point2f origin;
    cv::Point2f column_step;
    cv::Point2f row_step;
    std::array<int, 9> blob_at{};  // index into the blob list, or -1
    std::size_t count = 0;
    float rms = 0.0F;

    [[nodiscard]] cv::Point2f at(float column, float row) const {
        return origin + column * column_step + row * row_step;
    }
    [[nodiscard]] float pitch() const { return 0.5F * (length(column_step) + length(row_step)); }
};

[[nodiscard]] float cross(const cv::Point2f& a, const cv::Point2f& b) {
    return (a.x * b.y) - (a.y * b.x);
}

// The column and row of cell 0..8 of a side, read left to right, top to bottom.
struct CellPosition {
    float column = 0.0F;
    float row = 0.0F;
};

[[nodiscard]] CellPosition position_of(std::size_t cell) {
    const std::size_t column = cell % 3;
    const std::size_t row = cell / 3;
    return {.column = static_cast<float>(column), .row = static_cast<float>(row)};
}

// Blob i taken as a cell, its neighbour j and the neighbour k on its other
// axis as the next two along: the grid they lay through the plane.
struct Hypothesis {
    cv::Point2f origin;
    cv::Point2f u;
    cv::Point2f v;
    float det = 0.0F;
};

// A blob on the hypothesis' grid: its nearest node, `a` steps along u and `b`
// along v.
struct Placed {
    int a = 0;
    int b = 0;
    int blob = 0;
};

// The blobs of the pool that sit on a node of the grid, at most two steps out
// in either direction.
[[nodiscard]] std::vector<Placed> place_on_grid(const std::vector<Blob>& blobs,
                                                const std::vector<int>& pool,
                                                const Hypothesis& grid) {
    std::vector<Placed> placed;
    for (const int n : pool) {
        const cv::Point2f d = blobs.at(static_cast<std::size_t>(n)).centre - grid.origin;
        const float a = cross(d, grid.v) / grid.det;
        const float b = cross(grid.u, d) / grid.det;
        const int ia = static_cast<int>(std::lround(a));
        const int ib = static_cast<int>(std::lround(b));
        if (std::abs(ia) > 2 || std::abs(ib) > 2) {
            continue;
        }
        const float residual =
            std::max(std::abs(a - static_cast<float>(ia)), std::abs(b - static_cast<float>(ib)));
        if (residual >= kMaxCellResidual) {
            continue;
        }
        placed.push_back({.a = ia, .b = ib, .blob = n});
    }
    return placed;
}

// Which blob is on each cell (row * 3 + column) of a 3x3 window of the grid,
// or -1, and how many cells have one.
struct Window {
    std::array<int, 9> cells{};
    int count = 0;
};

// The window whose first node is (offset_a, offset_b): nothing unless its
// blobs span all three columns and rows and no two share a cell.
[[nodiscard]] std::optional<Window> window_at(const std::vector<Placed>& placed, int offset_a,
                                              int offset_b) {
    Window window;
    window.cells.fill(-1);
    int min_a = 3;
    int max_a = -1;
    int min_b = 3;
    int max_b = -1;
    for (const Placed& p : placed) {
        const int column = p.a - offset_a;
        const int row = p.b - offset_b;
        if (column < 0 || column > 2 || row < 0 || row > 2) {
            continue;
        }
        const std::size_t cell =
            (static_cast<std::size_t>(row) * 3U) + static_cast<std::size_t>(column);
        int& slot = window.cells.at(cell);
        if (slot >= 0) {
            return std::nullopt;  // two blobs on one cell
        }
        slot = p.blob;
        ++window.count;
        min_a = std::min(min_a, column);
        max_a = std::max(max_a, column);
        min_b = std::min(min_b, row);
        max_b = std::max(max_b, row);
    }
    if (max_a - min_a != 2 || max_b - min_b != 2) {
        return std::nullopt;
    }
    return window;
}

// The lattice a window's blobs fit by least squares, with its steps as the
// map from cell to pixels gives them.
[[nodiscard]] std::optional<Lattice> lattice_of_window(const std::vector<Blob>& blobs,
                                                       const Window& window) {
    cv::Mat design(window.count, 3, CV_64F);
    cv::Mat target(window.count, 2, CV_64F);
    int row_index = 0;
    for (std::size_t cell = 0; cell < 9; ++cell) {
        if (window.cells.at(cell) < 0) {
            continue;
        }
        const cv::Point2f centre = blobs.at(static_cast<std::size_t>(window.cells.at(cell))).centre;
        const CellPosition position = position_of(cell);
        design.at<double>(row_index, 0) = static_cast<double>(position.column);
        design.at<double>(row_index, 1) = static_cast<double>(position.row);
        design.at<double>(row_index, 2) = 1.0;
        target.at<double>(row_index, 0) = static_cast<double>(centre.x);
        target.at<double>(row_index, 1) = static_cast<double>(centre.y);
        ++row_index;
    }
    cv::Mat solution;
    if (!cv::solve(design, target, solution, cv::DECOMP_SVD)) {
        return std::nullopt;
    }
    Lattice lattice;
    lattice.column_step = {static_cast<float>(solution.at<double>(0, 0)),
                           static_cast<float>(solution.at<double>(0, 1))};
    lattice.row_step = {static_cast<float>(solution.at<double>(1, 0)),
                        static_cast<float>(solution.at<double>(1, 1))};
    lattice.origin = {static_cast<float>(solution.at<double>(2, 0)),
                      static_cast<float>(solution.at<double>(2, 1))};
    lattice.blob_at = window.cells;
    lattice.count = static_cast<std::size_t>(window.count);

    double squared = 0.0;
    for (std::size_t cell = 0; cell < 9; ++cell) {
        if (window.cells.at(cell) < 0) {
            continue;
        }
        const CellPosition position = position_of(cell);
        const cv::Point2f fitted = lattice.at(position.column, position.row);
        const cv::Point2f actual = blobs.at(static_cast<std::size_t>(window.cells.at(cell))).centre;
        squared += static_cast<double>(length(fitted - actual) * length(fitted - actual));
    }
    lattice.rms = static_cast<float>(std::sqrt(squared / window.count)) / lattice.pitch();
    return lattice;
}

// Clockwise steps, so the cells read as the side looks from outside: a
// lattice that came out the other way round is its own transpose.
void turn_clockwise(Lattice& lattice) {
    if (cross(lattice.column_step, lattice.row_step) >= 0.0F) {
        return;
    }
    std::swap(lattice.column_step, lattice.row_step);
    std::array<int, 9> transposed{};
    for (std::size_t cell = 0; cell < 9; ++cell) {
        transposed.at(((cell % 3) * 3) + (cell / 3)) = lattice.blob_at.at(cell);
    }
    lattice.blob_at = transposed;
}

// A 3x3 window that some hypothesis made, and the lattice fitted through it.
// `blobs_on` is the set of blobs it holds as a bit per blob (a frame is never
// read with more than kMaxBlobs, which fits a 64-bit word), so two windows
// that are the same blobs are known without comparing their fits.
struct Found {
    Lattice lattice;
    std::uint64_t blobs_on = 0;
};

[[nodiscard]] std::uint64_t blob_set(const std::array<int, 9>& cells) {
    std::uint64_t set = 0;
    for (const int blob : cells) {
        if (blob >= 0) {
            set |= std::uint64_t{1} << static_cast<unsigned>(blob);
        }
    }
    return set;
}

// The hypothesis that blob i, its neighbour j and the neighbour k on its other
// axis are one cell and the next two along: every 3x3 window of that grid with
// at least six blobs and a tight fit, not only the fullest. Two sides seen at
// much the same angle make one continuous grid, in which a window across their
// shared edge holds as many blobs as either side and can fit as well; the
// fullest window is then not a side, and taking it leaves the rest of both
// sides with no lattice. Every window is offered and the choice is made across
// them (find_lattices).
void windows_through(const std::vector<Blob>& blobs, const std::vector<int>& pool, int i, int j,
                     int k, std::vector<Found>& windows) {
    Hypothesis grid;
    grid.origin = blobs.at(static_cast<std::size_t>(i)).centre;
    grid.u = blobs.at(static_cast<std::size_t>(j)).centre - grid.origin;
    grid.v = blobs.at(static_cast<std::size_t>(k)).centre - grid.origin;
    grid.det = cross(grid.u, grid.v);
    if (std::abs(grid.det) < kMinSkew * length(grid.u) * length(grid.v)) {
        return;  // the three are nearly in a line
    }

    const std::vector<Placed> placed = place_on_grid(blobs, pool, grid);
    if (placed.size() < kMinLatticeBlobs) {
        return;
    }
    for (int offset_a = -2; offset_a <= 0; ++offset_a) {
        for (int offset_b = -2; offset_b <= 0; ++offset_b) {
            const std::optional<Window> window = window_at(placed, offset_a, offset_b);
            if (!window.has_value() || std::cmp_less(window->count, kMinLatticeBlobs)) {
                continue;
            }
            const std::uint64_t set = blob_set(window->cells);
            const bool known = std::ranges::any_of(
                windows, [set](const Found& other) { return other.blobs_on == set; });
            if (known) {
                continue;
            }
            std::optional<Lattice> lattice = lattice_of_window(blobs, *window);
            if (!lattice.has_value() || lattice->rms > kMaxLatticeRms) {
                continue;
            }
            turn_clockwise(*lattice);
            windows.push_back(Found{.lattice = *lattice, .blobs_on = set});
        }
    }
}

// The pool's blobs within reach of the seed, the nearest `kMaxNeighbours` of
// them when there are more.
[[nodiscard]] std::vector<int> neighbours_in_reach(const std::vector<Blob>& blobs,
                                                   const std::vector<int>& pool, int seed_index) {
    const Blob& seed = blobs.at(static_cast<std::size_t>(seed_index));
    std::vector<int> near;
    for (const int n : pool) {
        if (n != seed_index && length(blobs.at(static_cast<std::size_t>(n)).centre - seed.centre) <
                                   kNeighbourReach * seed.side) {
            near.push_back(n);
        }
    }
    if (near.size() > kMaxNeighbours) {
        const auto nearer = [&](int a, int b) {
            return length(blobs.at(static_cast<std::size_t>(a)).centre - seed.centre) <
                   length(blobs.at(static_cast<std::size_t>(b)).centre - seed.centre);
        };
        std::partial_sort(near.begin(), near.begin() + kMaxNeighbours, near.end(), nearer);
        near.resize(kMaxNeighbours);
    }
    return near;
}

// A lattice's four outer corners in reading order: top-left, top-right,
// bottom-right, bottom-left. Edge e runs from corner e to corner e + 1.
[[nodiscard]] std::array<cv::Point2f, 4> outer_corners(const Lattice& lattice) {
    return {
        lattice.at(-0.5F, -0.5F),
        lattice.at(2.5F, -0.5F),
        lattice.at(2.5F, 2.5F),
        lattice.at(-0.5F, 2.5F),
    };
}

// Lattices a and b meet along an edge when an edge of each has both its ends
// at the other's. Cube sides seen from outside, both read clockwise, walk
// their common edge in opposite directions; the same way round is not a cube.
[[nodiscard]] std::vector<SharedEdge> shared_edges(const std::vector<Lattice>& lattices) {
    std::vector<SharedEdge> edges;
    for (std::size_t a = 0; a < lattices.size(); ++a) {
        for (std::size_t b = a + 1; b < lattices.size(); ++b) {
            const auto ca = outer_corners(lattices.at(a));
            const auto cb = outer_corners(lattices.at(b));
            const float reach = kCornerReach * lattices.at(a).pitch();
            for (std::size_t ea = 0; ea < 4; ++ea) {
                for (std::size_t eb = 0; eb < 4; ++eb) {
                    const bool opposed = length(ca.at(ea) - cb.at((eb + 1) % 4)) < reach &&
                                         length(ca.at((ea + 1) % 4) - cb.at(eb)) < reach;
                    if (opposed) {
                        edges.push_back(SharedEdge{
                            .first = a,
                            .second = b,
                            .on_first = static_cast<GridEdge>(ea),
                            .on_second = static_cast<GridEdge>(eb),
                        });
                    }
                }
            }
        }
    }
    return edges;
}

// The windows weighed against one another: the best by blobs, then by fit. The
// choice is quadratic (a pair of sides) to cubic (three) in this, and a frame
// with a cube in it has far fewer distinct windows than that.
constexpr std::size_t kMaxWindows = 24;

// Every distinct window that some hypothesis makes - from every blob as origin
// and every pair of its neighbours as the two axes - best first, and no more
// than kMaxWindows of them. `seen` is how many there were before that.
[[nodiscard]] std::vector<Found> candidate_windows(const std::vector<Blob>& blobs,
                                                   std::size_t* seen) {
    std::vector<int> pool(blobs.size());
    for (std::size_t n = 0; n < blobs.size(); ++n) {
        pool.at(n) = static_cast<int>(n);
    }
    std::vector<Found> windows;
    for (const int i : pool) {
        const std::vector<int> near = neighbours_in_reach(blobs, pool, i);
        for (const int j : near) {
            for (const int k : near) {
                if (j != k) {
                    windows_through(blobs, pool, i, j, k, windows);
                }
            }
        }
    }
    if (seen != nullptr) {
        *seen = windows.size();
    }
    std::ranges::sort(windows, [](const Found& l, const Found& r) {
        return std::tuple(l.lattice.count, -l.lattice.rms) >
               std::tuple(r.lattice.count, -r.lattice.rms);
    });
    if (windows.size() > kMaxWindows) {
        windows.resize(kMaxWindows);
    }
    return windows;
}

// Which pairs of windows meet along an edge. Windows that share a blob are the
// same side seen twice, or one across the edge of another, and never meet.
using Meetings = std::vector<std::vector<bool>>;

[[nodiscard]] Meetings meetings_of(const std::vector<Found>& windows) {
    Meetings meets(windows.size(), std::vector<bool>(windows.size(), false));
    for (std::size_t a = 0; a < windows.size(); ++a) {
        for (std::size_t b = a + 1; b < windows.size(); ++b) {
            if ((windows.at(a).blobs_on & windows.at(b).blobs_on) != 0) {
                continue;
            }
            const std::vector<Lattice> pair = {windows.at(a).lattice, windows.at(b).lattice};
            const bool touch = !shared_edges(pair).empty();
            meets.at(a).at(b) = touch;
            meets.at(b).at(a) = touch;
        }
    }
    return meets;
}

// Two or three disjoint windows, as indices into the list, and how well they
// make a corner.
struct Partition {
    std::array<std::size_t, kMaxSides> picks{};
    std::size_t size = 0;
    // How many of them meet another, and the blobs and fit of those that do.
    std::size_t touching = 0;
    std::size_t blobs = 0;
    float rms = 0.0F;

    [[nodiscard]] bool better_than(const Partition& other) const {
        return std::tuple(touching, blobs, -rms) >
               std::tuple(other.touching, other.blobs, -other.rms);
    }
};

[[nodiscard]] Partition partition_of(const std::vector<Found>& windows, const Meetings& meets,
                                     std::initializer_list<std::size_t> picks) {
    Partition partition;
    for (const std::size_t pick : picks) {
        partition.picks.at(partition.size++) = pick;
    }
    for (std::size_t x = 0; x < partition.size; ++x) {
        const std::size_t window = partition.picks.at(x);
        const bool touches = std::ranges::any_of(
            std::views::iota(std::size_t{0}, partition.size),
            [&](std::size_t y) { return y != x && meets.at(window).at(partition.picks.at(y)); });
        if (touches) {
            ++partition.touching;
            partition.blobs += windows.at(window).lattice.count;
            partition.rms += windows.at(window).lattice.rms;
        }
    }
    return partition;
}

// The two or three sides of a corner view: the set of disjoint windows that
// most of them touch, then that has the most blobs on those that do, then
// that fits tightest.
//
// This was greedy - the window with the most blobs, its blobs set aside, then
// the best of those left - and two sides at much the same angle (a cube turned
// so that neither is more than 60 degrees from the camera, the middle of every
// quarter-turn of a tumble) read as one continuous grid, so that a window
// across their shared edge, nine blobs and as tight as either side, was taken
// first and left six blobs and three, which make no lattice: 18 blobs, one
// lattice, no read. Disjoint windows are the cube's two sides; a window across
// the edge overlaps both.
[[nodiscard]] std::vector<Lattice> find_lattices(const std::vector<Blob>& blobs,
                                                 std::size_t* windows_seen = nullptr) {
    const std::vector<Found> windows = candidate_windows(blobs, windows_seen);
    if (windows.size() < 2) {
        return {};
    }
    const Meetings meets = meetings_of(windows);

    const auto disjoint = [&](std::size_t a, std::size_t b) {
        return (windows.at(a).blobs_on & windows.at(b).blobs_on) == 0;
    };
    std::optional<Partition> best;
    const auto consider = [&](std::initializer_list<std::size_t> picks) {
        const Partition partition = partition_of(windows, meets, picks);
        if (partition.touching >= 2 && (!best.has_value() || partition.better_than(*best))) {
            best = partition;
        }
    };
    for (std::size_t a = 0; a < windows.size(); ++a) {
        for (std::size_t b = a + 1; b < windows.size(); ++b) {
            if (!disjoint(a, b)) {
                continue;
            }
            consider({a, b});
            for (std::size_t c = b + 1; c < windows.size(); ++c) {
                if (disjoint(a, c) && disjoint(b, c)) {
                    consider({a, b, c});
                }
            }
        }
    }

    std::vector<Lattice> found;
    if (best.has_value()) {
        for (std::size_t n = 0; n < best->size; ++n) {
            found.push_back(windows.at(best->picks.at(n)).lattice);
        }
    }
    return found;
}

}  // namespace

std::optional<CornerRead> read_corner(const cv::Mat& bgr) {
    const std::optional<PreparedFrame> frame = prepare_frame(bgr);
    return frame.has_value() ? read_corner(*frame) : std::nullopt;
}

std::optional<CornerRead> read_corner(const PreparedFrame& frame) {
    return read_corner_explained(frame).read;
}

CornerExplained read_corner_explained(const PreparedFrame& frame) {
    CUBE_ZONE_SCOPED_N("vision.corner_view.read_corner");
    const cv::Mat& working = frame.working;
    const float scale = frame.scale;

    CornerExplained explained;
    const std::vector<Blob> blobs = oblique_blobs(frame);
    explained.blobs = blobs.size();
    if (blobs.size() < 2 * kMinLatticeBlobs) {
        explained.stage = CornerStage::too_few_blobs;
        return explained;
    }
    if (blobs.size() > kMaxBlobs) {
        explained.stage = CornerStage::too_many_blobs;
        return explained;
    }
    const std::vector<Lattice> lattices = find_lattices(blobs, &explained.windows);
    if (lattices.size() < 2) {
        explained.stage = CornerStage::no_pair_of_lattices;
        return explained;
    }
    const std::vector<SharedEdge> edges = shared_edges(lattices);
    if (edges.empty()) {
        explained.stage = CornerStage::no_shared_edge;
        return explained;
    }

    // Keep the lattices that touch another, renumbering the edges.
    std::vector<std::size_t> kept;
    std::array<int, kMaxSides> renumbered{};
    renumbered.fill(-1);
    for (const SharedEdge& edge : edges) {
        for (const std::size_t index : {edge.first, edge.second}) {
            if (renumbered.at(index) < 0) {
                renumbered.at(index) = static_cast<int>(kept.size());
                kept.push_back(index);
            }
        }
    }

    CornerRead read;
    for (const std::size_t index : kept) {
        const Lattice& lattice = lattices.at(index);
        const int half_width =
            std::max(2, static_cast<int>(kPatchHalfWidth * std::min(length(lattice.column_step),
                                                                    length(lattice.row_step))));
        ObliqueSide side;
        side.pitch = lattice.pitch() * scale;
        for (std::size_t cell = 0; cell < 9; ++cell) {
            const CellPosition position = position_of(cell);
            const cv::Point2f centre = lattice.at(position.column, position.row);
            const std::optional<cv::Vec3f> lab = sticker_lab(working, centre, half_width);
            if (!lab.has_value()) {
                explained.stage = CornerStage::off_the_frame;
                return explained;
            }
            side.centres.at(cell) = centre * scale;
            side.lab.at(cell) = *lab;
            side.found.at(cell) = lattice.blob_at.at(cell) >= 0;
        }
        read.sides.push_back(side);
    }
    for (const SharedEdge& edge : edges) {
        read.edges.push_back(SharedEdge{
            .first = static_cast<std::size_t>(renumbered.at(edge.first)),
            .second = static_cast<std::size_t>(renumbered.at(edge.second)),
            .on_first = edge.on_first,
            .on_second = edge.on_second,
        });
    }
    explained.stage = CornerStage::read;
    explained.read = std::move(read);
    return explained;
}

// --- fit_corner --------------------------------------------------------------

namespace {

using solver::Color;
using solver::Face;

// rotated[i] = source[kQuarterSource[i]]: a side turned a quarter clockwise in
// the picture (the same permutation scan_assembly searches over).
constexpr std::array<std::size_t, 9> kQuarterSource = {6, 3, 0, 7, 4, 1, 8, 5, 2};

// For `turns` quarter-turns, the cell of the unturned side that lands at each
// cell.
[[nodiscard]] std::array<std::size_t, 9> source_after(int turns) {
    std::array<std::size_t, 9> source = {0, 1, 2, 3, 4, 5, 6, 7, 8};
    for (int turn = 0; turn < turns; ++turn) {
        std::array<std::size_t, 9> next{};
        for (std::size_t i = 0; i < 9; ++i) {
            next.at(i) = source.at(kQuarterSource.at(i));
        }
        source = next;
    }
    return source;
}

// The cells along an edge of a row-major 3x3, in the order the edge is walked.
constexpr std::array<std::array<std::size_t, 3>, 4> kEdgeCells = {
    {
        {0, 1, 2},  // top, left to right
        {2, 5, 8},  // right, top to bottom
        {8, 7, 6},  // bottom, right to left
        {6, 3, 0},  // left, bottom to top
    },
};

[[nodiscard]] int to_int(GridEdge edge) {
    return static_cast<int>(edge);
}

// The side across an edge of outside_view(face).
[[nodiscard]] Face across(Face face, int edge) {
    switch (edge) {
        case 0:
            return solver::top_of(face);
        case 1:
            return solver::right_of(face);
        case 2:
            return solver::opposite(solver::top_of(face));
        default:
            return solver::opposite(solver::right_of(face));
    }
}

using Vec3 = std::array<int, 3>;

// Where a sticker of outside_view(face) sits on the cube, each coordinate in
// {-1, 0, 1}, in the frame cube.cpp lays the cube out in.
[[nodiscard]] Vec3 position_of(Face face, std::size_t cell) {
    const Vec3 normal = solver::detail::direction(face);
    const Vec3 up = solver::detail::direction(solver::top_of(face));
    const Vec3 right = solver::detail::direction(solver::right_of(face));
    const int column = static_cast<int>(cell % 3) - 1;
    const int row = 1 - static_cast<int>(cell / 3);
    return {
        normal.at(0) + (column * right.at(0)) + (row * up.at(0)),
        normal.at(1) + (column * right.at(1)) + (row * up.at(1)),
        normal.at(2) + (column * right.at(2)) + (row * up.at(2)),
    };
}

[[nodiscard]] int dot(const Vec3& a, const Vec3& b) {
    return (a.at(0) * b.at(0)) + (a.at(1) * b.at(1)) + (a.at(2) * b.at(2));
}

[[nodiscard]] Face face_with_centre(const solver::Cube& cube, Color colour) {
    for (const Face face : solver::kAllFaces) {
        if (cube.face(face).at(4) == colour) {
            return face;
        }
    }
    return Face::U;
}

struct SideMatch {
    Face face = Face::U;
    std::array<bool, 4> possible{};  // which quarter-turns the side may be in
    float distance = 0.0F;           // of the best
    bool close_enough = false;       // the best is within the distance allowed
};

[[nodiscard]] SideMatch match_side(const ObliqueSide& side, const solver::Cube& cube,
                                   const SessionColours& colours, float max_distance) {
    // The centre names the side.
    std::size_t nearest = 0;
    float nearest_distance = std::numeric_limits<float>::max();
    for (std::size_t colour = 0; colour < colours.size(); ++colour) {
        const float distance = colour_distance(side.lab.at(4), colours.at(colour));
        if (distance < nearest_distance) {
            nearest_distance = distance;
            nearest = colour;
        }
    }
    SideMatch match;
    match.face = face_with_centre(cube, static_cast<Color>(nearest));
    const std::array<Color, 9> outside = solver::outside_view(cube, match.face);

    std::array<float, 4> distances{};
    float best = std::numeric_limits<float>::max();
    for (int turns = 0; turns < 4; ++turns) {
        const std::array<std::size_t, 9> source = source_after(turns);
        float sum = 0.0F;
        for (std::size_t cell = 0; cell < 9; ++cell) {
            sum +=
                colour_distance(side.lab.at(cell),
                                colours.at(static_cast<std::size_t>(outside.at(source.at(cell)))));
        }
        distances.at(static_cast<std::size_t>(turns)) = sum / 9.0F;
        best = std::min(best, sum / 9.0F);
    }
    match.distance = best;
    match.close_enough = best <= max_distance;
    for (std::size_t turns = 0; turns < 4; ++turns) {
        match.possible.at(turns) = distances.at(turns) <= best + kCornerTurnTolerance &&
                                   distances.at(turns) <= max_distance;
    }
    return match;
}

// Whether sides a and b, in turns ra and rb, meet along the edge that the read
// puts them on, with their stickers paired as the cube has them.
[[nodiscard]] bool edge_holds(const SharedEdge& edge, const SideMatch& a, int ra,
                              const SideMatch& b, int rb) {
    const int a_edge = (((to_int(edge.on_first) - ra) % 4) + 4) % 4;
    const int b_edge = (((to_int(edge.on_second) - rb) % 4) + 4) % 4;
    if (across(a.face, a_edge) != b.face || across(b.face, b_edge) != a.face) {
        return false;
    }
    const Vec3 normal_a = solver::detail::direction(a.face);
    const Vec3 normal_b = solver::detail::direction(b.face);
    const std::array<std::size_t, 9> source_a = source_after(ra);
    const std::array<std::size_t, 9> source_b = source_after(rb);
    const auto& cells_a = kEdgeCells.at(static_cast<std::size_t>(to_int(edge.on_first)));
    const auto& cells_b = kEdgeCells.at(static_cast<std::size_t>(to_int(edge.on_second)));
    // The coordinate along which the edge runs: the one neither normal has.
    std::size_t along = 0;
    for (std::size_t axis = 0; axis < 3; ++axis) {
        if (normal_a.at(axis) == 0 && normal_b.at(axis) == 0) {
            along = axis;
        }
    }
    for (std::size_t t = 0; t < 3; ++t) {
        const Vec3 pa = position_of(a.face, source_a.at(cells_a.at(t)));
        const Vec3 pb = position_of(b.face, source_b.at(cells_b.at(2 - t)));
        if (pa.at(along) != pb.at(along) || dot(pa, normal_b) != 1 || dot(pb, normal_a) != 1) {
            return false;
        }
    }
    return true;
}

}  // namespace

CornerFit fit_corner(const CornerRead& read, const solver::Cube& cube,
                     const SessionColours& colours, float max_distance) {
    CUBE_ZONE_SCOPED_N("vision.corner_view.fit_corner");
    CornerFit fit;
    if (read.sides.size() < 2 || read.edges.empty()) {
        return fit;
    }
    std::vector<SideMatch> matches;
    float total = 0.0F;
    bool all_close = true;
    for (const ObliqueSide& side : read.sides) {
        const SideMatch match = match_side(side, cube, colours, max_distance);
        total += match.distance;
        all_close = all_close && match.close_enough;
        matches.push_back(match);
    }
    fit.distance = total / static_cast<float>(matches.size());
    if (!all_close) {
        return fit;
    }

    for (const SharedEdge& edge : read.edges) {
        const SideMatch& a = matches.at(edge.first);
        const SideMatch& b = matches.at(edge.second);
        if (a.face == b.face || a.face == solver::opposite(b.face)) {
            return fit;
        }
        bool holds = false;
        for (int ra = 0; ra < 4 && !holds; ++ra) {
            for (int rb = 0; rb < 4 && !holds; ++rb) {
                holds = a.possible.at(static_cast<std::size_t>(ra)) &&
                        b.possible.at(static_cast<std::size_t>(rb)) &&
                        edge_holds(edge, a, ra, b, rb);
            }
        }
        if (!holds) {
            return fit;
        }
    }
    fit.fits = true;
    return fit;
}

std::vector<CornerFit> fit_corner_each(const CornerRead& read,
                                       const std::vector<solver::Cube>& cubes,
                                       const SessionColours& colours, float max_distance) {
    std::vector<CornerFit> fits;
    fits.reserve(cubes.size());
    if (read.sides.size() < 2 || read.edges.empty()) {
        fits.assign(cubes.size(), CornerFit{});
        return fits;
    }
    // The colour each side's centre is named by - match_side's first step,
    // which does not look at the cube - and so which face of a cube the side is.
    std::vector<std::size_t> named;
    named.reserve(read.sides.size());
    for (const ObliqueSide& side : read.sides) {
        std::size_t nearest = 0;
        float nearest_distance = std::numeric_limits<float>::max();
        for (std::size_t colour = 0; colour < colours.size(); ++colour) {
            const float distance = colour_distance(side.lab.at(4), colours.at(colour));
            if (distance < nearest_distance) {
                nearest_distance = distance;
                nearest = colour;
            }
        }
        named.push_back(nearest);
    }

    // What a fit of this read depends on in a cube: which face each side is,
    // and that face's nine colours as an outside viewer sees it.
    using Shown = std::array<std::uint8_t, kMaxSides * 10>;
    std::vector<std::pair<Shown, CornerFit>> known;
    for (const solver::Cube& cube : cubes) {
        Shown shown{};
        std::size_t at = 0;
        for (const std::size_t colour : named) {
            const Face face = face_with_centre(cube, static_cast<Color>(colour));
            shown.at(at++) = static_cast<std::uint8_t>(face);
            for (const Color sticker : solver::outside_view(cube, face)) {
                shown.at(at++) = static_cast<std::uint8_t>(sticker);
            }
        }
        const auto same = std::ranges::find_if(
            known, [&shown](const auto& entry) { return entry.first == shown; });
        if (same != known.end()) {
            fits.push_back(same->second);
            continue;
        }
        const CornerFit fit = fit_corner(read, cube, colours, max_distance);
        known.emplace_back(shown, fit);
        fits.push_back(fit);
    }
    return fits;
}

}  // namespace cube::vision
