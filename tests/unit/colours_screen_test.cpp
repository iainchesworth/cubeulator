// The screen where a cube is typed in a colour at a time (ColoursScreen.qml) behind
// the app's own controllers: the sides named round the grid, what the person paints
// arriving in the controller, the status that follows, the cube drawn beside it, and
// what happens on the way out.
#include <catch2/catch_test_macros.hpp>

#include "colour_name.hpp"
#include "cube_view_reading.hpp"
#include "qml_test_app.hpp"

#include <cube/nxn/cube.hpp>
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
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>

#include <array>
#include <memory>
#include <string>
#include <vector>

using cube::nxn::NCube;
using cube::solver::Color;
using cube::test::wait_for;

namespace {

[[nodiscard]] QObject& app_state() {
    auto* state = cube::test::qml_engine().singletonInstance<QObject*>("Cubeulator", "AppState");
    REQUIRE(state != nullptr);
    return *state;
}

[[nodiscard]] cube::app::ColourEntry& entry() {
    return cube::test::app_controllers().colours;
}

class Screen {
public:
    Screen() {
        QQmlComponent component(&cube::test::qml_engine());
        component.loadFromModule("Cubeulator", "ColoursScreen");
        if (component.isError()) {
            FAIL(component.errorString().toStdString());
        }
        item_.reset(qobject_cast<QQuickItem*>(component.create()));
        if (item_ == nullptr) {
            FAIL(component.errorString().toStdString());
        }
        item_->setWidth(900);
        item_->setHeight(700);
    }

    [[nodiscard]] QQuickItem* item() const { return item_.get(); }
    [[nodiscard]] QObject* find(const std::string& name) const {
        const QString wanted = QString::fromStdString(name);
        if (auto* found = item_->findChild<QObject*>(wanted)) {
            return found;
        }
        return find_visual(item_.get(), wanted);
    }
    [[nodiscard]] QObject& require(const std::string& name) const {
        QObject* found = find(name);
        INFO(name);
        REQUIRE(found != nullptr);
        return *found;
    }
    [[nodiscard]] QVariant get(const char* name) const { return item_->property(name); }
    void set(const char* name, const QVariant& value) { REQUIRE(item_->setProperty(name, value)); }
    void click(const std::string& name) const {
        REQUIRE(QMetaObject::invokeMethod(&require(name), "clicked"));
    }
    // What the screen draws a colour as.
    [[nodiscard]] QColor hex(const QString& name) const {
        QVariant result;
        REQUIRE(QMetaObject::invokeMethod(item_.get(), "colourHex", Q_RETURN_ARG(QVariant, result),
                                          Q_ARG(QVariant, name)));
        return result.value<QColor>();
    }
    [[nodiscard]] QString status_headline() const {
        return require("colours.status.headline").property("text").toString();
    }
    void paint(int index) {
        REQUIRE(QMetaObject::invokeMethod(item_.get(), "paint", Q_ARG(QVariant, index)));
    }

private:
    static QQuickItem* find_visual(QQuickItem* item, const QString& name) {
        if (item->objectName() == name) {
            return item;
        }
        for (QQuickItem* child : item->childItems()) {
            if (QQuickItem* found = find_visual(child, name)) {
                return found;
            }
        }
        return nullptr;
    }

    std::unique_ptr<QQuickItem> item_;
};

struct HomeAgain {
    HomeAgain() = default;
    HomeAgain(const HomeAgain&) = delete;
    HomeAgain& operator=(const HomeAgain&) = delete;
    HomeAgain(HomeAgain&&) = delete;
    HomeAgain& operator=(HomeAgain&&) = delete;
    ~HomeAgain() {
        app_state().setProperty("screen", QStringLiteral("launch"));
        app_state().setProperty("solveCamera", true);
        app_state().setProperty("entrySize", 3);
    }
};

}  // namespace

