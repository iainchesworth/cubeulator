// The 3D cube on the solve screen, held to the solver's own cube.
//
// Cube3DView.qml is loaded for real and its stickers are read back out of
// the scene: where each one sits and which way it faces, taken from the
// nodes' own transforms (see cube_view_reading.hpp). That is compared,
// sticker for sticker, with cube::solver::Cube - so what is proved is what is
// drawn, not a model of it.
//
// The direction of a turn is the point. A layer shown turning the wrong way,
// or a side painted as its own mirror image, sends someone following the
// screen the wrong way with a real cube in their hands.
#include <catch2/catch_test_macros.hpp>

#include "cube_view_reading.hpp"
#include "qml_test_app.hpp"

#include <cube/solver/cube.hpp>
#include <cube/solver/hold.hpp>
#include <cube/solver/notation.hpp>
#include <cube/solver/outside_view.hpp>
#include <cube/solver/solver.hpp>

#include <QColor>
#include <QMetaObject>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuaternion>
#include <QQuickItem>
#include <QString>
#include <QUrl>
#include <QVariant>

#include <cmath>
#include <cstddef>
#include <memory>
#include <optional>
#include <vector>

using cube::solver::Color;
using cube::solver::Cube;
using cube::solver::Face;
using cube::solver::Hold;
using cube::solver::kAllFaces;
using cube::solver::Move;
using cube::solver::Place;
using cube::test::all_moves;
using cube::test::dot;
using cube::test::letter;
using cube::test::Placement;
using cube::test::Vec;
using cube::test::wait_for;

namespace {

// Six colours no two of which could be confused, whatever the palette.
[[nodiscard]] QColor paint(Color colour) {
    switch (colour) {
        case Color::White:
            return {250, 250, 250};
        case Color::Yellow:
            return {240, 220, 0};
        case Color::Green:
            return {0, 160, 60};
        case Color::Blue:
            return {0, 70, 200};
        case Color::Orange:
            return {250, 120, 0};
        case Color::Red:
            return {200, 0, 30};
    }
    return {};
}

// The real Cube3DView, wrapped only to count its movePlayed signals.
constexpr auto kHarness = R"QML(
import QtQuick
import Cubeulator
Cube3DView {
    property int played: 0
    onMovePlayed: played += 1
    // Still, as on the solve screen.
    autoRotate: false
    interactive: false
    homeRotation: Qt.vector3d(0, 0, 0)
}
)QML";

// A Cube3DView on its own, with no window: nothing is drawn, and nothing
// needs to be.
class View {
public:
    View() {
        QQmlComponent component(&cube::test::qml_engine());
        component.setData(kHarness, QUrl(QStringLiteral("qrc:/cube_view_test_harness.qml")));
        if (component.isError()) {
            FAIL(component.errorString().toStdString());
        }
        item_.reset(qobject_cast<QQuickItem*>(component.create()));
        if (item_ == nullptr) {
            FAIL(component.errorString().toStdString());
        }
        // Turns are played fast; how long they take is not under test.
        set("turnDuration", 40);
    }

    void set(const char* name, const QVariant& value) { REQUIRE(item_->setProperty(name, value)); }
    [[nodiscard]] QVariant get(const char* name) const { return item_->property(name); }
    [[nodiscard]] QObject* item() const { return item_.get(); }

    void show(const Cube& cube) {
        // A fresh JavaScript object each time; see cube::test::painting().
        set("faceletColors",
            QVariant::fromValue(cube::test::painting(cube::test::qml_engine(), cube, &paint)));
    }
    [[nodiscard]] bool shows(const Cube& cube) const {
        return cube::test::shows(item_.get(), cube, &paint);
    }
    [[nodiscard]] std::vector<Placement> placements() const {
        return cube::test::placements(item_.get());
    }

    [[nodiscard]] int played() const { return get("played").toInt(); }

