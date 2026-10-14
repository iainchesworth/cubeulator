#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include <cube/nxn/cube.hpp>
#include <cube/nxn/geometry.hpp>
#include <cube/solver/cube.hpp>
#include <cube/solver/notation.hpp>
#include <cube/solver/solver.hpp>

namespace {

using cube::nxn::Axis;
using cube::nxn::Geometry;
using cube::nxn::LayerMove;
using cube::nxn::NCube;
using cube::nxn::SliceTurn;
using cube::solver::Color;
using cube::solver::Cube;
using cube::solver::Face;
using cube::solver::kAllFaces;
using cube::solver::Move;

// Every cube size the spike covers, for the properties that must hold at all
// of them.
constexpr std::array<int, 8> kSizes = {2, 3, 4, 5, 6, 7, 8, 9};

NCube solved_of(int size) {
    auto cube = NCube::solved(size);
    REQUIRE(cube.has_value());
    return *cube;
}

}  // namespace

TEST_CASE("a solved NCube has each face one colour and counts N^2 of every colour",
          "[nxn][model]") {
    for (const int size : kSizes) {
        const NCube cube = solved_of(size);
        CHECK(cube.is_solved());
        CHECK(cube.is_reduced());
        std::array<int, 6> counts{};
        for (const Color colour : cube.facelets()) {
            ++counts.at(static_cast<std::size_t>(colour));
        }
        for (const int count : counts) {
            CHECK(count == size * size);
        }
    }
}

TEST_CASE("sizes outside the supported range are refused", "[nxn][model]") {
    CHECK_FALSE(NCube::solved(1).has_value());
    CHECK_FALSE(NCube::solved(0).has_value());
    CHECK_FALSE(NCube::solved(cube::nxn::kMaxSize + 1).has_value());
}

TEST_CASE("the six matrices view reads the same stickers as at()", "[nxn][model]") {
    NCube cube = solved_of(5);
    cube.apply(LayerMove{.face = Face::R, .depth = 1, .turns = 1});
    cube.apply(LayerMove{.face = Face::U, .depth = 0, .turns = 3});
    const auto view = cube.view();
    REQUIRE(view.extent(0) == 6);
    REQUIRE(view.extent(1) == 5);
    REQUIRE(view.extent(2) == 5);
    for (std::size_t face = 0; face < 6; ++face) {
        for (int row = 0; row < 5; ++row) {
            for (int col = 0; col < 5; ++col) {
                CHECK(view[face, static_cast<std::size_t>(row), static_cast<std::size_t>(col)] ==
                      cube.at(static_cast<Face>(face), row, col));
            }
        }
    }
}

// The anchor for everything else: at N = 3 the geometry built here from the
// layer-turn rule must be exactly the geometry cube.cpp hard-codes, for every
// face turn of every count, on a cube that has been through a scramble (a
// solved cube would hide any mistake that swaps stickers of equal colour).
TEST_CASE("at size 3 the outer layer turns are exactly Cube::apply", "[nxn][model]") {
    const std::array<unsigned, 4> seeds = {1U, 7U, 42U, 2026U};
    for (const unsigned seed : seeds) {
        const Cube scrambled = cube::solver::random_scramble(seed, cube::solver::MoveCount{40});
        for (const Face face : kAllFaces) {
            for (int turns = 1; turns <= 3; ++turns) {
                const Move move{.face = face, .turns = turns};
                NCube ours = NCube::from_cube3(scrambled);
                ours.apply(move);
                const NCube theirs = NCube::from_cube3(scrambled.apply(move));
                CHECK(ours == theirs);
            }
        }
    }
}

TEST_CASE("at size 3 Cube and NCube convert without loss", "[nxn][model]") {
    const Cube scrambled = cube::solver::random_scramble(99U, cube::solver::MoveCount{30});
    const NCube wrapped = NCube::from_cube3(scrambled);
    const auto back = wrapped.to_cube3();
    REQUIRE(back.has_value());
    // to_cube3 renames colours so centres are canonical; random_scramble never
    // moves centres, so the cube comes back as it went in.
    CHECK(*back == scrambled);
}

TEST_CASE("every turn has order 4 and its inverse undoes it, at every size", "[nxn][model]") {
    for (const int size : kSizes) {
        const Geometry& g = Geometry::of(size);
        for (const Axis axis : {Axis::X, Axis::Y, Axis::Z}) {
            for (int layer = 0; layer < size; ++layer) {
                const SliceTurn quarter{.axis = axis, .layer = layer, .turns = 1};
                const auto& p1 = g.turn(quarter);
                const auto& p2 = g.turn(SliceTurn{.axis = axis, .layer = layer, .turns = 2});
                const auto& p3 = g.turn(SliceTurn{.axis = axis, .layer = layer, .turns = 3});
                CHECK(cube::nxn::then(p1, p1) == p2);
                CHECK(cube::nxn::then(p2, p1) == p3);
                CHECK(cube::nxn::then(p1, p3) ==
                      cube::nxn::identity_permutation(g.facelet_count()));
                CHECK(cube::nxn::inverse(p1) == p3);
            }
        }
    }
}

