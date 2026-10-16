// The 3D cube on the solve screen for a cube that is not a 3x3, held to the
// library's own cube.
//
// CubeNView.qml is loaded for real and its stickers are read back out of it
// (cube_view_reading.hpp) and compared, sticker for sticker, with
// cube::nxn::NCube at every size from 2 to 9: so what is proved is what is
// drawn. The direction of a turn is the point, as for the 3x3: layers shown
// turning the wrong way, or a side painted as its own mirror image, send
// someone following the screen the wrong way with a real cube in their hands.
#include <catch2/catch_test_macros.hpp>

#include "cube_view_reading.hpp"
#include "qml_test_app.hpp"

#include <cube/nxn/cube.hpp>
#include <cube/nxn/geometry.hpp>
#include <cube/solver/cube.hpp>

#include <QColor>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QImage>
#include <QMetaObject>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QVariant>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <memory>
#include <utility>
#include <vector>

using cube::nxn::LayerMove;
using cube::nxn::NCube;
using cube::solver::Color;
using cube::solver::Face;
using cube::test::letter;
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

// The real CubeNView, wrapped only to count its movePlayed signals.
constexpr auto kHarness = R"QML(
import QtQuick
import Cubeulator
CubeNView {
    property int played: 0
    onMovePlayed: played += 1
    // Still, as on the solve screen.
    autoRotate: false
    interactive: false
    homeRotation: Qt.vector3d(0, 0, 0)
}
)QML";

// A CubeNView on its own, with no window: nothing is drawn, and nothing needs to be.
class View {
public:
    View() {
        QQmlComponent component(&cube::test::qml_engine());
        component.setData(kHarness, QUrl(QStringLiteral("qrc:/cube_n_view_test_harness.qml")));
        if (component.isError()) {
            FAIL(component.errorString().toStdString());
        }
        item_.reset(qobject_cast<QQuickItem*>(component.create()));
        if (item_ == nullptr) {
            FAIL(component.errorString().toStdString());
        }
        // Turns are played fast; how long they take is not under test.
        set("turnDuration", 5);
    }

    void set(const char* name, const QVariant& value) { REQUIRE(item_->setProperty(name, value)); }
    [[nodiscard]] QVariant get(const char* name) const { return item_->property(name); }
    [[nodiscard]] QObject* item() const { return item_.get(); }

    void show(const NCube& cube) {
        // A fresh JavaScript object each time; see cube::test::painting().
        set("faceletColors",
            QVariant::fromValue(cube::test::painting(cube::test::qml_engine(), cube, &paint)));
    }
    [[nodiscard]] bool shows(const NCube& cube) const {
        return cube::test::shows(item_.get(), cube, &paint);
    }

    [[nodiscard]] int played() const { return get("played").toInt(); }

    // Starts the layers `first` to `last` of `face` turning `turns` quarter turns round.
    void start(Face face, int first, int last, int turns) {
        QVariant started;
        REQUIRE(QMetaObject::invokeMethod(item_.get(), "playMove", Q_RETURN_ARG(QVariant, started),
                                          Q_ARG(QVariant, letter(face)), Q_ARG(QVariant, first),
                                          Q_ARG(QVariant, last), Q_ARG(QVariant, turns)));
        REQUIRE(started.toBool());
    }

    // Turns them all the way and waits for the view to say so.
    void play(Face face, int first, int last, int turns) {
        const int before = played();
        start(face, first, last, turns);
        CHECK(get("playing").toBool());
        REQUIRE(wait_for([&]() { return played() > before; }));
        CHECK(played() == before + 1);
        CHECK_FALSE(get("playing").toBool());
    }

    void call(const char* method) { REQUIRE(QMetaObject::invokeMethod(item_.get(), method)); }

    // Nudges the layers a move turns, or stops nudging.
    void guide(Face face, int first, int last, int turns) {
        set("guideTurns", turns);
        set("guideFirst", first);
        set("guideLast", last);
        set("guideFace", letter(face));
    }
    void stop_guiding() { set("guideFace", QString()); }

    [[nodiscard]] double turn_angle() const { return get("turnAngle").toDouble(); }

private:
    std::unique_ptr<QQuickItem> item_;
};

// Scrambled with every side mixed, so a sticker in the wrong place shows.
[[nodiscard]] NCube scrambled(int size, unsigned seed) {
    return cube::nxn::random_scramble({.size = size, .seed = seed, .moves = 40 + (8 * size)});
}

// The cube after the layers `first` to `last` of `face` turn `turns` quarter turns.
[[nodiscard]] NCube after(NCube cube, Face face, int first, int last, int turns) {
    for (int depth = first; depth <= last; ++depth) {
        cube.apply(LayerMove{.face = face, .depth = depth, .turns = turns});
    }
    return cube;
}

