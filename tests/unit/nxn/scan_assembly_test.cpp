#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <random>
#include <string>
#include <utility>
#include <vector>

#include <cube/model/facelet.hpp>
#include <cube/nxn/cube.hpp>
#include <cube/nxn/outside_view.hpp>
#include <cube/nxn/scan_assembly.hpp>
#include <cube/nxn/validate.hpp>
#include <cube/solver/scan_assembly.hpp>
#include <cube/solver/search_work.hpp>

#include "../deterministic_rng.hpp"

// Putting six scanned sides together with cube::nxn::validate standing in for
// the 3x3's solvability test. The sides here are exactly what a camera pointed
// at each side of a cube sees, held in a random quarter-turn and shown in a
// random order; the cube they came from has to be among the cubes they make,
// and at 3x3 the cubes have to be the ones the 3x3 assembly makes.

namespace {

using cube::nxn::NCube;
using cube::nxn::ScannedSides;
using cube::solver::Color;
using cube::solver::Face;

// The sides of `cube` as six views a person would show: each a random quarter
// turn in its plane, the six in a random order.
ScannedSides shown(const NCube& cube, std::mt19937& rng) {
    ScannedSides sides;
    std::array<std::size_t, 6> order = {0, 1, 2, 3, 4, 5};
    std::ranges::shuffle(order, rng);
    const int size = cube.size();
    for (std::size_t i = 0; i < 6; ++i) {
        std::vector<Color> view =
            cube::nxn::outside_view(cube, cube::solver::kAllFaces.at(order.at(i)));
        const int turns = static_cast<int>(rng() % 4);
        for (int turn = 0; turn < turns; ++turn) {
            std::vector<Color> next(view.size());
            for (int row = 0; row < size; ++row) {
                for (int col = 0; col < size; ++col) {
                    next.at((static_cast<std::size_t>(row) * static_cast<std::size_t>(size)) +
                            static_cast<std::size_t>(col)) =
                        view.at((static_cast<std::size_t>(size - 1 - col) *
                                 static_cast<std::size_t>(size)) +
                                static_cast<std::size_t>(row));
                }
            }
            view = std::move(next);
        }
        sides.at(i) = std::move(view);
    }
    return sides;
}

// Whether `made` holds `cube` or the same cube held another way up. A scramble
// turns middle layers too, which carry the centres about, and the assembly puts
// a side where its centre colour says it goes.
bool holds_cube(const std::vector<NCube>& made, const NCube& cube) {
    const cube::nxn::Geometry& geometry = cube::nxn::Geometry::of(cube.size());
    for (std::size_t rotation = 0; rotation < geometry.rotations().size(); ++rotation) {
        NCube held = cube;
        held.apply(geometry.rotation_permutation(rotation));
        if (std::ranges::find(made, held) != made.end()) {
            return true;
        }
    }
    return false;
}

cube::model::FacetColor facet(Color colour) {
    switch (colour) {
        case Color::White:
            return cube::model::FacetColor::white;
        case Color::Yellow:
            return cube::model::FacetColor::yellow;
        case Color::Green:
            return cube::model::FacetColor::green;
        case Color::Blue:
            return cube::model::FacetColor::blue;
        case Color::Orange:
            return cube::model::FacetColor::orange;
        case Color::Red:
            return cube::model::FacetColor::red;
    }
    return cube::model::FacetColor::unknown;
}

}  // namespace

TEST_CASE("six sides of a scrambled cube make that cube, at every odd size", "[nxn][scan]") {
    std::mt19937 rng = cube::test::deterministic_rng(7);
    for (const int size : {3, 5, 7}) {
        CAPTURE(size);
        int ambiguous = 0;
        const int cubes = size == 7 ? 40 : 150;
        for (int i = 0; i < cubes; ++i) {
            const NCube cube = cube::nxn::random_scramble(
                {.size = size, .seed = static_cast<unsigned>(100 + i), .moves = 20 * size});
            const ScannedSides sides = shown(cube, rng);
            const std::vector<NCube> made = cube::nxn::assemble_all_scanned_sides(size, sides, 64);
            REQUIRE_FALSE(made.empty());
            REQUIRE(holds_cube(made, cube));
            for (const NCube& each : made) {
                REQUIRE(cube::nxn::validate(each).ok());
            }
            if (made.size() > 1) {
                ++ambiguous;
            } else {
                REQUIRE(holds_cube({cube::nxn::assemble_scanned_sides(size, sides).value()}, cube));
            }
        }
        // The share of cubes whose six flat sides do not say which they are.
        // (About one in eighty at 3x3.)
        WARN("size " << size << ": " << ambiguous << " of " << cubes << " scrambles are ambiguous");
        CHECK(ambiguous * 10 <= cubes);
    }
}

