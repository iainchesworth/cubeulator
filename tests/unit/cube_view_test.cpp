// The 3D cube on the solve screen, held to the solver's own cube.
//
// Cube3DView.qml is loaded for real and its stickers are read back out of
// the scene: where each one sits and which way it faces, taken from the
// nodes' own transforms (stickerPlacements). That is compared, sticker for
// sticker, with cube::solver::Cube - so what is proved is what is drawn, not
// a model of it.
//
// The direction of a turn is the point. A layer shown turning the wrong way,
// or a side painted as its own mirror image, sends someone following the
// screen the wrong way with a real cube in their hands.
#include <catch2/catch_test_macros.hpp>

#include <cube/solver/cube.hpp>
#include <cube/solver/notation.hpp>
#include <cube/solver/outside_view.hpp>
#include <cube/solver/solver.hpp>

#include <QColor>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QMetaObject>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QString>
#include <QUrl>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>

#include <array>
#include <cmath>
#include <cstddef>
#include <memory>
#include <optional>
#include <vector>

using cube::solver::Color;
using cube::solver::Cube;
using cube::solver::Face;
using cube::solver::kAllFaces;
using cube::solver::Move;

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

[[nodiscard]] QString letter(Face face) {
    switch (face) {
        case Face::U:
            return QStringLiteral("U");
        case Face::D:
            return QStringLiteral("D");
        case Face::L:
            return QStringLiteral("L");
        case Face::R:
            return QStringLiteral("R");
        case Face::F:
            return QStringLiteral("F");
        case Face::B:
            return QStringLiteral("B");
    }
    return {};
}

// The cube as the solve screen hands it to the view: each side as seen from
// outside, which is what SolveController::stickers() publishes.
[[nodiscard]] QVariantMap painting(const Cube& cube) {
    QVariantMap sides;
    for (const Face face : kAllFaces) {
        QVariantList colours;
        for (const Color colour : cube::solver::outside_view(cube, face)) {
            colours.append(paint(colour).name());
        }
        sides.insert(letter(face), colours);
    }
    return sides;
}

struct Vec {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

[[nodiscard]] Vec vec(const QVariant& list) {
    const QVariantList parts = list.toList();
    return {parts.at(0).toDouble(), parts.at(1).toDouble(), parts.at(2).toDouble()};
}

[[nodiscard]] double dot(const Vec& a, const Vec& b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}
[[nodiscard]] Vec operator-(const Vec& a, const Vec& b) {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

struct Placement {
    QColor colour;
    Vec cubie;   // which cubie position the sticker is on: x L to R, y D to U, z B to F
    Vec facing;  // which way it faces
};

// The side a sticker is on, when it faces squarely along an axis.
[[nodiscard]] std::optional<Face> side_facing(const Vec& facing) {
    constexpr double kSquare = 0.999;
    if (facing.x > kSquare) {
        return Face::R;
    }
    if (facing.x < -kSquare) {
        return Face::L;
    }
    if (facing.y > kSquare) {
        return Face::U;
    }
    if (facing.y < -kSquare) {
        return Face::D;
    }
    if (facing.z > kSquare) {
        return Face::F;
    }
    if (facing.z < -kSquare) {
        return Face::B;
    }
    return std::nullopt;
}

// Where the solver keeps the sticker at cubie (x, y, z) of a side, restated
// from the layout documented in cube.hpp rather than borrowed from the code
// under test: U and D are indexed (row = z + 1, col = x + 1), L and R
// (row = 1 - y, col = z + 1), F and B (row = 1 - y, col = x + 1). Were this
// restatement wrong for a side, no turn that carries stickers on or off that
// side could match Cube::apply below.
[[nodiscard]] int solver_index(Face face, int x, int y, int z) {
    switch (face) {
        case Face::U:
        case Face::D:
            return (z + 1) * 3 + (x + 1);
        case Face::L:
        case Face::R:
            return (1 - y) * 3 + (z + 1);
        case Face::F:
        case Face::B:
            return (1 - y) * 3 + (x + 1);
    }
    return 0;
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

template <typename Done>
[[nodiscard]] bool wait_for(Done done, int milliseconds = 5000) {
    QElapsedTimer waited;
    waited.start();
    while (!done() && waited.elapsed() < milliseconds) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
    }
    return done();
}

// A Cube3DView on its own, with no window: nothing is drawn, and nothing
// needs to be.
class View {
public:
    View() {
        QQmlComponent component(&engine_);
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

    void show(const Cube& cube) { set("faceletColors", painting(cube)); }

    [[nodiscard]] std::vector<Placement> placements() const {
        QVariant result;
        REQUIRE(QMetaObject::invokeMethod(item_.get(), "stickerPlacements",
                                          Q_RETURN_ARG(QVariant, result)));
        std::vector<Placement> out;
        for (const QVariant& entry : result.toList()) {
            const QVariantMap map = entry.toMap();
            out.push_back({QColor(map.value("color").toString()), vec(map.value("cubie")),
                           vec(map.value("facing"))});
        }
        return out;
    }

    // The stickers as the solver would hold them: [side][index] -> colour.
    // Fails unless every sticker sits squarely on a cubie position, facing
    // squarely outwards, and all 54 places are filled once each.
    [[nodiscard]] std::array<std::array<QColor, 9>, 6> read() const {
        std::array<std::array<QColor, 9>, 6> sides{};
        std::array<std::array<int, 9>, 6> seen{};
        for (const Placement& placement : placements()) {
            const std::optional<Face> side = side_facing(placement.facing);
            REQUIRE(side.has_value());
            const std::array<double, 3> at = {placement.cubie.x, placement.cubie.y,
                                              placement.cubie.z};
            std::array<int, 3> cell{};
            for (std::size_t axis = 0; axis < 3; ++axis) {
                cell[axis] = static_cast<int>(std::lround(at[axis]));
                REQUIRE(std::abs(at[axis] - cell[axis]) < 0.01);
                REQUIRE(std::abs(cell[axis]) <= 1);
            }
            const auto index =
                static_cast<std::size_t>(solver_index(*side, cell[0], cell[1], cell[2]));
            sides[static_cast<std::size_t>(*side)][index] = placement.colour;
            ++seen[static_cast<std::size_t>(*side)][index];
        }
        for (const auto& side : seen) {
            for (const int count : side) {
                REQUIRE(count == 1);
            }
        }
        return sides;
    }

    [[nodiscard]] bool shows(const Cube& cube) const {
        const auto sides = read();
        bool same = true;
        for (const Face face : kAllFaces) {
            for (int index = 0; index < 9; ++index) {
                const QColor seen =
                    sides[static_cast<std::size_t>(face)][static_cast<std::size_t>(index)];
                if (seen != paint(cube.at(face, index))) {
                    UNSCOPED_INFO("side " << letter(face).toStdString() << " index " << index
                                          << " shows " << seen.name().toStdString()
                                          << ", the solver has "
                                          << paint(cube.at(face, index)).name().toStdString());
                    same = false;
                }
            }
        }
        return same;
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
    QQmlEngine engine_;
    std::unique_ptr<QQuickItem> item_;
};

[[nodiscard]] std::vector<Move> all_moves() {
    std::vector<Move> moves;
    for (const Face face : kAllFaces) {
        for (int turns = 1; turns <= 3; ++turns) {
            moves.push_back(Move{face, turns});
        }
    }
    return moves;
}

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
