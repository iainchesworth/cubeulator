#include <cube/nxn/cube.hpp>
#include <cube/nxn/outside_view.hpp>
#include <cube/profiling.hpp>
#include <cube/solver/hold.hpp>
#include <cube/vision/corner_fit.hpp>
#include <cube/vision/corner_view.hpp>
#include <cube/vision/direct_scan.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <unordered_map>
#include <vector>

// fit_corner for a cube of any size (corner_fit.hpp): whether a candidate cube could have
// produced a corner read. The 3x3's (corner_view.cpp) names each side by its centre; a side
// of an even size has none, and a side of an odd size is as well named by what is on it, so
// here every side is matched to a face of the cube by its N x N colours alone.

namespace cube::vision {

namespace {

using solver::Face;

using Vec3 = std::array<int, 3>;

// A side turned `turns` quarter-turns clockwise in the picture: the cell of the unturned
// side that lands at each cell (the same permutation the 3x3's fit uses).
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): the side's size, then the turns
[[nodiscard]] std::vector<std::size_t> source_after(int n, int turns) {
    const auto width = static_cast<std::size_t>(n);
    std::vector<std::size_t> source(width * width);
    for (std::size_t i = 0; i < source.size(); ++i) {
        source.at(i) = i;
    }
    for (int turn = 0; turn < turns; ++turn) {
        std::vector<std::size_t> next(source.size());
        for (std::size_t i = 0; i < next.size(); ++i) {
            const std::size_t row = i / width;
            const std::size_t column = i % width;
            next.at(i) = source.at(((width - 1 - column) * width) + row);
        }
        source = std::move(next);
    }
    return source;
}

// The cell t along an edge of a row-major N x N side, in the order the edge is walked: the
// top left to right, the right top to bottom, the bottom right to left, the left bottom to
// top.
[[nodiscard]] std::size_t edge_cell(int n, GridEdge edge, std::size_t t) {
    const auto width = static_cast<std::size_t>(n);
    switch (edge) {
        case GridEdge::Top:
            return t;
        case GridEdge::Right:
            return (t * width) + (width - 1);
        case GridEdge::Bottom:
            return (width * width) - 1 - t;
        case GridEdge::Left:
            return (width - 1 - t) * width;
    }
    return 0;
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

// Where a sticker of outside_view(face) sits on the cube, in half-stickers from its middle:
// each coordinate in [-n, n], the faces at +-n and the stickers of a face at -(n-1), -(n-3),
// ... n-1 across it.
[[nodiscard]] Vec3 position_of(Face face, std::size_t cell, int n) {
    const Vec3 normal = solver::detail::direction(face);
    const Vec3 up = solver::detail::direction(solver::top_of(face));
    const Vec3 right = solver::detail::direction(solver::right_of(face));
    const auto width = static_cast<std::size_t>(n);
    const int column = (2 * static_cast<int>(cell % width)) - (n - 1);
    const int row = (n - 1) - (2 * static_cast<int>(cell / width));
    return {
        (n * normal.at(0)) + (column * right.at(0)) + (row * up.at(0)),
        (n * normal.at(1)) + (column * right.at(1)) + (row * up.at(1)),
        (n * normal.at(2)) + (column * right.at(2)) + (row * up.at(2)),
    };
}

[[nodiscard]] int dot(const Vec3& a, const Vec3& b) {
    return (a.at(0) * b.at(0)) + (a.at(1) * b.at(1)) + (a.at(2) * b.at(2));
}

// A face of the candidate and a quarter-turn that lays a side on it.
struct Option {
    Face face = Face::U;
    int turns = 0;
};

// How a side of the read sits against the six faces of a cube: the mean distance
// of its colours from a face's, in each quarter-turn.
using FaceDistances = std::array<std::array<float, 4>, 6>;

// The faces and turns that a side may be, and how near the best sits.
struct SideOptions {
    std::vector<Option> options;
    float distance = 0.0F;
    bool close_enough = false;
};

class Fitter {
public:
    Fitter(const CornerRead& read, const SessionColours& colours, float max_distance)
        : read_(&read), max_distance_(max_distance) {
        const auto width = static_cast<std::size_t>(read.size);
        const std::size_t cells = width * width;
        for (int turns = 0; turns < 4; ++turns) {
            sources_.at(static_cast<std::size_t>(turns)) = source_after(read.size, turns);
        }
        // How far each cell of each side is from each of the six colours, once.
        for (const ObliqueSide& side : read.sides) {
            std::vector<std::array<float, 6>> table(cells);
            for (std::size_t cell = 0; cell < cells; ++cell) {
                for (std::size_t colour = 0; colour < 6; ++colour) {
                    table.at(cell).at(colour) =
                        colour_distance(side.lab.at(cell), colours.at(colour));
                }
            }
            tables_.push_back(std::move(table));
            scores_.emplace_back();
        }
    }

    [[nodiscard]] CornerFit fit(const nxn::NCube& cube) {
        CornerFit result;
        if (read_->sides.size() < 2 || read_->edges.empty()) {
            return result;
        }
        std::array<std::string, 6> keys;
        std::array<std::vector<solver::Color>, 6> views;
        for (const Face face : solver::kAllFaces) {
            const auto at = static_cast<std::size_t>(face);
            views.at(at) = nxn::outside_view(cube, face);
            std::string key;
            key.reserve(views.at(at).size());
            for (const solver::Color colour : views.at(at)) {
                key.push_back(static_cast<char>(colour));
            }
            keys.at(at) = std::move(key);
        }

        std::vector<SideOptions> matches;
        float total = 0.0F;
        bool all_close = true;
        for (std::size_t side = 0; side < read_->sides.size(); ++side) {
            SideOptions match = options_of(side, keys);
            total += match.distance;
            all_close = all_close && match.close_enough;
            matches.push_back(std::move(match));
        }
        result.distance = total / static_cast<float>(matches.size());
        if (!all_close) {
            return result;
        }
        for (const SharedEdge& edge : read_->edges) {
            const SideOptions& a = matches.at(edge.first);
            const SideOptions& b = matches.at(edge.second);
            const bool holds = std::ranges::any_of(a.options, [&](const Option& left) {
                return std::ranges::any_of(
                    b.options, [&](const Option& right) { return edge_holds(edge, left, right); });
            });
            if (!holds) {
                return result;
            }
        }
        result.fits = true;
        return result;
    }

private:
    // The distances of a side to a face of the given colours, remembered: the faces of
    // the cubes six sides make are mostly the same faces.
    [[nodiscard]] const std::array<float, 4>& distances(std::size_t side, const std::string& key) {
        auto& known = scores_.at(side);
        const auto found = known.find(key);
        if (found != known.end()) {
            return found->second;
        }
        std::array<float, 4> by_turn{};
        const auto& table = tables_.at(side);
        for (std::size_t turns = 0; turns < 4; ++turns) {
            const std::vector<std::size_t>& source = sources_.at(turns);
            float sum = 0.0F;
            for (std::size_t cell = 0; cell < source.size(); ++cell) {
                const auto colour =
                    static_cast<std::size_t>(static_cast<unsigned char>(key.at(source.at(cell))));
                sum += table.at(cell).at(colour);
            }
            by_turn.at(turns) = sum / static_cast<float>(source.size());
        }
        return known.emplace(key, by_turn).first->second;
    }

    [[nodiscard]] SideOptions options_of(std::size_t side, const std::array<std::string, 6>& keys) {
        FaceDistances faces{};
        float best = std::numeric_limits<float>::max();
        for (std::size_t face = 0; face < 6; ++face) {
            faces.at(face) = distances(side, keys.at(face));
            best = std::min(best, *std::ranges::min_element(faces.at(face)));
        }
        SideOptions match;
        match.distance = best;
        match.close_enough = best <= max_distance_;
        for (std::size_t face = 0; face < 6; ++face) {
            const float face_best = *std::ranges::min_element(faces.at(face));
            if (face_best > best + kCornerTurnTolerance) {
                continue;
            }
            for (std::size_t turns = 0; turns < 4; ++turns) {
                if (faces.at(face).at(turns) <= face_best + kCornerTurnTolerance &&
                    faces.at(face).at(turns) <= max_distance_) {
                    match.options.push_back(
                        Option{.face = static_cast<Face>(face), .turns = static_cast<int>(turns)});
                }
            }
        }
        return match;
    }

    // Whether sides a and b, in turns ra and rb, meet along the edge that the read puts
    // them on, with their stickers paired as the cube has them.
    [[nodiscard]] bool edge_holds(const SharedEdge& edge, const Option& a, const Option& b) const {
        const int n = read_->size;
        if (a.face == b.face || a.face == solver::opposite(b.face)) {
            return false;
        }
        const int a_edge = (((static_cast<int>(edge.on_first) - a.turns) % 4) + 4) % 4;
        const int b_edge = (((static_cast<int>(edge.on_second) - b.turns) % 4) + 4) % 4;
        if (across(a.face, a_edge) != b.face || across(b.face, b_edge) != a.face) {
            return false;
        }
        const Vec3 normal_a = solver::detail::direction(a.face);
        const Vec3 normal_b = solver::detail::direction(b.face);
        const std::vector<std::size_t>& source_a = sources_.at(static_cast<std::size_t>(a.turns));
        const std::vector<std::size_t>& source_b = sources_.at(static_cast<std::size_t>(b.turns));
        // The coordinate along which the edge runs: the one neither normal has.
        std::size_t along = 0;
        for (std::size_t axis = 0; axis < 3; ++axis) {
            if (normal_a.at(axis) == 0 && normal_b.at(axis) == 0) {
                along = axis;
            }
        }
        const auto width = static_cast<std::size_t>(n);
        for (std::size_t t = 0; t < width; ++t) {
            const Vec3 pa = position_of(a.face, source_a.at(edge_cell(n, edge.on_first, t)), n);
            const Vec3 pb =
                position_of(b.face, source_b.at(edge_cell(n, edge.on_second, width - 1 - t)), n);
            if (pa.at(along) != pb.at(along) || dot(pa, normal_b) != n - 1 ||
                dot(pb, normal_a) != n - 1) {
                return false;
            }
        }
        return true;
    }

    const CornerRead* read_;
    float max_distance_;
    std::array<std::vector<std::size_t>, 4> sources_;
    std::vector<std::vector<std::array<float, 6>>> tables_;
    std::vector<std::unordered_map<std::string, std::array<float, 4>>> scores_;
};

}  // namespace

std::vector<CornerFit> fit_corner_each(const CornerRead& read, const std::vector<nxn::NCube>& cubes,
                                       const SessionColours& colours, float max_distance) {
    CUBE_ZONE_SCOPED_N("vision.corner_view.fit_corner_n");
    std::vector<CornerFit> fits;
    fits.reserve(cubes.size());
    Fitter fitter(read, colours, max_distance);
    for (const nxn::NCube& cube : cubes) {
        fits.push_back(cube.size() == read.size ? fitter.fit(cube) : CornerFit{});
    }
    return fits;
}

}  // namespace cube::vision