// A selection of moves for a cube of `size`: from every side, the outer layer, the
// next, the farthest, two blocks and the whole cube turned as one, each at
// an angle that cycles through quarter, half and three-quarters.
struct Move {
    Face face;
    int first;
    int last;
    int turns;
};

[[nodiscard]] std::vector<Move> moves_for(int size) {
    std::vector<std::pair<int, int>> spans;
    for (const auto& span : {std::pair{0, 0}, std::pair{1, 1}, std::pair{size - 1, size - 1},
                             std::pair{0, 1}, std::pair{0, size - 1}}) {
        if (std::ranges::find(spans, span) == spans.end()) {
            spans.push_back(span);
        }
    }
    std::vector<Move> moves;
    int count = 0;
    for (const Face face : cube::solver::kAllFaces) {
        for (const auto& [first, last] : spans) {
            moves.push_back(
                Move{.face = face, .first = first, .last = last, .turns = 1 + (count++ % 3)});
        }
    }
    return moves;
}

}  // namespace

TEST_CASE("the cube is painted as the library has it, at every size", "[qml][cube-n-view]") {
    View view;
    for (int size = cube::nxn::kMinSize; size <= cube::nxn::kMaxSize; ++size) {
        INFO("size " << size);
        const NCube solved = *NCube::solved(size);
        view.show(solved);
        CHECK(view.get("cubeSize").toInt() == size);
        CHECK(view.shows(solved));

        for (unsigned seed = 1; seed <= 3; ++seed) {
            const NCube cube = scrambled(size, seed);
            view.show(cube);
            CHECK(view.shows(cube));
            // A check that could not fail would prove nothing.
            CHECK_FALSE(view.shows(after(cube, Face::R, 0, 0, 1)));
        }
    }
}

TEST_CASE("an unpainted cube is each side its own colour, at the size it is given",
          "[qml][cube-n-view]") {
    View view;
    for (const int size : {2, 3, 5, 7}) {
        INFO("size " << size);
        view.set("faceletColors", QVariant());
        view.set("size", size);
        view.set("topColor", QColor("#f00001"));
        view.set("frontColor", QColor("#00f002"));
        CHECK(view.get("cubeSize").toInt() == size);
        const auto placements = cube::test::placements(view.item());
        REQUIRE(placements.size() == static_cast<std::size_t>(6 * size * size));
        int top = 0;
        int front = 0;
        for (const cube::test::Placement& placement : placements) {
            if (placement.facing.y > 0.5) {
                CHECK(placement.colour == QColor("#f00001"));
                ++top;
            }
            if (placement.facing.z > 0.5) {
                CHECK(placement.colour == QColor("#00f002"));
                ++front;
            }
        }
        CHECK(top == size * size);
        CHECK(front == size * size);
    }
}

TEST_CASE("every move turns the layers the library turns, the way it turns them",
          "[qml][cube-n-view]") {
    View view;
    for (const int size : {2, 3, 4, 5, 6, 7, 9}) {
        const NCube cube = scrambled(size, 7);
        for (const Move& move : moves_for(size)) {
            INFO("size " << size << " side " << letter(move.face).toStdString() << " layers "
                         << move.first << " to " << move.last << " turns " << move.turns);
            view.show(cube);
            REQUIRE(view.shows(cube));

            view.play(move.face, move.first, move.last, move.turns);
            const NCube turned = after(cube, move.face, move.first, move.last, move.turns);
            REQUIRE(view.shows(turned));

            // The screen then repaints the cube as the library now has it, with every
            // layer back where it started. Nothing may change.
            view.show(turned);
            REQUIRE(view.shows(turned));
        }
    }
}

TEST_CASE("moves played one after another without repainting stay with the library",
          "[qml][cube-n-view]") {
    View view;
    for (const int size : {4, 5}) {
        NCube cube = scrambled(size, 21);
        view.show(cube);
        for (const Face face : cube::solver::kAllFaces) {
            for (int depth = 0; depth < size; ++depth) {
                INFO("size " << size << " side " << letter(face).toStdString() << " depth "
                             << depth);
                view.play(face, depth, depth, 1 + (depth % 3));
                cube = after(cube, face, depth, depth, 1 + (depth % 3));
                REQUIRE(view.shows(cube));
            }
        }
    }
}

