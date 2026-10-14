#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include <cube/nxn/permutation.hpp>
#include <cube/solver/cube.hpp>

// The shape of an N x N x N cube: where each of its 6 * N * N stickers sits,
// what every layer turn does to them, and which stickers are the same kind of
// piece. Everything here is derived once per size from a single rule - a
// layer turn rotates every cubie in the layer, taking each cubie's stickers
// with it - so nothing in this file is written out per size and nothing in it
// is specific to 3. See docs/nxn-solver.md.

namespace cube::nxn {

using solver::Color;
using solver::Face;
using solver::kAllFaces;

// The sizes the tables are built for. The lower bound is the smallest cube
// that has turning layers; the upper bound only caps table memory (a size
// costs 6 * N^2 two-byte entries per layer turn, 9 * N turns).
inline constexpr int kMinSize = 2;
inline constexpr int kMaxSize = 9;

// The three directions a layer can be perpendicular to. X runs left to right
// (L -> R), Y down to up (D -> U) and Z back to front (B -> F), the axes
// Cube's own layout is written in, so the positive end of each is the face
// whose plain letter turns clockwise about it: R, U, F.
enum class Axis : std::uint8_t { X, Y, Z };

[[nodiscard]] constexpr Axis axis_of(Face face) noexcept {
    switch (face) {
        case Face::L:
        case Face::R:
            return Axis::X;
        case Face::U:
        case Face::D:
            return Axis::Y;
        case Face::F:
        case Face::B:
            return Axis::Z;
    }
    return Axis::X;
}

[[nodiscard]] constexpr bool is_positive(Face face) noexcept {
    return face == Face::R || face == Face::U || face == Face::F;
}

[[nodiscard]] constexpr Face face_on(Axis axis, bool positive) noexcept {
    switch (axis) {
        case Axis::X:
            return positive ? Face::R : Face::L;
        case Axis::Y:
            return positive ? Face::U : Face::D;
        case Axis::Z:
            return positive ? Face::F : Face::B;
    }
    return Face::U;
}

// A turn of one layer, in the form every table is indexed by: which axis, how
// far along it (0 at the negative end), how many clockwise quarter turns as
// seen from the positive end. The same layer is reached from either end of
// its axis, so a turn has exactly one such spelling.
struct SliceTurn {
    Axis axis = Axis::X;
    int layer = 0;
    int turns = 1;

    [[nodiscard]] constexpr SliceTurn inverse() const noexcept {
        return SliceTurn{.axis = axis, .layer = layer, .turns = 4 - turns};
    }

    friend constexpr bool operator==(const SliceTurn&, const SliceTurn&) = default;
};

// A turn of one layer as it is written in cubing notation: the layer is named
// by the face it is reached from and how deep it is (0 is that face's own
// outer layer, 1 the layer under it), and `turns` count clockwise quarter
// turns as seen from outside that face. R is {R, 0, 1}, R' is {R, 0, 3}, and
// the layer a 5x5 writes "2R" is {R, 1, 1}. A 3x3 algorithm is therefore a
// list of moves with depth 0, on a cube of any size.
struct LayerMove {
    Face face = Face::U;
    int depth = 0;
    int turns = 1;

    [[nodiscard]] constexpr LayerMove inverse() const noexcept {
        return LayerMove{.face = face, .depth = depth, .turns = 4 - turns};
    }

    friend constexpr bool operator==(const LayerMove&, const LayerMove&) = default;
};

[[nodiscard]] constexpr SliceTurn to_slice_turn(LayerMove move, int size) noexcept {
    const bool positive = is_positive(move.face);
    return SliceTurn{
        .axis = axis_of(move.face),
        .layer = positive ? size - 1 - move.depth : move.depth,
        .turns = positive ? move.turns : 4 - move.turns,
    };
}

// The notation for a slice turn, written from the nearer end of its axis (the
// positive end when the layer is exactly in the middle).
[[nodiscard]] constexpr LayerMove to_layer_move(SliceTurn turn, int size) noexcept {
    const int from_negative = turn.layer;
    const int from_positive = size - 1 - turn.layer;
    if (from_negative < from_positive) {
        return LayerMove{
            .face = face_on(turn.axis, false),
            .depth = from_negative,
            .turns = 4 - turn.turns,
        };
    }
    return LayerMove{.face = face_on(turn.axis, true), .depth = from_positive, .turns = turn.turns};
}

// What a sticker belongs to, by where its cubie sits.
enum class Role : std::uint8_t {
    Corner,      // three stickers; the same on every cube
    Wing,        // an edge cubie that is not the middle of its edge
    MidEdge,     // the middle cubie of an edge (odd sizes only)
    Centre,      // a face cubie that is not the middle of its face
    FaceCentre,  // the middle cubie of a face (odd sizes only)
};

struct Position {
    Face face = Face::U;
    int row = 0;
    int col = 0;
};

// One of the twelve places an edge can be: the edge shared by two faces.
struct EdgeLocation {
    Face a = Face::U;
    Face b = Face::F;
};

// A place for a wing cubie: which edge it is on, and the two stickers it
// carries. `primary` is the one in the lower-numbered sticker orbit; the
// orbits are rotation-invariant, so a wing's primary sticker is always at a
// primary position and a wing can only ever be in a slot in one orientation
// (what looks like a flipped wing is a different piece in that slot).
struct WingSlot {
    std::uint16_t primary = 0;
    std::uint16_t secondary = 0;
    std::uint8_t location = 0;
};

// The 24 orientations of the whole cube, as the signed axis map each one is:
// the image of axis `i` is `sign[i]` times axis `axis[i]`.
struct Rotation {
    std::array<std::uint8_t, 3> axis{0, 1, 2};
    std::array<int, 3> sign{1, 1, 1};
};

class Geometry {
public:
    // The tables for a cube of `size` x `size` x `size`, built on first use
    // and kept for the life of the program. Safe from any thread. `size`
    // outside [kMinSize, kMaxSize] is a precondition violation.
    [[nodiscard]] static const Geometry& of(int size);