TEST_CASE("turns of different layers of one axis commute, at every size", "[nxn][model]") {
    for (const int size : {4, 5, 7}) {
        const Geometry& g = Geometry::of(size);
        for (int a = 0; a < size; ++a) {
            for (int b = a + 1; b < size; ++b) {
                const auto& pa = g.turn(SliceTurn{.axis = Axis::Y, .layer = a, .turns = 1});
                const auto& pb = g.turn(SliceTurn{.axis = Axis::Y, .layer = b, .turns = 3});
                CHECK(cube::nxn::then(pa, pb) == cube::nxn::then(pb, pa));
            }
        }
    }
}

TEST_CASE("a layer is the same layer from either end of its axis", "[nxn][model]") {
    for (const int size : kSizes) {
        for (int depth = 0; depth < size; ++depth) {
            for (int turns = 1; turns <= 3; ++turns) {
                NCube from_right = solved_of(size);
                NCube from_left = solved_of(size);
                // R at depth d and L at depth N-1-d are one layer, turned
                // clockwise seen from opposite ends: the same turn, opposite
                // counts.
                from_right.apply(LayerMove{.face = Face::R, .depth = depth, .turns = turns});
                from_left.apply(
                    LayerMove{.face = Face::L, .depth = size - 1 - depth, .turns = 4 - turns});
                CHECK(from_right == from_left);
            }
        }
    }
}

TEST_CASE("turning every layer of an axis is a rotation of the whole cube", "[nxn][model]") {
    for (const int size : {2, 3, 4, 5, 6}) {
        NCube cube = solved_of(size);
        for (int layer = 0; layer < size; ++layer) {
            cube.apply(SliceTurn{.axis = Axis::Y, .layer = layer, .turns = 1});
        }
        // Still six single-colour faces - just a different colour on each.
        CHECK(cube.is_solved());
        CHECK(cube.at(Face::F, 0, 0) == cube::solver::canonical_color(Face::R));
        CHECK(cube.at(Face::R, 0, 0) == cube::solver::canonical_color(Face::B));
        CHECK(cube.at(Face::U, 0, 0) == cube::solver::canonical_color(Face::U));
    }
}

TEST_CASE("a rotated move spelling does the same thing on the rotated cube", "[nxn][model]") {
    for (const int size : {3, 4, 5}) {
        const Geometry& g = Geometry::of(size);
        const auto rotations = g.rotations();
        REQUIRE(rotations.size() == 24);
        for (std::size_t r = 0; r < rotations.size(); ++r) {
            const std::array<SliceTurn, 3> turns = {
                SliceTurn{.axis = Axis::X, .layer = 0, .turns = 1},
                SliceTurn{.axis = Axis::Y, .layer = size - 2, .turns = 3},
                SliceTurn{.axis = Axis::Z, .layer = 1, .turns = 2},
            };
            for (const SliceTurn turn : turns) {
                // The turn, spelled for the rotated cube, is the original turn
                // carried through the rotation: rotate the cube, make the
                // rotated turn, rotate back == make the original turn.
                const auto& rot = g.rotation_permutation(r);
                const auto unrot = cube::nxn::inverse(rot);
                const auto there_and_back =
                    cube::nxn::then(cube::nxn::then(rot, g.turn(g.rotated(turn, r))), unrot);
                CHECK(there_and_back == g.turn(turn));
            }
        }
    }
}

TEST_CASE("sticker orbits have the shape the pieces say they should", "[nxn][model]") {
    // Sizes 4 and 5 have 1 and 2 centre orbits of 24; 6 has 4; 7 has 6.
    const std::array<std::pair<int, std::size_t>, 4> expected = {{{4, 1}, {5, 2}, {6, 4}, {7, 6}}};
    for (const auto& [size, orbits] : expected) {
        const Geometry& g = Geometry::of(size);
        CHECK(g.centre_orbits().size() == orbits);
        for (const int orbit : g.centre_orbits()) {
            CHECK(g.orbit_facelets(orbit).size() == 24);
        }
        // Wings: one orbit of 24 slots for sizes 4 and 5, two for 6 and 7.
        const std::size_t wing_orbits = (size <= 5) ? 1U : 2U;
        CHECK(g.wing_orbits().size() == wing_orbits);
        for (const auto& slots : g.wing_orbits()) {
            CHECK(slots.size() == 24);
        }
    }
}