TEST_CASE("a turn cut short lands where the whole turn would have", "[qml][cube-n-view]") {
    View view;
    view.set("turnDuration", 2000);
    const NCube cube = scrambled(5, 31);
    for (const Move& move : {Move{.face = Face::U, .first = 0, .last = 0, .turns = 1},
                             Move{.face = Face::L, .first = 1, .last = 2, .turns = 2},
                             Move{.face = Face::B, .first = 2, .last = 2, .turns = 3},
                             Move{.face = Face::R, .first = 0, .last = 1, .turns = 1}}) {
        INFO("side " << letter(move.face).toStdString() << " turns " << move.turns);
        view.show(cube);
        const int before = view.played();
        view.start(move.face, move.first, move.last, move.turns);
        // Part-way round, then told to finish.
        REQUIRE(wait_for([&]() { return std::abs(view.turn_angle()) > 5.0; }));
        CHECK(view.get("playing").toBool());
        view.call("finishPlaying");
        CHECK(view.played() == before + 1);
        CHECK_FALSE(view.get("playing").toBool());
        REQUIRE(view.shows(after(cube, move.face, move.first, move.last, move.turns)));
    }
}

TEST_CASE("a move that is not a move on this cube is refused", "[qml][cube-n-view]") {
    View view;
    view.show(*NCube::solved(4));
    for (const Move& move : {Move{.face = Face::U, .first = 0, .last = 0, .turns = 0},
                             Move{.face = Face::U, .first = 0, .last = 0, .turns = 4},
                             Move{.face = Face::U, .first = 2, .last = 1, .turns = 1},
                             Move{.face = Face::U, .first = 0, .last = 4, .turns = 1},
                             Move{.face = Face::U, .first = -1, .last = 0, .turns = 1}}) {
        INFO("layers " << move.first << " to " << move.last << " turns " << move.turns);
        QVariant started;
        REQUIRE(QMetaObject::invokeMethod(
            view.item(), "playMove", Q_RETURN_ARG(QVariant, started), Q_ARG(QVariant, QString("U")),
            Q_ARG(QVariant, move.first), Q_ARG(QVariant, move.last), Q_ARG(QVariant, move.turns)));
        CHECK_FALSE(started.toBool());
        CHECK_FALSE(view.get("playing").toBool());
    }
    CHECK(view.shows(*NCube::solved(4)));
}

TEST_CASE("the nudge leans the way the move goes, and leaves the cube as it was",
          "[qml][cube-n-view]") {
    View view;
    const NCube cube = scrambled(5, 41);
    const double nudge = view.get("nudgeAngle").toDouble();
    view.show(cube);

    // The turn's sign is the layers' own: about the positive end of the axis,
    // clockwise is negative, and the nudge leans with it. R, U and F are looked at
    // from the positive end, L, D and B from the negative.
    struct Lean {
        Face face;
        int turns;
        double sign;
    };
    for (const Lean lean : {Lean{.face = Face::R, .turns = 1, .sign = -1},
                            Lean{.face = Face::R, .turns = 3, .sign = 1},
                            Lean{.face = Face::L, .turns = 1, .sign = 1},
                            Lean{.face = Face::L, .turns = 3, .sign = -1},
                            Lean{.face = Face::U, .turns = 1, .sign = -1},
                            Lean{.face = Face::D, .turns = 1, .sign = 1},
                            Lean{.face = Face::F, .turns = 3, .sign = 1},
                            Lean{.face = Face::B, .turns = 1, .sign = 1},
                            Lean{.face = Face::R, .turns = 2, .sign = -1}}) {
        INFO("side " << letter(lean.face).toStdString() << " turns " << lean.turns);
        view.show(cube);
        view.guide(lean.face, 1, 2, lean.turns);
        const double reach = (lean.turns == 2 ? 1.6 : 0.6) * nudge;
        REQUIRE(wait_for([&]() { return std::abs(view.turn_angle()) > reach; }));
        CHECK(view.get("nudging").toBool());
        CHECK_FALSE(view.get("playing").toBool());
        CHECK((view.turn_angle() > 0 ? 1.0 : -1.0) == lean.sign);
        // The layers lean; the cube is the one painted.
        CHECK(view.shows(cube));
        view.stop_guiding();
    }

    // With no side to guide, nothing nudges.
    view.show(cube);
    CHECK(wait_for([&]() { return !view.get("nudging").toBool(); }));
    CHECK(std::abs(view.turn_angle()) < 1e-9);
}