TEST_CASE("the screen names the sides round the grid, whichever side is painted",
          "[qml][colours]") {
    const HomeAgain home;
    REQUIRE(QMetaObject::invokeMethod(&app_state(), "openColours", Q_ARG(QVariant, 5)));
    CHECK(app_state().property("screen").toString() == "colours");
    CHECK(entry().size() == 5);

    Screen screen;
    CHECK(screen.get("size").toInt() == 5);
    // What lies against each edge of each side as seen from outside: the top, right,
    // bottom and left of the grid.
    struct Edges {
        const char* side;
        const char* top;
        const char* right;
        const char* bottom;
        const char* left;
    };
    for (const Edges& edges : {
             Edges{.side = "U", .top = "B", .right = "R", .bottom = "F", .left = "L"},
             Edges{.side = "D", .top = "F", .right = "R", .bottom = "B", .left = "L"},
             Edges{.side = "F", .top = "U", .right = "R", .bottom = "D", .left = "L"},
             Edges{.side = "B", .top = "U", .right = "L", .bottom = "D", .left = "R"},
             Edges{.side = "L", .top = "U", .right = "F", .bottom = "D", .left = "B"},
             Edges{.side = "R", .top = "U", .right = "B", .bottom = "D", .left = "F"},
         }) {
        INFO("side " << edges.side);
        screen.click(std::string("colours.side.") + edges.side);
        CHECK(screen.get("side").toString() == QString::fromLatin1(edges.side));
        // Under test a string is its id; the id names the side.
        const auto word = [](const std::string& side) {
            switch (side.front()) {
                case 'U':
                    return QString("side.top");
                case 'D':
                    return QString("side.bottom");
                case 'F':
                    return QString("side.front");
                case 'B':
                    return QString("side.back");
                case 'L':
                    return QString("side.left");
                default:
                    return QString("side.right");
            }
        };
        CHECK(screen.require("colours.edge.top").property("text").toString() == word(edges.top));
        CHECK(screen.require("colours.edge.right").property("text").toString() ==
              word(edges.right));
        CHECK(screen.require("colours.edge.bottom").property("text").toString() ==
              word(edges.bottom));
        CHECK(screen.require("colours.edge.left").property("text").toString() == word(edges.left));
    }
}

TEST_CASE("what is painted arrives in the controller, and the cube drawn beside it shows it",
          "[qml][colours]") {
    const HomeAgain home;
    REQUIRE(QMetaObject::invokeMethod(&app_state(), "openColours", Q_ARG(QVariant, 4)));
    Screen screen;
    CHECK(entry().size() == 4);
    CHECK(screen.status_headline() == "review.status.emptyN");

    // Colour 2 (green) on the front's top-left, and the cube beside shows it.
    screen.click("colours.side.F");
    // (A swatch is a press on a rectangle; the system walk presses them with the mouse.)
    screen.set("brush", 2);
    screen.paint(0);
    const QVariantList front = entry().sides().value("F").toList();
    CHECK(front.at(0).toString() == "Green");
    CHECK(front.at(1).toString().isEmpty());
    CHECK(screen.status_headline() == "review.status.partial");
    CHECK(screen.require("colours.sticker.0").property("color").value<QColor>() ==
          screen.hex("Green"));

    // The seventh swatch takes it away again.
    CHECK(screen.find("colours.colour.-1") != nullptr);
    screen.set("brush", -1);
    screen.paint(0);
    CHECK(entry().sides().value("F").toList().at(0).toString().isEmpty());

    // Fill this side, and clear all.
    screen.set("brush", 1);
    screen.click("colours.fillSide");
    CHECK(entry().paintedOn("F") == 16);
    screen.click("colours.clearAll");
    CHECK_FALSE(entry().touched());
}

