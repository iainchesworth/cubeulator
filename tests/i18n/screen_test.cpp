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
#include <cube/solver/solver.hpp>

#include <QColor>
#include <QDir>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QImage>
#include <QMetaObject>
#include <QQmlComponent>
#include <QQuickItem>
#include <QQuickWindow>
#include <QStringList>
#include <QSurfaceFormat>
#include <QUrl>
#include <QVariant>

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
                QString::fromLatin1(item->metaObject()->className())
                    .startsWith(QLatin1String("AppButton")) &&
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

    // The buttons pushed below the bottom of the screen, which cannot be
    // pressed. Inside a scrolling area below the fold is where they belong.
    [[nodiscard]] QStringList buttons_below_screen() const {
        QStringList bad;
        visit(screen_.get(), [&](QQuickItem* item) {
            if (!item->isVisible() || !QString::fromLatin1(item->metaObject()->className())
                                           .startsWith(QLatin1String("AppButton"))) {
                return;
            }
            for (const QQuickItem* up = item->parentItem(); up != nullptr; up = up->parentItem()) {
                if (up->inherits("QQuickFlickable")) {
                    return;
                }
            }
            const QPointF bottom = item->mapToItem(screen_.get(), QPointF(0, item->height()));
            if (bottom.y() > screen_->height() + 0.5) {
                bad.append(item->property("text").toString());
            }
        });
        return bad;
    }

    // The labels that do not fit inside the button they are in.
    [[nodiscard]] QStringList labels_outside_buttons() const {
        QStringList bad;
        visit(screen_.get(), [&](QQuickItem* item) {
            if (!item->isVisible() || !QString::fromLatin1(item->metaObject()->className())
                                           .startsWith(QLatin1String("AppButton"))) {
                return;
            }
            const QRectF box =
                item->mapRectToItem(screen_.get(), QRectF(0, 0, item->width(), item->height()));
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
    // The id of the same button, for a language that is no longer English
    // under the screen: the words it shows there are what the catalogue says.
    const char* act_id = nullptr;
    explicit PermissionWords(const Screen& screen) {
        const bool denied = screen.item()->property("denied").toBool();
        title = denied ? QStringLiteral("Camera access is off") : QStringLiteral("Camera access");
        act = denied ? QStringLiteral("Try again") : QStringLiteral("Allow camera");
        act_id = denied ? "common.tryAgain" : "permission.allow";
    }
};

TEST_CASE("a screen that is up changes language without being made again",
          "[i18n][screen][switch]") {
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

namespace {

// Every converted screen, at both sizes, in `language`: no string cut off, run
// off the window or out of its pane, and no label out of its button. The
// pseudo-locale is the proxy for a real translation (40% longer than the
// English); the same checks run on the real Japanese and Chinese below.
// A solution in the controller, for the screen that lists one: its rows are
// the longest strings it has. Made once; the controller keeps it.
void have_a_solution() {
    SolveController& solve = t::controllers().solve;
    if (solve.totalMoves() == 0) {
        solve.solveCube(cube::solver::random_scramble(21));
    }
    REQUIRE(wait_for([&]() { return !solve.isSolving(); }, 60'000));
    REQUIRE(solve.totalMoves() > 0);
}

void holds_the_layout_in(const char* language) {
    have_a_solution();
    struct Size {
        int w;
        int h;
        const char* what;
    };
    const Size sizes[] = {{900, 640, "wide"}, {390, 780, "narrow"}};
    for (const char* type : {"PermissionScreen", "CompleteScreen", "LaunchScreen", "SettingsScreen",
                             "SolutionScreen"}) {
        for (const Size& size : sizes) {
            in_language(language);
            Screen screen(type, size.w, size.h);
            INFO(language << ", " << type << ", " << size.what);
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
                // The box the string was given holds it: down, for one that
                // wraps; and across for every one, wrapped or not. A wrapped
                // line that is still wider than its box is a word with no
                // place to break, or a box measured by something other than
                // the text it holds.
                if (wraps) {
                    CHECK(content_h <= text->height() + 0.5);
                }
                CHECK(content_w <= text->width() + 0.5);
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
            // And a button the longer text has pushed off the bottom of the
            // window is a button nobody can press.
            for (const QString& pushed : screen.buttons_below_screen()) {
                FAIL_CHECK("button below the window: " << str(pushed));
            }
        }
    }
}

}  // namespace

TEST_CASE("every converted screen holds the pseudo-locale without cutting a string off",
          "[i18n][screen][pseudo]") {
    holds_the_layout_in("xx");
}

TEST_CASE("every converted screen holds the real Japanese and Chinese without cutting a string off",
          "[i18n][screen][cjk]") {
    // The drafts, not their proxy: whatever the machine draws Han and kana
    // with, a translated string must fit the box the English was given.
    for (const char* language : {"ja", "zh-CN"}) {
        holds_the_layout_in(language);
    }
}

TEST_CASE(
    "every converted screen holds the real German, French and Spanish without cutting a string off",
    "[i18n][screen][latin]") {
    // The drafts, not their proxy. German is the long one: its compounds
    // ("Vierteldrehung", "Geräteeinstellungen") have no place to break, so a
    // box measured for the English is the likeliest to be cut here.
    for (const char* language : {"de", "fr", "es"}) {
        holds_the_layout_in(language);
    }
}

TEST_CASE("the face follows the app's language for Japanese and Chinese, and back",
          "[i18n][screen][face]") {
    // Japanese and Chinese share code points and not shapes. A family left to
    // glyph fallback draws one in the other's forms, so Theme names it by the
    // language (docs/design/platforms.md, "Fonts"). One probe, made in English,
    // read again after each switch: the binding is live.
    in_language("en");
    QQmlComponent component(&t::engine());
    component.setData(
        "import QtQuick\nimport Cubeulator\nItem { property string face: Theme.fontHeading }",
        QUrl(QStringLiteral("qrc:/i18n_test_face.qml")));
    REQUIRE_FALSE(component.isError());
    std::unique_ptr<QObject> probe(component.create());
    REQUIRE(probe != nullptr);
    auto face = [&](const char* code) {
        in_language(code);
        return probe->property("face").toString();
    };
    const QStringList installed = QFontDatabase::families();
    auto has_any = [&](std::initializer_list<const char*> names) {
        for (const char* name : names) {
            if (installed.contains(QLatin1String(name))) {
                return true;
            }
        }
        return false;
    };

    CHECK(face("en") == QLatin1String("Archivo"));

    const QString ja = face("ja");
    const QString zh = face("zh-CN");
    INFO("ja: " << str(ja) << ", zh-CN: " << str(zh));
    // Whatever it names exists on this machine: a family that is not installed
    // would be glyph fallback again, under a name.
    for (const QString& chosen : {ja, zh}) {
        CHECK((chosen == QLatin1String("Archivo") || installed.contains(chosen)));
    }
    // Where the machine has a face for a language, that is the one chosen.
    if (has_any(
            {"Yu Gothic UI", "Meiryo UI", "Hiragino Sans", "Noto Sans CJK JP", "Noto Sans JP"})) {
        CHECK(ja != QLatin1String("Archivo"));
    }
    if (has_any({"Microsoft YaHei UI", "Microsoft YaHei", "PingFang SC", "Noto Sans CJK SC",
                 "Noto Sans SC"})) {
        CHECK(zh != QLatin1String("Archivo"));
    }
    // And they are not the same face: that is the point.
    if (ja != QLatin1String("Archivo") && zh != QLatin1String("Archivo")) {
        CHECK(ja != zh);
    }

    // Every other language keeps Archivo, and a switch back is live.
    for (const char* code : {"de", "he", "xx", "en"}) {
        INFO(code);
        CHECK(face(code) == QLatin1String("Archivo"));
    }
}

TEST_CASE("right to left mirrors the layout, and left to right puts it back",
          "[i18n][screen][rtl]") {
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

    // A left-to-right language that is not English. German is translated (a
    // draft), so the button is found by the words the language gives it, not
    // by the English the right-to-left three still show.
    in_language("de");
    screen.settle();
    const QString german = qtTrId(words.act_id);
    REQUIRE(german != words.act);
    act = screen.button(german);
    REQUIRE(act != nullptr);
    CHECK(screen.left_of(act) < screen.item()->width() / 2);
}

TEST_CASE("the cube view is not mirrored, however the screen around it is",
          "[i18n][screen][rtl][cube]") {
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
        if (cls.startsWith(QLatin1String("CubeView")) ||
            cls.startsWith(QLatin1String("BobbingCube"))) {
            cubes.append(item);
        }
    }
    REQUIRE_FALSE(cubes.isEmpty());
    for (QQuickItem* cube : cubes) {
        CHECK_FALSE(screen.mirrored_under(cube));
    }
    in_language("en");
}

TEST_CASE("the solution screen mirrors for a right-to-left language, and its cube does not",
          "[i18n][screen][rtl][solution]") {
    have_a_solution();
    in_language("he");
    Screen screen("SolutionScreen", 900, 640);
    CHECK(screen.mirrored_under(screen.item()));
    // The list is in the pane the layout trails to: on the left.
    QQuickItem* list = nullptr;
    QList<QQuickItem*> cubes;
    for (QQuickItem* item : screen.find_all("QQuickItem")) {
        if (item->objectName() == QLatin1String("solution.list")) {
            list = item;
        }
        if (QString::fromLatin1(item->metaObject()->className())
                .startsWith(QLatin1String("Cube3DView"))) {
            cubes.append(item);
        }
    }
    REQUIRE(list != nullptr);
    REQUIRE_FALSE(cubes.isEmpty());
    CHECK(screen.left_of(list) < screen.item()->width() / 2);
    for (QQuickItem* cube : cubes) {
        CHECK(screen.left_of(cube) > screen.item()->width() / 2 - 1);
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

// A fresh JavaScript object each time, as the screens assign it; a QVariantMap
// would not repaint an unchanged cube on Qt 6.12. See cube::test::painting().
[[nodiscard]] QVariant painting(const Cube& cube) {
    return QVariant::fromValue(cube::test::painting(t::engine(), cube, &paint));
}

}  // namespace

TEST_CASE(
    "in a right-to-left language the 3D cube is the solver's cube and every turn goes the solver's "
    "way",
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
                                          Q_ARG(QVariant, letter(move.face)),
                                          Q_ARG(QVariant, move.turns)));
        REQUIRE(started.toBool());
        REQUIRE(wait_for([&]() { return view->property("played").toInt() > before; }));
        // The layer turned the way the solver turns it: the stickers read
        // back are the solver's cube after the move, not its mirror image.
        CHECK(cube::test::shows(view, cube.apply(move), &paint));
    }

    in_language("en");
}

// AppButton on its own: a label wraps in the room it is given and takes its
// whole width again when the room comes back; a label is never wider than the
// box it is set in, in any script (a button sized by one measurement of the
// string and a text laid out by another is how a chip came to run a pixel past
// its row).
namespace {

[[nodiscard]] std::unique_ptr<QQuickItem> make_button(const QString& text, double max_width) {
    QQmlComponent component(&t::engine());
    component.setData(R"QML(
        import QtQuick
        import Cubeulator
        AppButton {}
    )QML",
                      QUrl(QStringLiteral("qrc:/i18n_test_button.qml")));
    REQUIRE_FALSE(component.isError());
    std::unique_ptr<QQuickItem> button(qobject_cast<QQuickItem*>(component.create()));
    REQUIRE(button != nullptr);
    REQUIRE(button->setProperty("text", text));
    REQUIRE(button->setProperty("maxWidth", max_width));
    QCoreApplication::processEvents();
    return button;
}

[[nodiscard]] QQuickItem* label_of(QQuickItem* button) {
    QQuickItem* found = nullptr;
    std::function<void(QQuickItem*)> walk = [&](QQuickItem* item) {
        if (found == nullptr && item->inherits("QQuickText")) {
            found = item;
        }
        for (QQuickItem* child : item->childItems()) {
            walk(child);
        }
    };
    walk(button);
    return found;
}

}  // namespace

TEST_CASE("a button wraps in a narrow room and takes its whole width again in a wide one",
          "[i18n][screen][button]") {
    const QString text = QStringLiteral("Same as this device (Deutsch) and rather more besides");
    auto button = make_button(text, -1);
    QQuickItem* label = label_of(button.get());
    REQUIRE(label != nullptr);

    // Room to spare: one line, as wide as the label.
    const double whole = button->width();
    const double one_line = label->height();
    CHECK(whole > 200);
    CHECK(label->property("contentWidth").toDouble() <= label->width() + 0.5);

    // A narrow room: the same label on more lines, the button no wider than it
    // was told, nothing wider than the box it is in.
    REQUIRE(button->setProperty("maxWidth", 160.0));
    QCoreApplication::processEvents();
    CHECK(button->width() <= 160.5);
    CHECK(label->height() > one_line * 1.5);
    CHECK(label->property("contentWidth").toDouble() <= label->width() + 0.5);
    CHECK(button->height() >= label->height());

    // And the room comes back: the whole label on one line again, which is
    // what a window being widened does to a chip row.
    REQUIRE(button->setProperty("maxWidth", 600.0));
    QCoreApplication::processEvents();
    CHECK(button->width() == whole);
    CHECK(label->height() == one_line);
    CHECK(button->height() == 44);
}

TEST_CASE("a label is not wider than the box it is set in, in any of the nine scripts",
          "[i18n][screen][button]") {
    // The nine languages, each in itself, as the language row shows them, and
    // the longest of the interface strings in a script that shapes (Arabic).
    const QStringList names = {
        QStringLiteral("English"), QStringLiteral("Deutsch"), QStringLiteral("Français"),
        QStringLiteral("Español"), QStringLiteral("日本語"),  QStringLiteral("简体中文"),
        QStringLiteral("עברית"),   QStringLiteral("العربية"), QStringLiteral("יידיש")};
    for (const QString& name : names) {
        for (const double max_width : {-1.0, 60.0}) {
            INFO(name.toStdString() << " in " << max_width);
            auto button = make_button(name, max_width);
            QQuickItem* label = label_of(button.get());
            REQUIRE(label != nullptr);
            CHECK(label->property("contentWidth").toDouble() <= label->width() + 0.5);
            // And the button holds the label: nothing hangs out of it.
            CHECK(label->width() <= button->width() + 0.5);
        }
    }
}

// A picture of a screen in a language, for whoever reviews what the pseudo-locale
// and a right-to-left language look like. Hidden: it writes files, and the
// system tests' screenshot tour (tests/system) is the permanent one.
//
//   CUBE_I18N_SHOT_DIR=out CUBE_I18N_SHOT_SCREEN=LaunchScreen CUBE_I18N_SHOT_LANG=xx //
//   cube_i18n_screen_tests "[.shots]"
//
// One picture a process: a second grabWindow() with the 3D cube in the scene
// crashed the solve lane's gallery test (docs/app-experience-plan.md, "Two
// hazards").
TEST_CASE("a picture of a screen in a language", "[.shots]") {
    const QString dir = qEnvironmentVariable("CUBE_I18N_SHOT_DIR");
    const QString type =
        qEnvironmentVariable("CUBE_I18N_SHOT_SCREEN", QStringLiteral("LaunchScreen"));
    const QString lang = qEnvironmentVariable("CUBE_I18N_SHOT_LANG", QStringLiteral("en"));
    const QStringList size =
        qEnvironmentVariable("CUBE_I18N_SHOT_SIZE", QStringLiteral("900x640")).split('x');
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
    const QString file =
        QStringLiteral("%1/%2_%3_%4x%5.png").arg(dir, type, lang, size.at(0), size.at(1));
    REQUIRE(image.save(file));
    WARN("wrote " << str(file));
}