TEST_CASE("the cube is three scene objects whatever its size", "[qml][cube-n-view]") {
    View view;
    for (const int size : {2, 5, 9}) {
        INFO("size " << size);
        view.show(*NCube::solved(size));
        int built = 0;
        int active = 0;
        for (QObject* object : view.item()->findChildren<QObject*>()) {
            if (qstrcmp(object->metaObject()->className(), "CubeNGeometry") != 0) {
                continue;
            }
            ++built;
            if (object->property("active").toBool()) {
                ++active;
                // At rest the one that shows is the whole cube: 12 N^2 quads.
                CHECK(object->property("quads").toInt() == 12 * size * size);
            }
        }
        CHECK(built == 3);
        CHECK(active == 1);
    }
    // In hand it is the rest and the layers instead.
    view.show(*NCube::solved(5));
    view.guide(Face::F, 0, 0, 1);
    REQUIRE(wait_for([&]() { return view.get("held").isValid() && !view.get("held").isNull(); }));
    int active = 0;
    int quads = 0;
    for (QObject* object : view.item()->findChildren<QObject*>()) {
        if (qstrcmp(object->metaObject()->className(), "CubeNGeometry") == 0 &&
            object->property("active").toBool()) {
            ++active;
            quads += object->property("quads").toInt();
        }
    }
    CHECK(active == 2);
    // The cube and the cut: N^2 plastic quads on each side of it.
    CHECK(quads == (12 * 5 * 5) + (2 * 5 * 5));
}

// Pictures of the cube at each size, for looking at rather than for checking.
//
// Hidden ("[.gallery]") and inert unless CUBE_GALLERY_DIR names a directory: it
// puts a real CubeNView in a real window, parked off-screen, and saves what the
// window renders. It needs a GPU, so it is never part of the ordinary test run.
// One picture per run (grabbing a window with a 3D scene in it twice is not
// safe on every backend); CUBE_GALLERY_SHOT names it, as "<size>-rest" or
// "<size>-lifted", for example "5-lifted".
//
//   set CUBE_GALLERY_DIR=C:\some\dir
//   set CUBE_GALLERY_SHOT=5-lifted
//   cube_qml_tests "[.gallery]"
TEST_CASE("n view gallery", "[.gallery]") {
    const QString out = qEnvironmentVariable("CUBE_GALLERY_DIR");
    if (out.isEmpty()) {
        SKIP("set CUBE_GALLERY_DIR to a directory to write the pictures to");
    }
    REQUIRE(QDir().mkpath(out));
    const QString wanted = qEnvironmentVariable("CUBE_GALLERY_SHOT", "5-rest");
    const QStringList parts = wanted.split(QLatin1Char('-'));
    REQUIRE(parts.size() == 2);
    const int size = parts.at(0).toInt();
    REQUIRE(size >= cube::nxn::kMinSize);
    REQUIRE(size <= cube::nxn::kMaxSize);

    QQuickWindow window;
    window.setFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowDoesNotAcceptFocus);
    window.setPosition(-4000, -4000);

    QQmlComponent component(&cube::test::qml_engine());
    component.setData(R"QML(
import QtQuick
import Cubeulator
CubeNView {
    autoRotate: false
    interactive: false
    evenLight: true
    homeRotation: Qt.vector3d(0, 0, 0)
    viewPitch: 24
    viewYaw: -32
}
)QML",
                      QUrl(QStringLiteral("qrc:/cube_n_view_gallery.qml")));
    std::unique_ptr<QQuickItem> view(
        qobject_cast<QQuickItem*>(component.beginCreate(cube::test::qml_engine().rootContext())));
    if (view == nullptr) {
        FAIL(component.errorString().toStdString());
    }
    view->setParentItem(window.contentItem());
    component.completeCreate();
    view->setWidth(640);
    view->setHeight(640);
    window.resize(640, 640);

    const NCube cube = scrambled(size, 5);
    REQUIRE(view->setProperty("faceletColors", QVariant::fromValue(cube::test::painting(
                                                   cube::test::qml_engine(), cube, &paint))));
    if (parts.at(1) == QLatin1String("lifted")) {
        // The layer the move turns, a quarter way round on its nudge.
        REQUIRE(view->setProperty("guideFace", QStringLiteral("R")));
        REQUIRE(view->setProperty("guideFirst", size / 2));
        REQUIRE(view->setProperty("guideLast", size / 2));
        REQUIRE(view->setProperty("guideTurns", 1));
        REQUIRE(view->setProperty("nudgeAngle", 40.0));
    }
    window.show();
    if (parts.at(1) == QLatin1String("lifted")) {
        REQUIRE(wait_for([&]() { return std::abs(view->property("turnAngle").toDouble()) > 28.0; },
                         20'000));
    } else {
        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < 1500) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
        }
    }
    const QImage image = window.grabWindow();
    REQUIRE_FALSE(image.isNull());
    REQUIRE(image.save(QDir(out).filePath(wanted + ".png")));
}
