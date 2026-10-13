// The converted screens, switched live between languages.
//
// What the plan asks of E2b, on the screens it converted:
//   * the language changes under a screen that is already up, with no
//     restart, and every string follows;
//   * the pseudo-locale (longer, accented, bracketed) truncates nothing;
//   * right to left mirrors the layout - and the cube, and the direction of
//     any turn, do not move.
//
// The whole app, driven through its windows, is the system tests' job
// (tests/system, E0a); this is the part that needs no window and so runs
// anywhere the solve lane's cube tests run.
#include "cube_view_reading.hpp"
#include "screen_app.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cube/solver/cube.hpp>
#include <cube/solver/notation.hpp>
#include <cube/solver/outside_view.hpp>
#include <cube/solver/solver.hpp>

#include <QColor>
#include <QGuiApplication>
#include <QMetaObject>
#include <QQmlComponent>
#include <QDir>
#include <QImage>
#include <QQuickItem>
#include <QQuickWindow>
#include <QStringList>
#include <QSurfaceFormat>
#include <QUrl>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>

#include <functional>
#include <memory>

using cube::solver::Color;
using cube::solver::Cube;
using cube::solver::Face;
using cube::solver::Move;
using cube::test::all_moves;
using cube::test::letter;
using cube::test::wait_for;

namespace {

namespace t = cube::i18n_test;

[[nodiscard]] std::string str(const QString& s) {
    return s.toStdString();
}

// A screen of the app as an item, laid out at a window's size, inside the
// same root the app puts it in: mirrored when the language is written right
// to left (Main.qml's Loader sets exactly this).
class Screen {
public:
    Screen(const QString& type, int width, int height) {
        QQmlComponent root_component(&t::engine());
        root_component.setData(R"QML(
            import QtQuick
            import Cubeulator
            Item {
                LayoutMirroring.enabled: LanguageManager.rtl
                LayoutMirroring.childrenInherit: true
            }
        )QML",
                               QUrl(QStringLiteral("qrc:/i18n_test_root.qml")));
        REQUIRE_FALSE(root_component.isError());
        root_.reset(qobject_cast<QQuickItem*>(root_component.create()));
        REQUIRE(root_ != nullptr);
        root_->setWidth(width);
        root_->setHeight(height);

        QQmlComponent component(&t::engine());
        component.loadFromModule("Cubeulator", type);
        INFO(str(component.errorString()));
        REQUIRE_FALSE(component.isError());
        screen_.reset(qobject_cast<QQuickItem*>(component.create()));
        REQUIRE(screen_ != nullptr);
        screen_->setParentItem(root_.get());
        screen_->setWidth(width);
        screen_->setHeight(height);
        settle();
    }

    [[nodiscard]] QQuickItem* item() const { return screen_.get(); }
    [[nodiscard]] QQuickItem* root() const { return root_.get(); }

    // Puts the screen in `window`, so that it can be drawn.
    void show_in(QQuickWindow& window) {
        window.resize(static_cast<int>(root_->width()), static_cast<int>(root_->height()));
        root_->setParentItem(window.contentItem());
        settle();
    }

    // Positioners lay their children out when a window polishes them; there
    // is no window, so ask each, outermost first.
    void settle() {
        QCoreApplication::processEvents();
        for (int pass = 0; pass < 3; ++pass) {
            lay_out(screen_.get());
            QCoreApplication::processEvents();
        }
    }

    // Every visible item of `class_name`.
    [[nodiscard]] QList<QQuickItem*> find_all(const char* class_name) const {
        QList<QQuickItem*> found;
        visit(screen_.get(), [&](QQuickItem* item) {
            if (item->isVisible() && item->inherits(class_name)) {
                found.append(item);
            }
        });
        return found;
    }

