#include <cube/nxn/cube.hpp>

#include <cube/nxn/state.hpp>

#include <algorithm>
#include <array>
#include <cassert>
#include <optional>
#include <random>
#include <utility>

namespace cube::nxn {

namespace {

constexpr std::size_t kColorCount = 6;

[[nodiscard]] constexpr std::size_t slot_of(Color color) noexcept {
    return static_cast<std::size_t>(color);
}

}  // namespace

Result<NCube> NCube::solved(int size) {
    if (size < kMinSize || size > kMaxSize) {
        return std::unexpected(CubeError::invalid_cube_state);
    }
    const Geometry& geometry = Geometry::of(size);
    std::vector<Color> facelets(geometry.facelet_count());
    for (std::size_t i = 0; i < facelets.size(); ++i) {
        facelets.at(i) = solver::canonical_color(geometry.face_of(i));
    }
    return NCube{geometry, std::move(facelets)};
}

Result<NCube> NCube::from_facelets(int size, std::span<const Color> facelets) {
    if (size < kMinSize || size > kMaxSize) {
        return std::unexpected(CubeError::invalid_cube_state);
    }
    const Geometry& geometry = Geometry::of(size);
    if (facelets.size() != geometry.facelet_count()) {
        return std::unexpected(CubeError::invalid_cube_state);
    }
    std::array<std::size_t, kColorCount> counts{};
    for (const Color color : facelets) {
        ++counts.at(slot_of(color));
    }
    const auto expected = static_cast<std::size_t>(size) * static_cast<std::size_t>(size);
    if (!std::ranges::all_of(counts, [expected](std::size_t n) { return n == expected; })) {
        return std::unexpected(CubeError::invalid_cube_state);
    }
    return NCube{geometry, std::vector<Color>(facelets.begin(), facelets.end())};
}

NCube NCube::from_cube3(const solver::Cube& cube) {
    const Geometry& geometry = Geometry::of(3);
    std::vector<Color> facelets;
    facelets.reserve(geometry.facelet_count());
    for (const Face face : kAllFaces) {
        for (const Color color : cube.face(face)) {
            facelets.push_back(color);
        }
    }
    return NCube{geometry, std::move(facelets)};
}

void NCube::apply(const Permutation& permutation) {
    assert(permutation.size() == facelets_.size());
    std::vector<Color> next(facelets_.size());
    std::ranges::transform(permutation, next.begin(),
                           [this](std::uint16_t from) { return facelets_[from]; });
    facelets_ = std::move(next);
}

void NCube::apply(SliceTurn turn) {
    const int quarter = ((turn.turns % 4) + 4) % 4;
    if (quarter == 0) {
        return;
    }
    assert(turn.layer >= 0 && turn.layer < size());
    apply(geometry_->turn(SliceTurn{.axis = turn.axis, .layer = turn.layer, .turns = quarter}));
}

void NCube::apply(LayerMove move) {
    apply(to_slice_turn(move, size()));
}

void NCube::apply(solver::Move move) {
    apply(LayerMove{.face = move.face, .depth = 0, .turns = move.turns});
}

bool NCube::is_solved() const noexcept {
    const std::size_t area = geometry_->face_area();
    for (std::size_t face = 0; face < kAllFaces.size(); ++face) {
        const auto begin = facelets_.begin() + static_cast<std::ptrdiff_t>(face * area);
        if (!std::all_of(begin, begin + static_cast<std::ptrdiff_t>(area),
                         [first = *begin](Color c) { return c == first; })) {
            return false;
        }
    }
    return true;
}

namespace {

// What an edge location shows: the colour on its first face and the colour on
// its second. Paired edges show one such pair all along their length.
struct Display {
    Color on_a;
    Color on_b;

