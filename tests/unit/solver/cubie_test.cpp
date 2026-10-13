#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <random>

#include <cube/solver/cube.hpp>
#include <cube/solver/solver.hpp>

#include "cubie.hpp"

using cube::solver::Color;
using cube::solver::Cube;
using cube::solver::Face;
using cube::solver::kAllFaces;
using cube::solver::Move;
using cube::solver::random_scramble;
using cube::solver::detail::CubieCube;
using cube::solver::detail::is_identity;
using cube::solver::detail::kMoveCount;
using cube::solver::detail::move_cubie;
using cube::solver::detail::move_from_index;
using cube::solver::detail::move_index;
using cube::solver::detail::multiply;
using cube::solver::detail::to_cubie;

TEST_CASE("a solved cube is the identity cubie", "[cubie]") {
    const auto cubie = to_cubie(Cube::solved());
    REQUIRE(cubie.has_value());
    CHECK(is_identity(*cubie));
}

TEST_CASE("move indices round-trip and cover each face's three turns", "[cubie]") {
    for (std::size_t i = 0; i < kMoveCount; ++i) {
        const Move move = move_from_index(i);
        CHECK(move_index(move) == i);
        CHECK(move.turns >= 1);
        CHECK(move.turns <= 3);
    }
    CHECK(move_index(Move{Face::U, 1}) == 0);
    CHECK(move_index(Move{Face::B, 3}) == 17);
}

TEST_CASE("a quarter turn has the classical effect on the cubie model", "[cubie]") {
    // U moves the four top corners and edges one place round and nothing
    // else, and turning the top never changes an orientation. These are the
    // standard definitions (Kociemba's cpU/epU), which is what the two
    // solvers' coordinates and tables assume.
    const CubieCube& u = move_cubie(move_index(Move{Face::U, 1}));
    CHECK(u.cp == std::array<std::uint8_t, 8>{3, 0, 1, 2, 4, 5, 6, 7});
    CHECK(u.ep == std::array<std::uint8_t, 12>{3, 0, 1, 2, 4, 5, 6, 7, 8, 9, 10, 11});
    for (const std::uint8_t twist : u.co) {
        CHECK(twist == 0);
    }
    for (const std::uint8_t flip : u.eo) {
        CHECK(flip == 0);
    }

    // F and B quarter turns flip the four edges they move; R and L, and
    // every half turn, flip none. That is what makes <U, D, R2, L2, F2, B2>
    // the group the two-phase search's first phase aims for.
    auto flips = [](const CubieCube& c) {
        int n = 0;
        for (const std::uint8_t f : c.eo) {
            n += f;
        }
        return n;
    };
    CHECK(flips(move_cubie(move_index(Move{Face::F, 1}))) == 4);
    CHECK(flips(move_cubie(move_index(Move{Face::B, 3}))) == 4);
    CHECK(flips(move_cubie(move_index(Move{Face::R, 1}))) == 0);
    CHECK(flips(move_cubie(move_index(Move{Face::F, 2}))) == 0);
}

TEST_CASE("four quarter turns of any face come back to the start", "[cubie]") {
    for (const Face face : kAllFaces) {
        CubieCube state;
        for (int i = 0; i < 4; ++i) {
            state = multiply(state, move_cubie(move_index(Move{face, 1})));
        }
        CHECK(is_identity(state));
    }
}

TEST_CASE("the cubie model agrees with the facelet model on every move from random cubes",
          "[cubie]") {
    // The one test that ties the two representations together: whatever
    // cube Cube::apply produces must decode to the cubie that multiplying
    // by the move cubie predicts. A transcription error in either model, or
    // a wrong orientation convention, fails here.
    std::mt19937 rng(20261013);
    for (int trial = 0; trial < 150; ++trial) {
        const Cube cube = random_scramble(rng(), 5 + static_cast<int>(rng() % 40));
        const auto before = to_cubie(cube);
        REQUIRE(before.has_value());
        for (std::size_t m = 0; m < kMoveCount; ++m) {
            const auto after = to_cubie(cube.apply(move_from_index(m)));
            REQUIRE(after.has_value());
            CHECK(*after == multiply(*before, move_cubie(m)));
        }
    }
}

TEST_CASE("a cube that cannot be reached by turning has no cubie", "[cubie]") {
    // One edge flipped in place: a physically plausible picture (every
    // colour count and piece is right) that no sequence of turns produces.
    std::array<std::array<Color, 9>, 6> facelets{};
    const Cube solved = Cube::solved();
    for (const Face face : kAllFaces) {
        facelets[static_cast<std::size_t>(face)] = solved.face(face);
    }
    std::swap(facelets[static_cast<std::size_t>(Face::U)][7],
              facelets[static_cast<std::size_t>(Face::F)][1]);

    const auto flipped = Cube::from_facelets(facelets);
    REQUIRE(flipped.has_value());
    CHECK_FALSE(to_cubie(*flipped).has_value());
}
