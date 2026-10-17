// Putting a cube right by hand (CubeEdit): the colours of a cube of any size edited a
// sticker, a side or a stroke at a time, with undo, redo and a way back to the
// reading it began as, and a plain answer to whether what is entered is a cube a
// person could hold. No screen and no Qt: what the screen shows is this, said.
#include <catch2/catch_test_macros.hpp>

#include "controllers/cube_edit.hpp"

#include <cube/nxn/cube.hpp>
#include <cube/nxn/geometry.hpp>
#include <cube/nxn/strategy.hpp>
#include <cube/solver/cube.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

using cube::app::CubeEdit;
using cube::app::EditState;
using cube::app::kUnpainted;
using cube::app::parse_side;
using cube::app::Reading;
using cube::nxn::Fault;
using cube::nxn::NCube;
using cube::solver::Color;
using cube::solver::Face;

namespace {

constexpr std::array<int, 7> kSizes = {2, 3, 4, 5, 6, 7, 9};

Reading reading_of(const NCube& cube) {
    Reading reading;
    reading.size = cube.size();
    for (const Color colour : cube.facelets()) {
        reading.stickers.push_back(static_cast<std::int8_t>(colour));
    }
    return reading;
}

// What a person sees of `face`, top-left first: restated here from the layout, not
// borrowed from CubeEdit, so that what it makes of a reading is checked against
// something that is not its own.
std::vector<std::int8_t> seen_from_outside(const NCube& cube, Face face) {
    const int size = cube.size();
    std::vector<std::int8_t> out;
    for (int row = 0; row < size; ++row) {
        for (int col = 0; col < size; ++col) {
            const bool across = face == Face::R || face == Face::B;
            const bool down = face == Face::D;
            out.push_back(static_cast<std::int8_t>(
                cube.at(face, down ? size - 1 - row : row, across ? size - 1 - col : col)));
        }
    }
    return out;
}

NCube scrambled(int size, unsigned seed) {
    return cube::nxn::random_scramble({.size = size, .seed = seed, .moves = 30 + (4 * size)});
}

CubeEdit loaded(const NCube& cube) {
    CubeEdit edit;
    edit.load(reading_of(cube));
    return edit;
}

}  // namespace

TEST_CASE("a reading is shown as a person sees each side, at every size", "[cube_edit]") {
    for (const int size : kSizes) {
        const NCube cube = scrambled(size, 11U);
        const CubeEdit edit = loaded(cube);
        CHECK(edit.size() == size);
        CHECK(edit.from_reading());
        CHECK_FALSE(edit.changed());
        for (const Face face : cube::solver::kAllFaces) {
            const auto seen = seen_from_outside(cube, face);
            for (int i = 0; i < size * size; ++i) {
                INFO("size " << size << " face " << static_cast<int>(face) << " sticker " << i);
                REQUIRE(edit.colour(face, i) == seen.at(static_cast<std::size_t>(i)));
            }
        }
        CHECK(edit.status().state == EditState::Valid);
    }
}

TEST_CASE("a blank cube counts down, then says what is wrong", "[cube_edit]") {
    CubeEdit edit(3);
    CHECK(edit.status().state == EditState::Empty);
    CHECK(edit.status().to_go == 54);
    CHECK_FALSE(edit.from_reading());

    CHECK(edit.paint(Face::U, 0, 0));
    CHECK(edit.status().state == EditState::Partial);
    CHECK(edit.status().to_go == 53);

    // Everything painted, but white ten times and orange eight.
    for (const Face face : cube::solver::kAllFaces) {
        edit.fill(face, static_cast<std::int8_t>(face));
    }
    edit.paint(Face::F, 0, 0);
    const auto status = edit.status();
    REQUIRE(status.state == EditState::Miscount);
    REQUIRE(status.over.size() == 1);
    REQUIRE(status.under.size() == 1);
    CHECK(status.over.front().colour == 0);
    CHECK(status.over.front().count == 10);
    CHECK(status.under.front().colour == 4);  // the front side was painted orange
    CHECK(status.under.front().count == 8);
}

TEST_CASE("an edit that changes nothing is not a step", "[cube_edit]") {
    CubeEdit edit = loaded(scrambled(3, 1U));
    const std::int8_t same = edit.colour(Face::F, 4);
    CHECK_FALSE(edit.paint(Face::F, 4, same));
    CHECK_FALSE(edit.can_undo());
    CHECK_FALSE(edit.paint(Face::F, 9, 0));  // off the side
    CHECK_FALSE(edit.paint(Face::F, -1, 0));
    CHECK_FALSE(edit.paint(Face::F, 0, 6));  // no such colour
    CHECK_FALSE(edit.fill(Face::F, 7));
    CHECK_FALSE(edit.can_undo());
}

