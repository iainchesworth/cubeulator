#include <cube/nxn/geometry.hpp>

#include <algorithm>
#include <cassert>
#include <map>
#include <memory>
#include <mutex>
#include <ranges>
#include <utility>

namespace cube::nxn {

namespace {

// A point or direction in the cube's integer space. Coordinates are doubled
// and centred, so a cubie of an N-cube sits at an odd or even value in
// [-(N-1), N-1] by parity of N and a turn is exact in integers at every size:
// the cubie centres are -2, 0, 2 for a 3-cube and -3, -1, 1, 3 for a 4-cube.
struct Vec3 {
    int x = 0;
    int y = 0;
    int z = 0;
};

[[nodiscard]] constexpr int component(Vec3 v, Axis axis) noexcept {
    switch (axis) {
        case Axis::X:
            return v.x;
        case Axis::Y:
            return v.y;
        case Axis::Z:
            return v.z;
    }
    return 0;
}

[[nodiscard]] constexpr Vec3 unit(Axis axis) noexcept {
    switch (axis) {
        case Axis::X:
            return {.x = 1, .y = 0, .z = 0};
        case Axis::Y:
            return {.x = 0, .y = 1, .z = 0};
        case Axis::Z:
            return {.x = 0, .y = 0, .z = 1};
    }
    return {};
}

[[nodiscard]] constexpr Vec3 outward_normal(Face face) noexcept {
    const Vec3 along = unit(axis_of(face));
    const int sign = is_positive(face) ? 1 : -1;
    return {.x = along.x * sign, .y = along.y * sign, .z = along.z * sign};
}

[[nodiscard]] constexpr int dot(Vec3 a, Vec3 b) noexcept {
    return (a.x * b.x) + (a.y * b.y) + (a.z * b.z);
}

[[nodiscard]] constexpr Vec3 cross(Vec3 a, Vec3 b) noexcept {
    return {
        .x = (a.y * b.z) - (a.z * b.y),
        .y = (a.z * b.x) - (a.x * b.z),
        .z = (a.x * b.y) - (a.y * b.x),
    };
}

// One clockwise quarter turn of `v` about the positive end of `axis`, seen
// from that end. The same formula Cube's own geometry uses for a face turn
// (cube.cpp's rotate_quarter): the part of `v` along the axis stays, the
// perpendicular part is turned by cross(v, axis).
[[nodiscard]] constexpr Vec3 quarter_turn(Vec3 v, Axis axis) noexcept {
    const Vec3 n = unit(axis);
    const int along = dot(v, n);
    const Vec3 across = cross(v, n);
    return {.x = (n.x * along) + across.x,
            .y = (n.y * along) + across.y,
            .z = (n.z * along) + across.z};
}

// A sticker as geometry: the cubie it is on and the way it faces.
struct Sticker {
    Vec3 cubie;
    Vec3 normal;
};

// The sticker at `at` - the doubled-coordinate form of cube.cpp's
// position_of, which at N = 3 it equals exactly. U and D use (x, z) =
// (col, row); R and L use (y, z) = (-row, col); F and B use (x, y) = (col,
// -row), all measured from the cube's centre.
[[nodiscard]] constexpr Sticker sticker_at(Position at, int size) noexcept {
    const int reach = size - 1;
    const int a = (2 * at.col) - reach;
    const int b = (2 * at.row) - reach;
    const Vec3 normal = outward_normal(at.face);
    switch (at.face) {
        case Face::U:
            return {.cubie = {.x = a, .y = reach, .z = b}, .normal = normal};
        case Face::D:
            return {.cubie = {.x = a, .y = -reach, .z = b}, .normal = normal};
        case Face::R:
            return {.cubie = {.x = reach, .y = -b, .z = a}, .normal = normal};
        case Face::L:
            return {.cubie = {.x = -reach, .y = -b, .z = a}, .normal = normal};
        case Face::F:
            return {.cubie = {.x = a, .y = -b, .z = reach}, .normal = normal};
        case Face::B:
            return {.cubie = {.x = a, .y = -b, .z = -reach}, .normal = normal};
    }
    return {};
}

[[nodiscard]] constexpr Face face_from_normal(Vec3 n) noexcept {
    if (n.x != 0) {
        return n.x > 0 ? Face::R : Face::L;
    }
    if (n.y != 0) {
        return n.y > 0 ? Face::U : Face::D;
    }
    return n.z > 0 ? Face::F : Face::B;
}

// The inverse of sticker_at: which (face, row, col) a sticker is.
[[nodiscard]] constexpr Position position_of(Sticker s, int size) noexcept {
    const int reach = size - 1;
    const Face face = face_from_normal(s.normal);
    switch (face) {
        case Face::U:
        case Face::D:
            return {.face = face, .row = (s.cubie.z + reach) / 2, .col = (s.cubie.x + reach) / 2};
        case Face::R:
        case Face::L:
            return {.face = face, .row = (reach - s.cubie.y) / 2, .col = (s.cubie.z + reach) / 2};
        case Face::F:
        case Face::B:
            return {.face = face, .row = (reach - s.cubie.y) / 2, .col = (s.cubie.x + reach) / 2};
    }
    return {};
}

// A sticker's facelet number, which is face * N^2 + row * N + col.
[[nodiscard]] std::uint16_t facelet_of(Sticker s, int size) noexcept {
    const Position p = position_of(s, size);
    const auto n = static_cast<std::size_t>(size);
    return static_cast<std::uint16_t>((static_cast<std::size_t>(p.face) * n * n) +
                                      (static_cast<std::size_t>(p.row) * n) +
                                      static_cast<std::size_t>(p.col));
}

[[nodiscard]] std::vector<Sticker> make_stickers(int size) {
    const auto n = static_cast<std::size_t>(size);
    std::vector<Sticker> stickers;
    stickers.reserve(6 * n * n);
    for (const Face face : kAllFaces) {
        for (int row = 0; row < size; ++row) {
            for (int col = 0; col < size; ++col) {
                stickers.push_back(sticker_at({.face = face, .row = row, .col = col}, size));
            }
        }
    }
    return stickers;
}

// The three kinds of layer turn of every layer, indexed
// [(axis * N + layer) * 3 + (quarter - 1)].
//
// A turn of layer L of axis A moves exactly the stickers whose cubie has
// coordinate 2L - (N-1) along A, each by the rotation about A's positive end;
// the cubie and the way the sticker faces turn together.
[[nodiscard]] std::vector<Permutation> make_turns(const std::vector<Sticker>& stickers, int size) {
    std::vector<Permutation> turns;
    turns.reserve(static_cast<std::size_t>(3) * static_cast<std::size_t>(size) * 3);
    const int reach = size - 1;
    for (const Axis axis : {Axis::X, Axis::Y, Axis::Z}) {
        for (int layer = 0; layer < size; ++layer) {
            const int plane = (2 * layer) - reach;
            for (int quarter = 1; quarter <= 3; ++quarter) {
                Permutation gather = identity_permutation(stickers.size());
                for (std::size_t i = 0; i < stickers.size(); ++i) {
                    if (component(stickers.at(i).cubie, axis) != plane) {
                        continue;
                    }
                    Sticker moved = stickers.at(i);
                    for (int step = 0; step < quarter; ++step) {
                        moved.cubie = quarter_turn(moved.cubie, axis);
                        moved.normal = quarter_turn(moved.normal, axis);
                    }
                    // The sticker that was at i is at facelet_of(moved) now, so
                    // the sticker showing there afterwards came from i.
                    gather.at(facelet_of(moved, size)) = static_cast<std::uint16_t>(i);
                }
                turns.push_back(std::move(gather));
            }
        }
    }
    return turns;
}

// A whole-cube rotation as the integer matrix that applies it to a vector.
using Matrix = std::array<std::array<int, 3>, 3>;

[[nodiscard]] constexpr Vec3 apply(const Matrix& m, Vec3 v) noexcept {
    const auto row = [&v](const std::array<int, 3>& r) {
        return (r.at(0) * v.x) + (r.at(1) * v.y) + (r.at(2) * v.z);
    };
    return {.x = row(m.at(0)), .y = row(m.at(1)), .z = row(m.at(2))};
}

[[nodiscard]] constexpr Matrix compose(const Matrix& outer, const Matrix& inner) noexcept {
    Matrix result{};
    for (std::size_t r = 0; r < 3; ++r) {
        for (std::size_t c = 0; c < 3; ++c) {
            for (std::size_t k = 0; k < 3; ++k) {
                result.at(r).at(c) += outer.at(r).at(k) * inner.at(k).at(c);
            }
        }
    }
    return result;
}

[[nodiscard]] constexpr Matrix quarter_matrix(Axis axis) noexcept {
    const Vec3 ex = quarter_turn({.x = 1, .y = 0, .z = 0}, axis);
    const Vec3 ey = quarter_turn({.x = 0, .y = 1, .z = 0}, axis);
    const Vec3 ez = quarter_turn({.x = 0, .y = 0, .z = 1}, axis);
    return {{{ex.x, ey.x, ez.x}, {ex.y, ey.y, ez.y}, {ex.z, ey.z, ez.z}}};
}

[[nodiscard]] constexpr Matrix identity_matrix() noexcept {
    return {{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}};
}

[[nodiscard]] Rotation to_rotation(const Matrix& m) noexcept {
    Rotation rotation;
    for (std::size_t column = 0; column < 3; ++column) {
        for (std::size_t row = 0; row < 3; ++row) {
            const int entry = m.at(row).at(column);
            if (entry != 0) {
                rotation.axis.at(column) = static_cast<std::uint8_t>(row);
                rotation.sign.at(column) = entry;
            }
        }
    }
    return rotation;
}

struct Rotations {
    std::vector<Rotation> axes;
    std::vector<Permutation> permutations;
};

// The 24 rotations of the whole cube, by closure of the three quarter turns,
// identity first.
[[nodiscard]] Rotations make_rotations(const std::vector<Sticker>& stickers, int size) {
    std::vector<Matrix> matrices{identity_matrix()};
    for (std::size_t next = 0; next < matrices.size(); ++next) {
        for (const Axis axis : {Axis::X, Axis::Y, Axis::Z}) {
            const Matrix candidate = compose(quarter_matrix(axis), matrices.at(next));
            if (std::ranges::find(matrices, candidate) == matrices.end()) {
                matrices.push_back(candidate);
            }
        }
    }
    assert(matrices.size() == 24);

    Rotations rotations;
    for (const Matrix& m : matrices) {
        rotations.axes.push_back(to_rotation(m));
        Permutation gather(stickers.size());
        for (std::size_t i = 0; i < stickers.size(); ++i) {
            const Sticker& s = stickers.at(i);
            const Sticker moved{.cubie = apply(m, s.cubie), .normal = apply(m, s.normal)};
            gather.at(facelet_of(moved, size)) = static_cast<std::uint16_t>(i);
        }
        rotations.permutations.push_back(std::move(gather));
    }
    return rotations;
}

struct Orbits {
    std::vector<int> of;                               // orbit of each facelet
    std::vector<std::vector<std::uint16_t>> facelets;  // each orbit's facelets
};

// Sticker orbits: stickers a rotation of the cube carries into each other,
// numbered by the smallest facelet in each.
[[nodiscard]] Orbits make_orbits(std::span<const Permutation> rotation_perms, std::size_t count) {
    std::vector<std::size_t> parent(count);
    std::ranges::copy(std::views::iota(std::size_t{0}, count), parent.begin());
    const auto root = [&parent](std::size_t i) {
        while (parent.at(i) != i) {
            parent.at(i) = parent.at(parent.at(i));
            i = parent.at(i);
        }
        return i;
    };
    for (const Permutation& gather : rotation_perms) {
        for (std::size_t i = 0; i < count; ++i) {
            const std::size_t a = root(i);
            const std::size_t b = root(gather.at(i));
            if (a != b) {
                parent.at(std::max(a, b)) = std::min(a, b);
            }
        }
    }

    Orbits orbits;
    orbits.of.assign(count, -1);
    for (std::size_t i = 0; i < count; ++i) {
        const std::size_t r = root(i);
        if (r == i) {
            orbits.of.at(i) = static_cast<int>(orbits.facelets.size());
            orbits.facelets.emplace_back();
        } else {
            orbits.of.at(i) = orbits.of.at(r);
        }
        orbits.facelets.at(static_cast<std::size_t>(orbits.of.at(i)))
            .push_back(static_cast<std::uint16_t>(i));
    }
    return orbits;
}

// Roles, from how many of a cubie's coordinates are at the cube's extreme.
[[nodiscard]] std::vector<Role> make_roles(const std::vector<Sticker>& stickers, int size) {
    const int reach = size - 1;
    const auto extreme = [reach](int v) { return v == reach || v == -reach; };
    std::vector<Role> roles;
    roles.reserve(stickers.size());
    for (const Sticker& s : stickers) {
        const Vec3 c = s.cubie;
        const int extremes = static_cast<int>(extreme(c.x)) + static_cast<int>(extreme(c.y)) +
                             static_cast<int>(extreme(c.z));
        const int middles =
            static_cast<int>(c.x == 0) + static_cast<int>(c.y == 0) + static_cast<int>(c.z == 0);
        if (extremes == 3) {
            roles.push_back(Role::Corner);
        } else if (extremes == 2) {
            roles.push_back(middles == 1 ? Role::MidEdge : Role::Wing);
        } else {
            roles.push_back(middles == 2 ? Role::FaceCentre : Role::Centre);
        }
    }
    return roles;
}

using EdgeLocations = std::array<EdgeLocation, Geometry::kEdgeLocations>;

// The twelve edge locations in a fixed order: the edges around the Y axis
// first (U's four, then D's four), then the four vertical ones.
[[nodiscard]] EdgeLocations make_edge_locations() noexcept {
    EdgeLocations locations{};
    std::size_t next = 0;
    for (const Face vertical : {Face::U, Face::D}) {
        for (const Face side : {Face::F, Face::R, Face::B, Face::L}) {
            locations.at(next++) = {.a = vertical, .b = side};
        }
    }
    for (const Face front_back : {Face::F, Face::B}) {
        for (const Face side : {Face::R, Face::L}) {
            locations.at(next++) = {.a = front_back, .b = side};
        }
    }
    return locations;
}

[[nodiscard]] std::size_t location_between(const EdgeLocations& locations, Face x,
                                           Face y) noexcept {
    for (std::size_t i = 0; i < locations.size(); ++i) {
        const EdgeLocation& loc = locations.at(i);
        if ((loc.a == x && loc.b == y) || (loc.a == y && loc.b == x)) {
            return i;
        }
    }
    return 0;
}

struct Pieces {
    std::vector<std::array<std::uint16_t, 3>> corners;
    std::array<std::array<std::uint16_t, 2>, Geometry::kEdgeLocations> mid_edges{};
    std::vector<std::vector<WingSlot>> wing_orbits;
};

// Group the stickers into cubies, then read off the corners, the middle cubie
// of each edge and the wing slots.
[[nodiscard]] Pieces make_pieces(const std::vector<Sticker>& stickers,
                                 const std::vector<Role>& roles, const std::vector<int>& orbit_of,
                                 const EdgeLocations& locations, int size) {
    const auto area = static_cast<std::size_t>(size) * static_cast<std::size_t>(size);
    const auto face_of = [area](std::uint16_t facelet) {
        return static_cast<Face>(facelet / area);
    };
    std::map<std::array<int, 3>, std::vector<std::uint16_t>> cubies;
    for (std::size_t i = 0; i < stickers.size(); ++i) {
        const Vec3 c = stickers.at(i).cubie;
        cubies[{c.x, c.y, c.z}].push_back(static_cast<std::uint16_t>(i));
    }

    Pieces pieces;
    std::map<int, std::vector<WingSlot>> wings_by_orbit;
    for (const auto& [at, facelets] : cubies) {
        const Role role = roles.at(facelets.front());
        if (role == Role::Corner) {
            pieces.corners.push_back({facelets.at(0), facelets.at(1), facelets.at(2)});
            continue;
        }
        if (role != Role::Wing && role != Role::MidEdge) {
            continue;
        }
        const std::size_t loc =
            location_between(locations, face_of(facelets.at(0)), face_of(facelets.at(1)));
        if (role == Role::MidEdge) {
            pieces.mid_edges.at(loc) = {facelets.at(0), facelets.at(1)};
            continue;
        }
        std::uint16_t primary = facelets.at(0);
        std::uint16_t secondary = facelets.at(1);
        if (orbit_of.at(secondary) < orbit_of.at(primary)) {
            std::swap(primary, secondary);
        }
        wings_by_orbit[orbit_of.at(primary)].push_back(WingSlot{
            .primary = primary,
            .secondary = secondary,
            .location = static_cast<std::uint8_t>(loc),
        });
    }
    for (auto& [orbit_id, slots] : wings_by_orbit) {
        std::ranges::sort(slots, {}, &WingSlot::primary);
        pieces.wing_orbits.push_back(std::move(slots));
    }
    return pieces;
}

}  // namespace

const Geometry& Geometry::of(int size) {
    assert(size >= kMinSize && size <= kMaxSize);
    static std::array<std::once_flag, static_cast<std::size_t>(kMaxSize) + 1> built;
    static std::array<std::unique_ptr<const Geometry>, static_cast<std::size_t>(kMaxSize) + 1>
        cache;
    const auto slot = static_cast<std::size_t>(size);
    std::call_once(built.at(slot), [slot, size] {
        cache.at(slot) = std::unique_ptr<const Geometry>(new Geometry(size));
    });
    return *cache.at(slot);
}

Geometry::Geometry(int size)
    : size_(size),
      facelet_count_(static_cast<std::size_t>(6) * static_cast<std::size_t>(size) *
                     static_cast<std::size_t>(size)) {
    const std::vector<Sticker> stickers = make_stickers(size);
    turns_ = make_turns(stickers, size);

    Rotations rotations = make_rotations(stickers, size);
    rotations_ = std::move(rotations.axes);
    rotation_perms_ = std::move(rotations.permutations);

    Orbits orbits = make_orbits(rotation_perms_, facelet_count_);
    orbits_ = std::move(orbits.of);
    orbit_facelets_ = std::move(orbits.facelets);

    roles_ = make_roles(stickers, size);
    for (std::size_t o = 0; o < orbit_facelets_.size(); ++o) {
        if (roles_.at(orbit_facelets_.at(o).front()) == Role::Centre) {
            centre_orbits_.push_back(static_cast<int>(o));
        }
    }

    edge_locations_ = make_edge_locations();
    Pieces pieces = make_pieces(stickers, roles_, orbits_, edge_locations_, size);
    corners_ = std::move(pieces.corners);
    mid_edges_ = pieces.mid_edges;
    wing_orbits_ = std::move(pieces.wing_orbits);
}

Position Geometry::position(std::size_t facelet) const noexcept {
    const std::size_t area = face_area();
    const auto within = static_cast<int>(facelet % area);
    return Position{
        .face = static_cast<Face>(facelet / area),
        .row = within / size_,
        .col = within % size_,
    };
}

const Permutation& Geometry::turn(SliceTurn turn) const noexcept {
    const auto base = (static_cast<std::size_t>(turn.axis) * static_cast<std::size_t>(size_) +
                       static_cast<std::size_t>(turn.layer)) *
                      3;
    return turns_.at(base + static_cast<std::size_t>(turn.turns) - 1);
}

SliceTurn Geometry::rotated(SliceTurn turn, std::size_t rotation) const noexcept {
    const Rotation& r = rotations_.at(rotation);
    const auto from = static_cast<std::size_t>(turn.axis);
    const int sign = r.sign.at(from);
    const int plane = (2 * turn.layer) - (size_ - 1);
    return SliceTurn{
        .axis = static_cast<Axis>(r.axis.at(from)),
        .layer = ((sign * plane) + (size_ - 1)) / 2,
        .turns = sign > 0 ? turn.turns : 4 - turn.turns,
    };
}

}  // namespace cube::nxn
