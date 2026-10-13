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
#include <QElapsedTimer>
#include <QMetaObject>
#include <QPointF>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QString>
#include <QVariant>
#include <QVariantMap>

#include <array>
#include <cmath>
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
    [[nodiscard]] QQuickItem* item() const { return item_.get(); }
    [[nodiscard]] QObject* find(const char* name) const {
        return item_->findChild<QObject*>(QString::fromLatin1(name));
    }
    // An item made by a Repeater is a visual child, not a QObject child, so
    // find() cannot see it; this walks the visual tree.
    [[nodiscard]] QQuickItem* find_visual(const QString& name) const {
        return find_in(item_.get(), name);
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

TEST_CASE("the camera preview closes in on the cube and follows it", "[qml][solve-screen]") {
    cube::app::DirectScan& camera = cube::test::app_controllers().directScan;
    const Screen screen;
    QObject* preview = screen.find("solve.camera");
    REQUIRE(preview != nullptr);
    // Let go of a lost cube at once, rather than after the seconds a person gets.
    REQUIRE(preview->setProperty("lostAfter", 40));
    camera.setWatching(true);

    const auto scale = [&]() { return preview->property("pictureScale").toDouble(); };
    const auto whole = [&]() { return preview->property("wholeScale").toDouble(); };
    // Where a pixel of the camera's picture lands in the preview.
    const auto lands = [&](const cv::Point2f& pixel) {
        return QPointF(
            preview->property("pictureX").toDouble() + static_cast<double>(pixel.x) * scale(),
            preview->property("pictureY").toDouble() + static_cast<double>(pixel.y) * scale());
    };
    const auto in_the_middle = [&](const cv::Point2f& pixel) {
        const QPointF at = lands(pixel);
        return std::abs(at.x() - preview->property("width").toDouble() / 2) < 3.0 &&
               std::abs(at.y() - preview->property("height").toDouble() / 2) < 3.0;
    };
    const std::array<Color, 9> side =
        cube::solver::camera_view(cube::solver::random_scramble(3), cube::solver::kStatedHold);
    const auto show = [&](const cube::test::Pose& pose) {
        camera.processFrame(cube::test::draw_side(side, pose));
    };

    // Nothing in view: the whole picture.
    camera.processFrame(cube::test::room({640, 360}));
    REQUIRE(wait_for([&]() { return std::abs(scale() - whole()) < 1e-3; }));

    // A cube off to one side and small in the picture: brought to the
    // middle, and bigger.
    const cube::test::Pose pose{.centre = {180.0F, 110.0F}, .pitch = 30.0F};
    show(pose);
    REQUIRE(wait_for([&]() { return in_the_middle(pose.centre) && scale() > 1.8 * whole(); }));
    // in_the_middle() holds within 3 px, well before the view has stopped: it
    // eases towards its aim (SmoothedAnimation, 350 ms in SolveScreen.qml), and
    // on a loaded machine it is still moving. Take the baseline only once it
    // has stood still for longer than the ease, in wall time - wait_for() spins
    // processEvents(5 ms), so two equal reads in a row would prove nothing.
    {
        QElapsedTimer still;
        still.start();
        QPointF last = lands(pose.centre);
        double last_scale = scale();
        REQUIRE(wait_for([&]() {
            const QPointF now = lands(pose.centre);
            if (std::abs(now.x() - last.x()) > 0.01 || std::abs(now.y() - last.y()) > 0.01 ||
                std::abs(scale() - last_scale) > 1e-4) {
                last = now;
                last_scale = scale();
                still.restart();
            }
            return still.elapsed() > 400;
        }));
    }
    const double closed_in = scale();

    // Shifted by a few pixels, as a hand does: the view stays put.
    cube::test::Pose shifted = pose;
    shifted.centre.x += 5.0F;
    const QPointF before = lands(pose.centre);
    for (int i = 0; i < 10; ++i) {
        show(shifted);
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    }
    CHECK(std::abs(lands(pose.centre).x() - before.x()) < 0.5);
    CHECK(std::abs(scale() - closed_in) < 1e-3);

    // Carried across the frame: followed.
    const cube::test::Pose across{.centre = {460.0F, 240.0F}, .pitch = 30.0F};
    show(across);
    REQUIRE(wait_for([&]() { return in_the_middle(across.centre); }));

    // Hard against the edge of the picture the view goes as far as the
    // picture does and no further: nothing outside the frame is shown.
    const cube::test::Pose corner{.centre = {70.0F, 60.0F}, .pitch = 30.0F};
    show(corner);
    REQUIRE(wait_for([&]() { return std::abs(lands({0.0F, 0.0F}).x() - 2.0) < 0.5; }));
    CHECK(lands({0.0F, 0.0F}).y() <= 2.0 + 0.5);

    // Taken away: after a moment the view opens out again.
    camera.processFrame(cube::test::room({640, 360}));
    REQUIRE(wait_for([&]() { return std::abs(scale() - whole()) < 1e-3; }));

    camera.setWatching(false);
}

// ---------------------------------------------------------------------------
// Choosing how the cube is solved
// ---------------------------------------------------------------------------

namespace {

// The persisted choice. The picker writes it; a binding there hands it to the
// controller, which re-solves from the cube as it stands.
[[nodiscard]] QObject* settings_state() {
    QObject* settings =
        cube::test::qml_engine().singletonInstance<QObject*>("Cubeulator", "SettingsState");
    REQUIRE(settings != nullptr);
    return settings;
}

void choose_method(int method) {
    REQUIRE(settings_state()->setProperty("solveMethod", method));
    SolveController& solver = cube::test::app_controllers().solve;
    REQUIRE(wait_for(
        [&]() { return static_cast<int>(solver.method()) == method && !solver.isSolving(); },
        60'000));
}

// What the picker's button for `method` looks like: primary when chosen.
[[nodiscard]] QString variant_of(const Screen& screen, int method) {
    QObject* button = screen.find_visual(QStringLiteral("solve.method.%1").arg(method));
    REQUIRE(button != nullptr);
    return button->property("variant").toString();
}

void press_method(const Screen& screen, int method) {
    QObject* button = screen.find_visual(QStringLiteral("solve.method.%1").arg(method));
    REQUIRE(button != nullptr);
    REQUIRE(QMetaObject::invokeMethod(button, "clicked"));
}

}  // namespace

TEST_CASE("the solve screen's picker changes how the cube is solved, from where it stands",
          "[qml][solve-screen][solve-method]") {
    SolveController& solver = cube::test::app_controllers().solve;
    const Cube scrambled = cube::solver::random_scramble(11);

    choose_method(SolveController::Beginner);
    solve_in_app(scrambled);
    Screen screen;
    screen.settle();

    // Beginner: five stages, and the picker says so.
    CHECK(screen.get("stageCount").toInt() == 5);
    CHECK(screen.get("stagePosition").toInt() == 0);
    CHECK(variant_of(screen, 0) == "primary");
    CHECK(variant_of(screen, 1) == "secondary");
    CHECK(variant_of(screen, 2) == "secondary");

    // Part-way in, switch to Intermediate by pressing its button.
    for (int i = 0; i < 4; ++i) {
        const int index = solver.moveIndex();
        screen.call("next");
        REQUIRE(wait_for([&]() { return solver.moveIndex() == index + 1; }));
    }
    const Cube here = solver.currentCube();
    press_method(screen, SolveController::Intermediate);
    REQUIRE(wait_for(
        [&]() { return solver.method() == SolveController::Intermediate && !solver.isSolving(); },
        60'000));
    screen.settle();

    CHECK(settings_state()->property("solveMethod").toInt() == SolveController::Intermediate);
    CHECK(solver.currentCube() == here);
    CHECK(solver.moveIndex() == 0);
    CHECK(screen.get("stageCount").toInt() == 4);
    CHECK(screen.get("stagePosition").toInt() == 0);
    CHECK(variant_of(screen, 0) == "secondary");
    CHECK(variant_of(screen, 1) == "primary");
    // The cube on screen is still the cube in hand, repainted not reset.
    CHECK(screen.shows(here));

    // And on to Advanced: two stages, then solved by stepping the screen.
    press_method(screen, SolveController::Advanced);
    REQUIRE(wait_for(
        [&]() { return solver.method() == SolveController::Advanced && !solver.isSolving(); },
        60'000));
    screen.settle();
    CHECK(screen.get("stageCount").toInt() == 2);
    CHECK(solver.currentCube() == here);
    CHECK(screen.shows(here));

    Cube expected = here;
    while (!solver.atEnd()) {
        const int index = solver.moveIndex();
        const Move move{side_of(solver.currentMoveFace()), solver.currentMoveTurns()};
        screen.settle();
        screen.call("next");
        REQUIRE(wait_for([&]() { return solver.moveIndex() == index + 1; }));
        expected = expected.apply(move);
        REQUIRE(solver.currentCube() == expected);
    }
    CHECK(expected.is_solved());
    // At the end the last stage is the one shown as current.
    CHECK(screen.get("stagePosition").toInt() == 1);

    choose_method(SolveController::Beginner);
}

TEST_CASE("each stage of each method has words", "[qml][solve-screen][solve-method]") {
    // The stages are named by cube::solver::Stage value; a value with no copy
    // would show a blank heading. Every stage of every method must have a
    // title and a line saying what it does (ids, so under test they are the
    // ids themselves - never empty and never undefined).
    solve_in_app(cube::solver::random_scramble(12));
    const Screen screen;
    for (int stage = 0; stage < 8; ++stage) {
        INFO("stage " << stage);
        QVariant copy;
        REQUIRE(QMetaObject::invokeMethod(screen.item(), "copyFor", Q_RETURN_ARG(QVariant, copy),
                                          Q_ARG(QVariant, stage)));
        const QVariantMap words = copy.toMap();
        CHECK_FALSE(words.value("title").toString().isEmpty());
        CHECK_FALSE(words.value("detail").toString().isEmpty());
    }
}
