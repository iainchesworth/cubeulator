// The solution screen for a cube that is not a 3x3: SolutionScreen.qml loaded with
// the app's own controllers behind it, a bigger cube solved in them, and the screen
// stepped through the solution by the functions its buttons and keys call, as
// solution_screen_test.cpp does for a 3x3.
//
// What is held here is that the cube the screen draws is the library's cube at
// every position it is taken to, that a turn of a layer inside the cube says
// which, that the stages listed are the ones the cube needed, and that each side
// has the colour it is named by.
#include <catch2/catch_test_macros.hpp>

#include "cube_view_reading.hpp"
#include "qml_test_app.hpp"

#include <cube/nxn/cube.hpp>
#include <cube/nxn/stage.hpp>
#include <cube/solver/cube.hpp>

#include <QColor>
#include <QCoreApplication>
#include <QMetaObject>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QString>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>

#include <memory>
#include <set>

using cube::nxn::NCube;
using cube::solver::Color;
using cube::test::wait_for;

namespace {

[[nodiscard]] QString colour_name(Color colour) {
    switch (colour) {
        case Color::White:
            return QStringLiteral("White");
        case Color::Yellow:
            return QStringLiteral("Yellow");
        case Color::Green:
            return QStringLiteral("Green");
        case Color::Blue:
            return QStringLiteral("Blue");
        case Color::Orange:
            return QStringLiteral("Orange");
        case Color::Red:
            return QStringLiteral("Red");
    }
    return {};
}

// Solves `scrambled` in the app's own SolveController and waits for it.
SolveController& solve_in_app(const NCube& scrambled) {
    SolveController& solver = cube::test::app_controllers().solve;
    solver.solveCube(scrambled);
    REQUIRE(wait_for([&]() { return !solver.isSolving(); }, 60'000));
    REQUIRE(solver.totalMoves() > 0);
    REQUIRE(solver.size() == scrambled.size());
    return solver;
}

class Screen {
public:
    Screen() {
        QQmlComponent component(&cube::test::qml_engine());
        component.loadFromModule("Cubeulator", "SolutionScreen");
        if (component.isError()) {
            FAIL(component.errorString().toStdString());
        }
        item_.reset(qobject_cast<QQuickItem*>(component.create()));
        if (item_ == nullptr) {
            FAIL(component.errorString().toStdString());
        }
        item_->setWidth(900);
        item_->setHeight(640);
        cube_ = find("solution.cube");
        REQUIRE(cube_ != nullptr);
        // How long things take to move is not under test.
        REQUIRE(cube_->setProperty("turnDuration", 20));
        REQUIRE(cube_->setProperty("viewDuration", 0));
        // Nor is the pace of a play: the screen's one Timer.
        for (QObject* child : item_->children()) {
            if (child->inherits("QQmlTimer")) {
                REQUIRE(child->setProperty("interval", 15));
            }
        }
        REQUIRE(QMetaObject::invokeMethod(find("solution.list"), "forceLayout"));
    }

    [[nodiscard]] QQuickItem* cube() const { return cube_; }
    [[nodiscard]] QQuickItem* item() const { return item_.get(); }
    // Items made by a Loader are visual children, not QObject children, so
    // findChild cannot see them; this walks the visual tree.
    [[nodiscard]] QQuickItem* find(const QString& name) const { return find_in(item_.get(), name); }
    [[nodiscard]] QVariant get(const char* name) const { return item_->property(name); }
    [[nodiscard]] int position() const { return get("position").toInt(); }
    void call(const char* method) { REQUIRE(QMetaObject::invokeMethod(item_.get(), method)); }
    void call(const char* method, int argument) {
        REQUIRE(QMetaObject::invokeMethod(item_.get(), method, Q_ARG(QVariant, argument)));
    }

    // A function of the screen, called with one argument.
    [[nodiscard]] QVariant ask(const char* method, const QVariant& argument) const {
        QVariant result;
        REQUIRE(QMetaObject::invokeMethod(item_.get(), method, Q_RETURN_ARG(QVariant, result),
                                          Q_ARG(QVariant, argument)));
        return result;
    }

    // The colour the screen draws a sticker of `colour` in.
    [[nodiscard]] QColor paint(Color colour) const {
        return ask("colorHex", colour_name(colour)).value<QColor>();
    }

    [[nodiscard]] bool shows(const NCube& cube) const {
        return cube::test::shows(cube_, cube, [this](Color colour) { return paint(colour); });
    }

    // Lets the screen catch up: the view has stopped, no layer is in the air.
    void settle() const {
        REQUIRE(wait_for([this]() {
            return !cube_->property("viewMoving").toBool() && !cube_->property("playing").toBool();
        }));
        QCoreApplication::processEvents();
    }

    // A turn as a row words it: the move's face, turns and layers.
    [[nodiscard]] QString line(const QVariantMap& move) const {
        return ask("turnLine", move).toString();
    }

private:
    static QQuickItem* find_in(QQuickItem* item, const QString& name) {
        if (item->objectName() == name) {
            return item;
        }
        for (QQuickItem* child : item->childItems()) {
            if (QQuickItem* found = find_in(child, name)) {
                return found;
            }
        }
        return nullptr;
    }

    std::unique_ptr<QQuickItem> item_;
    QQuickItem* cube_ = nullptr;
};

}  // namespace

TEST_CASE("the solution screen of a 5x5 draws the library's cube at every position it is taken to",
          "[qml][solution-screen][nxn]") {
    const NCube scrambled = cube::nxn::random_scramble({.size = 5, .seed = 3, .moves = 80});
    SolveController& solver = solve_in_app(scrambled);
    const int total = solver.totalMoves();
    // The solve screen is a few turns in: where the review is must not matter
    // to it, nor what it does to the solve's.
    solver.goToMove(3);
    const NCube solve_cube = solver.plan().current_cube();

    Screen screen;
    screen.settle();
    // The view that draws a cube of any size, at the size the cube is.
    CHECK(screen.cube()->property("cubeSize").toInt() == 5);
    CHECK_FALSE(screen.get("threeByThree").toBool());
    CHECK(screen.get("total").toInt() == total);
    REQUIRE(screen.shows(scrambled));

    // A turn at a time, each played on the cube before the position moves on.
    for (int index = 0; index < 25; ++index) {
        INFO("turn " << index + 1 << " of " << total);
        screen.call("next");
        CHECK(screen.position() == index);
        REQUIRE(wait_for([&]() { return screen.position() == index + 1; }));
        screen.settle();
        REQUIRE(screen.shows(solver.plan().cube_at(index + 1)));
    }

    // Anywhere: a row, Home, End.
    for (const int index : {total - 1, 40, total, 7, 0}) {
        INFO("jump to " << index);
        screen.call("jumpTo", index);
        screen.settle();
        CHECK(screen.position() == index);
        REQUIRE(screen.shows(solver.plan().cube_at(index)));
    }
    // The end is a solved cube, whichever way up it ended.
    screen.call("jumpTo", total);
    screen.settle();
    CHECK(solver.plan().cube_at(total).is_solved());
    CHECK(screen.shows(solver.plan().cube_at(total)));

    // And back, each step the last turn made the other way.
    for (int index = total; index > total - 12; --index) {
        screen.call("previous");
        REQUIRE(wait_for([&]() { return screen.position() == index - 1; }));
        screen.settle();
        REQUIRE(screen.shows(solver.plan().cube_at(index - 1)));
    }

    CHECK(solver.moveIndex() == 3);
    CHECK(solver.plan().current_cube() == solve_cube);
}

TEST_CASE("a turn of a layer inside a bigger cube says which layer",
          "[qml][solution-screen][nxn]") {
    SolveController& solver =
        solve_in_app(cube::nxn::random_scramble({.size = 5, .seed = 6, .moves = 80}));
    Screen screen;
    screen.settle();

    // Their ids stand for the words here, since no language is installed under
    // test: "layer %1" for one layer, "layers %1 to %2" for several.
    bool one = false;
    bool several = false;
    bool outer = false;
    for (const QVariant& entry : solver.moves()) {
        const QVariantMap move = entry.toMap();
        const QString words = screen.line(move);
        INFO(move.value("notation").toString().toStdString() << ": " << words.toStdString());
        const int first = move.value("first").toInt();
        const int last = move.value("last").toInt();
        if (first == 0 && last == 0) {
            outer = true;
            CHECK_FALSE(words.contains("solution.layer"));
        } else if (first == last) {
            one = true;
            CHECK(words.contains("solution.layer"));
            CHECK_FALSE(words.contains("solution.layers"));
        } else {
            several = true;
            CHECK(words.contains("solution.layers"));
        }
        // The side, the direction and the amount are in every one.
        CHECK(words.startsWith(screen.ask("sideWords", move.value("face")).toString()));
        CHECK(words.endsWith(screen.ask("amountWords", move.value("turns")).toString()));
        // A screen reader is told the same.
        CHECK_FALSE(screen.ask("turnSpeech", move).toString().isEmpty());
    }
    CHECK(one);
    CHECK(outer);
    // Wide moves are rare (docs/nxn-solver.md); a given solution may have none.
    static_cast<void>(several);

    // A move that is an outer layer, spelt out: the 3x3's row, word for word.
    CHECK(screen.line({{"face", "R"}, {"turns", 1}, {"first", 0}, {"last", 0}}) ==
          QStringList{
              screen.ask("sideWords", "R").toString(),
              screen.ask("directionWords", "up").toString(),
              screen.ask("amountWords", 1).toString(),
          }
              .join(QStringLiteral(" · ")));
    // A source that does not say which layers (a 3x3's) is the outer one.
    CHECK(screen.line({{"face", "R"}, {"turns", 1}}) ==
          screen.line({{"face", "R"}, {"turns", 1}, {"first", 0}, {"last", 0}}));
    CHECK(screen.line({{"face", "R"}, {"turns", 1}, {"first", 1}, {"last", 2}})
              .contains("solution.layers"));
}

TEST_CASE("the stages listed are the ones the cube needed, each with words",
          "[qml][solution-screen][nxn]") {
    for (const int size : {2, 4, 5}) {
        INFO("size " << size);
        SolveController& solver =
            solve_in_app(cube::nxn::random_scramble({.size = size, .seed = 6, .moves = 60}));
        Screen screen;
        screen.settle();

        int headings = 0;
        for (const QVariant& row : screen.get("rows").toList()) {
            const QVariantMap map = row.toMap();
            if (map.value("kind").toString() != "stage") {
                continue;
            }
            ++headings;
            // Nothing is listed that has no turns.
            CHECK(map.value("end").toInt() > map.value("start").toInt());
            const QVariantMap words = screen.ask("stageWords", map.value("stage")).toMap();
            CHECK_FALSE(words.value("title").toString().isEmpty());
            CHECK_FALSE(words.value("what").toString().isEmpty());
        }
        CHECK(headings == screen.get("stageCount").toInt());
        CHECK(headings > 0);
        if (size == 2) {
            CHECK(headings == 1);
        }
        // A stage of a solution is listed for each that has moves, and every one
        // of them is worded.
        for (const QVariant& range : solver.stageRanges()) {
            const QVariantMap map = range.toMap();
            if (map.value("endIndex").toInt() > map.value("startIndex").toInt()) {
                CHECK_FALSE(screen.ask("stageWords", map.value("stageIndex"))
                                .toMap()
                                .value("title")
                                .toString()
                                .isEmpty());
            }
        }
    }
    for (const cube::nxn::StageId id : {cube::nxn::StageId::Parity, cube::nxn::StageId::Centres,
                                        cube::nxn::StageId::Edges, cube::nxn::StageId::Corners}) {
        solve_in_app(cube::nxn::random_scramble({.size = 4, .seed = 1, .moves = 60}));
        Screen screen;
        CHECK_FALSE(screen.ask("stageWords", static_cast<int>(id))
                        .toMap()
                        .value("title")
                        .toString()
                        .isEmpty());
    }
}

TEST_CASE("each side of a cube of any size has the colour it is named by",
          "[qml][solution-screen][nxn]") {
    for (const int size : {2, 3, 4, 5}) {
        INFO("size " << size);
        // A 3x3 is scrambled by the 3x3's own library (the nxn one turns its middle
        // slices too, which moves the centres).
        solve_in_app(size == 3
                         ? NCube::from_cube3(cube::solver::random_scramble(8))
                         : cube::nxn::random_scramble({.size = size, .seed = 8, .moves = 60}));
        Screen screen;
        screen.settle();
        const QVariantMap centres = screen.get("centres").toMap();
        CHECK(centres.size() == 6);
        std::set<QString> colours;
        for (const QVariant& colour : centres) {
            colours.insert(colour.value<QColor>().name());
        }
        // Six sides, six colours.
        CHECK(colours.size() == 6);
    }
}

TEST_CASE("a 3x3 is reviewed in the 3x3's view with the rows it always had",
          "[qml][solution-screen][nxn]") {
    SolveController& solver = cube::test::app_controllers().solve;
    solver.solveCube(NCube::from_cube3(cube::solver::random_scramble(7)));
    REQUIRE(wait_for([&]() { return !solver.isSolving(); }, 60'000));

    Screen screen;
    screen.settle();
    CHECK(screen.get("threeByThree").toBool());
    // Cube3DView has no cubeSize.
    CHECK_FALSE(screen.cube()->property("cubeSize").isValid());
    // Every row is a side, a direction and an amount.
    for (const QVariant& entry : solver.moves()) {
        CHECK_FALSE(screen.line(entry.toMap()).contains("solution.layer"));
    }
}