    // Starts the layer of `move` turning all the way round.
    void start(Move move) {
        QVariant started;
        REQUIRE(QMetaObject::invokeMethod(item_.get(), "playMove", Q_RETURN_ARG(QVariant, started),
                                          Q_ARG(QVariant, letter(move.face)),
                                          Q_ARG(QVariant, move.turns)));
        REQUIRE(started.toBool());
    }

    // Turns the layer of `move` all the way and waits for the view to say so.
    void play(Move move) {
        const int before = played();
        start(move);
        CHECK(get("playing").toBool());
        REQUIRE(wait_for([&]() { return played() > before; }));
        CHECK(played() == before + 1);
        CHECK_FALSE(get("playing").toBool());
    }

    void call(const char* method) { REQUIRE(QMetaObject::invokeMethod(item_.get(), method)); }

    // Nudges the layer `move` turns, or stops nudging.
    void guide(std::optional<Move> move) {
        set("guideTurns", move.has_value() ? move->turns : 1);
        set("guideFace", move.has_value() ? letter(move->face) : QString());
    }

    [[nodiscard]] double turn_angle() const { return get("turnAngle").toDouble(); }

private:
    std::unique_ptr<QQuickItem> item_;
};

// Scrambles with every side mixed, so a sticker in the wrong place shows.
[[nodiscard]] std::vector<Cube> scrambles() {
    return {cube::solver::random_scramble(11), cube::solver::random_scramble(12),
            cube::solver::random_scramble(13)};
}

}  // namespace

TEST_CASE("the 3D cube is painted as the solver has the cube", "[qml][cube-view]") {
    View view;

    view.show(Cube::solved());
    CHECK(view.shows(Cube::solved()));

    for (const Cube& cube : scrambles()) {
        view.show(cube);
        CHECK(view.shows(cube));
        // A check that could not fail would prove nothing.
        CHECK_FALSE(view.shows(cube.apply(Move{Face::R, 1})));
    }
}

TEST_CASE("every move turns the layer the solver turns, the way the solver turns it",
          "[qml][cube-view]") {
    View view;
    for (const Cube& cube : scrambles()) {
        for (const Move move : all_moves()) {
            INFO("move " << cube::solver::to_string(move));
            view.show(cube);
            REQUIRE(view.shows(cube));

            view.play(move);
            const Cube turned = cube.apply(move);
            REQUIRE(view.shows(turned));

            // The screen then repaints the cube as the solver now has it,
            // with every layer back where it started. Nothing may change.
            view.show(turned);
            REQUIRE(view.shows(turned));
        }
    }
}

TEST_CASE("moves played one after another without repainting stay with the solver",
          "[qml][cube-view]") {
    View view;
    Cube cube = cube::solver::random_scramble(21);
    view.show(cube);
    for (int round = 0; round < 2; ++round) {
        for (const Move move : all_moves()) {
            INFO("move " << cube::solver::to_string(move));
            view.play(move);
            cube = cube.apply(move);
            REQUIRE(view.shows(cube));
        }
    }
}

TEST_CASE("a turn cut short lands where the whole turn would have", "[qml][cube-view]") {
    View view;
    view.set("turnDuration", 2000);
    const Cube cube = cube::solver::random_scramble(31);
    for (const Move move : all_moves()) {
        INFO("move " << cube::solver::to_string(move));
        view.show(cube);
        const int before = view.played();
        view.start(move);
        // Part-way round, then told to finish.
        REQUIRE(wait_for([&]() { return std::abs(view.turn_angle()) > 5.0; }));
        CHECK(view.get("playing").toBool());
        view.call("finishPlaying");
        CHECK(view.played() == before + 1);
        CHECK_FALSE(view.get("playing").toBool());
        REQUIRE(view.shows(cube.apply(move)));
    }
}

