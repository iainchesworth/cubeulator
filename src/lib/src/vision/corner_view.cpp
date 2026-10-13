#include <cube/vision/corner_view.hpp>

#include <cube/profiling.hpp>
#include <cube/solver/hold.hpp>
#include <cube/solver/outside_view.hpp>
#include <cube/vision/corner_fit.hpp>

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <utility>
#include <vector>

namespace cube::vision {

namespace {

// The reader works at this width, as read_side does.
constexpr int kWorkingWidth = 640;

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

[[nodiscard]] std::vector<Blob> oblique_blobs(const cv::Mat& bgr) {
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
        blobs.push_back(Blob{cv::Point2f(static_cast<float>(moments.m10 / moments.m00),
                                         static_cast<float>(moments.m01 / moments.m00)),
                             static_cast<float>(std::sqrt(area))});
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
    return a.x * b.y - a.y * b.x;
}

// The hypothesis that blob i, its neighbour j and the neighbour k on its other
// axis are one cell and the next two along: the 3x3 window holding the most
// blobs, fitted by least squares.
[[nodiscard]] std::optional<Lattice> lattice_through(const std::vector<Blob>& blobs,
                                                     const std::vector<int>& pool, int i, int j,
                                                     int k) {
    const cv::Point2f origin = blobs[static_cast<std::size_t>(i)].centre;
    const cv::Point2f u = blobs[static_cast<std::size_t>(j)].centre - origin;
    const cv::Point2f v = blobs[static_cast<std::size_t>(k)].centre - origin;
    const float det = cross(u, v);
    if (std::abs(det) < kMinSkew * length(u) * length(v)) {
        return std::nullopt;  // the three are nearly in a line
    }

    struct Placed {
        int a;
        int b;
        int blob;
    };
    std::vector<Placed> placed;
    for (const int n : pool) {
        const cv::Point2f d = blobs[static_cast<std::size_t>(n)].centre - origin;
        const float a = cross(d, v) / det;
        const float b = cross(u, d) / det;
        const int ia = static_cast<int>(std::lround(a));
        const int ib = static_cast<int>(std::lround(b));
        if (std::abs(ia) > 2 || std::abs(ib) > 2 ||
            std::max(std::abs(a - static_cast<float>(ia)), std::abs(b - static_cast<float>(ib))) >=
                kMaxCellResidual) {
            continue;
        }
        placed.push_back({ia, ib, n});
    }
    if (placed.size() < kMinLatticeBlobs) {
        return std::nullopt;
    }

    std::array<int, 9> best_cells{};
    best_cells.fill(-1);
    int best_count = 0;
    for (int offset_a = -2; offset_a <= 0; ++offset_a) {
        for (int offset_b = -2; offset_b <= 0; ++offset_b) {
            std::array<int, 9> cells{};
            cells.fill(-1);
            int count = 0;
            int min_a = 3;
            int max_a = -1;
            int min_b = 3;
            int max_b = -1;
            bool clash = false;
            for (const Placed& p : placed) {
                const int column = p.a - offset_a;
                const int row = p.b - offset_b;
                if (column < 0 || column > 2 || row < 0 || row > 2) {
                    continue;
                }
                int& slot = cells[static_cast<std::size_t>(row * 3 + column)];
                if (slot >= 0) {
                    clash = true;
                    break;
                }
                slot = p.blob;
                ++count;
                min_a = std::min(min_a, column);
                max_a = std::max(max_a, column);
                min_b = std::min(min_b, row);
                max_b = std::max(max_b, row);
            }
            if (clash || max_a - min_a != 2 || max_b - min_b != 2 || count <= best_count) {
                continue;
            }
            best_count = count;
            best_cells = cells;
        }
    }
    if (best_count < static_cast<int>(kMinLatticeBlobs)) {
        return std::nullopt;
    }

    cv::Mat design(best_count, 3, CV_64F);
    cv::Mat target(best_count, 2, CV_64F);
    int row_index = 0;
    for (std::size_t cell = 0; cell < 9; ++cell) {
        if (best_cells[cell] < 0) {
            continue;
        }
        const cv::Point2f centre = blobs[static_cast<std::size_t>(best_cells[cell])].centre;
        design.at<double>(row_index, 0) = static_cast<double>(cell % 3);
        design.at<double>(row_index, 1) = static_cast<double>(cell / 3);
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
    lattice.blob_at = best_cells;
    lattice.count = static_cast<std::size_t>(best_count);

    double squared = 0.0;
    for (std::size_t cell = 0; cell < 9; ++cell) {
        if (best_cells[cell] < 0) {
            continue;
        }
        const cv::Point2f fitted =
            lattice.at(static_cast<float>(cell % 3), static_cast<float>(cell / 3));
        const cv::Point2f actual = blobs[static_cast<std::size_t>(best_cells[cell])].centre;
        squared += static_cast<double>(length(fitted - actual) * length(fitted - actual));
    }
    lattice.rms = static_cast<float>(std::sqrt(squared / best_count)) / lattice.pitch();

    // Clockwise steps, so the cells read as the side looks from outside: a
    // lattice that came out the other way round is its own transpose.
    if (cross(lattice.column_step, lattice.row_step) < 0.0F) {
        std::swap(lattice.column_step, lattice.row_step);
        std::array<int, 9> transposed{};
        for (std::size_t cell = 0; cell < 9; ++cell) {
            transposed[(cell % 3) * 3 + cell / 3] = lattice.blob_at[cell];
        }
        lattice.blob_at = transposed;
    }
    return lattice;
}

[[nodiscard]] std::vector<Lattice> find_lattices(const std::vector<Blob>& blobs) {
    std::vector<int> pool(blobs.size());
    for (std::size_t n = 0; n < blobs.size(); ++n) {
        pool[n] = static_cast<int>(n);
    }
    std::vector<Lattice> found;
    while (found.size() < kMaxSides && pool.size() >= kMinLatticeBlobs) {
        std::optional<Lattice> best;
        for (const int i : pool) {
            const Blob& seed = blobs[static_cast<std::size_t>(i)];
            std::vector<int> near;
            for (const int n : pool) {
                if (n != i && length(blobs[static_cast<std::size_t>(n)].centre - seed.centre) <
                                  kNeighbourReach * seed.side) {
                    near.push_back(n);
                }
            }
            if (near.size() > kMaxNeighbours) {
                const auto nearer = [&](int a, int b) {
                    return length(blobs[static_cast<std::size_t>(a)].centre - seed.centre) <
                           length(blobs[static_cast<std::size_t>(b)].centre - seed.centre);
                };
                std::partial_sort(near.begin(), near.begin() + kMaxNeighbours, near.end(), nearer);
                near.resize(kMaxNeighbours);
            }
            for (const int j : near) {
                for (const int k : near) {
                    if (j == k) {
                        continue;
                    }
                    const std::optional<Lattice> candidate = lattice_through(blobs, pool, i, j, k);
                    if (!candidate.has_value() || candidate->rms > kMaxLatticeRms) {
                        continue;
                    }
                    if (!best.has_value() || candidate->count > best->count ||
                        (candidate->count == best->count && candidate->rms < best->rms)) {
                        best = candidate;
                    }
                }
            }
        }
        if (!best.has_value()) {
            break;
        }
        for (const int used : best->blob_at) {
            if (used >= 0) {
                std::erase(pool, used);
            }
        }
        found.push_back(*best);
    }
    return found;
}

// A lattice's four outer corners in reading order: top-left, top-right,
// bottom-right, bottom-left. Edge e runs from corner e to corner e + 1.
[[nodiscard]] std::array<cv::Point2f, 4> outer_corners(const Lattice& lattice) {
    return {lattice.at(-0.5F, -0.5F), lattice.at(2.5F, -0.5F), lattice.at(2.5F, 2.5F),
            lattice.at(-0.5F, 2.5F)};
}

// Lattices a and b meet along an edge when an edge of each has both its ends
// at the other's. Cube sides seen from outside, both read clockwise, walk
// their common edge in opposite directions; the same way round is not a cube.
[[nodiscard]] std::vector<SharedEdge> shared_edges(const std::vector<Lattice>& lattices) {
    std::vector<SharedEdge> edges;
    for (std::size_t a = 0; a < lattices.size(); ++a) {
        for (std::size_t b = a + 1; b < lattices.size(); ++b) {
            const auto ca = outer_corners(lattices[a]);
            const auto cb = outer_corners(lattices[b]);
            const float reach = kCornerReach * lattices[a].pitch();
            for (std::size_t ea = 0; ea < 4; ++ea) {
                for (std::size_t eb = 0; eb < 4; ++eb) {
                    const bool opposed = length(ca[ea] - cb[(eb + 1) % 4]) < reach &&
                                         length(ca[(ea + 1) % 4] - cb[eb]) < reach;
                    if (opposed) {
                        edges.push_back(
                            SharedEdge{a, b, static_cast<GridEdge>(ea), static_cast<GridEdge>(eb)});
                    }
                }
            }
        }
    }
    return edges;
}

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

}  // namespace

std::optional<CornerRead> read_corner(const cv::Mat& bgr) {
    CUBE_ZONE_SCOPED_N("vision.corner_view.read_corner");
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

    const std::vector<Blob> blobs = oblique_blobs(working);
    if (blobs.size() < 2 * kMinLatticeBlobs || blobs.size() > kMaxBlobs) {
        return std::nullopt;
    }
    const std::vector<Lattice> lattices = find_lattices(blobs);
    if (lattices.size() < 2) {
        return std::nullopt;
    }
    const std::vector<SharedEdge> edges = shared_edges(lattices);
    if (edges.empty()) {
        return std::nullopt;
    }

    // Keep the lattices that touch another, renumbering the edges.
    std::vector<std::size_t> kept;
    std::array<int, kMaxSides> renumbered{};
    renumbered.fill(-1);
    for (const SharedEdge& edge : edges) {
        for (const std::size_t index : {edge.first, edge.second}) {
            if (renumbered[index] < 0) {
                renumbered[index] = static_cast<int>(kept.size());
                kept.push_back(index);
            }
        }
    }

    CornerRead read;
    for (const std::size_t index : kept) {
        const Lattice& lattice = lattices[index];
        const int half_width =
            std::max(2, static_cast<int>(kPatchHalfWidth * std::min(length(lattice.column_step),
                                                                    length(lattice.row_step))));
        ObliqueSide side;
        side.pitch = lattice.pitch() * scale;
        for (std::size_t cell = 0; cell < 9; ++cell) {
            const cv::Point2f centre =
                lattice.at(static_cast<float>(cell % 3), static_cast<float>(cell / 3));
            const std::optional<cv::Vec3f> lab = patch_lab(working, centre, half_width);
            if (!lab.has_value()) {
                return std::nullopt;
            }
            side.centres[cell] = centre * scale;
            side.lab[cell] = *lab;
            side.found[cell] = lattice.blob_at[cell] >= 0;
        }
        read.sides.push_back(side);
    }
    for (const SharedEdge& edge : edges) {
        read.edges.push_back(SharedEdge{static_cast<std::size_t>(renumbered[edge.first]),
                                        static_cast<std::size_t>(renumbered[edge.second]),
                                        edge.on_first, edge.on_second});
    }
    return read;
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
            next[i] = source[kQuarterSource[i]];
        }
        source = next;
    }
    return source;
}

