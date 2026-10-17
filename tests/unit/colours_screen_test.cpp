// The screen where a cube is typed in a colour at a time (ColoursScreen.qml) behind
// the app's own controllers: the sides named round the grid, what the person paints
// arriving in the controller, the status that follows, the cube drawn beside it, and
// what happens on the way out.
#include <catch2/catch_test_macros.hpp>

#include "colour_name.hpp"
#include "controllers/scan_reading.hpp"
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
#include <cstdint>
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
        app_state().setProperty("coloursFromScan", false);
        entry().begin(3);
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

namespace {

// A cube the camera read: `cube`'s stickers, and the top-left sticker of the front
// (as seen from outside) one it was unsure of.
cube::app::Reading read_cube(const NCube& cube, bool doubting) {
    cube::app::Reading reading;
    reading.size = cube.size();
    for (const Color colour : cube.facelets()) {
        reading.stickers.push_back(static_cast<std::int8_t>(colour));
    }
    reading.doubtful.assign(reading.stickers.size(), 0);
    if (doubting) {
        const auto n = static_cast<std::size_t>(cube.size());
        reading.doubtful.at(static_cast<std::size_t>(cube::nxn::Face::F) * n * n) = 1;
    }
    return reading;
}

}  // namespace

TEST_CASE(
    "a scan being looked over is titled as a check, names its sides by their middles and marks a "
    "doubt",
    "[qml][colours][review]") {
    const HomeAgain home;
    const NCube cube = cube::nxn::random_scramble({.size = 3, .seed = 6, .moves = 40});
    entry().loadReading(read_cube(cube, true));
    app_state().setProperty("screen", QStringLiteral("colours"));

    Screen screen;
    CHECK(screen.get("fromScan").toBool());
    CHECK(screen.require("colours.title").property("text").toString() == "review.title.check");
    CHECK(screen.require("colours.intro").property("text").toString() == "review.intro.check");
    // Upright: the camera could say which way up each side was.
    CHECK_FALSE(screen.require("colours.notUpright").property("visible").toBool());
    // One sticker was doubted, and the note says so.
    CHECK(screen.require("colours.doubtNote").property("visible").toBool());

    // The sides are called by their middles, not by where they are.
    screen.click("colours.side.F");
    const QString centre = entry().centres().value("U").toString();
    CHECK(screen.require("colours.edge.top").property("text").toString() ==
          "colourSide." + centre.toLower());

    // The "?" is on the doubted sticker of the front and nowhere else on the side.
    CHECK(screen.require("colours.doubt.0").property("visible").toBool());
    for (int i = 1; i < 9; ++i) {
        CHECK_FALSE(
            screen.require("colours.doubt." + std::to_string(i)).property("visible").toBool());
    }
    // And on no other side.
    screen.click("colours.side.U");
    CHECK_FALSE(screen.require("colours.doubt.0").property("visible").toBool());
    // The sticker's name, for a screen reader, says the camera was unsure.
    screen.click("colours.side.F");
    CHECK(screen.require("colours.sticker.0").property("spoken").toString() ==
          "review.a11y.stickerUnsure");
    CHECK(screen.require("colours.sticker.1").property("spoken").toString() ==
          "review.a11y.sticker");

    // Painting the sticker settles the doubt.
    screen.set("brush", 3);
    screen.paint(0);
    CHECK_FALSE(screen.require("colours.doubt.0").property("visible").toBool());
    CHECK_FALSE(screen.require("colours.doubtNote").property("visible").toBool());
}

TEST_CASE("sides the camera could not turn to fit say so, and a side can be turned in place",
          "[qml][colours][review]") {
    const HomeAgain home;
    cube::app::Reading reading =
        read_cube(cube::nxn::random_scramble({.size = 3, .seed = 8, .moves = 40}), false);
    reading.upright = false;
    entry().loadReading(reading);

    Screen screen;
    CHECK(screen.require("colours.notUpright").property("visible").toBool());
    screen.click("colours.side.R");
    const QString before = entry().sideText("R");
    screen.click("colours.turnSide");
    CHECK(entry().sideText("R") != before);
    CHECK(entry().touched());
    for (int i = 0; i < 3; ++i) {
        screen.click("colours.turnSide");
    }
    CHECK(entry().sideText("R") == before);
}

