// What the camera read, handed to the review (scan_reading.hpp): the cube's stickers,
// and the ones the colour naming was not sure of marked on the right sticker however
// the camera had the side turned.
#include <catch2/catch_test_macros.hpp>

#include "controllers/cube_edit.hpp"
#include "controllers/scan_reading.hpp"

#include <cube/model/facelet.hpp>
#include <cube/nxn/cube.hpp>
#include <cube/nxn/outside_view.hpp>
#include <cube/nxn/scan_assembly.hpp>
#include <cube/nxn/strategy.hpp>
#include <cube/solver/cube.hpp>
#include <cube/vision/color_resolution.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

using cube::app::CubeEdit;
using cube::app::kDoubtBelow;
using cube::app::kUnpainted;
using cube::app::Reading;
using cube::model::Facelet;
using cube::model::FacetColor;
using cube::nxn::NCube;
using cube::solver::Color;
using cube::solver::Face;
using cube::vision::ColorResolution;

namespace {

FacetColor facet_of(Color colour) {
    switch (colour) {
        case Color::White:
            return FacetColor::white;
        case Color::Yellow:
            return FacetColor::yellow;
        case Color::Green:
            return FacetColor::green;
        case Color::Blue:
            return FacetColor::blue;
        case Color::Orange:
            return FacetColor::orange;
        case Color::Red:
            return FacetColor::red;
    }
    return FacetColor::unknown;
}

using Side = std::array<Facelet, 9>;

Side turned_clockwise(const Side& side) {
    Side out{};
    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 3; ++col) {
            out.at(static_cast<std::size_t>((col * 3) + (2 - row))) =
                side.at(static_cast<std::size_t>((row * 3) + col));
        }
    }
    return out;
}

// The sides of a cube named the way the scan names them: each as the camera saw it
// (outside view), turned by whatever the hand happened to hold it at, listed in
// another order from the cube's (the cube's U is slot 5, D slot 4, and so on), every
// sticker sure of itself. The Nth side in kAllFaces order is turned N % 4 times.
ColorResolution naming_of(const CubeEdit& seen) {
    ColorResolution out;
    std::size_t slot = 5;
    int turns = 0;
    for (const Face face : cube::solver::kAllFaces) {
        Side side{};
        for (int i = 0; i < 9; ++i) {
            side.at(static_cast<std::size_t>(i)) = Facelet{
                .color = facet_of(static_cast<Color>(seen.colour(face, i))), .confidence = 1.0F};
        }
        for (int t = 0; t < turns % 4; ++t) {
            side = turned_clockwise(side);
        }
        out.faces.at(slot--) = std::vector<Facelet>(side.begin(), side.end());
        ++turns;
    }
    return out;
}

Reading reading_of(const NCube& cube) {
    Reading reading;
    reading.size = 3;
    for (const Color colour : cube.facelets()) {
        reading.stickers.push_back(static_cast<std::int8_t>(colour));
    }
    return reading;
}

struct Scanned {
    NCube cube;
    cube::solver::Cube cube3;
    CubeEdit seen;
};

Scanned scanned(unsigned seed) {
    const NCube cube = cube::nxn::random_scramble({.size = 3, .seed = seed, .moves = 40});
    const auto cube3 = cube.to_cube3_as_held();
    REQUIRE(cube3.has_value());
    Scanned out{.cube = cube, .cube3 = *cube3, .seen = CubeEdit{}};
    out.seen.load(reading_of(cube));
    return out;
}

}  // namespace

TEST_CASE("a cube the scan made is handed over sticker for sticker", "[scan_reading]") {
    const Scanned s = scanned(9U);
    const Reading reading = cube::app::reading_from_cube(s.cube3);
    CHECK(reading.size == 3);
    CHECK(reading.upright);
    REQUIRE(reading.stickers.size() == 54);
    for (std::size_t i = 0; i < 54; ++i) {
        CHECK(reading.stickers.at(i) == static_cast<std::int8_t>(s.cube.facelet(i)));
        CHECK(reading.doubtful.at(i) == 0);
    }
}

