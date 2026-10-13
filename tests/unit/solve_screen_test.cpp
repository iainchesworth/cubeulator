// The solve screen itself: SolveScreen.qml loaded with the app's own
// controllers behind it, and stepped through a real solution by the same
// functions its buttons call.
//
// cube_view_test.cpp holds the 3D cube to the solver one move at a time.
// This holds the screen built on it to the whole solve: what is drawn stays
// the solver's cube at every step in both directions, the layer about to
// move is always in view however the cube is held, and it ends solved.
#include <catch2/catch_test_macros.hpp>

#include "cube_view_reading.hpp"
#include "drawn_cube.hpp"
#include "qml_test_app.hpp"

#include <cube/solver/cube.hpp>
#include <cube/solver/hold.hpp>
#include <cube/solver/notation.hpp>
#include <cube/solver/solver.hpp>
#include <cube/vision/direct_scan.hpp>

#include <QColor>
#include <QMetaObject>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QString>
#include <QVariant>
#include <QVariantMap>

#include <memory>
#include <optional>
#include <set>

using cube::solver::Color;
using cube::solver::Cube;
using cube::solver::Face;
using cube::solver::Hold;
using cube::solver::Move;
using cube::test::letter;
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

[[nodiscard]] Face side_of(const QString& side) {
    for (const Face face : cube::solver::kAllFaces) {
        if (letter(face) == side) {
            return face;
        }
    }
    FAIL("not a side: " << side.toStdString());
    return Face::U;
}