    // The text of every visible Text on the screen.
    [[nodiscard]] QStringList texts() const {
        QStringList out;
        for (QQuickItem* text : find_all("QQuickText")) {
            const QString value = text->property("text").toString();
            if (!value.isEmpty()) {
                out.append(value);
            }
        }
        return out;
    }

    // The first visible item whose class name starts with `prefix` and which
    // has a `text` property equal to `text`.
    [[nodiscard]] QQuickItem* button(const QString& text) const {
        QQuickItem* found = nullptr;
        visit(screen_.get(), [&](QQuickItem* item) {
            if (found == nullptr && item->isVisible() &&
                QString::fromLatin1(item->metaObject()->className()).startsWith(QLatin1String("AppButton")) &&
                item->property("text").toString() == text) {
                found = item;
            }
        });
        return found;
    }

    // Where `item` sits in the screen's own coordinates: the left edge.
    [[nodiscard]] double left_of(const QQuickItem* item) const {
        return item->mapToItem(screen_.get(), QPointF(0, 0)).x();
    }

    // The labels that do not fit inside the button they are in.
    [[nodiscard]] QStringList labels_outside_buttons() const {
        QStringList bad;
        visit(screen_.get(), [&](QQuickItem* item) {
            if (!item->isVisible() ||
                !QString::fromLatin1(item->metaObject()->className()).startsWith(QLatin1String("AppButton"))) {
                return;
            }
            const QRectF box = item->mapRectToItem(screen_.get(), QRectF(0, 0, item->width(), item->height()));
            visit(item, [&](QQuickItem* inner) {
                if (inner == item || !inner->isVisible() || !inner->inherits("QQuickText")) {
                    return;
                }
                const QString shown = inner->property("text").toString();
                if (shown.isEmpty()) {
                    return;
                }
                const double w = inner->property("contentWidth").toDouble();
                const QPointF at = inner->mapToItem(screen_.get(), QPointF(0, 0));
                if (at.x() < box.left() - 0.5 || at.x() + w > box.right() + 0.5) {
                    bad.append(shown);
                }
            });
        });
        return bad;
    }

    // Whether LayoutMirroring is in force where `parent` is: a probe made as
    // a child sees the inherited state.
    [[nodiscard]] bool mirrored_under(QQuickItem* parent) const {
        QQmlComponent probe(&t::engine());
        probe.setData("import QtQuick\nItem { property bool mirrored: LayoutMirroring.enabled }",
                      QUrl(QStringLiteral("qrc:/i18n_test_probe.qml")));
        REQUIRE_FALSE(probe.isError());
        std::unique_ptr<QQuickItem> item(qobject_cast<QQuickItem*>(probe.create()));
        REQUIRE(item != nullptr);
        item->setParentItem(parent);
        return item->property("mirrored").toBool();
    }

private:
    static void visit(QQuickItem* item, const std::function<void(QQuickItem*)>& fn) {
        fn(item);
        for (QQuickItem* child : item->childItems()) {
            visit(child, fn);
        }
    }
    static void lay_out(QQuickItem* item) {
        if (item->inherits("QQuickBasePositioner")) {
            QMetaObject::invokeMethod(item, "forceLayout");
        }
        for (QQuickItem* child : item->childItems()) {
            lay_out(child);
        }
    }

    std::unique_ptr<QQuickItem> root_;
    std::unique_ptr<QQuickItem> screen_;
};

// English first, whatever the last test left behind.
void in_language(const char* code) {
    REQUIRE(t::language().preview(QString::fromLatin1(code)));
    QCoreApplication::processEvents();
}

}  // namespace

// The permission screen words itself by whether the camera was refused, which
// is the machine's state, not the test's: ask the screen which it is showing.
struct PermissionWords {
    QString title;
    QString act;
    explicit PermissionWords(const Screen& screen) {
        const bool denied = screen.item()->property("denied").toBool();
        title = denied ? QStringLiteral("Camera access is off") : QStringLiteral("Camera access");
        act = denied ? QStringLiteral("Try again") : QStringLiteral("Allow camera");
    }
};