// The cells along an edge of a row-major 3x3, in the order the edge is walked.
constexpr std::array<std::array<std::size_t, 3>, 4> kEdgeCells = {{
    {0, 1, 2},  // top, left to right
    {2, 5, 8},  // right, top to bottom
    {8, 7, 6},  // bottom, right to left
    {6, 3, 0},  // left, bottom to top
}};

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
    return {normal[0] + column * right[0] + row * up[0],
            normal[1] + column * right[1] + row * up[1],
            normal[2] + column * right[2] + row * up[2]};
}

[[nodiscard]] int dot(const Vec3& a, const Vec3& b) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

[[nodiscard]] Face face_with_centre(const solver::Cube& cube, Color colour) {
    for (const Face face : solver::kAllFaces) {
        if (cube.face(face)[4] == colour) {
            return face;
        }
    }
    return Face::U;
}

struct SideMatch {
    Face face = Face::U;
    std::array<bool, 4> possible{};  // which quarter-turns the side may be in
    float distance = 0.0F;           // of the best
    bool close_enough = false;       // the best is within kCornerFitDistance
};

[[nodiscard]] SideMatch match_side(const ObliqueSide& side, const solver::Cube& cube,
                                   const SessionColours& colours) {
    // The centre names the side.
    std::size_t nearest = 0;
    float nearest_distance = std::numeric_limits<float>::max();
    for (std::size_t colour = 0; colour < colours.size(); ++colour) {
        const float distance = colour_distance(side.lab[4], colours[colour]);
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
            sum += colour_distance(side.lab[cell],
                                   colours[static_cast<std::size_t>(outside[source[cell]])]);
        }
        distances[static_cast<std::size_t>(turns)] = sum / 9.0F;
        best = std::min(best, sum / 9.0F);
    }
    match.distance = best;
    match.close_enough = best <= kCornerFitDistance;
    for (std::size_t turns = 0; turns < 4; ++turns) {
        match.possible[turns] = distances[turns] <= best + kCornerTurnTolerance &&
                                distances[turns] <= kCornerFitDistance;
    }
    return match;
}