TEST_CASE("the cube is typed in, said to be a cube, and solved", "[qml][colours]") {
    const HomeAgain home;
    const NCube cube = cube::nxn::random_scramble({.size = 3, .seed = 4, .moves = 40});
    REQUIRE(QMetaObject::invokeMethod(&app_state(), "openColours", Q_ARG(QVariant, 3)));
    Screen screen;

    // Every sticker of every side, painted as the cube has it.
    const auto paint_side = [&](const char* letter, cube::nxn::Face face) {
        screen.click(std::string("colours.side.") + letter);
        const std::vector<Color> stickers = cube::test::outside_view_of(cube, face);
        for (std::size_t i = 0; i < stickers.size(); ++i) {
            entry().paint(QString::fromLatin1(letter), static_cast<int>(i),
                          static_cast<int>(stickers.at(i)));
        }
    };
    // Not a cube until every side is painted, and Solve is off.
    QObject& solve = screen.require("colours.solve");
    paint_side("U", cube::nxn::Face::U);
    CHECK_FALSE(solve.property("enabled").toBool());
    paint_side("D", cube::nxn::Face::D);
    paint_side("L", cube::nxn::Face::L);
    paint_side("R", cube::nxn::Face::R);
    paint_side("F", cube::nxn::Face::F);
    CHECK_FALSE(solve.property("enabled").toBool());
    paint_side("B", cube::nxn::Face::B);
    CHECK(screen.status_headline() == "review.status.valid");
    CHECK(solve.property("enabled").toBool());
    // The side picker says which are full.
    CHECK(screen.require("colours.side.U").property("trailingIcon").toString() == "check");

    screen.click("colours.solve");
    CHECK(app_state().property("screen").toString() == "solve");
    CHECK_FALSE(app_state().property("solveCamera").toBool());
    SolveController& solver = cube::test::app_controllers().solve;
    REQUIRE(wait_for([&]() { return !solver.isSolving(); }, 60'000));
    CHECK(solver.size() == 3);
}

TEST_CASE("a cube that is not one says what is wrong and outlines a sticker of it",
          "[qml][colours]") {
    const HomeAgain home;
    REQUIRE(QMetaObject::invokeMethod(&app_state(), "openColours", Q_ARG(QVariant, 3)));
    Screen screen;
    // A solved cube, painted whole.
    const NCube solved = *NCube::solved(3);
    for (const cube::nxn::Face face : cube::solver::kAllFaces) {
        const QString letter = QString::fromLatin1(
            std::array{"U", "D", "L", "R", "F", "B"}.at(static_cast<std::size_t>(face)));
        const std::vector<Color> stickers = cube::test::outside_view_of(solved, face);
        for (std::size_t i = 0; i < stickers.size(); ++i) {
            entry().paint(letter, static_cast<int>(i), static_cast<int>(stickers.at(i)));
        }
    }
    REQUIRE(screen.status_headline() == "review.status.valid");

    // One too many of a colour: said, with how many.
    const QString top_left = entry().sides().value("U").toList().front().toString();
    const int other = top_left == "White" ? 1 : 0;
    entry().paint("U", 0, other);
    CHECK(screen.status_headline() == "review.status.miscount");
    CHECK_FALSE(screen.require("colours.status.detail").property("text").toString().isEmpty());
    CHECK_FALSE(screen.require("colours.solve").property("enabled").toBool());
    // Put back.
    entry().paint("U", 0, static_cast<int>(solved.at(cube::nxn::Face::U, 0, 0)));
    REQUIRE(screen.status_headline() == "review.status.valid");

    // Two stickers swapped across pieces: every colour right, not a cube. The sticker
    // to look at is outlined on the side it is on.
    const int up = static_cast<int>(solved.at(cube::nxn::Face::U, 0, 0));
    const int down = static_cast<int>(solved.at(cube::nxn::Face::D, 0, 0));
    entry().paint("U", 0, down);
    entry().paint("D", 0, up);
    CHECK(screen.status_headline() == "review.status.impossible");
    CHECK_FALSE(screen.require("colours.status.detail").property("text").toString().isEmpty());
    CHECK_FALSE(screen.require("colours.solve").property("enabled").toBool());
}

TEST_CASE("leaving with colours entered asks first, and with none does not", "[qml][colours]") {
    const HomeAgain home;
    REQUIRE(QMetaObject::invokeMethod(&app_state(), "openColours", Q_ARG(QVariant, 2)));
    Screen screen;

    // Nothing painted: back goes at once, to the screen for another size.
    screen.click("colours.back");
    CHECK(app_state().property("screen").toString() == "entry");

    REQUIRE(QMetaObject::invokeMethod(&app_state(), "openColours", Q_ARG(QVariant, 2)));
    entry().paint("U", 0, 3);
    REQUIRE(entry().touched());
    screen.click("colours.back");
    CHECK(app_state().property("screen").toString() == "colours");
    CHECK(screen.get("confirmingLeave").toBool());
    // Keep editing: nothing lost.
    REQUIRE(QMetaObject::invokeMethod(&screen.require("colours.discard"), "kept"));
    CHECK_FALSE(screen.get("confirmingLeave").toBool());
    CHECK(entry().touched());
    // Discard: gone.
    screen.click("colours.back");
    REQUIRE(QMetaObject::invokeMethod(&screen.require("colours.discard"), "confirmed"));
    CHECK(app_state().property("screen").toString() == "entry");
}

// Pictures of the screen, for looking at rather than for checking. Hidden
// ("[.gallery]") and inert unless CUBE_GALLERY_DIR names a directory.
// CUBE_GALLERY_SHOT is "<size>-<where>": "empty", "partial" (the top and front
// painted), "full" (a cube, painted whole) or "bad" (one sticker swapped across
// pieces, so that it is not a cube); the window is CUBE_GALLERY_WIDTH x HEIGHT.
TEST_CASE("colours screen gallery", "[.gallery]") {
    const QString out = qEnvironmentVariable("CUBE_GALLERY_DIR");
    if (out.isEmpty()) {
        SKIP("set CUBE_GALLERY_DIR to a directory to write the pictures to");
    }
    REQUIRE(QDir().mkpath(out));
    const QString wanted = qEnvironmentVariable("CUBE_GALLERY_SHOT", "5-full");
    const QStringList parts = wanted.split(QLatin1Char('-'));
    REQUIRE(parts.size() == 2);
    const int size = parts.at(0).toInt();
    const int width = qEnvironmentVariable("CUBE_GALLERY_WIDTH", "900").toInt();
    const int height = qEnvironmentVariable("CUBE_GALLERY_HEIGHT", "700").toInt();

    REQUIRE(QMetaObject::invokeMethod(&app_state(), "openColours", Q_ARG(QVariant, size)));
    QQuickWindow window;
    window.setFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowDoesNotAcceptFocus);
    window.setPosition(-4000, -4000);
    QQmlComponent component(&cube::test::qml_engine());
    component.loadFromModule("Cubeulator", "ColoursScreen");
    std::unique_ptr<QQuickItem> screen(
        qobject_cast<QQuickItem*>(component.beginCreate(cube::test::qml_engine().rootContext())));
    if (screen == nullptr) {
        FAIL(component.errorString().toStdString());
    }
    screen->setParentItem(window.contentItem());
    component.completeCreate();
    screen->setWidth(width);
    screen->setHeight(height);
    window.resize(width, height);

    const NCube cube =
        cube::nxn::random_scramble({.size = size, .seed = 4, .moves = 40 + (8 * size)});
    const std::array<const char*, 6> letters = {"U", "D", "L", "R", "F", "B"};
    for (const cube::nxn::Face face : cube::solver::kAllFaces) {
        const QString letter = QString::fromLatin1(letters.at(static_cast<std::size_t>(face)));
        if (parts.at(1) == QLatin1String("empty") ||
            (parts.at(1) == QLatin1String("partial") && letter != "U" && letter != "F")) {
            continue;
        }
        const std::vector<Color> stickers = cube::test::outside_view_of(cube, face);
        for (std::size_t i = 0; i < stickers.size(); ++i) {
            entry().paint(letter, static_cast<int>(i), static_cast<int>(stickers.at(i)));
        }
    }
    if (parts.at(1) == QLatin1String("bad")) {
        const QString top = entry().sides().value("U").toList().front().toString();
        const QString bottom = entry().sides().value("D").toList().front().toString();
        const auto number = [](const QString& name) {
            for (int colour = 0; colour < 6; ++colour) {
                if (colour_name(static_cast<Color>(colour)) == name) {
                    return colour;
                }
            }
            return 0;
        };
        entry().paint("U", 0, number(bottom));
        entry().paint("D", 0, number(top));
    }
    window.show();
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < 1500) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
    }
    const QImage image = window.grabWindow();
    REQUIRE_FALSE(image.isNull());
    REQUIRE(image.save(
        QDir(out).filePath("colours-" + wanted + "-" + QString::number(width) + ".png")));
    app_state().setProperty("screen", QStringLiteral("launch"));
}
