// A cube typed in by hand (ColourEntry): painted a sticker at a time as seen from
// outside, at every size, and judged by the library - counted, then asked whether
// it is a cube a person could hold, and if not what is wrong and where.
#include <catch2/catch_test_macros.hpp>

#include "colour_name.hpp"
#include "controllers/colour_entry.hpp"
#include "solve_controller.hpp"

#include <cube/nxn/cube.hpp>
#include <cube/nxn/geometry.hpp>
#include <cube/solver/cube.hpp>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

#include <array>
#include <cstddef>
#include <string>
#include <vector>

using cube::app::ColourEntry;
using cube::nxn::Face;
using cube::nxn::NCube;
using cube::solver::Color;

namespace {

constexpr std::array<const char*, 6> kLetters = {"U", "D", "L", "R", "F", "B"};

// The colours of `cube` as a person would paint them: each side as seen from
// outside, top-left first. (Restated here from the layout, not borrowed from the
// library, so that what the controller makes of it is checked against something
// that is not its own.)
std::vector<Color> seen_from_outside(const NCube& cube, Face face) {
    const int size = cube.size();
    std::vector<Color> stickers;
    for (int row = 0; row < size; ++row) {
        for (int col = 0; col < size; ++col) {
            const bool across = face == Face::R || face == Face::B;
            const bool down = face == Face::D;
            stickers.push_back(
                cube.at(face, down ? size - 1 - row : row, across ? size - 1 - col : col));
        }
    }
    return stickers;
}

// Paints `entry` as `cube` is, sticker by sticker.
void paint_all(ColourEntry& entry, const NCube& cube) {
    entry.begin(cube.size());
    for (const Face face : cube::solver::kAllFaces) {
        const std::vector<Color> stickers = seen_from_outside(cube, face);
        for (std::size_t i = 0; i < stickers.size(); ++i) {
            entry.paint(QString::fromLatin1(kLetters.at(static_cast<std::size_t>(face))),
                        static_cast<int>(i), static_cast<int>(stickers.at(i)));
        }
    }
}

QString state_of(const ColourEntry& entry) {
    return entry.status().value("state").toString();
}

bool wait_until_solved(SolveController& solve) {
    QElapsedTimer waited;
    waited.start();
    while (solve.isSolving() && waited.elapsed() < 60'000) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    }
    return !solve.isSolving();
}

}  // namespace

TEST_CASE("a new cube has nothing painted, at any size", "[qt][controllers][colours]") {
    SolveController solve;
    ColourEntry entry(solve);
    for (int size = cube::nxn::kMinSize; size <= cube::nxn::kMaxSize; ++size) {
        INFO("size " << size);
        entry.begin(size);
        CHECK(entry.size() == size);
        CHECK_FALSE(entry.touched());
        CHECK(state_of(entry) == "empty");
        CHECK(entry.status().value("toGo").toInt() == 6 * size * size);
        const QVariantMap sides = entry.sides();
        REQUIRE(sides.size() == 6);
        for (const char* letter : kLetters) {
            const QVariantList side = sides.value(QString::fromLatin1(letter)).toList();
            CHECK(side.size() == static_cast<qsizetype>(size) * size);
            for (const QVariant& sticker : side) {
                CHECK(sticker.toString().isEmpty());
            }
            CHECK(entry.paintedOn(QString::fromLatin1(letter)) == 0);
        }
    }
    // A size that is no size is the nearest one that is.
    entry.begin(1);
    CHECK(entry.size() == cube::nxn::kMinSize);
    entry.begin(40);
    CHECK(entry.size() == cube::nxn::kMaxSize);
}

TEST_CASE("every side reads back as the cube has it, at every size", "[qt][controllers][colours]") {
    SolveController solve;
    ColourEntry entry(solve);
    for (int size = cube::nxn::kMinSize; size <= cube::nxn::kMaxSize; ++size) {
        INFO("size " << size);
        const NCube cube = cube::nxn::random_scramble({.size = size, .seed = 3, .moves = 60});
        paint_all(entry, cube);
        CHECK(entry.touched());
        const QVariantMap sides = entry.sides();
        for (const Face face : cube::solver::kAllFaces) {
            const QString letter = QString::fromLatin1(kLetters.at(static_cast<std::size_t>(face)));
            const std::vector<Color> expected = seen_from_outside(cube, face);
            const QVariantList side = sides.value(letter).toList();
            REQUIRE(side.size() == static_cast<qsizetype>(expected.size()));
            CHECK(entry.paintedOn(letter) == size * size);
            for (std::size_t i = 0; i < expected.size(); ++i) {
                CAPTURE(letter.toStdString(), i);
                CHECK(side.at(static_cast<qsizetype>(i)).toString() == colour_name(expected.at(i)));
            }
        }
        CHECK(state_of(entry) == "valid");
    }
}

TEST_CASE("a cube painted is solved as the cube it is", "[qt][controllers][colours]") {
    SolveController solve;
    ColourEntry entry(solve);
    for (const int size : {2, 3, 4, 5, 6}) {
        INFO("size " << size);
        const NCube cube = cube::nxn::random_scramble({.size = size, .seed = 8, .moves = 50});
        paint_all(entry, cube);
        const QVariantMap result = entry.solve();
        REQUIRE(result.value("ok").toBool());
        REQUIRE(wait_until_solved(solve));
        CHECK(solve.size() == size);
        // The cube, held the way the solver names its sides (yellow on top, green in
        // front): the same cube, turned as a whole.
        const NCube& start = solve.plan().cube_at(0);
        const cube::nxn::Geometry& geometry = cube::nxn::Geometry::of(size);
        bool same_cube = false;
        for (std::size_t rotation = 0; rotation < geometry.rotations().size(); ++rotation) {
            NCube turned = cube;
            turned.apply(geometry.rotation_permutation(rotation));
            same_cube = same_cube || turned == start;
        }
        CHECK(same_cube);
        const auto scheme = cube::nxn::scheme_of(start);
        REQUIRE(scheme.has_value());
        for (const Face face : cube::solver::kAllFaces) {
            CHECK(scheme->at(static_cast<std::size_t>(face)) ==
                  cube::solver::canonical_color(face));
        }
        solve.goToMove(solve.totalMoves());
        CHECK(solve.plan().current_cube().is_solved());
    }
}

TEST_CASE("a cube not all painted says how many stickers are to go", "[qt][controllers][colours]") {
    SolveController solve;
    ColourEntry entry(solve);
    entry.begin(4);
    entry.paint("U", 0, 0);
    CHECK(entry.touched());
    CHECK(state_of(entry) == "partial");
    CHECK(entry.status().value("toGo").toInt() == (6 * 16) - 1);
    CHECK(entry.paintedOn("U") == 1);
    // The same sticker again changes nothing; another colour on it is still one sticker.
    entry.paint("U", 0, 0);
    entry.paint("U", 0, 3);
    CHECK(entry.status().value("toGo").toInt() == (6 * 16) - 1);
    // A sticker taken away again.
    entry.paint("U", 0, -1);
    CHECK(entry.status().value("toGo").toInt() == 6 * 16);
    CHECK(state_of(entry) == "empty");
    // Nothing to solve yet.
    CHECK_FALSE(entry.solve().value("ok").toBool());
    CHECK_FALSE(solve.isSolving());

    // Whole sides at a time.
    entry.paintSide("D", 1);
    CHECK(entry.paintedOn("D") == 16);
    CHECK(entry.status().value("toGo").toInt() == 5 * 16);
    entry.paintSide("D", -1);
    CHECK(entry.paintedOn("D") == 0);
    entry.paintSide("U", 2);
    entry.clearAll();
    CHECK_FALSE(entry.touched());
    CHECK(state_of(entry) == "empty");
}

TEST_CASE("what is not a sticker is ignored", "[qt][controllers][colours]") {
    SolveController solve;
    ColourEntry entry(solve);
    entry.begin(3);
    entry.paint("X", 0, 0);
    entry.paint("U", -1, 0);
    entry.paint("U", 9, 0);
    entry.paint("U", 0, 6);
    entry.paint("U", 0, -2);
    entry.paintSide("Q", 0);
    entry.paintSide("U", 7);
    CHECK_FALSE(entry.touched());
    CHECK(entry.paintedOn("Q") == 0);
}

TEST_CASE("a colour used too often or too seldom is said, with how many",
          "[qt][controllers][colours]") {
    SolveController solve;
    ColourEntry entry(solve);
    for (const int size : {2, 3, 5}) {
        INFO("size " << size);
        NCube cube = *NCube::solved(size);
        paint_all(entry, cube);
        REQUIRE(state_of(entry) == "valid");
        // One sticker of the top given the colour of the bottom.
        const Color top = cube.at(Face::U, 0, 0);
        const Color bottom = cube.at(Face::D, 0, 0);
        entry.paint("U", 0, static_cast<int>(bottom));
        const QVariantMap status = entry.status();
        REQUIRE(status.value("state").toString() == "miscount");
        const QVariantList over = status.value("over").toList();
        const QVariantList under = status.value("under").toList();
        REQUIRE(over.size() == 1);
        REQUIRE(under.size() == 1);
        CHECK(over.front().toMap().value("colour").toString() == colour_name(bottom));
        CHECK(over.front().toMap().value("count").toInt() == (size * size) + 1);
        CHECK(under.front().toMap().value("colour").toString() == colour_name(top));
        CHECK(under.front().toMap().value("count").toInt() == (size * size) - 1);
        CHECK_FALSE(entry.solve().value("ok").toBool());
    }
}

TEST_CASE("colours that add up but are not a cube say what is wrong, and where",
          "[qt][controllers][colours]") {
    SolveController solve;
    ColourEntry entry(solve);
    for (const int size : {2, 3, 4, 5}) {
        INFO("size " << size);
        const NCube cube = *NCube::solved(size);
        paint_all(entry, cube);
        REQUIRE(state_of(entry) == "valid");

        // The three stickers of one corner turned round in place: a twisted corner.
        const int top_right_front = ((size - 1) * size) + (size - 1);  // U, row size-1, col size-1
        const QVariantMap sides = entry.sides();
        const auto read = [&](const char* side, int index) {
            return sides.value(side).toList().at(index).toString();
        };
        // U's bottom-right sticker, F's top-right sticker and R's top-left sticker meet.
        const QString u = read("U", top_right_front);
        const QString f = read("F", size - 1);
        const QString r = read("R", 0);
        const auto index_of = [](const QString& name) {
            for (int colour = 0; colour < 6; ++colour) {
                if (colour_name(static_cast<Color>(colour)) == name) {
                    return colour;
                }
            }
            return -1;
        };
        entry.paint("U", top_right_front, index_of(f));
        entry.paint("F", size - 1, index_of(r));
        entry.paint("R", 0, index_of(u));
        const QVariantMap status = entry.status();
        INFO(status.value("fault").toString().toStdString());
        REQUIRE(status.value("state").toString() == "impossible");
        CHECK(status.value("fault").toString() == "twistedCorner");
        // It points at a sticker of that corner.
        const QVariantMap suspect = status.value("suspect").toMap();
        REQUIRE(suspect.contains("side"));
        const bool one_of_them =
            (suspect.value("side").toString() == "U" &&
             suspect.value("index").toInt() == top_right_front) ||
            (suspect.value("side").toString() == "F" &&
             suspect.value("index").toInt() == size - 1) ||
            (suspect.value("side").toString() == "R" && suspect.value("index").toInt() == 0);
        CHECK(one_of_them);
        CHECK_FALSE(entry.solve().value("ok").toBool());
        CHECK_FALSE(solve.isSolving());

        // Put back, it is a cube again.
        entry.paint("U", top_right_front, index_of(u));
        entry.paint("F", size - 1, index_of(f));
        entry.paint("R", 0, index_of(r));
        CHECK(state_of(entry) == "valid");
    }
}

TEST_CASE("two stickers swapped between pieces is not a cube either",
          "[qt][controllers][colours]") {
    SolveController solve;
    ColourEntry entry(solve);
    for (const int size : {2, 3, 4, 7}) {
        INFO("size " << size);
        paint_all(entry, *NCube::solved(size));
        // The top-left sticker of the top and of the bottom swapped: no corner is made of that.
        const QVariantMap sides = entry.sides();
        const auto index_of = [](const QString& name) {
            for (int colour = 0; colour < 6; ++colour) {
                if (colour_name(static_cast<Color>(colour)) == name) {
                    return colour;
                }
            }
            return -1;
        };
        const int up = index_of(sides.value("U").toList().front().toString());
        const int down = index_of(sides.value("D").toList().front().toString());
        entry.paint("U", 0, down);
        entry.paint("D", 0, up);
        const QVariantMap status = entry.status();
        CHECK(status.value("state").toString() == "impossible");
        CHECK_FALSE(status.value("fault").toString().isEmpty());
        CHECK_FALSE(entry.solve().value("ok").toBool());
    }
}

namespace {

cube::app::Reading reading_of(const NCube& cube, bool doubting_the_first_of_front) {
    cube::app::Reading reading;
    reading.size = cube.size();
    for (const Color colour : cube.facelets()) {
        reading.stickers.push_back(static_cast<std::int8_t>(colour));
    }
    reading.doubtful.assign(reading.stickers.size(), 0);
    if (doubting_the_first_of_front) {
        const auto n = static_cast<std::size_t>(cube.size());
        reading.doubtful.at(static_cast<std::size_t>(Face::F) * n * n) = 1;
    }
    return reading;
}

}  // namespace

TEST_CASE("a cube the camera read can be corrected and put back", "[qt][controllers][colours]") {
    SolveController solve;
    ColourEntry entry(solve);
    const NCube cube = cube::nxn::random_scramble({.size = 3, .seed = 4, .moves = 40});
    entry.loadReading(reading_of(cube, true));

    CHECK(entry.fromScan());
    CHECK_FALSE(entry.touched());  // nothing to lose by leaving yet
    CHECK(state_of(entry) == "valid");
    CHECK(entry.doubtCount() == 1);
    CHECK(entry.doubts().value("F").toList().front().toBool());
    CHECK_FALSE(entry.doubts().value("U").toList().front().toBool());
    // The odd size names its sides by their middles.
    CHECK(entry.centres().value("F").toString() == colour_name(cube.at(Face::F, 1, 1)));

    // Correct one sticker: the cube is no longer a cube, and says so.
    const QString was = entry.sides().value("U").toList().at(0).toString();
    entry.paint("U", 0, was == "Red" ? 3 : 5);
    CHECK(entry.touched());
    CHECK(state_of(entry) != "valid");
    CHECK_FALSE(entry.solve().value("ok").toBool());  // never reaches the solver

    entry.undo();
    CHECK(state_of(entry) == "valid");
    CHECK_FALSE(entry.touched());
    CHECK(entry.canRedo());
    entry.redo();
    CHECK(entry.touched());

    entry.revert();
    CHECK_FALSE(entry.touched());
    CHECK(entry.sides().value("U").toList().at(0).toString() == was);
    CHECK(state_of(entry) == "valid");

    // A new cube forgets the reading.
    entry.begin(3);
    CHECK_FALSE(entry.fromScan());
    CHECK(entry.doubtCount() == 0);
    CHECK_FALSE(entry.canUndo());
}

TEST_CASE("a whole side can be described in letters", "[qt][controllers][colours]") {
    SolveController solve;
    ColourEntry entry(solve);
    entry.begin(3);

    QVariantMap result = entry.describeSide("F", "ggg/gbg/ggg");
    CHECK(result.value("ok").toBool());
    CHECK(entry.paintedOn("F") == 9);
    CHECK(entry.sides().value("F").toList().at(4).toString() == "Blue");
    CHECK(entry.sideText("F") == "GGG/GBG/GGG");
    CHECK(entry.touched());

    result = entry.describeSide("F", "ggg/gbg");
    CHECK_FALSE(result.value("ok").toBool());
    CHECK(result.value("error").toString() == "tooFew");
    CHECK(result.value("at").toInt() == 6);
    result = entry.describeSide("F", "ggg/gxg/ggg");
    CHECK(result.value("error").toString() == "unknownLetter");
    CHECK(result.value("at").toInt() == 5);
    // A side that was not understood is left as it was.
    CHECK(entry.sideText("F") == "GGG/GBG/GGG");
    CHECK(entry.describeSide("Q", "ggggggggg").value("error").toString() == "noSuchSide");

    // One step to undo, and "?" leaves a sticker unpainted.
    entry.describeSide("F", "ggg/g?g/ggg");
    CHECK(entry.paintedOn("F") == 8);
    entry.undo();
    CHECK(entry.sideText("F") == "GGG/GBG/GGG");
}

TEST_CASE("a side described for a bigger cube takes that many letters",
          "[qt][controllers][colours]") {
    SolveController solve;
    ColourEntry entry(solve);
    for (const int size : {2, 4, 5, 7}) {
        entry.begin(size);
        INFO("size " << size);
        const QString letters(size * size, QLatin1Char('y'));
        CHECK(entry.describeSide("D", letters).value("ok").toBool());
        CHECK(entry.paintedOn("D") == size * size);
        CHECK_FALSE(entry.describeSide("D", letters + "y").value("ok").toBool());
        // The side's own text is what describes it again.
        CHECK(entry.describeSide("D", entry.sideText("D")).value("ok").toBool());
    }
}

TEST_CASE("a side that was read the wrong way up is turned and put right",
          "[qt][controllers][colours]") {
    SolveController solve;
    ColourEntry entry(solve);
    entry.begin(3);
    entry.describeSide("R", "WYG/BOR/WYG");
    entry.rotateSide("R", 1);
    CHECK(entry.sideText("R") == "WBW/YOY/GRG");
    entry.rotateSide("R", -1);
    CHECK(entry.sideText("R") == "WYG/BOR/WYG");
    entry.undo();
    entry.undo();
    CHECK(entry.sideText("R") == "WYG/BOR/WYG");
}

TEST_CASE("a stroke painted over several stickers undoes together", "[qt][controllers][colours]") {
    SolveController solve;
    ColourEntry entry(solve);
    entry.begin(5);
    entry.beginStroke();
    for (int i = 0; i < 5; ++i) {
        entry.paint("U", i, 0);
    }
    entry.endStroke();
    CHECK(entry.paintedOn("U") == 5);
    entry.undo();
    CHECK(entry.paintedOn("U") == 0);
    CHECK_FALSE(entry.canUndo());
}
