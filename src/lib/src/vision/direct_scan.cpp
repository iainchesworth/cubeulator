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

// Groups kept. A read that is no side seen so far starts a group of its own,
// and every read is compared with every group, so a scene full of lattices
// that are not a cube (a chessboard, a patterned shirt) must not grow them
// without bound. Six are sides; the rest are strays that rarely reach two
// reads. When there are more than this the least-supported stray goes.
constexpr std::size_t kMaxGroups = 24;

// Lightness and chroma carry shading; hue does not. Down-weighting the
// first two keeps a shaded sticker close to its own colour.
constexpr float kShadingWeight = 0.25F;

// A side of a corner view counts as a read of its side only if it was seen
// whole enough and not too foreshortened. The ten sides the recorded sessions
// show in two-side views (cube_scan corner-colours) sat 3 to 7 Lab units from
// their side but one: a strip with six of nine found and a step under half the
// other, at 12, the same-side distance. Another six-of-nine strip sat at 6, so
// these are a margin and not a measured edge: a sticker the fit placed is what
// a finger looks like, and ten sides are too few to say where the line is.
constexpr int kMinCornerFound = 7;
constexpr float kMinCornerAspect = 0.45F;

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
[[nodiscard]] std::vector<Blob> sticker_blobs(const PreparedFrame& frame) {
    const std::vector<std::vector<cv::Point>>& contours = frame.contours;

    const double frame_area =
        static_cast<double>(frame.working.rows) * static_cast<double>(frame.working.cols);
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

[[nodiscard]] float length(const cv::Point2f& v) {
    return std::hypot(v.x, v.y);
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
[[nodiscard]] SideFit fit_to_side(const std::array<cv::Vec3f, 9>& read,
                                  const std::array<cv::Vec3f, 9>& side, int prior_roll) {
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

// How many corner reads fit each candidate.
[[nodiscard]] std::vector<std::size_t> corner_fits(const std::vector<solver::Cube>& candidates,
                                                   const std::vector<CornerRead>& corners,
                                                   const SessionColours& colours) {
    std::vector<std::size_t> fits(candidates.size(), 0);
    for (const CornerRead& read : corners) {
        for (std::size_t i = 0; i < candidates.size(); ++i) {
            if (fit_corner(read, candidates.at(i), colours).fits) {
                ++fits.at(i);
            }
        }
    }
    return fits;
}

// The candidate among several that the corner reads say it is, if they do:
// the one that most of them fit, and more than any other. A read that fits
// every candidate (it showed sides whose edges the candidates agree on) says
// nothing, and a read that fits none is a blurred frame.
[[nodiscard]] std::optional<std::size_t> choose_by_corners(const std::vector<std::size_t>& fits) {
    const auto best = std::ranges::max_element(fits);
    if (best == fits.end() || *best == 0 || std::count(fits.begin(), fits.end(), *best) != 1) {
        return std::nullopt;
    }
    return static_cast<std::size_t>(best - fits.begin());
}

// The corner of the cube whose view tells the most of the cubes still possible
// apart: of the candidates, those the corner reads fit best (all of them when
// none fits), and of the eight corners the one at which the most pairs of them
// differ on a side the corner shows. A corner shows all nine stickers of each of
// its three sides, so two cubes are told apart there when any of those three
// sides is not the same on both. The centres are the same on every candidate
// (they are what names the sides), so the corner is said by their colours.
[[nodiscard]] std::optional<std::array<solver::Color, 3>> settling_corner_of(
    const std::vector<solver::Cube>& candidates, const std::vector<std::size_t>& fits) {
    std::size_t most = 0;
    for (const std::size_t count : fits) {
        most = std::max(most, count);
    }
    std::vector<const solver::Cube*> remaining;
    for (std::size_t i = 0; i < candidates.size(); ++i) {
        if (i >= fits.size() || fits.at(i) == most) {
            remaining.push_back(&candidates.at(i));
        }
    }
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
                std::size_t told = 0;
                for (std::size_t a = 0; a < remaining.size(); ++a) {
                    for (std::size_t b = a + 1; b < remaining.size(); ++b) {
                        const bool differ = std::ranges::any_of(corner, [&](Face face) {
                            return remaining.at(a)->face(face) != remaining.at(b)->face(face);
                        });
                        told += differ ? 1U : 0U;
                    }
                }
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
    const std::vector<solver::Cube> candidates = solver::assemble_all_scanned_faces(named);
    trace.candidates = candidates.size();
    if (candidates.size() == 1) {
        trace.path = AssemblyPath::single_candidate;
        out.result = ScannedCube{.cube = candidates.front(), .colours = colours_of(named, faces)};
        return out;
    }
    if (candidates.size() > 1) {
        const SessionColours colours = colours_of(named, faces);
        trace.corner_fits = corner_fits(candidates, corners, colours);
        trace.chosen = choose_by_corners(trace.corner_fits);
        if (trace.chosen.has_value()) {
            trace.path = AssemblyPath::chosen_by_corners;
            out.result = ScannedCube{.cube = candidates.at(*trace.chosen), .colours = colours};
            return out;
        }
        trace.path = AssemblyPath::ambiguous;
        trace.error = CubeError::ambiguous_scan;
        trace.settling_corner = settling_corner_of(candidates, trace.corner_fits);
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

// Naming and assembly of six sides, with the working of both.
struct Evaluation {
    std::optional<WhiteReference> white;
    Result<ColorResolution> naming = std::unexpected(CubeError::face_capture_incomplete);
    std::optional<AssemblyTrace> assembly;
    Result<ScannedCube> result = std::unexpected(CubeError::face_capture_incomplete);
};

[[nodiscard]] Evaluation evaluate(const std::array<CapturedFace, 6>& faces,
                                  const std::vector<CornerRead>& corners) {
    Evaluation evaluation;
    // Named in the colours the light would have given under neutral light,
    // once the cube's own white says what the light did. The sides are only
    // ever named here, after all six are in, so nothing seen earlier has to
    // be taken back. The session's colours stay as measured (assemble gets
    // `faces`, not `seen`): a corner or a held read later is measured the
    // same way.
    evaluation.white = find_white_reference(faces);
    std::array<CapturedFace, 6> seen = faces;
    if (evaluation.white.has_value()) {
        for (CapturedFace& face : seen) {
            for (cv::Vec3f& lab : face.lab) {
                lab = neutralised(*evaluation.white, lab);
            }
        }
    }
    evaluation.naming = resolve_colors_explained(
        seen, evaluation.white.has_value() ? std::optional(evaluation.white->side) : std::nullopt);
    if (!evaluation.naming) {
        evaluation.result = std::unexpected(evaluation.naming.error());
        return evaluation;
    }
    Assembled assembled = assemble(evaluation.naming->faces, faces, corners);
    evaluation.result = std::move(assembled.result);
    evaluation.assembly = std::move(assembled.trace);
    if (evaluation.result.has_value() && evaluation.white.has_value()) {
        evaluation.result->cast = evaluation.white->cast;
    }
    return evaluation;
}

}  // namespace

std::optional<SideRead> read_side(const cv::Mat& bgr) {
    const std::optional<PreparedFrame> frame = prepare_frame(bgr);
    return frame.has_value() ? read_side(*frame) : std::nullopt;
}

std::optional<SideRead> read_side(const PreparedFrame& frame) {
    CUBE_ZONE_SCOPED_N("vision.direct_scan.read_side");
    const cv::Mat& working = frame.working;
    const float scale = frame.scale;

    const std::vector<Blob> blobs = sticker_blobs(frame);
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
    const Upright upright = upright_centres(*lattice);
    const std::array<cv::Point2f, 9>& centres = upright.centres;

    SideRead read;
    read.pitch = pitch * scale;
    read.stickers_found = static_cast<int>(lattice->blobs);
    read.found = upright.found;
    for (std::size_t i = 0; i < 9; ++i) {
        const std::optional<cv::Vec3f> lab = sticker_lab(working, centres.at(i), half_width);
        if (!lab.has_value()) {
            return std::nullopt;
        }
        read.lab.at(i) = *lab;
        read.centres.at(i) = centres.at(i) * scale;
    }
    return read;
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
        const SideFit fit = fit_to_side(lab, groups_.at(i).fused, groups_.at(i).roll);
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

void SideScan::add(const std::optional<SideRead>& read) {
    CUBE_ZONE_SCOPED_N("vision.direct_scan.add");
    in_view_.clear();
    if (!read.has_value()) {
        previous_centre_.reset();
        return;
    }
    // A side square to the camera: whatever corner view came before is over.
    previous_corner_centres_.clear();
    const bool steady = previous_centre_.has_value() && held_still(*read, *previous_centre_);
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
void SideScan::merge_alike_groups() {
    bool merged = true;
    while (merged) {
        merged = false;
        for (std::size_t keep = 0; keep < groups_.size() && !merged; ++keep) {
            for (std::size_t other = keep + 1; other < groups_.size() && !merged; ++other) {
                Group& kept = groups_.at(keep);
                // Two groups are each in the turn their side was first seen
                // in, so no turn is more likely than another.
                const SideFit fit = fit_to_side(groups_.at(other).fused, kept.fused, 0);
                if (!fit.same) {
                    continue;
                }
                for (const auto& read : groups_.at(other).reads) {
                    kept.reads.push_back(rolled(read, fit.roll));
                }
                for (const auto& read : groups_.at(other).flat_reads) {
                    kept.flat_reads.push_back(rolled(read, fit.roll));
                }
                kept.fused =
                    fuse(kept.flat_reads.size() >= kMinReads ? kept.flat_reads : kept.reads);
                groups_.erase(groups_.begin() + static_cast<std::ptrdiff_t>(other));
                for (std::size_t& viewed : in_view_) {
                    if (viewed == other) {
                        viewed = keep;
                    } else if (viewed > other) {
                        --viewed;
                    }
                }
                std::ranges::sort(in_view_);
                const auto duplicates = std::ranges::unique(in_view_);
                in_view_.erase(duplicates.begin(), duplicates.end());
                merged = true;
            }
        }
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

}  // namespace

void SideScan::add_corner(const std::optional<CornerRead>& read) {
    in_view_.clear();
    if (!read.has_value()) {
        previous_corner_centres_.clear();
        return;
    }
    // Bounded: the newest are the ones worth having, and each is a few
    // hundred bytes.
    constexpr std::size_t kKeptCornerReads = 48;
    if (corners_.size() >= kKeptCornerReads) {
        corners_.erase(corners_.begin());
    }
    corners_.push_back(*read);

    // Each side that was seen whole and held still is a read of its side.
    std::vector<cv::Point2f> centres;
    for (const ObliqueSide& side : read->sides) {
        const cv::Point2f centre = side.centres.at(4);
        centres.push_back(centre);
        if (!seen_whole_enough(side)) {
            continue;
        }
        const float reach = kSteadyFraction * side.pitch;
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
        Side side{.reads = group.reads.size(),
                  .in_view = std::ranges::find(in_view_, i) != in_view_.end()};
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
    return evaluate(faces, corners_).result;
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
    Evaluation evaluation = evaluate(faces, corners_);
    diagnosis.white = evaluation.white;
    if (evaluation.naming.has_value()) {
        diagnosis.naming = std::move(*evaluation.naming);
    }
    diagnosis.assembly = std::move(evaluation.assembly);
    diagnosis.result = std::move(evaluation.result);
    return diagnosis;
}

}  // namespace cube::vision