TEST_CASE("a 3x3 assembles the cubes the 3x3 assembly makes", "[nxn][scan]") {
    std::mt19937 rng = cube::test::deterministic_rng(11);
    for (int i = 0; i < 120; ++i) {
        const NCube cube = cube::nxn::random_scramble(
            {.size = 3, .seed = static_cast<unsigned>(500 + i), .moves = 25});
        const ScannedSides sides = shown(cube, rng);

        std::array<std::array<cube::model::Facelet, 9>, 6> faces{};
        for (std::size_t side = 0; side < 6; ++side) {
            for (std::size_t cell = 0; cell < 9; ++cell) {
                faces.at(side).at(cell) = cube::model::Facelet{
                    .color = facet(sides.at(side).at(cell)),
                    .confidence = 1.0F,
                };
            }
        }
        const std::vector<cube::solver::Cube> old =
            cube::solver::assemble_all_scanned_faces(faces, 64);
        const std::vector<NCube> now = cube::nxn::assemble_all_scanned_sides(3, sides, 64);
        REQUIRE(now.size() == old.size());
        for (const cube::solver::Cube& each : old) {
            REQUIRE(std::ranges::find(now, NCube::from_cube3(each)) != now.end());
        }
    }
}

TEST_CASE("sides that are not a cube make none", "[nxn][scan]") {
    std::mt19937 rng = cube::test::deterministic_rng(3);
    for (const int size : {3, 5}) {
        CAPTURE(size);
        const NCube cube = cube::nxn::random_scramble({.size = size, .seed = 9, .moves = 40});
        const ScannedSides sides = shown(cube, rng);
        const auto area = static_cast<std::size_t>(size) * static_cast<std::size_t>(size);

        SECTION("two stickers of different colours traded across two sides") {
            // Which side is which is by the centres; trade two stickers of
            // different colours that are not centres.
            ScannedSides wrong = sides;
            std::size_t a = 0;
            std::size_t b = 0;
            for (std::size_t cell = 0; cell < area; ++cell) {
                if (cell == area / 2) {
                    continue;
                }
                if (wrong.at(0).at(cell) != wrong.at(1).at(cell) && cell != area / 2) {
                    a = cell;
                    b = cell;
                    break;
                }
            }
            std::swap(wrong.at(0).at(a), wrong.at(1).at(b));
            CHECK(cube::nxn::assemble_all_scanned_sides(size, wrong).empty());
        }
        SECTION("one sticker repainted") {
            ScannedSides wrong = sides;
            Color& sticker = wrong.at(2).at(0);
            sticker = sticker == Color::White ? Color::Red : Color::White;
            CHECK(cube::nxn::assemble_all_scanned_sides(size, wrong).empty());
        }
        SECTION("the same side twice") {
            ScannedSides wrong = sides;
            wrong.at(1) = wrong.at(0);
            CHECK(cube::nxn::assemble_all_scanned_sides(size, wrong).empty());
        }
        SECTION("a side of the wrong size") {
            ScannedSides wrong = sides;
            wrong.at(1).pop_back();
            CHECK(cube::nxn::assemble_all_scanned_sides(size, wrong).empty());
        }
    }
}

TEST_CASE("six sides of an even size make the cube they came from, with no centre to pin them by",
          "[nxn][scan]") {
    std::mt19937 rng = cube::test::deterministic_rng(5);
    for (const int size : {2, 4, 6}) {
        CAPTURE(size);
        const int cubes = size == 6 ? 25 : 60;
        std::size_t most = 0;
        int ambiguous = 0;
        for (int i = 0; i < cubes; ++i) {
            const NCube cube = cube::nxn::random_scramble(
                {.size = size, .seed = static_cast<unsigned>(300 + i), .moves = 20 * size});
            const ScannedSides sides = shown(cube, rng);
            const std::vector<NCube> made =
                cube::nxn::assemble_all_scanned_sides(size, sides, 4096);
            REQUIRE_FALSE(made.empty());
            REQUIRE(holds_cube(made, cube));
            for (const NCube& each : made) {
                REQUIRE(cube::nxn::validate(each).ok());
            }
            most = std::max(most, made.size());
            ambiguous += made.size() > 1 ? 1 : 0;
        }
        WARN("size " << size << ": " << ambiguous << " of " << cubes
                     << " scrambles make more than one cube from six flat sides, up to " << most);
    }
}

TEST_CASE("the search for a cube is bounded", "[nxn][scan]") {
    // 4^6 = 4096 turns is the most there are to look at; pruning by the pieces
    // makes it a small share of that for sides that are no cube, and a 7x7's
    // search is not dearer in nodes than a 3x3's.
    std::mt19937 rng = cube::test::deterministic_rng(13);
    for (const int size : {3, 5, 7}) {
        CAPTURE(size);
        const NCube cube =
            cube::nxn::random_scramble({.size = size, .seed = 21, .moves = 40 * size});
        const ScannedSides sides = shown(cube, rng);
        cube::solver::detail::tally() = {};
        const std::vector<NCube> made = cube::nxn::assemble_all_scanned_sides(size, sides, 16);
        REQUIRE_FALSE(made.empty());
        const std::uint64_t nodes = cube::solver::detail::tally().nodes;
        INFO("nodes " << nodes);
        CHECK(nodes <= 4096U + 1024U + 256U + 64U + 16U + 4U + 1U);
    }
}
