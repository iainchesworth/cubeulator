#include <catch2/catch_test_macros.hpp>

#include <cube/solver/cube.hpp>
#include <cube/solver/hold.hpp>
#include <cube/solver/outside_view.hpp>
#include <cube/solver/solver.hpp>

#include <array>
#include <cstddef>
#include <set>
#include <utility>

using cube::solver::all_holds;
using cube::solver::camera_view;
using cube::solver::canonical_color;
using cube::solver::Color;
using cube::solver::Cube;
using cube::solver::Face;
using cube::solver::Hold;
using cube::solver::kAllFaces;
using cube::solver::kAllPlaces;
using cube::solver::kStatedHold;
using cube::solver::opposite;
using cube::solver::Place;
using cube::solver::place_of;
using cube::solver::rotation;
using cube::solver::side_at;

namespace {

using Vec = std::array<int, 3>;

// A side's outward direction, x from L to R, y from D to U, z from B to F.
[[nodiscard]] Vec direction(Face face) {
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

[[nodiscard]] Vec times(const std::array<int, 9>& m, const Vec& v) {
    return {(m.at(0) * v.at(0)) + (m.at(1) * v.at(1)) + (m.at(2) * v.at(2)),
            (m.at(3) * v.at(0)) + (m.at(4) * v.at(1)) + (m.at(5) * v.at(2)),
            (m.at(6) * v.at(0)) + (m.at(7) * v.at(1)) + (m.at(8) * v.at(2))};
}

[[nodiscard]] int determinant(const std::array<int, 9>& m) {
    return (m.at(0) * ((m.at(4) * m.at(8)) - (m.at(5) * m.at(7)))) -
           (m.at(1) * ((m.at(3) * m.at(8)) - (m.at(5) * m.at(6)))) +
           (m.at(2) * ((m.at(3) * m.at(7)) - (m.at(4) * m.at(6))));
}

// Where the solver keeps the sticker at cubie (x, y, z) of a side - the
// layout documented in cube.hpp, restated.
[[nodiscard]] int solver_index(Face face, const Vec& at) {
    switch (face) {
        case Face::U:
        case Face::D:
            return ((at.at(2) + 1) * 3) + (at.at(0) + 1);
        case Face::L:
        case Face::R:
            return ((1 - at.at(1)) * 3) + (at.at(2) + 1);
        case Face::F:
        case Face::B:
            return ((1 - at.at(1)) * 3) + (at.at(0) + 1);
    }
    return 0;
}

}  // namespace

TEST_CASE("the stated hold is yellow on top, green in front, orange on the right",
          "[solver][hold]") {
    CHECK(canonical_color(side_at(kStatedHold, Place::Top)) == Color::Yellow);
    CHECK(canonical_color(side_at(kStatedHold, Place::Front)) == Color::Green);
    // A real cube held that way has orange on the right and red on the left;
    // the other way round is its mirror image.
    CHECK(canonical_color(side_at(kStatedHold, Place::Right)) == Color::Orange);
    CHECK(canonical_color(side_at(kStatedHold, Place::Left)) == Color::Red);
    CHECK(canonical_color(side_at(kStatedHold, Place::Bottom)) == Color::White);
    CHECK(canonical_color(side_at(kStatedHold, Place::Back)) == Color::Blue);
}

TEST_CASE("there are 24 holds and each puts every side in one place", "[solver][hold]") {
    std::set<std::array<int, 9>> rotations;
    for (const Hold& hold : all_holds()) {
        REQUIRE(cube::solver::is_valid(hold));
        std::set<Face> sides;
        for (const Place place : kAllPlaces) {
            const Face side = side_at(hold, place);
            sides.insert(side);
            CHECK(place_of(hold, side) == place);
        }
        CHECK(sides.size() == 6);
        CHECK(side_at(hold, Place::Bottom) == opposite(side_at(hold, Place::Top)));
        CHECK(side_at(hold, Place::Back) == opposite(side_at(hold, Place::Front)));
        CHECK(side_at(hold, Place::Left) == opposite(side_at(hold, Place::Right)));

        // The rotation takes each side's direction to its place: right is
        // +x, top is +y, front is +z. A determinant of +1 makes it a turn of
        // the cube and not a reflection of it.
        const std::array<int, 9> turn = rotation(hold);
        CHECK(times(turn, direction(side_at(hold, Place::Right))) == Vec{1, 0, 0});
        CHECK(times(turn, direction(side_at(hold, Place::Top))) == Vec{0, 1, 0});
        CHECK(times(turn, direction(side_at(hold, Place::Front))) == Vec{0, 0, 1});
        CHECK(determinant(turn) == 1);
        rotations.insert(turn);
    }
    CHECK(rotations.size() == 24);

    CHECK_FALSE(cube::solver::is_valid(Hold{Face::U, Face::U}));
    CHECK_FALSE(cube::solver::is_valid(Hold{Face::U, Face::D}));
}

TEST_CASE("a camera facing the holder sees the far side, the right way up", "[solver][hold]") {
    // Worked out from where each sticker is in space, not from the unfolded
    // views camera_view() is built on. The camera looks along the holder's
    // +z at the side facing -z; the holder's up is the picture's up, and
    // the picture's right is the holder's left, -x.
    for (const unsigned seed : {3U, 4U, 5U}) {
        const Cube cube = cube::solver::random_scramble(seed);
        for (const Hold& hold : all_holds()) {
            const Face far = side_at(hold, Place::Back);
            const std::array<int, 9> turn = rotation(hold);
            const Vec out = direction(far);

            std::array<Color, 9> expected{};
            int placed = 0;
            for (int x = -1; x <= 1; ++x) {
                for (int y = -1; y <= 1; ++y) {
                    for (int z = -1; z <= 1; ++z) {
                        const Vec at{x, y, z};
                        if ((at.at(0) * out.at(0)) + (at.at(1) * out.at(1)) +
                                (at.at(2) * out.at(2)) !=
                            1) {
                            continue;  // not on the far side
                        }
                        const Vec held = times(turn, at);
                        REQUIRE(held.at(2) == -1);
                        const int row = 1 - held.at(1);
                        const int column = 1 + held.at(0);
                        // Seen from behind, the holder's +x is on the left.
                        expected.at(static_cast<std::size_t>((row * 3) + (2 - column))) =
                            cube.at(far, solver_index(far, at));
                        ++placed;
                    }
                }
            }
            REQUIRE(placed == 9);
            CHECK(camera_view(cube, hold) == expected);
        }
    }
}

TEST_CASE("held as stated, the camera sees the blue side with yellow at the top",
          "[solver][hold]") {
    const Cube cube = cube::solver::random_scramble(8);
    CHECK(camera_view(cube, kStatedHold) == cube::solver::outside_view(cube, Face::B));
}