TEST_CASE("every correction can be taken back, or all of them at once", "[qml][colours][review]") {
    const HomeAgain home;
    entry().loadReading(
        read_cube(cube::nxn::random_scramble({.size = 3, .seed = 3, .moves = 40}), false));

    Screen screen;
    CHECK_FALSE(screen.require("colours.undo").property("enabled").toBool());
    CHECK_FALSE(screen.require("colours.redo").property("enabled").toBool());
    CHECK_FALSE(screen.require("colours.revert").property("enabled").toBool());
    CHECK(screen.require("colours.revert").property("visible").toBool());

    const QVariantList was = entry().sides().value("U").toList();
    screen.click("colours.side.U");
    screen.set("brush", was.at(0).toString() == "Red" ? 3 : 5);
    screen.paint(0);
    CHECK(entry().sides().value("U").toList().at(0) != was.at(0));
    CHECK(screen.require("colours.undo").property("enabled").toBool());
    CHECK(screen.require("colours.revert").property("enabled").toBool());
    CHECK_FALSE(screen.status_headline() == "review.status.valid");
    // Solve is off for a cube that cannot be one: there is no way round it.
    CHECK_FALSE(screen.require("colours.solve").property("enabled").toBool());

    screen.click("colours.undo");
    CHECK(entry().sides().value("U").toList().at(0) == was.at(0));
    CHECK(screen.require("colours.redo").property("enabled").toBool());
    CHECK(screen.status_headline() == "review.status.valid");
    CHECK(screen.require("colours.solve").property("enabled").toBool());
    screen.click("colours.redo");
    CHECK(entry().sides().value("U").toList().at(0) != was.at(0));

    screen.click("colours.revert");
    CHECK(entry().sides().value("U").toList().at(0) == was.at(0));
    CHECK_FALSE(entry().touched());
}

TEST_CASE("a side is described in letters, and a description that is wrong changes nothing",
          "[qml][colours][review]") {
    const HomeAgain home;
    REQUIRE(QMetaObject::invokeMethod(&app_state(), "openColours", Q_ARG(QVariant, 3)));
    Screen screen;

    CHECK_FALSE(screen.require("colours.describeBox").property("visible").toBool());
    screen.click("colours.side.F");
    screen.click("colours.describe");
    CHECK(screen.require("colours.describeBox").property("visible").toBool());
    QObject& field = screen.require("colours.describeText");
    // It opens with the side as it stands: nothing painted yet.
    CHECK(field.property("text").toString() == "\?\?\?/\?\?\?/\?\?\?");

    // Too few, and a letter that is no colour: said, and nothing is painted.
    field.setProperty("text", "GGG/GBG");
    screen.click("colours.describeApply");
    CHECK(screen.require("colours.describeError").property("visible").toBool());
    CHECK(screen.require("colours.describeError").property("text").toString() ==
          "review.describe.tooFew");
    CHECK(entry().paintedOn("F") == 0);
    field.setProperty("text", "GGG/GXG/GGG");
    screen.click("colours.describeApply");
    CHECK(screen.require("colours.describeError").property("text").toString() ==
          "review.describe.unknownLetter");
    CHECK(entry().paintedOn("F") == 0);
    // Typing again takes the complaint away.
    field.setProperty("text", "GGG/GBG/GGG");
    CHECK_FALSE(screen.require("colours.describeError").property("visible").toBool());

    screen.click("colours.describeApply");
    CHECK_FALSE(screen.require("colours.describeBox").property("visible").toBool());
    CHECK(entry().sideText("F") == "GGG/GBG/GGG");
    CHECK(entry().sides().value("F").toList().at(4).toString() == "Blue");
    // One step to undo.
    screen.click("colours.undo");
    CHECK(entry().paintedOn("F") == 0);

    // Changing side while the box is open describes that side instead.
    screen.click("colours.describe");
    screen.click("colours.side.R");
    CHECK(field.property("text").toString() == "\?\?\?/\?\?\?/\?\?\?");
    screen.click("colours.describeCancel");
    CHECK_FALSE(screen.require("colours.describeBox").property("visible").toBool());
}

namespace {

// What a window does when it polishes: positioners lay out their children, outermost first.
void lay_out(QQuickItem* item) {
    if (item->inherits("QQuickBasePositioner")) {
        QMetaObject::invokeMethod(item, "forceLayout");
    }
    for (QQuickItem* child : item->childItems()) {
        lay_out(child);
    }
}

}  // namespace

TEST_CASE("the box for describing a side scrolls into view when it is opened in a short window",
          "[qml][colours][review]") {
    const HomeAgain home;
    REQUIRE(QMetaObject::invokeMethod(&app_state(), "openColours", Q_ARG(QVariant, 3)));
    Screen screen;
    screen.item()->setHeight(520);
    QQuickItem* flick = nullptr;
    for (QQuickItem* child : screen.item()->childItems()) {
        if (child->inherits("QQuickFlickable")) {
            flick = child;
        }
    }
    REQUIRE(flick != nullptr);
    for (int pass = 0; pass < 3; ++pass) {
        lay_out(screen.item());
    }
    CHECK(flick->property("contentY").toDouble() == 0.0);

    screen.click("colours.describe");
    for (int pass = 0; pass < 3; ++pass) {
        lay_out(screen.item());  // what the window would do before the next frame
    }
    // The box is below the fold of a window this short, and the screen scrolls to it.
    REQUIRE(wait_for([&]() { return flick->property("contentY").toDouble() > 0.0; }, 3000));
    auto* box = static_cast<QQuickItem*>(&screen.require("colours.describeBox"));
    const QPointF bottom = box->mapToItem(screen.item(), QPointF(0, box->height()));
    CHECK(bottom.y() <= screen.item()->height() + 0.5);
    CHECK(bottom.y() > 0.0);
}