TEST_CASE("the nudge leans the way the move goes, and leaves the cube as it was",
          "[qml][cube-view]") {
    View view;
    const Cube cube = cube::solver::random_scramble(41);
    const double nudge = view.get("nudgeAngle").toDouble();

    for (const Move move : all_moves()) {
        INFO("move " << cube::solver::to_string(move));
        view.show(cube);
        const std::vector<Placement> home = view.placements();

        // Where every sticker ends up when the move is made - already held
        // to Cube::apply above, and wanted here only for its direction.
        view.play(move);
        const std::vector<Placement> made = view.placements();
        REQUIRE(view.shows(cube.apply(move)));
        view.show(cube);

        view.guide(move);
        // A quarter turn nudges one beat out; a half turn goes a second beat.
        const double reach = (move.turns == 2 ? 1.6 : 0.6) * nudge;
        REQUIRE(wait_for([&]() { return std::abs(view.turn_angle()) > reach; }));
        CHECK(view.get("nudging").toBool());
        CHECK_FALSE(view.get("playing").toBool());
        const std::vector<Placement> leaning = view.placements();
        REQUIRE(leaning.size() == home.size());

        int moving = 0;
        for (std::size_t i = 0; i < home.size(); ++i) {
            const Vec travel = made[i].cubie - home[i].cubie;
            const Vec lean = leaning[i].cubie - home[i].cubie;
            const Vec turn = made[i].facing - home[i].facing;
            const Vec tilt = leaning[i].facing - home[i].facing;
            const bool travels = dot(travel, travel) > 0.5;
            const bool turns = dot(turn, turn) > 0.5;
            if (!travels && !turns) {
                // Not carried anywhere by the move: must not stir. (The
                // centre of the layer turns on the spot and is among these.)
                CHECK(dot(lean, lean) < 1e-6);
                continue;
            }
            ++moving;
            if (move.turns == 2) {
                continue;  // a half turn ends in the same place either way round
            }
            // Part of the way there - not part of the way to the opposite turn.
            if (travels) {
                CHECK(dot(lean, travel) > 0.05);
            }
            if (turns) {
                CHECK(dot(tilt, turn) > 0.05);
            }
        }
        // A layer carries 21 stickers; the centre one only turns on the spot.
        CHECK(moving == 20);
        // Never so far round that it could be taken for the turn itself.
        CHECK(std::abs(view.turn_angle()) < 80.0);

        view.guide(std::nullopt);
        REQUIRE(wait_for([&]() { return !view.get("nudging").toBool(); }));
        CHECK(view.shows(cube));
    }
}

TEST_CASE("asking for the move while its layer is mid-nudge still makes the move",
          "[qml][cube-view]") {
    View view;
    const Cube cube = cube::solver::random_scramble(51);
    for (const Move move : all_moves()) {
        INFO("move " << cube::solver::to_string(move));
        view.show(cube);
        view.guide(move);
        REQUIRE(wait_for([&]() { return std::abs(view.turn_angle()) > 8.0; }));
        view.play(move);
        REQUIRE(view.shows(cube.apply(move)));
    }
}

TEST_CASE("the cube is turned the way it is held, and never mirrored", "[qml][cube-view]") {
    View view;
    view.set("viewDuration", 0);
    cube::app::HeldCube& held = cube::test::app_controllers().held;
    const Cube cube = cube::solver::random_scramble(61);
    view.show(cube);

    for (const Hold& hold : cube::solver::all_holds()) {
        INFO("up " << letter(hold.up).toStdString() << " front "
                   << letter(hold.front).toStdString());
        held.setHold(hold);
        view.set("holdRotation", QVariant::fromValue(held.rotation()));
        REQUIRE(wait_for([&]() { return !view.get("viewMoving").toBool(); }));

        // Seen square on (no viewpoint applied): the side held on top faces
        // up the screen, the side held in front faces the viewer, and the
        // side on the holder's right faces right. All three together rule
        // out a reflection.
        const auto faces = [&](Place place, const Vec& way) {
            const Vec seen =
                cube::test::side_direction(view.item(), cube::solver::side_at(hold, place));
            return dot(seen, way) > 0.999;
        };
        CHECK(faces(Place::Top, {0, 1, 0}));
        CHECK(faces(Place::Front, {0, 0, 1}));
        CHECK(faces(Place::Right, {1, 0, 0}));
        CHECK(faces(Place::Bottom, {0, -1, 0}));
        CHECK(faces(Place::Back, {0, 0, -1}));
        CHECK(faces(Place::Left, {-1, 0, 0}));

        // Turning the whole cube moves no sticker on it.
        CHECK(view.shows(cube));
    }
    held.setHold(cube::solver::kStatedHold);
}