// Whether sides a and b, in turns ra and rb, meet along the edge that the read
// puts them on, with their stickers paired as the cube has them.
[[nodiscard]] bool edge_holds(const SharedEdge& edge, const SideMatch& a, int ra,
                              const SideMatch& b, int rb) {
    const int a_edge = ((to_int(edge.on_first) - ra) % 4 + 4) % 4;
    const int b_edge = ((to_int(edge.on_second) - rb) % 4 + 4) % 4;
    if (across(a.face, a_edge) != b.face || across(b.face, b_edge) != a.face) {
        return false;
    }
    const Vec3 normal_a = solver::detail::direction(a.face);
    const Vec3 normal_b = solver::detail::direction(b.face);
    const std::array<std::size_t, 9> source_a = source_after(ra);
    const std::array<std::size_t, 9> source_b = source_after(rb);
    const auto& cells_a = kEdgeCells[static_cast<std::size_t>(to_int(edge.on_first))];
    const auto& cells_b = kEdgeCells[static_cast<std::size_t>(to_int(edge.on_second))];
    // The coordinate along which the edge runs: the one neither normal has.
    std::size_t along = 0;
    for (std::size_t axis = 0; axis < 3; ++axis) {
        if (normal_a[axis] == 0 && normal_b[axis] == 0) {
            along = axis;
        }
    }
    for (std::size_t t = 0; t < 3; ++t) {
        const Vec3 pa = position_of(a.face, source_a[cells_a[t]]);
        const Vec3 pb = position_of(b.face, source_b[cells_b[2 - t]]);
        if (pa[along] != pb[along] || dot(pa, normal_b) != 1 || dot(pb, normal_a) != 1) {
            return false;
        }
    }
    return true;
}

}  // namespace