TEST_CASE("undo and redo walk back and forth, and a new edit ends the future", "[cube_edit]") {
    CubeEdit edit = loaded(NCube::solved(3).value());
    const std::int8_t was = edit.colour(Face::U, 0);

    REQUIRE(edit.paint(Face::U, 0, 5));
    REQUIRE(edit.paint(Face::U, 1, 5));
    CHECK(edit.changed());
    CHECK(edit.undo());
    CHECK(edit.colour(Face::U, 0) == 5);
    CHECK(edit.colour(Face::U, 1) == was);
    CHECK(edit.can_redo());
    CHECK(edit.redo());
    CHECK(edit.colour(Face::U, 1) == 5);
    CHECK_FALSE(edit.redo());

    CHECK(edit.undo());
    // A new edit after an undo: what was undone cannot come back.
    CHECK(edit.paint(Face::U, 2, 4));
    CHECK_FALSE(edit.can_redo());

    CHECK(edit.undo());
    CHECK(edit.undo());
    CHECK_FALSE(edit.can_undo());
    CHECK_FALSE(edit.undo());
    CHECK(edit.colour(Face::U, 0) == was);
    CHECK_FALSE(edit.changed());
}

TEST_CASE("a stroke over several stickers is one step", "[cube_edit]") {
    CubeEdit edit = loaded(NCube::solved(4).value());
    edit.begin_stroke();
    for (int i = 0; i < 4; ++i) {
        CHECK(edit.paint(Face::F, i, 5));
    }
    edit.end_stroke();
    edit.paint(Face::F, 4, 5);  // outside a stroke: a step of its own

    CHECK(edit.undo());
    CHECK(edit.colour(Face::F, 4) != 5);
    CHECK(edit.colour(Face::F, 3) == 5);
    CHECK(edit.undo());
    for (int i = 0; i < 4; ++i) {
        CHECK(edit.colour(Face::F, i) != 5);
    }
    CHECK_FALSE(edit.can_undo());
}

TEST_CASE("revert goes back to the reading, and can itself be undone", "[cube_edit]") {
    const NCube cube = scrambled(3, 3U);
    CubeEdit edit = loaded(cube);
    edit.fill(Face::R, 0);
    edit.paint(Face::U, 4, 1);
    REQUIRE(edit.changed());

    CHECK(edit.revert());
    CHECK_FALSE(edit.changed());
    for (const Face face : cube::solver::kAllFaces) {
        CHECK(std::vector<std::int8_t>{edit.colour(face, 0)}.front() ==
              seen_from_outside(cube, face).front());
    }
    CHECK_FALSE(edit.revert());  // already there

    CHECK(edit.undo());  // the revert was a step
    CHECK(edit.changed());

    // A blank cube reverts to blank.
    CubeEdit blank(5);
    blank.fill(Face::U, 2);
    CHECK(blank.revert());
    CHECK(blank.status().state == EditState::Empty);
}

TEST_CASE("the sticker the reader doubted stops being doubted once it is painted", "[cube_edit]") {
    NCube cube = scrambled(3, 5U);
    Reading reading = reading_of(cube);
    reading.doubtful.assign(reading.stickers.size(), 0);
    reading.doubtful.at((static_cast<std::size_t>(Face::R) * 9) + 0) = 1;
    CubeEdit edit;
    edit.load(reading);

    // Facelet 0 of R is the top-left in the library's numbering, the top-right as
    // seen from outside.
    CHECK(edit.doubtful(Face::R, 2));
    CHECK(edit.doubtful_on(Face::R) == 1);
    CHECK(edit.doubtful_on(Face::U) == 0);

    // Painting it the colour it already had still settles the doubt: that is the
    // person saying "yes, this one".
    CHECK(edit.paint(Face::R, 2, edit.colour(Face::R, 2)));
    CHECK_FALSE(edit.doubtful(Face::R, 2));
    // ...and the cube is not "changed" by it, only confirmed.
    CHECK_FALSE(edit.changed());
    CHECK(edit.undo());
    CHECK(edit.doubtful(Face::R, 2));
}