TEST_CASE("a screen that is up changes language without being made again", "[i18n][screen][switch]") {
    in_language("en");
    Screen screen("PermissionScreen", 900, 640);
    const PermissionWords words(screen);
    REQUIRE(screen.texts().contains(words.title));
    REQUIRE(screen.button(words.act) != nullptr);

    // The pseudo-locale: every string is the bracketed, accented one.
    in_language("xx");
    const QStringList pseudo = screen.texts();
    REQUIRE_FALSE(pseudo.isEmpty());
    for (const QString& text : pseudo) {
        INFO("shown in the pseudo-locale: " << str(text));
        // Product name and nothing else may lack the brackets: an unbracketed
        // string is one that never went through an id lookup.
        CHECK(text.startsWith(QLatin1Char('[')));
        CHECK(text.endsWith(QLatin1Char(']')));
    }
    CHECK_FALSE(screen.texts().contains(words.title));

    // Hebrew has no translation yet: the English shows, the direction flips.
    in_language("he");
    CHECK(t::language().rtl());
    CHECK(QGuiApplication::layoutDirection() == Qt::RightToLeft);
    CHECK(screen.texts().contains(words.title));
    CHECK(screen.mirrored_under(screen.item()));

    // And back: the same screen, the same item, English and left to right.
    in_language("en");
    CHECK_FALSE(t::language().rtl());
    CHECK(QGuiApplication::layoutDirection() == Qt::LeftToRight);
    CHECK(screen.texts().contains(words.title));
    CHECK_FALSE(screen.mirrored_under(screen.item()));
}

TEST_CASE("every converted screen holds the pseudo-locale without cutting a string off",
          "[i18n][screen][pseudo]") {
    struct Size {
        int w;
        int h;
        const char* what;
    };
    const Size sizes[] = {{900, 640, "wide"}, {390, 780, "narrow"}};
    for (const char* type : {"PermissionScreen", "CompleteScreen", "LaunchScreen", "SettingsScreen"}) {
        for (const Size& size : sizes) {
            in_language("xx");
            Screen screen(type, size.w, size.h);
            INFO(type << ", " << size.what);
            int checked = 0;
            for (QQuickItem* text : screen.find_all("QQuickText")) {
                const QString shown = text->property("text").toString();
                if (shown.isEmpty()) {
                    continue;
                }
                INFO("text: " << str(shown));
                ++checked;
                // A string that elides, or is cut at a line count, says so.
                CHECK_FALSE(text->property("truncated").toBool());

                const double content_w = text->property("contentWidth").toDouble();
                const double content_h = text->property("contentHeight").toDouble();
                const bool wraps = text->property("wrapMode").toInt() != 0;
                // The box the string was given holds it: across for a line
                // that does not wrap, down for one that does.
                if (!wraps) {
                    CHECK(content_w <= text->width() + 0.5);
                } else {
                    CHECK(content_h <= text->height() + 0.5);
                }
                // And it stays inside the window: nothing runs off an edge.
                const QPointF at = text->mapToItem(screen.item(), QPointF(0, 0));
                CHECK(at.x() >= -0.5);
                CHECK(at.x() + content_w <= screen.item()->width() + 0.5);
                // And inside the pane it was put in: a string longer than its
                // column does not cross into the next one.
                if (const QQuickItem* parent = text->parentItem();
                    parent != nullptr && parent != screen.item() && parent->width() > 0) {
                    CHECK(text->x() + content_w <= parent->width() + 0.5);
                }
            }
            // A screen that read no text proves nothing.
            CHECK(checked >= 3);

            // A button's label lies inside the button: one that is longer
            // than a fixed-width button runs out of it.
            for (const QString& overflow : screen.labels_outside_buttons()) {
                FAIL_CHECK("label outside its button: " << str(overflow));
            }
        }
    }
}