CornerFit fit_corner(const CornerRead& read, const solver::Cube& cube,
                     const SessionColours& colours) {
    CUBE_ZONE_SCOPED_N("vision.corner_view.fit_corner");
    CornerFit fit;
    if (read.sides.size() < 2 || read.edges.empty()) {
        return fit;
    }
    std::vector<SideMatch> matches;
    float total = 0.0F;
    bool all_close = true;
    for (const ObliqueSide& side : read.sides) {
        const SideMatch match = match_side(side, cube, colours);
        total += match.distance;
        all_close = all_close && match.close_enough;
        matches.push_back(match);
    }
    fit.distance = total / static_cast<float>(matches.size());
    if (!all_close) {
        return fit;
    }

    for (const SharedEdge& edge : read.edges) {
        const SideMatch& a = matches[edge.first];
        const SideMatch& b = matches[edge.second];
        if (a.face == b.face || a.face == solver::opposite(b.face)) {
            return fit;
        }
        bool holds = false;
        for (int ra = 0; ra < 4 && !holds; ++ra) {
            for (int rb = 0; rb < 4 && !holds; ++rb) {
                holds = a.possible[static_cast<std::size_t>(ra)] &&
                        b.possible[static_cast<std::size_t>(rb)] && edge_holds(edge, a, ra, b, rb);
            }
        }
        if (!holds) {
            return fit;
        }
    }
    fit.fits = true;
    return fit;
}

}  // namespace cube::vision