TEST_CASE("a doubt lands on the sticker it was about, however the side was turned",
          "[scan_reading]") {
    const Scanned s = scanned(12U);

    // Each side in turn has its top-middle sticker (as the cube shows it) doubted. The
    // naming has every side turned by a different amount (naming_of), so the doubt is
    // marked on the sticker that has gone where the top-middle went.
    for (const Face doubted : cube::solver::kAllFaces) {
        ColorResolution naming = naming_of(s.seen);
        std::size_t slot = 5;
        int turns = 0;
        for (const Face face : cube::solver::kAllFaces) {
            if (face == doubted) {
                Side marked{};
                for (auto& sticker : marked) {
                    sticker.confidence = 1.0F;
                }
                marked.at(1).confidence = 0.0F;
                for (int t = 0; t < turns % 4; ++t) {
                    marked = turned_clockwise(marked);
                }
                for (std::size_t i = 0; i < 9; ++i) {
                    naming.faces.at(slot).at(i).confidence = marked.at(i).confidence;
                }
            }
            --slot;
            ++turns;
        }

        CubeEdit edit;
        edit.load(cube::app::reading_from_cube(s.cube3, naming));
        INFO("doubted side " << static_cast<int>(doubted));
        for (const Face face : cube::solver::kAllFaces) {
            for (int i = 0; i < 9; ++i) {
                CHECK(edit.doubtful(face, i) == (face == doubted && i == 1));
            }
        }
        CHECK(edit.doubtful_on(doubted) == 1);
    }
}

TEST_CASE("a confident sticker is not doubted, a borderline one is", "[scan_reading]") {
    const Scanned s = scanned(2U);
    ColorResolution naming = naming_of(s.seen);
    // Slot 5 is the cube's U, unturned: its sticker 0 just under the line, its
    // sticker 1 just over.
    naming.faces.at(5).at(0).confidence = kDoubtBelow - 0.01F;
    naming.faces.at(5).at(1).confidence = kDoubtBelow + 0.01F;
    CubeEdit edit;
    edit.load(cube::app::reading_from_cube(s.cube3, naming));
    CHECK(edit.doubtful(Face::U, 0));
    CHECK_FALSE(edit.doubtful(Face::U, 1));
}

TEST_CASE("a side the naming and the cube disagree about is left unmarked", "[scan_reading]") {
    const Scanned s = scanned(5U);
    ColorResolution naming = naming_of(s.seen);
    // Slot 5 (the cube's U) names a corner differently from the cube, and doubts it.
    auto& side = naming.faces.at(5);
    side.at(0).color = side.at(0).color == FacetColor::red ? FacetColor::blue : FacetColor::red;
    side.at(0).confidence = 0.0F;
    CubeEdit edit;
    edit.load(cube::app::reading_from_cube(s.cube3, naming));
    CHECK(edit.doubtful_on(Face::U) == 0);
}

TEST_CASE("sides that make no cube are placed by their middles, as the camera had them",
          "[scan_reading]") {
    const Scanned s = scanned(14U);
    ColorResolution naming = naming_of(s.seen);
    naming.faces.at(3).at(8).confidence = 0.05F;  // one doubted sticker

    const auto reading = cube::app::reading_from_named_sides(naming);
    REQUIRE(reading.has_value());
    CHECK_FALSE(reading->upright);

    CubeEdit edit;
    edit.load(*reading);
    CHECK_FALSE(edit.upright());
    CHECK(edit.from_reading());
    int doubted = 0;
    for (const Face face : cube::solver::kAllFaces) {
        // The middle names the side: it ended up on the side that wears that colour.
        CHECK(edit.centre(face) == static_cast<std::int8_t>(cube::solver::canonical_color(face)));
        CHECK(edit.painted_on(face) == 9);
        doubted += edit.doubtful_on(face);
    }
    CHECK(doubted == 1);
}

TEST_CASE("a side whose middle is already taken, or is no colour, is not placed twice",
          "[scan_reading]") {
    ColorResolution naming;
    for (auto& side : naming.faces) {
        for (auto& sticker : side) {
            sticker = Facelet{.color = FacetColor::red, .confidence = 1.0F};
        }
    }
    const auto reading = cube::app::reading_from_named_sides(naming);
    REQUIRE(reading.has_value());
    int placed = 0;
    for (const std::int8_t colour : reading->stickers) {
        placed += colour == kUnpainted ? 0 : 1;
    }
    CHECK(placed == 9);  // six sides said red: only one side of the cube is red

    const ColorResolution unnamed;
    CHECK_FALSE(cube::app::reading_from_named_sides(unnamed).has_value());
}