TEST_CASE("a side turned in place carries its doubts with it", "[cube_edit]") {
    NCube cube = NCube::solved(3).value();
    Reading reading = reading_of(cube);
    reading.doubtful.assign(reading.stickers.size(), 0);
    reading.doubtful.at(static_cast<std::size_t>(Face::F) * 9) = 1;  // F, top-left
    CubeEdit edit;
    edit.load(reading);
    REQUIRE(edit.doubtful(Face::F, 0));

    // Paint one sticker so the side is not uniform and a turn shows.
    edit.paint(Face::F, 1, 5);
    CHECK(edit.rotate_side(Face::F, 1));
    CHECK(edit.colour(Face::F, 5) == 5);  // top-middle -> right-middle
    CHECK(edit.doubtful(Face::F, 2));     // top-left -> top-right
    CHECK_FALSE(edit.doubtful(Face::F, 0));
    CHECK(edit.doubtful_on(Face::F) == 1);
    CHECK(edit.undo());
    CHECK(edit.doubtful(Face::F, 0));
}

TEST_CASE("describing a side reads letters in reading order", "[cube_edit]") {
    SECTION("three ways of writing the same side") {
        const auto rows = parse_side("WWW/WRW/WWW", 3);
        const auto lines = parse_side("WWW\nWRW\nWWW\n", 3);
        const auto run = parse_side("wwwwrwwww", 3);
        REQUIRE(rows.error.empty());
        CHECK(rows.colours == lines.colours);
        CHECK(rows.colours == run.colours);
        REQUIRE(rows.colours.size() == 9);
        CHECK(rows.colours.at(4) == 5);  // R is red
        CHECK(rows.colours.at(0) == 0);
    }
    SECTION("every letter") {
        const auto all = parse_side("W Y G B O R", 2);  // 6 colours for a 4-sticker side
        CHECK(all.error == "tooMany");
        const auto four = parse_side("YGBO", 2);
        REQUIRE(four.error.empty());
        CHECK(four.colours == std::vector<std::int8_t>{1, 2, 3, 4});
    }
    SECTION("a gap is a sticker left unpainted") {
        const auto gap = parse_side("WW?WW.WWW", 3);
        REQUIRE(gap.error.empty());
        CHECK(gap.colours.at(2) == kUnpainted);
        CHECK(gap.colours.at(5) == kUnpainted);
    }
    SECTION("what is wrong is said, and where") {
        const auto few = parse_side("WWWWWWWW", 3);
        CHECK(few.error == "tooFew");
        CHECK(few.at == 8);
        const auto many = parse_side("WWWWWWWWWW", 3);
        CHECK(many.error == "tooMany");
        CHECK(many.at == 10);
        const auto bad = parse_side("WWW WXW WWW", 3);
        CHECK(bad.error == "unknownLetter");
        CHECK(bad.at == 5);
        CHECK(parse_side("", 3).error == "tooFew");
        // Digits and words are not colours: the letters are not translated.
        CHECK(parse_side("white", 2).error == "unknownLetter");
    }
    SECTION("any size") {
        for (const int size : kSizes) {
            const std::string text(static_cast<std::size_t>(size) * static_cast<std::size_t>(size),
                                   'g');
            const auto side = parse_side(text, size);
            CHECK(side.error.empty());
            CHECK(side.colours.size() == static_cast<std::size_t>(size * size));
        }
    }
}

TEST_CASE("a described side replaces the read one in one step", "[cube_edit]") {
    const NCube cube = scrambled(3, 7U);
    CubeEdit edit = loaded(cube);
    const auto before = seen_from_outside(cube, Face::B);

    std::vector<std::int8_t> yellow(9, 1);
    CHECK(edit.set_side(Face::B, yellow));
    for (int i = 0; i < 9; ++i) {
        CHECK(edit.colour(Face::B, i) == 1);
    }
    CHECK(edit.undo());
    for (int i = 0; i < 9; ++i) {
        CHECK(edit.colour(Face::B, i) == before.at(static_cast<std::size_t>(i)));
    }

    CHECK_FALSE(edit.set_side(Face::B, std::vector<std::int8_t>(8, 1)));  // wrong length
    CHECK_FALSE(edit.set_side(Face::B, std::vector<std::int8_t>(9, 9)));  // no such colour
}

TEST_CASE("turning a side in place is clockwise as seen from outside", "[cube_edit]") {
    for (const int size : kSizes) {
        CubeEdit edit(size);
        // A corner mark: only the top-left sticker is painted.
        edit.paint(Face::D, 0, 3);
        CHECK(edit.rotate_side(Face::D, 1));
        CHECK(edit.colour(Face::D, size - 1) == 3);  // top-left -> top-right
        CHECK(edit.colour(Face::D, 0) == kUnpainted);
        CHECK(edit.rotate_side(Face::D, 1));
        CHECK(edit.colour(Face::D, (size * size) - 1) == 3);  // -> bottom-right
        CHECK(edit.rotate_side(Face::D, -2));
        CHECK(edit.colour(Face::D, 0) == 3);  // back
        // Four quarter turns, or none, change nothing.
        CHECK_FALSE(edit.rotate_side(Face::D, 4));
        CHECK_FALSE(edit.rotate_side(Face::D, 0));
        CHECK_FALSE(edit.rotate_side(Face::U, 1));  // an empty side turns into itself
    }
}