TEST_CASE("the viewpoint shows the top, the front and the right side", "[qml][cube-view]") {
    // Positive pitch tips the top towards the viewer and negative yaw brings
    // the right side round - the signs the solve screen relies on.
    View view;
    view.set("viewDuration", 0);
    view.set("viewPitch", 24);
    view.set("viewYaw", -32);
    REQUIRE(wait_for([&]() { return !view.get("viewMoving").toBool(); }));

    const auto towards_viewer = [&](Face side) {
        return cube::test::side_direction(view.item(), side).z;
    };
    CHECK(towards_viewer(Face::U) > 0.3);
    CHECK(towards_viewer(Face::F) > 0.3);
    CHECK(towards_viewer(Face::R) > 0.3);
    CHECK(towards_viewer(Face::D) < -0.3);
    CHECK(towards_viewer(Face::B) < -0.3);
    CHECK(towards_viewer(Face::L) < -0.3);
    // Upright: the top still faces up the screen, not off to one side.
    CHECK(std::abs(cube::test::side_direction(view.item(), Face::U).x) < 1e-6);
}

TEST_CASE("a dragged view shows the back, and comes home when let go", "[qml][cube-view]") {
    View view;
    view.set("viewDuration", 0);
    view.set("dragTurnsView", true);
    view.set("viewPitch", 24);
    view.set("viewYaw", -32);
    REQUIRE(wait_for([&]() { return !view.get("viewMoving").toBool(); }));
    const auto towards_viewer = [&](Face side) {
        return cube::test::side_direction(view.item(), side).z;
    };
    const Cube cube = cube::solver::random_scramble(71);
    view.show(cube);
    view.guide(Move{Face::R, 1});
    REQUIRE(wait_for([&]() { return view.get("nudging").toBool(); }));

    // Half a turn round and tipped over: the back and the bottom come into
    // view, the front and the top go out of it.
    view.set("dragYaw", 180.0);
    view.set("dragPitch", -48.0);
    CHECK(towards_viewer(Face::B) > 0.3);
    CHECK(towards_viewer(Face::D) > 0.3);
    CHECK(towards_viewer(Face::F) < -0.3);
    CHECK(towards_viewer(Face::U) < -0.3);
    // Looking round the cube is not turning it: the layer goes on nudging.
    CHECK(view.get("nudging").toBool());

    view.call("releaseView");
    REQUIRE(wait_for([&]() {
        return view.get("dragYaw").toDouble() == 0.0 && view.get("dragPitch").toDouble() == 0.0;
    }));
    CHECK(towards_viewer(Face::U) > 0.3);
    CHECK(towards_viewer(Face::F) > 0.3);
    CHECK(towards_viewer(Face::R) > 0.3);

    // Wound round more than once, it still comes home, and the short way.
    view.set("dragYaw", 760.0);
    view.call("releaseView");
    CHECK(std::abs(view.get("dragYaw").toDouble()) <= 180.0);
    REQUIRE(wait_for([&]() { return view.get("dragYaw").toDouble() == 0.0; }));

    // And no sticker has moved for any of it.
    view.guide(std::nullopt);
    REQUIRE(wait_for([&]() { return !view.get("nudging").toBool(); }));
    CHECK(view.shows(cube));
}