TEST_CASE("the demo misread is not a cube, and putting two stickers back makes it one",
          "[scan_reading][demo]") {
    for (const int size : {2, 3, 4, 5, 7}) {
        for (const unsigned seed : {1U, 2U, 3U, 11U}) {
            INFO("size " << size << " seed " << seed);
            CubeEdit edit;
            edit.load(cube::app::demo_reading(seed, size));
            CHECK(edit.upright());
            // Counts are right and the pieces are not: the kind of misread a count cannot see.
            CHECK(edit.status().state == cube::app::EditState::Impossible);
            CHECK(edit.doubtful_on(Face::D) == 1);

            const auto answer = cube::app::demo_misread_stickers(seed, size);
            int doubted = 0;
            for (const Face face : cube::solver::kAllFaces) {
                doubted += edit.doubtful_on(face);
            }
            CHECK(doubted == 3);
            for (const auto& sticker : answer) {
                const Face face = sticker.side == 'U'   ? Face::U
                                  : sticker.side == 'D' ? Face::D
                                  : sticker.side == 'L' ? Face::L
                                  : sticker.side == 'R' ? Face::R
                                  : sticker.side == 'F' ? Face::F
                                                        : Face::B;
                CHECK(edit.doubtful(face, sticker.index));
                CHECK(edit.colour(face, sticker.index) != sticker.colour);
                edit.paint(face, sticker.index, sticker.colour);
            }
            CHECK(edit.status().state == cube::app::EditState::Valid);
        }
    }
    CubeEdit turned;
    turned.load(cube::app::demo_reading(1U, 3, false));
    CHECK_FALSE(turned.upright());
}

TEST_CASE("a 5x5 scan is read for review at its size, doubts and all", "[scan_reading][nxn]") {
    const NCube cube = cube::nxn::random_scramble({.size = 5, .seed = 6, .moves = 100});
    cube::nxn::ScannedSides sides;
    ColorResolution naming;
    for (std::size_t i = 0; i < 6; ++i) {
        sides.at(i) = cube::nxn::outside_view(cube, cube::solver::kAllFaces.at(i));
        naming.faces.at(i).clear();
        for (const Color colour : sides.at(i)) {
            naming.faces.at(i).push_back(Facelet{.color = facet_of(colour), .confidence = 1.0F});
        }
    }
    // One sticker the naming was unsure of.
    naming.faces.at(2).at(7).confidence = kDoubtBelow - 0.01F;
    // The scan's cube: the sides put together, as the scan puts them.
    const auto made = cube::nxn::assemble_scanned_sides(5, sides);
    REQUIRE(made.has_value());

    const Reading reading = cube::app::reading_from_cube(*made, naming);
    CHECK(reading.size == 5);
    REQUIRE(reading.stickers.size() == 150);
    REQUIRE(reading.doubtful.size() == 150);
    for (std::size_t i = 0; i < 150; ++i) {
        CHECK(reading.stickers.at(i) == static_cast<std::int8_t>(made->facelets()[i]));
    }
    CHECK(std::ranges::count(reading.doubtful, std::uint8_t{1}) == 1);

    // Sides that made no cube are placed by their middles, at their size.
    const auto named = cube::app::reading_from_named_sides(naming);
    REQUIRE(named.has_value());
    CHECK(named->size == 5);
    CHECK_FALSE(named->upright);
    CHECK(std::ranges::count(named->stickers, kUnpainted) == 0);
    CHECK(std::ranges::count(named->doubtful, std::uint8_t{1}) == 1);

    // An even size has no middle to place a side by.
    ColorResolution four;
    for (auto& side : four.faces) {
        side.assign(16, Facelet{.color = FacetColor::red, .confidence = 1.0F});
    }
    CHECK_FALSE(cube::app::reading_from_named_sides(four).has_value());
}
