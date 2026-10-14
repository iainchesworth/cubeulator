// The solution screen itself: SolutionScreen.qml loaded with the app's own
// controllers behind it, and stepped through a real solution by the same
// functions its buttons and keys call.
//
// cube_view_test.cpp holds the 3D cube to the solver one move at a time. This
// holds the screen built on it to the whole solve: the cube it draws is the
// solver's cube at every position, in both directions, and it keeps a
// position of its own - the solve screen's is not touched.
#include <catch2/catch_test_macros.hpp>

#include "cube_view_reading.hpp"
#include "qml_test_app.hpp"

#include <cube/solver/cube.hpp>
#include <cube/solver/notation.hpp>
#include <cube/solver/solver.hpp>

#include <QColor>
#include <QCoreApplication>
#include <QMetaObject>
#include <QObject>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QString>
#include <QVariant>
#include <QVariantMap>

#include <memory>
#include <set>

using cube::solver::Color;
using cube::solver::Cube;
using cube::solver::Face;
using cube::solver::Move;
using cube::test::letter;
using cube::test::wait_for;

namespace {

[[nodiscard]] Face side_of(const QString& side) {
    for (const Face face : cube::solver::kAllFaces) {
        if (letter(face) == side) {
            return face;
        }
    }
    FAIL("not a side: " << side.toStdString());
    return Face::U;
}

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
SolveController& solve_in_app(const Cube& scrambled) {
    SolveController& solver = cube::test::app_controllers().solve;
    solver.solveCube(scrambled);
    REQUIRE(wait_for([&]() { return !solver.isSolving(); }, 60'000));
    REQUIRE(solver.totalMoves() > 0);
    REQUIRE(solver.currentCube() == scrambled);
    return solver;
}

class Screen {
public:
    explicit Screen(const QVariantMap& initial = {}) {
        QQmlComponent component(&cube::test::qml_engine());
        component.loadFromModule("Cubeulator", "SolutionScreen");
        if (component.isError()) {
            FAIL(component.errorString().toStdString());
        }
        item_.reset(qobject_cast<QQuickItem*>(component.createWithInitialProperties(initial)));
        if (item_ == nullptr) {
            FAIL(component.errorString().toStdString());
        }
        item_->setWidth(900);
        item_->setHeight(640);
        cube_ = find("solution.cube");
        REQUIRE(cube_ != nullptr);
        // How long things take to move is not under test.
        REQUIRE(cube_->setProperty("turnDuration", 30));
        REQUIRE(cube_->setProperty("viewDuration", 0));
        // Nor is the pace of a play: the screen's one Timer.
        bool timed = false;
        for (QObject* child : item_->children()) {
            if (child->inherits("QQmlTimer")) {
                REQUIRE(child->setProperty("interval", 15));
                timed = true;
            }
        }
        REQUIRE(timed);
        // A list built without a window makes the rows it was asked to
        // position; the first screenful is made when something asks.
        REQUIRE(QMetaObject::invokeMethod(find("solution.list"), "forceLayout"));
    }

    [[nodiscard]] QQuickItem* cube() const { return cube_; }
    [[nodiscard]] QQuickItem* item() const { return item_.get(); }
    // Items made by a Loader inside a list are visual children, not QObject
    // children, so findChild cannot see them; this walks the visual tree.
    [[nodiscard]] QQuickItem* find(const QString& name) const { return find_in(item_.get(), name); }
    [[nodiscard]] QVariant get(const char* name) const { return item_->property(name); }
    [[nodiscard]] int position() const { return get("position").toInt(); }
    [[nodiscard]] QString state() const { return get("reviewState").toString(); }
    [[nodiscard]] QString caption() const {
        return find("solution.caption")->property("text").toString();
    }
    [[nodiscard]] QString main_button() const {
        return find("solution.play")->property("text").toString();
    }
    [[nodiscard]] bool enabled(const char* name) const {
        QQuickItem* control = find(QString::fromLatin1(name));
        REQUIRE(control != nullptr);
        return control->property("enabled").toBool();
    }
    void call(const char* method) { REQUIRE(QMetaObject::invokeMethod(item_.get(), method)); }
    void call(const char* method, int argument) {
        REQUIRE(QMetaObject::invokeMethod(item_.get(), method, Q_ARG(QVariant, argument)));
    }

    // The colour the screen draws a sticker of `colour` in.
    [[nodiscard]] QColor paint(Color colour) const {
        QVariant result;
        REQUIRE(QMetaObject::invokeMethod(item_.get(), "colorHex", Q_RETURN_ARG(QVariant, result),
                                          Q_ARG(QVariant, colour_name(colour))));
        return result.value<QColor>();
    }

    [[nodiscard]] bool shows(const Cube& cube) const {
        return cube::test::shows(cube_, cube, [this](Color colour) { return paint(colour); });
    }

    // Lets the screen catch up: the view has stopped, no layer is in the air.
    void settle() const {
        REQUIRE(wait_for([this]() {
            return !cube_->property("viewMoving").toBool() && !cube_->property("playing").toBool();
        }));
        QCoreApplication::processEvents();
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

// The cube after `count` of the solver's moves, from `start`.
[[nodiscard]] Cube after(Cube start, const QVariantList& moves, int count) {
    for (int i = 0; i < count; ++i) {
        const QVariantMap move = moves.at(i).toMap();
        start =
            start.apply(Move{side_of(move.value("face").toString()), move.value("turns").toInt()});
    }
    return start;
}

}  // namespace

TEST_CASE("the solution screen opens on the cube as it was scanned, every turn under its stage",
          "[qml][solution-screen]") {
    const Cube scrambled = cube::solver::random_scramble(7);
    SolveController& solver = solve_in_app(scrambled);

    Screen screen;
    screen.settle();

    CHECK(screen.state() == "overview");
    CHECK(screen.position() == 0);
    CHECK(screen.get("total").toInt() == solver.totalMoves());
    CHECK(screen.shows(scrambled));
    CHECK(screen.caption() == qtTrId("solution.atStart"));
    // Nothing before the first turn to go back to; the rest can be pressed.
    CHECK_FALSE(screen.enabled("solution.previous"));
    CHECK(screen.enabled("solution.play"));
    CHECK(screen.enabled("solution.next"));
    CHECK(screen.main_button() == qtTrId("solution.play"));
    CHECK(screen.find("solution.back") != nullptr);

    // The method's line, and the first stage and its first turn in the list.
    // (The words themselves are the system tests': this binary has no
    // translations installed, so what it reads is the ids.)
    CHECK(screen.find("solution.method")->property("text").toString() ==
          qtTrId("solution.method.beginner"));
    QQuickItem* stage = screen.find("solution.stage1");
    REQUIRE(stage != nullptr);
    CHECK(stage->property("count").toInt() ==
          solver.stageRanges().at(0).toMap().value("endIndex").toInt() -
              solver.stageRanges().at(0).toMap().value("startIndex").toInt());
    QQuickItem* first = screen.find("solution.turn1");
    REQUIRE(first != nullptr);
    CHECK(first->property("selected").toBool());
    CHECK_FALSE(screen.find("solution.turn2")->property("selected").toBool());
}

TEST_CASE(
    "stepping the solution screen keeps its cube the solver's cube, to solved and back, "
    "and leaves the solve where it was",
    "[qml][solution-screen]") {
    const Cube scrambled = cube::solver::random_scramble(11);
    SolveController& solver = solve_in_app(scrambled);
    const QVariantList moves = solver.moves();
    const int total = static_cast<int>(moves.size());
    REQUIRE(total > 6);

    // The solve screen is a few turns in: where the review is must not
    // matter to it, nor what it does to the solve's.
    for (int i = 0; i < 3; ++i) {
        solver.nextMove();
    }
    const Cube solve_cube = solver.currentCube();

    Screen screen;
    screen.settle();
    REQUIRE(screen.shows(scrambled));

    std::set<QString> kinds;
    for (int index = 0; index < total; ++index) {
        INFO("turn " << index + 1 << " of " << total);
        const QString notation = moves.at(index).toMap().value("notation").toString();
        kinds.insert(notation);

        screen.call("next");
        // The position is not taken until the layer has been seen to turn.
        CHECK(screen.position() == index);
        REQUIRE(wait_for([&]() { return screen.position() == index + 1; }));
        screen.settle();
        REQUIRE(screen.shows(after(scrambled, moves, index + 1)));
        CHECK(solver.moveIndex() == 3);
        CHECK(solver.currentCube() == solve_cube);
    }
    CHECK(kinds.size() > 3);

    CHECK(screen.state() == "end");
    CHECK(screen.shows(Cube::solved()));
    CHECK(screen.caption() == qtTrId("solution.atEnd"));
    CHECK_FALSE(screen.enabled("solution.next"));
    CHECK(screen.enabled("solution.previous"));
    CHECK(screen.main_button() == qtTrId("solution.startAgain"));
    // Nothing is selected once every turn has been made.
    QQuickItem* last = screen.find(QStringLiteral("solution.turn%1").arg(total));
    REQUIRE(last != nullptr);
    CHECK_FALSE(last->property("selected").toBool());

    // A press at the end does nothing.
    screen.call("next");
    CHECK(screen.position() == total);

    // And back: each step is the last turn made the other way.
    for (int index = total; index > 0; --index) {
        INFO("back from " << index);
        screen.call("previous");
        REQUIRE(wait_for([&]() { return screen.position() == index - 1; }));
        screen.settle();
        REQUIRE(screen.shows(after(scrambled, moves, index - 1)));
    }
    CHECK(screen.state() == "overview");
    CHECK(screen.shows(scrambled));
    CHECK(solver.moveIndex() == 3);
    CHECK(solver.currentCube() == solve_cube);
}

TEST_CASE("play steps on by itself, pause stops it, and the end offers to start again",
          "[qml][solution-screen]") {
    const Cube scrambled = cube::solver::random_scramble(13);
    SolveController& solver = solve_in_app(scrambled);
    const int total = solver.totalMoves();
    REQUIRE(total > 6);

    Screen screen;
    screen.settle();

    screen.call("toggle");
    CHECK(screen.state() == "playing");
    CHECK(screen.main_button() == qtTrId("solution.pause"));
    REQUIRE(wait_for([&]() { return screen.position() >= 3; }));

    // Pause: stays where it lands, and the button says Play again.
    screen.call("toggle");
    CHECK_FALSE(screen.get("playing").toBool());
    screen.settle();
    const int paused_at = screen.position();
    CHECK(paused_at >= 3);
    CHECK(screen.state() == (paused_at >= total ? "end" : "stepping"));
    QCoreApplication::processEvents();
    CHECK(screen.position() == paused_at);
    CHECK(screen.main_button() == qtTrId("solution.play"));
    CHECK(screen.shows(after(scrambled, solver.moves(), paused_at)));

    // Next, Previous and a row each stop a play that is running.
    screen.call("toggle");
    CHECK(screen.state() == "playing");
    screen.call("next");
    CHECK_FALSE(screen.get("playing").toBool());
    screen.call("toggle");
    CHECK(screen.state() == "playing");
    screen.call("previous");
    CHECK_FALSE(screen.get("playing").toBool());
    screen.settle();
    screen.call("toggle");
    screen.call("jumpTo", 2);
    CHECK_FALSE(screen.get("playing").toBool());
    CHECK(screen.position() == 2);
    screen.settle();

    // Play to the end: it stops there, solved, with the main button offering
    // to start again.
    screen.call("toggle");
    REQUIRE(wait_for([&]() { return screen.state() == "end"; }, 30'000));
    screen.settle();
    CHECK_FALSE(screen.get("playing").toBool());
    CHECK(screen.position() == total);
    CHECK(screen.shows(Cube::solved()));
    CHECK(screen.main_button() == qtTrId("solution.startAgain"));

    screen.call("toggle");
    CHECK(screen.state() == "overview");
    CHECK(screen.position() == 0);
    CHECK_FALSE(screen.get("playing").toBool());
    CHECK(screen.shows(scrambled));
}

TEST_CASE("a row, Home and End jump without turning anything, and reduced motion steps in no time",
          "[qml][solution-screen]") {
    const Cube scrambled = cube::solver::random_scramble(17);
    SolveController& solver = solve_in_app(scrambled);
    const QVariantList moves = solver.moves();
    const int total = static_cast<int>(moves.size());
    REQUIRE(total > 10);

    Screen screen;
    screen.settle();

    screen.call("jumpTo", 7);
    CHECK(screen.position() == 7);
    CHECK_FALSE(screen.cube()->property("playing").toBool());
    CHECK(screen.state() == "stepping");
    CHECK(screen.caption() != qtTrId("solution.atStart"));
    CHECK(screen.caption() != qtTrId("solution.atEnd"));
    CHECK(screen.shows(after(scrambled, moves, 7)));
    QQuickItem* row = screen.find("solution.turn8");
    REQUIRE(row != nullptr);
    CHECK(row->property("selected").toBool());
    CHECK_FALSE(screen.find("solution.turn1")->property("selected").toBool());

    screen.call("jumpTo", total + 50);
    CHECK(screen.position() == total);
    CHECK(screen.state() == "end");
    screen.call("jumpTo", -4);
    CHECK(screen.position() == 0);
    CHECK(screen.state() == "overview");

    // Reduced motion: a step is taken at once and nothing turns.
    REQUIRE(screen.item()->setProperty("reducedMotion", true));
    screen.call("next");
    CHECK(screen.position() == 1);
    CHECK_FALSE(screen.cube()->property("playing").toBool());
    CHECK(screen.shows(after(scrambled, moves, 1)));
    screen.call("next");
    screen.call("previous");
    CHECK(screen.position() == 1);
    CHECK(screen.shows(after(scrambled, moves, 1)));
}

TEST_CASE("the solution screen reviews the source it is given, and starts again when it changes",
          "[qml][solution-screen]") {
    const Cube first = cube::solver::random_scramble(23);
    SolveController& solver = solve_in_app(first);

    // A stand-in for a solve kept from another day: the same shape as the
    // controller, with its own method.
    QQmlComponent stub_component(&cube::test::qml_engine());
    stub_component.setData(R"(
        import QtQml
        import Cubeulator
        QtObject {
            property var moves: SolveController.moves
            property var stageRanges: SolveController.stageRanges
            property int method: 2
            signal solutionChanged()
            function stickersAt(turn) { return SolveController.stickersAt(turn); }
        }
    )",
                           QUrl());
    REQUIRE_FALSE(stub_component.isError());
    const std::unique_ptr<QObject> stub(stub_component.create());
    REQUIRE(stub != nullptr);

    Screen screen({{"source", QVariant::fromValue(stub.get())},
                   {"subtitle", QStringLiteral("14 October 2026")}});
    screen.settle();
    CHECK(screen.find("solution.method")->property("text").toString() ==
          qtTrId("solution.method.advanced"));
    CHECK(screen.find("solution.subtitle")->property("text").toString() ==
          QStringLiteral("14 October 2026"));
    CHECK(screen.shows(first));

    // Some way in, and a new solution lands: back to its start.
    screen.call("jumpTo", 4);
    REQUIRE(screen.position() == 4);
    const Cube second = cube::solver::random_scramble(29);
    solver.solveCube(second);
    REQUIRE(wait_for([&]() { return !solver.isSolving(); }, 60'000));
    QMetaObject::invokeMethod(stub.get(), "solutionChanged");
    CHECK(screen.position() == 0);
    CHECK(screen.state() == "overview");
    CHECK(screen.get("total").toInt() == solver.totalMoves());
    screen.settle();
    CHECK(screen.shows(second));
}
