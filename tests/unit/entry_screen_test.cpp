// The screen for a cube the camera does not read (EntryScreen.qml), and the way
// to it from Home: the size chosen, a scramble typed, what is wrong with it said
// and pointed at, and the solve begun with no camera.
#include <catch2/catch_test_macros.hpp>

#include "cube_view_reading.hpp"
#include "qml_test_app.hpp"

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
#include <QVariantMap>

#include <memory>
#include <string>

using cube::test::wait_for;

namespace {

[[nodiscard]] QObject& app_state() {
    auto* state = cube::test::qml_engine().singletonInstance<QObject*>("Cubeulator", "AppState");
    REQUIRE(state != nullptr);
    return *state;
}

// A screen of the app's own, loaded for real behind the app's controllers.
class Loaded {
public:
    explicit Loaded(const char* type) {
        QQmlComponent component(&cube::test::qml_engine());
        component.loadFromModule("Cubeulator", type);
        if (component.isError()) {
            FAIL(component.errorString().toStdString());
        }
        item_.reset(qobject_cast<QQuickItem*>(component.create()));
        if (item_ == nullptr) {
            FAIL(component.errorString().toStdString());
        }
        item_->setWidth(900);
        item_->setHeight(640);
    }

    // By object name, in the objects the screen made and in the items a Repeater made,
    // which are visual children and not QObject children.
    [[nodiscard]] QObject* find(const char* name) const {
        if (auto* found = item_->findChild<QObject*>(QString::fromLatin1(name))) {
            return found;
        }
        return find_visual(item_.get(), QString::fromLatin1(name));
    }
    [[nodiscard]] QObject& require(const char* name) const {
        QObject* found = find(name);
        INFO(name);
        REQUIRE(found != nullptr);
        return *found;
    }
    [[nodiscard]] QVariant get(const char* name) const { return item_->property(name); }
    void click(const std::string& name) const {
        REQUIRE(QMetaObject::invokeMethod(&require(name.c_str()), "clicked"));
    }
    void type(const QString& text) const {
        REQUIRE(require("entry.scramble").setProperty("text", text));
    }
    [[nodiscard]] bool visible(const char* name) const {
        return require(name).property("visible").toBool();
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

// Returns the app to Home, as it found it, when a test is done with the screens.
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

TEST_CASE("Home has a way to another size of cube, and back", "[qml][entry]") {
    const HomeAgain home;
    const Loaded launch("LaunchScreen");
    REQUIRE(app_state().setProperty("screen", QStringLiteral("launch")));
    launch.click("launch.other");
    CHECK(app_state().property("screen").toString() == "entry");
    REQUIRE(QMetaObject::invokeMethod(&app_state(), "closeEntry"));
    CHECK(app_state().property("screen").toString() == "launch");
}

TEST_CASE("the entry screen offers 2 to 5, and the camera at each", "[qml][entry]") {
    const HomeAgain home;
    const Loaded screen("EntryScreen");
    for (const int size : {2, 3, 4, 5}) {
        INFO("size " << size);
        screen.click("entry.size." + std::to_string(size));
        CHECK(screen.get("size").toInt() == size);
        // Every size offered can be scanned; under test a string is its id.
        CHECK(screen.visible("entry.scan"));
        CHECK(screen.require("entry.size.note").property("text").toString() ==
              "entry.size.camera");
    }
    CHECK(screen.find("entry.size.6") == nullptr);
    CHECK(screen.find("entry.size.1") == nullptr);
}

TEST_CASE("a scramble typed for a bigger cube is solved without the camera", "[qml][entry]") {
    const HomeAgain home;
    SolveController& solver = cube::test::app_controllers().solve;
    const Loaded screen("EntryScreen");
    REQUIRE(app_state().setProperty("screen", QStringLiteral("entry")));
    REQUIRE(app_state().setProperty("solveCamera", true));

    screen.click("entry.size.4");
    screen.type(QStringLiteral("R U 2R' Fw 3Fw' Uw2 L' D2"));
    screen.click("entry.solve");

    CHECK(app_state().property("screen").toString() == "solve");
    CHECK_FALSE(app_state().property("solveCamera").toBool());
    REQUIRE(wait_for([&]() { return !solver.isSolving(); }, 60'000));
    CHECK(solver.size() == 4);
    CHECK(solver.totalMoves() > 0);
}

TEST_CASE("a scramble that is wrong says so, points at the move, and starts nothing",
          "[qml][entry]") {
    const HomeAgain home;
    const Loaded screen("EntryScreen");
    REQUIRE(app_state().setProperty("screen", QStringLiteral("entry")));

    struct Case {
        int size;
        const char* text;
        const char* error;
        const char* selected;
        const char* words;
    };
    for (const Case& wrong : {
             Case{.size = 3,
                  .text = "R U Q F",
                  .error = "unknownMove",
                  .selected = "Q",
                  .words = "entry.error.unknownMove"},
             Case{.size = 4,
                  .text = "R 5L U",
                  .error = "layerOutOfRange",
                  .selected = "5L",
                  .words = "entry.error.layer"},
             Case{.size = 5,
                  .text = "",
                  .error = "empty",
                  .selected = "",
                  .words = "entry.error.empty"},
             Case{.size = 3,
                  .text = "R R'",
                  .error = "alreadySolved",
                  .selected = "",
                  .words = "entry.error.solved"},
         }) {
        INFO("size " << wrong.size << ": '" << wrong.text << "'");
        screen.click("entry.size." + std::to_string(wrong.size));
        screen.type(QString::fromLatin1(wrong.text));
        CHECK_FALSE(screen.visible("entry.error"));
        screen.click("entry.solve");
        CHECK(app_state().property("screen").toString() == "entry");
        REQUIRE(screen.visible("entry.error"));
        CHECK(screen.require("entry.error").property("text").toString() ==
              QString::fromLatin1(wrong.words));
        CHECK(screen.get("problem").toMap().value("error").toString() ==
              QString::fromLatin1(wrong.error));
        CHECK(screen.require("entry.scramble").property("selectedText").toString() ==
              QString::fromLatin1(wrong.selected));
        // Typing again clears it.
        screen.type(QString::fromLatin1(wrong.text) + " ");
        CHECK_FALSE(screen.visible("entry.error"));
    }
}

// Pictures of the entry screen and Home, for looking at rather than for checking.
// Hidden ("[.gallery]") and inert unless CUBE_GALLERY_DIR names a directory.
// CUBE_GALLERY_SHOT is "home", "entry", "entry-error" or "entry-5"; the window is
// CUBE_GALLERY_WIDTH x CUBE_GALLERY_HEIGHT (900 x 640).
TEST_CASE("entry screen gallery", "[.gallery]") {
    const QString out = qEnvironmentVariable("CUBE_GALLERY_DIR");
    if (out.isEmpty()) {
        SKIP("set CUBE_GALLERY_DIR to a directory to write the pictures to");
    }
    REQUIRE(QDir().mkpath(out));
    const QString wanted = qEnvironmentVariable("CUBE_GALLERY_SHOT", "entry");
    const int width = qEnvironmentVariable("CUBE_GALLERY_WIDTH", "900").toInt();
    const int height = qEnvironmentVariable("CUBE_GALLERY_HEIGHT", "640").toInt();

    QQuickWindow window;
    window.setFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowDoesNotAcceptFocus);
    window.setPosition(-4000, -4000);
    QQmlComponent component(&cube::test::qml_engine());
    component.loadFromModule("Cubeulator", wanted == "home" ? "LaunchScreen" : "EntryScreen");
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

    if (wanted == "entry-5") {
        REQUIRE(QMetaObject::invokeMethod(screen.get(), "solve"));  // nothing typed: a fault
    } else if (wanted == "entry-error") {
        REQUIRE(screen->findChild<QObject*>("entry.scramble")
                    ->setProperty("text", QStringLiteral("R U Q F")));
        REQUIRE(QMetaObject::invokeMethod(screen.get(), "solve"));
    }
    window.show();
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < 800) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
    }
    const QImage image = window.grabWindow();
    REQUIRE_FALSE(image.isNull());
    REQUIRE(image.save(QDir(out).filePath(wanted + "-" + QString::number(width) + ".png")));
}
