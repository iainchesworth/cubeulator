#pragma once

#include <cube/solver/cube.hpp>
#include <cube/solver/outside_view.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace cube::solver {

// How a cube is being held, as the person holding it sees it: which side is
// on top and which side faces them. Those two fix the other four, so a Hold
// is one of the cube's 24 orientations.
//
// A Move names a side of the cube, not a place in the hand: "R" is the side
// with the orange centre wherever it has got to. The Hold is what turns that
// into "the side on your left".
struct Hold {
    Face up = Face::U;
    Face front = Face::F;

    friend constexpr bool operator==(const Hold&, const Hold&) = default;
};

// The hold the solve screen asks for when it cannot see the cube: yellow on
// top, green facing you.
inline constexpr Hold kStatedHold{.up = Face::U, .front = Face::F};

// A hold is real only if its two sides meet along an edge.
[[nodiscard]] constexpr bool is_valid(const Hold& hold) noexcept {
    return hold.up != hold.front && hold.up != opposite(hold.front);
}

// Where a side is for the person holding the cube.
enum class Place : std::uint8_t { Top, Bottom, Left, Right, Front, Back };

inline constexpr std::array<Place, 6> kAllPlaces = {
    Place::Top, Place::Bottom, Place::Left, Place::Right, Place::Front, Place::Back,
};

namespace detail {

// A side's outward direction with x from L to R, y from D to U and z from B
// to F - the right-handed frame cube.cpp lays the cube out in.
constexpr std::array<int, 3> direction(Face face) noexcept {
    switch (face) {
        case Face::R:
            return {1, 0, 0};
        case Face::L:
            return {-1, 0, 0};
        case Face::U:
            return {0, 1, 0};
        case Face::D:
            return {0, -1, 0};
        case Face::F:
            return {0, 0, 1};
        case Face::B:
            return {0, 0, -1};
    }
    return {};
}

constexpr Face face_along(const std::array<int, 3>& direction) noexcept {
    if (direction.at(0) == 1) {
        return Face::R;
    }
    if (direction.at(0) == -1) {
        return Face::L;
    }
    if (direction.at(1) == 1) {
        return Face::U;
    }
    if (direction.at(1) == -1) {
        return Face::D;
    }
    if (direction.at(2) == 1) {
        return Face::F;
    }
    return Face::B;
}

constexpr std::array<int, 3> cross(const std::array<int, 3>& a,
                                   const std::array<int, 3>& b) noexcept {
    return {
        (a.at(1) * b.at(2)) - (a.at(2) * b.at(1)),
        (a.at(2) * b.at(0)) - (a.at(0) * b.at(2)),
        (a.at(0) * b.at(1)) - (a.at(1) * b.at(0)),
    };
}

}  // namespace detail

// The side at each place. Right is up crossed with front: the same handedness
// as the cube itself, so a hold never describes a mirror image.
[[nodiscard]] constexpr Face side_at(const Hold& hold, Place place) noexcept {
    switch (place) {
        case Place::Top:
            return hold.up;
        case Place::Bottom:
            return opposite(hold.up);
        case Place::Front:
            return hold.front;
        case Place::Back:
            return opposite(hold.front);
        case Place::Right:
            return detail::face_along(
                detail::cross(detail::direction(hold.up), detail::direction(hold.front)));
        case Place::Left:
            return opposite(detail::face_along(
                detail::cross(detail::direction(hold.up), detail::direction(hold.front))));
    }
    return hold.up;
}

[[nodiscard]] constexpr Place place_of(const Hold& hold, Face side) noexcept {
    for (const Place place : kAllPlaces) {
        if (side_at(hold, place) == side) {
            return place;
        }
    }
    return Place::Top;
}

// The rotation that carries the cube from its own frame into the holder's,
// where x is to their right, y is up and z is towards them: a 3x3 matrix,
// row-major, to multiply a column vector. Its rows are the directions, in
// the cube's frame, of the sides at the holder's right, top and front.
[[nodiscard]] constexpr std::array<int, 9> rotation(const Hold& hold) noexcept {
    const std::array<int, 3> right = detail::direction(side_at(hold, Place::Right));
    const std::array<int, 3> up = detail::direction(hold.up);
    const std::array<int, 3> front = detail::direction(hold.front);
    return {
        right.at(0), right.at(1), right.at(2), up.at(0),    up.at(1),
        up.at(2),    front.at(0), front.at(1), front.at(2),
    };
}

// All 24 holds.
[[nodiscard]] constexpr std::array<Hold, 24> all_holds() noexcept {
    std::array<Hold, 24> holds{};
    std::size_t count = 0;
    for (const Face up : kAllFaces) {
        for (const Face front : kAllFaces) {
            if (is_valid(Hold{.up = up, .front = front})) {
                holds.at(count++) = Hold{.up = up, .front = front};
            }
        }
    }
    return holds;
}

// What a camera looking at the holder sees of the cube: the side facing away
// from them, the right way up, as nine stickers row-major from the picture's
// top-left. The camera looks the opposite way to the holder, so the side on
// the holder's left is on the picture's right; nothing is mirrored by that,
// it is simply the cube seen from the other side.
[[nodiscard]] std::array<Color, 9> camera_view(const Cube& cube, const Hold& hold) noexcept;

}  // namespace cube::solver
