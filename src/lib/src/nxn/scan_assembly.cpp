#include <cube/nxn/scan_assembly.hpp>

#include <cube/nxn/validate.hpp>
#include <cube/profiling.hpp>
#include <cube/solver/search_work.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <utility>
#include <vector>

namespace cube::nxn {

namespace {

// The cube position a centre colour pins a side to: the inverse of
// canonical_color(), which is the scheme the 3x3 assembly pins by as well.
constexpr Face face_for_centre(Color colour) noexcept {
    switch (colour) {
        case Color::Yellow:
            return Face::U;
        case Color::White:
            return Face::D;
        case Color::Red:
            return Face::L;
        case Color::Orange:
            return Face::R;
        case Color::Green:
            return Face::F;
        case Color::Blue:
            return Face::B;
    }
    return Face::U;
}

// The three pairs of opposite colours, which no edge or corner carries two of.
constexpr int axis_pair_of(Color colour) noexcept {
    switch (colour) {
        case Color::White:
        case Color::Yellow:
            return 0;
        case Color::Orange:
        case Color::Red:
            return 1;
        case Color::Green:
        case Color::Blue:
            return 2;
    }
    return 0;
}

// The stickers that share a piece and so cannot show two colours of one axis
// pair: the pairs on an edge cubie (a wing, or the middle of an edge) and the
// triples on a corner. Taken from the geometry, so that it is the cube's own
// layout and not a second copy of it; each is filed under the lowest face it
// touches, which is when a search that turns the faces from the sixth to the
// first can look at it, every face it touches having been turned by then.
struct Seams {
    std::array<std::vector<std::array<std::uint16_t, 2>>, 6> edges;
    std::array<std::vector<std::array<std::uint16_t, 3>>, 6> corners;
};

[[nodiscard]] Seams build_seams(const Geometry& geometry) {
    Seams seams;
    const auto lowest = [&geometry](std::initializer_list<std::uint16_t> stickers) {
        std::size_t low = 6;
        for (const std::uint16_t sticker : stickers) {
            low = std::min(low, static_cast<std::size_t>(geometry.face_of(sticker)));
        }
        return low;
    };
    for (const auto& corner : geometry.corners()) {
        seams.corners.at(lowest({corner[0], corner[1], corner[2]})).push_back(corner);
    }
    for (const auto& orbit : geometry.wing_orbits()) {
        for (const WingSlot& slot : orbit) {
            seams.edges.at(lowest({slot.primary, slot.secondary}))
                .push_back({slot.primary, slot.secondary});
        }
    }
    if (geometry.size() % 2 == 1) {
        for (std::size_t location = 0; location < Geometry::kEdgeLocations; ++location) {
            const auto& edge = geometry.mid_edge(location);
            seams.edges.at(lowest({edge[0], edge[1]})).push_back(edge);
        }
    }
    return seams;
}

[[nodiscard]] const Seams& seams_of(int size) {
    static std::array<std::once_flag, kMaxSize + 1> built;
    static std::array<std::unique_ptr<Seams>, kMaxSize + 1> seams;
    const auto index = static_cast<std::size_t>(size);
    std::call_once(built.at(index), [&] {
        seams.at(index) = std::make_unique<Seams>(build_seams(Geometry::of(size)));
    });
    return *seams.at(index);
}

// One quarter-turn of a row-major N x N grid, as the 3x3 assembly turns one: the
// sticker that ends up at (row, col) came from (N - 1 - col, row). The search
// tries all four powers of it on every side, so which way round it is cannot
// change what can be reached.
[[nodiscard]] std::vector<Color> rotated_clockwise(const std::vector<Color>& side, int size) {
    std::vector<Color> out(side.size());
    for (int row = 0; row < size; ++row) {
        for (int col = 0; col < size; ++col) {
            out.at((static_cast<std::size_t>(row) * static_cast<std::size_t>(size)) +
                   static_cast<std::size_t>(col)) =
                side.at((static_cast<std::size_t>(size - 1 - col) * static_cast<std::size_t>(size)) +
                        static_cast<std::size_t>(row));
        }
    }
    return out;
}

// The sides, each set at the cube position its centre sticker pins it to, and
// on D, R and B mirrored left to right: NCube's layout is the camera's view on
// U, F and L and its mirror on the other three, which no quarter-turn bridges.
// Which mirror is immaterial (the other differs by a half-turn, which the
// search absorbs).
struct PinnedSides {
    std::array<std::vector<Color>, 6> base;  // by Face
    std::array<std::size_t, 6> input_index_of_face{};
};

[[nodiscard]] std::optional<PinnedSides> pin_sides(int size, const ScannedSides& sides) {
    if (size < kMinSize || size > kMaxSize || size % 2 == 0) {
        return std::nullopt;
    }
    const auto n = static_cast<std::size_t>(size);
    const std::size_t middle = ((n - 1) / 2 * n) + ((n - 1) / 2);
    PinnedSides pinned;
    std::array<bool, 6> taken{};
    for (std::size_t input = 0; input < sides.size(); ++input) {
        const std::vector<Color>& side = sides.at(input);
        if (side.size() != n * n) {
            return std::nullopt;
        }
        const Face face = face_for_centre(side.at(middle));
        const auto slot = static_cast<std::size_t>(face);
        if (taken.at(slot)) {
            return std::nullopt;
        }
        taken.at(slot) = true;
        pinned.input_index_of_face.at(slot) = input;
        pinned.base.at(slot) = side;
        if (face == Face::D || face == Face::R || face == Face::B) {
            for (std::size_t row = 0; row < n; ++row) {
                std::ranges::reverse(
                    pinned.base.at(slot).begin() + static_cast<std::ptrdiff_t>(row * n),
                    pinned.base.at(slot).begin() + static_cast<std::ptrdiff_t>((row + 1) * n));
            }
        }
    }
    return pinned;
}

// Every colour is on N^2 stickers (turning a side only moves them about it), or
// no combination is a cube.
[[nodiscard]] bool colour_counts_hold(int size, const std::array<std::vector<Color>, 6>& base) {
    std::array<std::size_t, 6> counts{};
    for (const auto& side : base) {
        for (const Color colour : side) {
            ++counts.at(static_cast<std::size_t>(colour));
        }
    }
    const auto each = static_cast<std::size_t>(size) * static_cast<std::size_t>(size);
    return std::ranges::all_of(counts, [each](std::size_t count) { return count == each; });
}

// The 4^6 turns of the sides, in the order of a base-4 number with face 0 the
// lowest digit, and each cube they make that validate() passes handed to
// `visit`, which says whether to go on. As the 3x3 search does it, the faces
// are turned from the sixth to the first, a combination is given up as soon as
// the faces turned so far put two stickers of one axis pair on a piece they all
// share, and a side that looks the same turned is tried once. What that skips
// is only what validate() would have refused or an earlier combination had
// already made.
template <typename Visit>
void walk_cubes(int size, const PinnedSides& pinned, const Visit& visit) {
    if (!colour_counts_hold(size, pinned.base)) {
        return;
    }
    const auto area = static_cast<std::size_t>(size) * static_cast<std::size_t>(size);
    std::array<std::array<std::vector<Color>, 4>, 6> rotations;
    for (std::size_t face = 0; face < 6; ++face) {
        rotations.at(face).at(0) = pinned.base.at(face);
        for (std::size_t quarter = 1; quarter < 4; ++quarter) {
            rotations.at(face).at(quarter) =
                rotated_clockwise(rotations.at(face).at(quarter - 1), size);
        }
    }
    std::array<std::array<std::size_t, 4>, 6> quarters{};
    std::array<std::size_t, 6> distinct{};
    for (std::size_t face = 0; face < 6; ++face) {
        for (std::size_t quarter = 0; quarter < 4; ++quarter) {
            const auto& turned = rotations.at(face);
            bool seen = false;
            for (std::size_t before = 0; before < distinct.at(face) && !seen; ++before) {
                seen = turned.at(quarters.at(face).at(before)) == turned.at(quarter);
            }
            if (!seen) {
                quarters.at(face).at(distinct.at(face)++) = quarter;
            }
        }
    }

    const Seams& seams = seams_of(size);
    std::vector<Color> candidate(6 * area, Color::White);
    const auto seams_hold = [&](std::size_t face) {
        const auto pair = [&candidate](std::uint16_t sticker) {
            return axis_pair_of(candidate.at(sticker));
        };
        for (const auto& edge : seams.edges.at(face)) {
            if (pair(edge[0]) == pair(edge[1])) {
                return false;
            }
        }
        for (const auto& corner : seams.corners.at(face)) {
            const int a = pair(corner[0]);
            const int b = pair(corner[1]);
            const int c = pair(corner[2]);
            if (a == b || a == c || b == c) {
                return false;
            }
        }
        return true;
    };

    bool going = true;
    std::uint64_t nodes = 0;
    const solver::detail::SearchWorkScope work([&nodes] { return nodes; });
    const auto turn = [&](const auto& self, std::size_t face) -> void {
        for (std::size_t at = 0; at < distinct.at(face) && going; ++at) {
            const std::vector<Color>& side = rotations.at(face).at(quarters.at(face).at(at));
            std::ranges::copy(side, candidate.begin() + static_cast<std::ptrdiff_t>(face * area));
            ++nodes;
            if (!seams_hold(face)) {
                continue;
            }
            if (face > 0) {
                self(self, face - 1);
                continue;
            }
            if (validate(size, candidate).ok()) {
                going = visit(candidate);
            }
        }
    };
    turn(turn, 5);
}

}  // namespace

std::vector<NCube> assemble_all_scanned_sides(int size, const ScannedSides& sides,
                                              std::size_t limit) {
    CUBE_ZONE_SCOPED_N("nxn.assemble_scan");
    std::vector<NCube> survivors;
    if (limit == 0) {
        return survivors;
    }
    const std::optional<PinnedSides> pinned = pin_sides(size, sides);
    if (!pinned) {
        return survivors;
    }
    walk_cubes(size, *pinned, [&](const std::vector<Color>& facelets) {
        const Result<NCube> cube = NCube::from_facelets(size, facelets);
        if (cube && std::ranges::find(survivors, *cube) == survivors.end()) {
            survivors.push_back(*cube);
        }
        return survivors.size() < limit;
    });
    return survivors;
}

Result<NCube> assemble_scanned_sides(int size, const ScannedSides& sides) {
    // Two are enough to know it is not one.
    std::vector<NCube> cubes = assemble_all_scanned_sides(size, sides, 2);
    if (cubes.size() != 1) {
        return std::unexpected(CubeError::invalid_cube_state);
    }
    return std::move(cubes.front());
}

}  // namespace cube::nxn