// Solves `scrambled` in the app's own SolveController and waits for it.
void solve_in_app(const Cube& scrambled) {
    SolveController& solver = cube::test::app_controllers().solve;
    solver.solveCube(scrambled);
    REQUIRE(wait_for([&]() { return !solver.isSolving(); }, 60'000));
    REQUIRE(solver.totalMoves() > 0);
    REQUIRE(solver.currentCube() == scrambled);
}

class Screen {
public:
    Screen() {
        QQmlComponent component(&cube::test::qml_engine());
        component.loadFromModule("Cubeulator", "SolveScreen");
        if (component.isError()) {
            FAIL(component.errorString().toStdString());
        }
        item_.reset(qobject_cast<QQuickItem*>(component.create()));
        if (item_ == nullptr) {
            FAIL(component.errorString().toStdString());
        }
        item_->setWidth(900);
        item_->setHeight(640);
        cube_ = item_->findChild<QQuickItem*>(QStringLiteral("solve.cube"));
        REQUIRE(cube_ != nullptr);
        // How long things take to move is not under test.
        REQUIRE(cube_->setProperty("turnDuration", 30));
        REQUIRE(cube_->setProperty("viewDuration", 0));
    }

    [[nodiscard]] QQuickItem* cube() const { return cube_; }
    [[nodiscard]] QObject* find(const char* name) const {
        return item_->findChild<QObject*>(QString::fromLatin1(name));
    }
    [[nodiscard]] QVariant get(const char* name) const { return item_->property(name); }
    void call(const char* method) { REQUIRE(QMetaObject::invokeMethod(item_.get(), method)); }

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

    // Lets the screen catch up: the view finishes turning, the guide
    // restarts.
    void settle() const {
        REQUIRE(wait_for([this]() {
            return !cube_->property("viewMoving").toBool() && !cube_->property("playing").toBool();
        }));
        QCoreApplication::processEvents();
    }

private:
    std::unique_ptr<QQuickItem> item_;
    QQuickItem* cube_ = nullptr;
};

}  // namespace

TEST_CASE("the solve screen's six colours are six different colours", "[qml][solve-screen]") {
    solve_in_app(cube::solver::random_scramble(7));
    const Screen screen;
    std::set<QString> colours;
    for (const Color colour :
         {Color::White, Color::Yellow, Color::Green, Color::Blue, Color::Orange, Color::Red}) {
        colours.insert(screen.paint(colour).name());
    }
    CHECK(colours.size() == 6);
}

TEST_CASE("stepping the solve screen keeps its cube the solver's cube, to solved and back",
          "[qml][solve-screen]") {
    SolveController& solver = cube::test::app_controllers().solve;
    const Cube scrambled = cube::solver::random_scramble(7);
    solve_in_app(scrambled);

    Screen screen;
    screen.settle();
    REQUIRE(screen.shows(scrambled));

    Cube expected = scrambled;
    std::set<Face> sides_turned;
    while (!solver.atEnd()) {
        const int index = solver.moveIndex();
        const Move move{side_of(solver.currentMoveFace()), solver.currentMoveTurns()};
        INFO("move " << index << ": " << cube::solver::to_string(move));
        sides_turned.insert(move.face);

        // The layer about to turn is the one the cube is nudging.
        screen.settle();
        CHECK(screen.cube()->property("guideFace").toString() == letter(move.face));
        CHECK(screen.cube()->property("guideTurns").toInt() == move.turns);

        screen.call("next");
        // The step is not taken until the layer has been seen to turn.
        CHECK(solver.moveIndex() == index);
        REQUIRE(wait_for([&]() { return solver.moveIndex() == index + 1; }));

        expected = expected.apply(move);
        REQUIRE(solver.currentCube() == expected);
        REQUIRE(screen.shows(expected));
    }
    CHECK(expected.is_solved());
    CHECK(sides_turned.size() == 6);
    screen.settle();
    CHECK(screen.cube()->property("guideFace").toString().isEmpty());
    CHECK_FALSE(screen.cube()->property("nudging").toBool());

    // A press at the end does nothing.
    screen.call("next");
    CHECK(solver.atEnd());

    // Solved is not a dead end: the screen can be left for the one that says
    // so - but only from the end.
    QObject* app_state =
        cube::test::qml_engine().singletonInstance<QObject*>("Cubeulator", "AppState");
    REQUIRE(app_state != nullptr);
    const QVariant came_from = app_state->property("screen");
    screen.call("finish");
    CHECK(app_state->property("screen").toString() == "complete");
    REQUIRE(app_state->setProperty("screen", came_from));
    for (const char* control : {"solve.next", "solve.previous", "solve.rescan"}) {
        INFO(control);
        CHECK(screen.find(control) != nullptr);
    }

    // And back again: each step back is the last move turned the other way.
    while (!solver.atStart()) {
        const int index = solver.moveIndex();
        INFO("back from move " << index);
        screen.call("previous");
        REQUIRE(wait_for([&]() { return solver.moveIndex() == index - 1; }));
        REQUIRE(screen.shows(solver.currentCube()));
    }
    // Part-way through, there is nothing to finish.
    screen.call("finish");
    CHECK(app_state->property("screen") == came_from);
    CHECK(solver.currentCube() == scrambled);
    CHECK(screen.shows(scrambled));
}

TEST_CASE("pressing on before a turn has finished takes both steps", "[qml][solve-screen]") {
    SolveController& solver = cube::test::app_controllers().solve;
    const Cube scrambled = cube::solver::random_scramble(9);
    solve_in_app(scrambled);

    Screen screen;
    REQUIRE(screen.cube()->setProperty("turnDuration", 600));
    screen.settle();

    const QVariantList moves = solver.moves();
    REQUIRE(moves.size() > 12);
    Cube expected = scrambled;
    for (int press = 0; press < 12; ++press) {
        const QVariantMap move = moves.at(press).toMap();
        expected = expected.apply(
            Move{side_of(move.value("face").toString()), move.value("turns").toInt()});
        screen.call("next");
    }
    // Eleven have landed; the twelfth is still turning.
    CHECK(solver.moveIndex() == 11);
    REQUIRE(wait_for([&]() { return solver.moveIndex() == 12; }));
    CHECK(solver.currentCube() == expected);
    CHECK(screen.shows(expected));
}

TEST_CASE("the layer about to move is in view however the cube is held", "[qml][solve-screen]") {
    cube::app::AppControllers& app = cube::test::app_controllers();
    SolveController& solver = app.solve;
    solve_in_app(cube::solver::random_scramble(7));

    Screen screen;
    for (const Hold& hold : cube::solver::all_holds()) {
        app.held.setHold(hold);
        while (!solver.atStart()) {
            solver.previousMove();
        }
        std::set<Face> sides_turned;
        // Far enough into the solution to have turned every side.
        for (int step = 0; step < 45 && !solver.atEnd(); ++step) {
            const Face side = side_of(solver.currentMoveFace());
            INFO("up " << letter(hold.up).toStdString() << " front "
                       << letter(hold.front).toStdString() << ", move " << solver.moveIndex()
                       << " on " << letter(side).toStdString());
            sides_turned.insert(side);
            screen.settle();

            // Its side faces the viewer: at least a quarter of it, never
            // edge-on or turned away.
            const cube::test::Vec facing = cube::test::side_direction(screen.cube(), side);
            const cube::solver::Place place = cube::solver::place_of(hold, side);
            const bool by_strips =
                place == cube::solver::Place::Left || place == cube::solver::Place::Bottom;
            if (by_strips) {
                // The left and bottom layers are shown by the two rows of
                // their stickers that are in view from the usual viewpoint,
                // which is not moved for them: the rows on the two sides
                // next to the layer that face the viewer.
                CHECK_FALSE(screen.get("viewTurned").toBool());
                int rows_in_view = 0;
                for (const Face other : cube::solver::kAllFaces) {
                    if (other != side && other != cube::solver::opposite(side) &&
                        cube::test::side_direction(screen.cube(), other).z > 0.25) {
                        ++rows_in_view;
                    }
                }
                CHECK(rows_in_view == 2);
            } else {
                CHECK(facing.z > 0.25);
                CHECK(screen.get("viewTurned").toBool() == (place == cube::solver::Place::Back));
            }
            // Whatever the viewpoint, the cube stays upright on screen.
            const cube::test::Vec top = cube::test::side_direction(screen.cube(), hold.up);
            CHECK(top.y > 0.5);

            solver.nextMove();
        }
        CHECK(sides_turned.size() == 6);
    }
    app.held.setHold(cube::solver::kStatedHold);
}

TEST_CASE("the camera turns the screen's cube to the hold it sees, and steps on a move it sees",
          "[qml][solve-screen][held-cube]") {
    // The whole chain, from a picture of the cube to the 3D scene: frames go
    // into the app's DirectScan as a camera's would, and what comes out is
    // read from the solve screen's cube.
    cube::app::AppControllers& app = cube::test::app_controllers();
    SolveController& solver = app.solve;
    cube::app::DirectScan& camera = app.directScan;
    const auto show = [&camera](const cv::Mat& frame, int frames) {
        for (int i = 0; i < frames; ++i) {
            camera.processFrame(frame);
        }
    };
    const auto away = [&camera]() { camera.processFrame(cube::test::room({640, 360})); };
    const int trust = cube::app::HeldCube::kFramesToTrust + 1;

    // Scan the cube from drawings of its six sides; the app's own wiring
    // hands it to the solver.
    const Cube scrambled = cube::solver::random_scramble(3);
    camera.setWatching(false);
    camera.restart();
    for (const Face face : cube::solver::kAllFaces) {
        show(cube::test::draw_side(cube::test::outside_view(scrambled, face), cube::test::Pose{}),
             static_cast<int>(cube::vision::SideScan::kMinReads) + 4);
        away();
    }
    REQUIRE(camera.complete());
    REQUIRE(wait_for([&]() { return !solver.isSolving(); }, 60'000));
    REQUIRE(solver.currentCube() == scrambled);

    Screen screen;
    camera.setWatching(true);
    screen.settle();
    CHECK(screen.get("holdWords").toString() == "Hold yellow on top, green facing you");

    // Held some other way: the cube on screen turns to match.
    const Hold hold{Face::R, Face::D};
    show(cube::test::draw_side(cube::solver::camera_view(scrambled, hold), cube::test::Pose{}),
         trust);
    REQUIRE(app.held.hold() == hold);
    screen.settle();
    CHECK(screen.get("holdWords").toString() == "Holding orange on top, white facing you");
    // From the usual viewpoint the top, the front and the right side of the
    // cube as held are the ones in view.
    for (const cube::solver::Place place :
         {cube::solver::Place::Top, cube::solver::Place::Front, cube::solver::Place::Right}) {
        // (A move on the back swings the view; look only when it is home.)
        if (!screen.get("viewTurned").toBool()) {
            CHECK(cube::test::side_direction(screen.cube(), cube::solver::side_at(hold, place)).z >
                  0.25);
        }
    }
    // Turning the whole cube moved no sticker on it.
    CHECK(screen.shows(scrambled));

    // The move asked for is made, and a side it changed is shown.
    const Move move{side_of(solver.currentMoveFace()), solver.currentMoveTurns()};
    const Cube moved = scrambled.apply(move);
    std::optional<Hold> witness;
    for (const Hold& candidate : cube::solver::all_holds()) {
        if (cube::solver::side_at(candidate, cube::solver::Place::Back) != move.face &&
            cube::solver::camera_view(moved, candidate) !=
                cube::solver::camera_view(scrambled, candidate)) {
            witness = candidate;
            break;
        }
    }
    REQUIRE(witness.has_value());
    away();
    show(cube::test::draw_side(cube::solver::camera_view(moved, *witness), cube::test::Pose{}),
         trust);
    // The screen turns the layer and steps on without a button being pressed.
    REQUIRE(wait_for([&]() { return solver.moveIndex() == 1; }));
    CHECK(solver.currentCube() == moved);
    screen.settle();
    CHECK(screen.shows(moved));
    CHECK(app.held.hold() == *witness);

    // Seen again, the same picture is just the cube as expected: no second step.
    show(cube::test::draw_side(cube::solver::camera_view(moved, *witness), cube::test::Pose{}),
         2 * trust);
    QCoreApplication::processEvents();
    screen.settle();
    CHECK(solver.moveIndex() == 1);

    camera.setWatching(false);
    camera.restart();
}