    [[nodiscard]] int size() const noexcept { return size_; }
    [[nodiscard]] std::size_t facelet_count() const noexcept { return facelet_count_; }
    [[nodiscard]] std::size_t face_area() const noexcept {
        return static_cast<std::size_t>(size_) * static_cast<std::size_t>(size_);
    }

    // Facelets are numbered face * N^2 + row * N + col (Face's enumerator
    // order, row-major), which for N = 3 is Cube's own numbering.
    [[nodiscard]] std::size_t index(Face face, int row, int col) const noexcept {
        return (static_cast<std::size_t>(face) * face_area()) +
               (static_cast<std::size_t>(row) * static_cast<std::size_t>(size_)) +
               static_cast<std::size_t>(col);
    }
    [[nodiscard]] Position position(std::size_t facelet) const noexcept;
    [[nodiscard]] Face face_of(std::size_t facelet) const noexcept {
        return static_cast<Face>(facelet / face_area());
    }

    // The rearrangement a layer turn makes. `turn.turns` is 1..3; a turn of
    // 0 (or 4) is the identity and has no table.
    [[nodiscard]] const Permutation& turn(SliceTurn turn) const noexcept;

    // All 24 orientations of the whole cube, identity first.
    [[nodiscard]] const std::vector<Rotation>& rotations() const noexcept { return rotations_; }
    [[nodiscard]] const Permutation& rotation_permutation(std::size_t rotation) const noexcept {
        return rotation_perms_.at(rotation);
    }
    // `turn` as seen after the whole cube is rotated: the same physical turn
    // spelled for the rotated cube.
    [[nodiscard]] SliceTurn rotated(SliceTurn turn, std::size_t rotation) const noexcept;

    // --- Pieces ------------------------------------------------------------

    [[nodiscard]] Role role(std::size_t facelet) const noexcept { return roles_.at(facelet); }

    // The sticker orbit: the set of positions the rotations of the whole cube
    // carry a sticker between. A layer turn is part of a rotation of the
    // cube, so it only ever moves a sticker within its orbit.
    [[nodiscard]] int orbit(std::size_t facelet) const noexcept { return orbits_.at(facelet); }
    [[nodiscard]] std::size_t orbit_count() const noexcept { return orbit_facelets_.size(); }
    [[nodiscard]] const std::vector<std::uint16_t>& orbit_facelets(int orbit) const noexcept {
        return orbit_facelets_.at(static_cast<std::size_t>(orbit));
    }
    // The orbits of Centre stickers, in ascending orbit order.
    [[nodiscard]] const std::vector<int>& centre_orbits() const noexcept { return centre_orbits_; }

    // The wing slots, grouped by orbit of the primary sticker: every group is
    // one set of interchangeable places (24 of them for any size from 4).
    [[nodiscard]] const std::vector<std::vector<WingSlot>>& wing_orbits() const noexcept {
        return wing_orbits_;
    }
    // Where a wing slot's edge is, and which two faces it is between.
    static constexpr std::size_t kEdgeLocations = 12;
    [[nodiscard]] const EdgeLocation& edge_location(std::size_t location) const noexcept {
        return edge_locations_.at(location);
    }
    // The stickers of the corner cubies: eight cubies, three stickers each.
    [[nodiscard]] const std::vector<std::array<std::uint16_t, 3>>& corners() const noexcept {
        return corners_;
    }
    // The two stickers of the middle cubie of an edge location (odd sizes).
    // Meaningful only when size() is odd.
    [[nodiscard]] const std::array<std::uint16_t, 2>& mid_edge(
        std::size_t location) const noexcept {
        return mid_edges_.at(location);
    }
    // The facelet of a face's middle cubie (odd sizes).
    [[nodiscard]] std::size_t face_centre(Face face) const noexcept {
        const int middle = (size_ - 1) / 2;
        return index(face, middle, middle);
    }

private:
    explicit Geometry(int size);

    int size_;
    std::size_t facelet_count_;

    // Indexed [axis * size + layer] * 3 + (turns - 1).
    std::vector<Permutation> turns_;
    std::vector<Rotation> rotations_;
    std::vector<Permutation> rotation_perms_;

    std::vector<Role> roles_;
    std::vector<int> orbits_;
    std::vector<std::vector<std::uint16_t>> orbit_facelets_;
    std::vector<int> centre_orbits_;
    std::vector<std::vector<WingSlot>> wing_orbits_;
    std::array<EdgeLocation, kEdgeLocations> edge_locations_{};
    std::vector<std::array<std::uint16_t, 3>> corners_;
    std::array<std::array<std::uint16_t, 2>, kEdgeLocations> mid_edges_{};
};

}  // namespace cube::nxn