TEST_CASE("an odd size names a side by its middle sticker, an even one has none", "[cube_edit]") {
    const NCube odd = NCube::solved(5).value();
    const CubeEdit edit = loaded(odd);
    for (const Face face : cube::solver::kAllFaces) {
        CHECK(edit.centre(face) == static_cast<std::int8_t>(odd.at(face, 2, 2)));
    }
    const CubeEdit even = loaded(NCube::solved(4).value());
    CHECK(even.centre(Face::U) == kUnpainted);
}

TEST_CASE("a cube that cannot be one is said to be so, at every size", "[cube_edit]") {
    for (const int size : kSizes) {
        const NCube cube = scrambled(size, 21U);
        CubeEdit edit = loaded(cube);
        REQUIRE(edit.status().state == EditState::Valid);

        // Two stickers of different colours swapped: the counts still hold, the cube
        // cannot.
        const int a = 0;
        int b = 1;
        while (edit.colour(Face::U, b) == edit.colour(Face::F, a) && b < size * size) {
            ++b;
        }
        const std::int8_t colour_a = edit.colour(Face::F, a);
        const std::int8_t colour_b = edit.colour(Face::U, b);
        REQUIRE(colour_a != colour_b);
        edit.paint(Face::F, a, colour_b);
        edit.paint(Face::U, b, colour_a);

        INFO("size " << size);
        const auto status = edit.status();
        CHECK(status.state == EditState::Impossible);
        CHECK(status.fault != Fault::None);

        // Putting them back makes it a cube again.
        edit.undo();
        edit.undo();
        CHECK(edit.status().state == EditState::Valid);
    }
}

TEST_CASE("a twisted corner points at a sticker of the corner", "[cube_edit]") {
    NCube cube = NCube::solved(3).value();
    CubeEdit edit = loaded(cube);
    // The up-front-right corner twisted in place: U, F and R stickers cycled.
    const std::int8_t u = edit.colour(Face::U, 8);
    const std::int8_t f = edit.colour(Face::F, 2);
    const std::int8_t r = edit.colour(Face::R, 0);
    edit.paint(Face::U, 8, f);
    edit.paint(Face::F, 2, r);
    edit.paint(Face::R, 0, u);

    const auto status = edit.status();
    REQUIRE(status.state == EditState::Impossible);
    CHECK(status.fault == Fault::TwistedCorner);
    REQUIRE(status.suspect.has_value());
    // One of the three stickers of that corner, as seen from outside.
    const auto place = *status.suspect;
    const bool is_corner = (place.face == Face::U && place.index == 8) ||
                           (place.face == Face::F && place.index == 2) ||
                           (place.face == Face::R && place.index == 0);
    CHECK(is_corner);

    // The 3x3 oracle agrees: the same stickers are not solvable.
    std::vector<Color> stickers;
    for (const std::int8_t colour : edit.stickers()) {
        stickers.push_back(static_cast<Color>(colour));
    }
    const auto held = NCube::from_facelets(3, stickers);
    REQUIRE(held.has_value());
    const auto as_cube3 = held->to_cube3_as_held();
    if (as_cube3.has_value()) {
        CHECK_FALSE(as_cube3->is_solvable());
    }
}

TEST_CASE("clearing all is a step that undoes", "[cube_edit]") {
    CubeEdit edit = loaded(scrambled(4, 2U));
    CHECK(edit.clear_all());
    CHECK(edit.status().state == EditState::Empty);
    CHECK_FALSE(edit.clear_all());
    CHECK(edit.undo());
    CHECK(edit.status().state == EditState::Valid);
    CHECK_FALSE(edit.changed());
}

TEST_CASE("history is bounded", "[cube_edit]") {
    CubeEdit edit(3);
    for (int i = 0; i < 1000; ++i) {
        edit.paint(Face::U, i % 9, static_cast<std::int8_t>((i / 9) % 6));
    }
    int undone = 0;
    while (edit.undo()) {
        ++undone;
    }
    CHECK(undone <= 256);
    CHECK(undone > 0);
}

TEST_CASE("a reading the wrong length is read as blank", "[cube_edit]") {
    CubeEdit edit;
    Reading reading;
    reading.size = 3;
    reading.stickers.assign(10, 0);
    edit.load(reading);
    CHECK(edit.status().state == EditState::Empty);
    CHECK(edit.size() == 3);
}