    friend constexpr bool operator==(const Display&, const Display&) = default;
};

[[nodiscard]] Display display_of(const NCube& cube, const EdgeLocation& location,
                                 std::array<std::uint16_t, 2> stickers) {
    const auto facelets = cube.facelets();
    const Color of_first = facelets[stickers.at(0)];
    const Color of_second = facelets[stickers.at(1)];
    if (cube.geometry().face_of(stickers.at(0)) == location.a) {
        return Display{.on_a = of_first, .on_b = of_second};
    }
    return Display{.on_a = of_second, .on_b = of_first};
}

// Every face's centre block is one colour.
[[nodiscard]] bool centre_blocks_are_solid(const NCube& cube) {
    const int n = cube.size();
    for (const Face face : kAllFaces) {
        for (int row = 1; row < n - 1; ++row) {
            for (int col = 1; col < n - 1; ++col) {
                if (cube.at(face, row, col) != cube.at(face, 1, 1)) {
                    return false;
                }
            }
        }
    }
    return true;
}

// Every edge location shows one pair all along it, the middle cubie included.
[[nodiscard]] bool edges_are_paired(const NCube& cube) {
    const Geometry& geometry = cube.geometry();
    std::array<std::optional<Display>, Geometry::kEdgeLocations> shown{};
    const auto agrees = [&shown](std::size_t location, const Display& display) {
        std::optional<Display>& seen = shown.at(location);
        if (!seen) {
            seen = display;
            return true;
        }
        return *seen == display;
    };
    for (const std::vector<WingSlot>& orbit : geometry.wing_orbits()) {
        for (const WingSlot& slot : orbit) {
            const Display display = display_of(cube, geometry.edge_location(slot.location),
                                               {slot.primary, slot.secondary});
            if (!agrees(slot.location, display)) {
                return false;
            }
        }
    }
    if (cube.size() % 2 == 1) {
        for (std::size_t loc = 0; loc < Geometry::kEdgeLocations; ++loc) {
            const Display display =
                display_of(cube, geometry.edge_location(loc), geometry.mid_edge(loc));
            if (!agrees(loc, display)) {
                return false;
            }
        }
    }
    return true;
}

// The six face colours of an odd cube: those of its face centre cubies.
[[nodiscard]] std::array<Color, 6> scheme_from_face_centres(const NCube& cube) {
    std::array<Color, 6> scheme{};
    for (const Face face : kAllFaces) {
        scheme.at(static_cast<std::size_t>(face)) =
            cube.facelets()[cube.geometry().face_centre(face)];
    }
    return scheme;
}

// Which colours share a corner with which.
using Adjacency = std::array<std::array<bool, kColorCount>, kColorCount>;

[[nodiscard]] Adjacency corner_adjacency(const NCube& cube) {
    Adjacency adjacent{};
    for (const auto& corner : cube.geometry().corners()) {
        for (std::size_t a = 0; a < 3; ++a) {
            for (std::size_t b = 0; b < 3; ++b) {
                if (a != b) {
                    adjacent.at(slot_of(cube.facelets()[corner.at(a)]))
                        .at(slot_of(cube.facelets()[corner.at(b)])) = true;
                }
            }
        }
    }
    return adjacent;
}

// The colours on the D, L and B stickers of the corner at down-back-left.
[[nodiscard]] std::optional<std::array<Color, 3>> down_back_left(const NCube& cube) {
    const Geometry& geometry = cube.geometry();
    for (const auto& corner : geometry.corners()) {
        std::array<std::optional<Color>, 6> on_face{};
        for (const std::uint16_t f : corner) {
            on_face.at(static_cast<std::size_t>(geometry.face_of(f))) = cube.facelets()[f];
        }
        const auto d = on_face.at(static_cast<std::size_t>(Face::D));
        const auto l = on_face.at(static_cast<std::size_t>(Face::L));
        const auto b = on_face.at(static_cast<std::size_t>(Face::B));
        if (d && l && b) {
            return std::array<Color, 3>{*d, *l, *b};
        }
    }
    return std::nullopt;
}

// The one colour that shares no corner with `colour`, if there is exactly one.
[[nodiscard]] std::optional<Color> opposite_of(const Adjacency& adjacent, Color colour) {
    std::optional<Color> found;
    for (std::size_t other = 0; other < kColorCount; ++other) {
        if (other == slot_of(colour) || adjacent.at(slot_of(colour)).at(other)) {
            continue;
        }
        if (found) {
            return std::nullopt;  // two candidates: no consistent scheme
        }
        found = static_cast<Color>(other);
    }
    return found;
}

// The six face colours of a cube with no face centres to ask, read off its
// corners. Two colours are opposite exactly when no corner carries both, and
// the corner at down-back-left fixes which of the six faces gets which colour.
[[nodiscard]] Result<std::array<Color, 6>> scheme_from_corners(const NCube& cube) {
    const auto dbl = down_back_left(cube);
    if (!dbl) {
        return std::unexpected(CubeError::invalid_cube_state);
    }
    const Adjacency adjacent = corner_adjacency(cube);
    const auto up = opposite_of(adjacent, dbl->at(0));
    const auto right = opposite_of(adjacent, dbl->at(1));
    const auto front = opposite_of(adjacent, dbl->at(2));
    if (!up || !right || !front) {
        return std::unexpected(CubeError::invalid_cube_state);
    }

    std::array<Color, 6> scheme{};
    scheme.at(static_cast<std::size_t>(Face::D)) = dbl->at(0);
    scheme.at(static_cast<std::size_t>(Face::L)) = dbl->at(1);
    scheme.at(static_cast<std::size_t>(Face::B)) = dbl->at(2);
    scheme.at(static_cast<std::size_t>(Face::U)) = *up;
    scheme.at(static_cast<std::size_t>(Face::R)) = *right;
    scheme.at(static_cast<std::size_t>(Face::F)) = *front;
    return scheme;
}

}  // namespace

bool NCube::is_reduced() const noexcept {
    return centre_blocks_are_solid(*this) && edges_are_paired(*this);
}

Result<std::array<Color, 6>> scheme_of(const NCube& cube) {
    const bool has_face_centres = cube.size() % 2 == 1 && cube.size() >= 3;
    const Result<std::array<Color, 6>> scheme =
        has_face_centres ? Result<std::array<Color, 6>>{scheme_from_face_centres(cube)}
                         : scheme_from_corners(cube);
    if (!scheme) {
        return scheme;
    }
    // Six faces, six different colours.
    std::array<bool, kColorCount> used{};
    for (const Color colour : *scheme) {
        if (used.at(slot_of(colour))) {
            return std::unexpected(CubeError::invalid_cube_state);
        }
        used.at(slot_of(colour)) = true;
    }
    return scheme;
}

Result<solver::Cube> NCube::to_cube3() const {
    const int n = size();
    if (n < 3 || !is_reduced()) {
        return std::unexpected(CubeError::invalid_cube_state);
    }
    // Each face's block colour is the colour the 3x3 must call that face's
    // own; the renaming sends it to canonical_color(face).
    std::array<Color, kColorCount> renamed{};
    std::array<bool, kColorCount> named{};
    const int middle = (n - 1) / 2;
    for (const Face face : kAllFaces) {
        const std::size_t block = slot_of(at(face, middle, middle));
        if (named.at(block)) {
            return std::unexpected(CubeError::invalid_cube_state);
        }
        named.at(block) = true;
        renamed.at(block) = solver::canonical_color(face);
    }

    // Read the 3x3 through the view that maps its stickers onto this cube's,
    // renaming each colour as it is read. Six distinct block colours name
    // every colour there is, so every sticker has a name.
    const Facelets3x3 view = virtual_3x3(*this);
    std::array<std::array<Color, 9>, 6> facelets3{};
    for (const Face face : kAllFaces) {
        for (std::size_t row = 0; row < 3; ++row) {
            for (std::size_t col = 0; col < 3; ++col) {
                const Color seen = view[static_cast<std::size_t>(face), row, col];
                facelets3.at(static_cast<std::size_t>(face)).at((row * 3) + col) =
                    renamed.at(slot_of(seen));
            }
        }
    }
    return solver::Cube::from_facelets(facelets3);
}

NCube random_scramble(ScrambleSpec spec) {
    // The engine's output is specified exactly; the standard distributions'
    // is not, so the draws are made by remainder to be the same everywhere.
    std::mt19937 rng{spec.seed};
    NCube cube = *NCube::solved(spec.size);
    for (int i = 0; i < spec.moves; ++i) {
        cube.apply(SliceTurn{
            .axis = static_cast<Axis>(rng() % 3U),
            .layer = static_cast<int>(rng() % static_cast<unsigned>(spec.size)),
            .turns = 1 + static_cast<int>(rng() % 3U),
        });
    }
    return cube;
}

}  // namespace cube::nxn