TEST_CASE("right to left mirrors the layout, and left to right puts it back", "[i18n][screen][rtl]") {
    // The permission screen's wide layout: the copy and the buttons in the
    // leading half, the reassurances in the trailing half.
    in_language("en");
    Screen screen("PermissionScreen", 900, 640);
    const PermissionWords words(screen);
    QQuickItem* act = screen.button(words.act);
    REQUIRE(act != nullptr);
    CHECK(screen.left_of(act) < screen.item()->width() / 2);

    for (const char* code : {"he", "ar", "yi"}) {
        INFO(code);
        in_language(code);
        screen.settle();
        act = screen.button(words.act);
        REQUIRE(act != nullptr);
        CHECK(screen.left_of(act) > screen.item()->width() / 2);
    }

    in_language("de");
    screen.settle();
    act = screen.button(words.act);
    REQUIRE(act != nullptr);
    CHECK(screen.left_of(act) < screen.item()->width() / 2);
}

TEST_CASE("the cube view is not mirrored, however the screen around it is", "[i18n][screen][rtl][cube]") {
    in_language("he");
    Screen screen("CompleteScreen", 900, 640);
    // The screen is mirrored...
    CHECK(screen.mirrored_under(screen.item()));
    // ...and the solved cube it draws is not.
    QList<QQuickItem*> cubes;
    for (QQuickItem* item : screen.find_all("QQuickItem")) {
        const QString cls = QString::fromLatin1(item->metaObject()->className());
        // The screen's own inline component wraps CubeView; QML names the
        // class after the type.
        if (cls.startsWith(QLatin1String("CubeView")) || cls.startsWith(QLatin1String("BobbingCube"))) {
            cubes.append(item);
        }
    }
    REQUIRE_FALSE(cubes.isEmpty());
    for (QQuickItem* cube : cubes) {
        CHECK_FALSE(screen.mirrored_under(cube));
    }
    in_language("en");
}

// The 3D cube is proved by reading its stickers back out of the scene, as
// the solve lane's own cube tests do - here under a mirrored root, in a
// right-to-left language, with nobody having opted the view out.
namespace {

constexpr auto kMirroredCube = R"QML(
import QtQuick
import Cubeulator
Item {
    LayoutMirroring.enabled: LanguageManager.rtl
    LayoutMirroring.childrenInherit: true
    property alias view: view
    Cube3DView {
        id: view
        property int played: 0
        onMovePlayed: played += 1
        autoRotate: false
        interactive: false
        homeRotation: Qt.vector3d(0, 0, 0)
    }
}
)QML";

[[nodiscard]] QColor paint(Color colour) {
    switch (colour) {
        case Color::White: return {250, 250, 250};
        case Color::Yellow: return {240, 220, 0};
        case Color::Green: return {0, 160, 60};
        case Color::Blue: return {0, 70, 200};
        case Color::Orange: return {250, 120, 0};
        case Color::Red: return {200, 0, 30};
    }
    return {};
}

[[nodiscard]] QVariantMap painting(const Cube& cube) {
    QVariantMap sides;
    for (const Face face : cube::solver::kAllFaces) {
        QVariantList colours;
        for (const Color colour : cube::solver::outside_view(cube, face)) {
            colours.append(paint(colour).name());
        }
        sides.insert(letter(face), colours);
    }
    return sides;
}

}  // namespace