TEST_CASE("a side of a bigger cube is described with as many letters as it has stickers",
          "[qml][colours][review]") {
    const HomeAgain home;
    REQUIRE(QMetaObject::invokeMethod(&app_state(), "openColours", Q_ARG(QVariant, 5)));
    Screen screen;
    screen.click("colours.side.U");
    screen.click("colours.describe");
    QObject& field = screen.require("colours.describeText");
    field.setProperty("text", QString(24, QLatin1Char('y')));
    screen.click("colours.describeApply");
    CHECK(screen.require("colours.describeError").property("text").toString() ==
          "review.describe.tooFew");
    field.setProperty("text", QString(25, QLatin1Char('y')));
    screen.click("colours.describeApply");
    CHECK(entry().paintedOn("U") == 25);
}

TEST_CASE("looking over a scan with nothing read goes nowhere", "[qml][colours][review]") {
    const HomeAgain home;
    app_state().setProperty("screen", QStringLiteral("scan"));
    QVariant result;
    REQUIRE(QMetaObject::invokeMethod(&app_state(), "reviewScan", Q_RETURN_ARG(QVariant, result)));
    CHECK_FALSE(result.toBool());
    CHECK(app_state().property("screen").toString() == "scan");
    CHECK_FALSE(app_state().property("coloursFromScan").toBool());
}

// Pictures of the screen that puts a misread scan right, for looking at rather than
// for checking.
//
// Hidden ("[.gallery]") and inert unless CUBE_GALLERY_DIR names a directory: it puts
// the real ColoursScreen in a real window, parked off-screen, loads the cube the
// demo misreads (CUBE_REVIEW_DEMO) and saves what the window renders. One picture a
// run (a second grab of a window with the 3D cube in it brings the process down), and
// CUBE_GALLERY_SHOT picks which:
//
//   set CUBE_GALLERY_DIR=C:\some\dir
//   set CUBE_GALLERY_SHOT=1-misread
//   cube_qml_tests "[.gallery][review]"
//
// 1-misread (wide), 2-misread-narrow (a phone), 3-turned (the camera could not tell
// which way up the sides were), 4-describing (the box open, with a refused
// description), 5-5x5 (a bigger cube).
TEST_CASE("review screen gallery", "[.gallery][review]") {
    const QString out = qEnvironmentVariable("CUBE_GALLERY_DIR");
    if (out.isEmpty()) {
        SKIP("set CUBE_GALLERY_DIR to a directory to write the pictures to");
    }
    REQUIRE(QDir().mkpath(out));
    const QString wanted = qEnvironmentVariable("CUBE_GALLERY_SHOT", "1-misread");

    struct Shot {
        const char* name;
        int width;
        int height;
        int size;
        bool upright;
        bool describing;
        const char* side;
    };
    const Shot shots[] = {
        {"1-misread", 900, 700, 3, true, false, "F"},
        {"2-misread-narrow", 390, 780, 3, true, false, "F"},
        {"3-turned", 900, 700, 3, false, false, "F"},
        {"4-describing", 900, 700, 3, true, true, "F"},
        {"5-5x5", 900, 700, 5, true, false, "F"},
    };
    const Shot* shot = nullptr;
    for (const Shot& candidate : shots) {
        if (wanted == QLatin1String(candidate.name)) {
            shot = &candidate;
        }
    }
    REQUIRE(shot != nullptr);

    entry().loadReading(cube::app::demo_reading(7U, shot->size, shot->upright));
    app_state().setProperty("coloursFromScan", true);
    app_state().setProperty("screen", QStringLiteral("colours"));

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

    window.resize(shot->width, shot->height);
    screen->setWidth(shot->width);
    screen->setHeight(shot->height);
    screen->setProperty("side", QString::fromLatin1(shot->side));
    if (shot->describing) {
        screen->setProperty("describing", true);
        QObject* field = screen->findChild<QObject*>(QStringLiteral("colours.describeText"));
        REQUIRE(field != nullptr);
        field->setProperty("text", QStringLiteral("GGG/GXG/GGG"));
        QObject* box = screen->findChild<QObject*>(QStringLiteral("colours.describeBox"));
        REQUIRE(box != nullptr);
        REQUIRE(QMetaObject::invokeMethod(box, "apply"));
    }
    if (shot->describing) {
        // Opened, with a description it refuses, before the window is up: the screen
        // lays itself out as it is shown, and scrolls to the box once it has.
        screen->setProperty("describing", true);
        QObject* field = screen->findChild<QObject*>(QStringLiteral("colours.describeText"));
        REQUIRE(field != nullptr);
        field->setProperty("text", QStringLiteral("GGG/GXG/GGG"));
        QObject* box = screen->findChild<QObject*>(QStringLiteral("colours.describeBox"));
        REQUIRE(box != nullptr);
        REQUIRE(QMetaObject::invokeMethod(box, "apply"));
    }
    window.show();
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < 1700) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
    }
    const QImage image = window.grabWindow();
    REQUIRE_FALSE(image.isNull());
    REQUIRE(image.save(QDir(out).filePath(wanted + ".png")));

    app_state().setProperty("coloursFromScan", false);
    app_state().setProperty("screen", QStringLiteral("launch"));
}