TEST_CASE("in a right-to-left language the 3D cube is the solver's cube and every turn goes the solver's way",
          "[i18n][screen][rtl][cube]") {
    in_language("ar");
    REQUIRE(QGuiApplication::layoutDirection() == Qt::RightToLeft);

    QQmlComponent component(&t::engine());
    component.setData(kMirroredCube, QUrl(QStringLiteral("qrc:/i18n_test_cube.qml")));
    INFO(str(component.errorString()));
    REQUIRE_FALSE(component.isError());
    std::unique_ptr<QQuickItem> root(qobject_cast<QQuickItem*>(component.create()));
    REQUIRE(root != nullptr);
    QQuickItem* view = root->property("view").value<QQuickItem*>();
    REQUIRE(view != nullptr);
    REQUIRE(view->setProperty("turnDuration", 40));

    // The control: this root is mirrored. If it were not, the rest would
    // prove nothing about mirroring.
    QQmlComponent probe(&t::engine());
    probe.setData("import QtQuick\nItem { property bool mirrored: LayoutMirroring.enabled }",
                  QUrl(QStringLiteral("qrc:/i18n_test_probe2.qml")));
    std::unique_ptr<QQuickItem> mirrored_probe(qobject_cast<QQuickItem*>(probe.create()));
    mirrored_probe->setParentItem(root.get());
    REQUIRE(mirrored_probe->property("mirrored").toBool());

    const Cube cube = cube::solver::random_scramble(77);
    REQUIRE(view->setProperty("faceletColors", painting(cube)));
    CHECK(cube::test::shows(view, cube, &paint));
    // A check that could not fail would prove nothing.
    CHECK_FALSE(cube::test::shows(view, cube.apply(Move{Face::R, 1}), &paint));

    for (const Move move : all_moves()) {
        INFO("move " << cube::solver::to_string(move));
        REQUIRE(view->setProperty("faceletColors", painting(cube)));
        REQUIRE(cube::test::shows(view, cube, &paint));
        const int before = view->property("played").toInt();
        QVariant started;
        REQUIRE(QMetaObject::invokeMethod(view, "playMove", Q_RETURN_ARG(QVariant, started),
                                          Q_ARG(QVariant, letter(move.face)), Q_ARG(QVariant, move.turns)));
        REQUIRE(started.toBool());
        REQUIRE(wait_for([&]() { return view->property("played").toInt() > before; }));
        // The layer turned the way the solver turns it: the stickers read
        // back are the solver's cube after the move, not its mirror image.
        CHECK(cube::test::shows(view, cube.apply(move), &paint));
    }

    in_language("en");
}

// A picture of a screen in a language, for whoever reviews what the pseudo-locale
// and a right-to-left language look like. Hidden: it writes files, and the
// system tests' screenshot tour (tests/system) is the permanent one.
//
//   CUBE_I18N_SHOT_DIR=out CUBE_I18N_SHOT_SCREEN=LaunchScreen CUBE_I18N_SHOT_LANG=xx //       cube_i18n_screen_tests "[.shots]"
//
// One picture a process: a second grabWindow() with the 3D cube in the scene
// crashed the solve lane's gallery test (docs/app-experience-plan.md, "Two
// hazards").
TEST_CASE("a picture of a screen in a language", "[.shots]") {
    const QString dir = qEnvironmentVariable("CUBE_I18N_SHOT_DIR");
    const QString type = qEnvironmentVariable("CUBE_I18N_SHOT_SCREEN", QStringLiteral("LaunchScreen"));
    const QString lang = qEnvironmentVariable("CUBE_I18N_SHOT_LANG", QStringLiteral("en"));
    const QStringList size = qEnvironmentVariable("CUBE_I18N_SHOT_SIZE", QStringLiteral("900x640")).split('x');
    REQUIRE_FALSE(dir.isEmpty());
    REQUIRE(size.size() == 2);
    REQUIRE(t::language().preview(lang));
    QCoreApplication::processEvents();

    Screen screen(type, size.at(0).toInt(), size.at(1).toInt());
    QQuickWindow window;
    screen.show_in(window);
    QCoreApplication::processEvents();
    const QImage image = window.grabWindow();
    REQUIRE_FALSE(image.isNull());
    QDir().mkpath(dir);
    const QString file = QStringLiteral("%1/%2_%3_%4x%5.png").arg(dir, type, lang, size.at(0), size.at(1));
    REQUIRE(image.save(file));
    WARN("wrote " << str(file));
}
