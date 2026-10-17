// Language: switched live, in the real window, from English to the
// pseudo-locale to Hebrew and back.
//
// What E2b (docs/i18n.md) promises of the converted screens, held where a
// person would see it: the strings follow without a restart, the layout
// mirrors for a right-to-left language and returns, the cube does not mirror
// when the pane around it does, and the pseudo-locale (every string accented,
// bracketed and 40% longer) cuts nothing off.
//
// The part that needs no window (every id in every language, the manager's
// start-up order, the 18 turns in a mirrored root) is cube_i18n_tests and
// cube_i18n_screen_tests.

#include "app.hpp"

#include "app_runtime.hpp"
#include "i18n/language_manager.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <cube/solver/solver.hpp>

#include <QGuiApplication>
#include <QImage>
#include <QMetaObject>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSettings>
#include <QStringList>
#include <QtGlobal>

#include <cmath>
#include <functional>
#include <set>
#include <string>
#include <vector>

namespace {

using cube::systest::App;
using cube::systest::Layout;

[[nodiscard]] std::string str(const QString& s) {
    return s.toStdString();
}

[[nodiscard]] cube::i18n::LanguageManager& language(App& app) {
    auto* manager = app.runtime().engine().singletonInstance<cube::i18n::LanguageManager*>(
        "Cubeulator", "LanguageManager");
    REQUIRE(manager != nullptr);
    return *manager;
}

// Leaves the language as the baseline has it, however a test ends: the app is
// shared by every test in the process.
class LanguageGuard {
public:
    explicit LanguageGuard(App& app) : app_(app) { restore(); }
    ~LanguageGuard() { restore(); }
    LanguageGuard(const LanguageGuard&) = delete;
    LanguageGuard& operator=(const LanguageGuard&) = delete;

private:
    void restore() {
        language(app_).useSystemLanguage();
        language(app_).preview(QStringLiteral("en"));
        QSettings store;
        store.remove(QStringLiteral("language/code"));
        store.sync();
        app_.pump(50);
    }
    App& app_;
};

void switch_to(App& app, const char* code) {
    REQUIRE(language(app).preview(QString::fromLatin1(code)));
    app.pump(60);
    app.settle();
}

// A solution in the controller, for the screens that have one to show: the
// solve screen and the solution. One serves every case; the controller keeps
// it.
void have_a_solution(App& app) {
    auto& solve = app.runtime().solve();
    if (solve.totalMoves() == 0) {
        solve.solveCube(cube::solver::random_scramble(21));
    }
    REQUIRE(app.wait_for([&]() { return !solve.isSolving(); }, 180'000));
}

void visit(QQuickItem* item, const std::function<void(QQuickItem*)>& fn) {
    fn(item);
    for (QQuickItem* child : item->childItems()) {
        visit(child, fn);
    }
}

// Every visible string of the screen on show that does not fit where it was
// put: elided, cut at a line count, wider than its pane, or off the window.
[[nodiscard]] QStringList strings_cut_off(App& app) {
    QStringList bad;
    QQuickItem* screen = app.screen_item();
    REQUIRE(screen != nullptr);
    const QRectF window(0, 0, app.window().width(), app.window().height());
    // Inside a scrolling area, below the fold is where things are supposed to
    // be.
    const auto scrolls = [](const QQuickItem* item) {
        for (const QQuickItem* up = item->parentItem(); up != nullptr; up = up->parentItem()) {
            if (up->inherits("QQuickFlickable")) {
                return true;
            }
        }
        return false;
    };
    visit(screen, [&](QQuickItem* item) {
        const QString cls = QString::fromLatin1(item->metaObject()->className());
        if (item->isVisible() && cls.startsWith(QLatin1String("AppButton")) && !scrolls(item) &&
            item->mapToScene(QPointF(0, item->height())).y() > window.bottom() + 0.5) {
            // A control pushed below the bottom of the window cannot be pressed.
            bad.append(QStringLiteral("(button) ") + item->property("text").toString().left(50));
        }
        if (!item->isVisible() || !item->inherits("QQuickText")) {
            return;
        }
        const QString shown = item->property("text").toString();
        if (shown.isEmpty() || item->opacity() < 0.01) {
            return;
        }
        const double content_w = item->property("contentWidth").toDouble();
        const double content_h = item->property("contentHeight").toDouble();
        const QPointF at = item->mapToScene(QPointF(0, 0));
        const bool elided = item->property("truncated").toBool();
        const bool off_window = at.x() < window.left() - 0.5 ||
                                at.x() + content_w > window.right() + 0.5 ||
                                (!scrolls(item) && at.y() + content_h > window.bottom() + 0.5);
        const QQuickItem* pane = item->parentItem();
        const bool out_of_pane = pane != nullptr && pane != screen && pane->width() > 0 &&
                                 item->x() + content_w > pane->width() + 0.5;
        if (elided || off_window || out_of_pane) {
            bad.append(shown.left(60));
        }
    });
    return bad;
}

// The picture reflected left to right. QImage::mirrored() is scheduled for
// deprecation in Qt 6.13 and already warns on 6.12 (and warnings are errors
// here); its replacement, flipped(), arrived in 6.9. The project still
// accepts Qt 6.5, so both are kept.
//
// Chosen with `if constexpr` rather than the preprocessor, because shared
// source takes no #if (docs/platform-isolation.md). It is a template so the
// branch not taken is never instantiated, which is what lets the other
// Qt's missing member compile.
template <typename Image>
[[nodiscard]] Image reflected(const Image& image) {
    if constexpr (QT_VERSION >= QT_VERSION_CHECK(6, 9, 0)) {
        return image.flipped(Qt::Horizontal);
    } else {
        return image.mirrored(true, false);
    }
}

}  // namespace

TEST_CASE("the language switches live: English, the pseudo-locale, Hebrew, and back",
          "[system][language]") {
    App& app = App::fresh();
    LanguageGuard guard(app);
    switch_to(app, "en");

    QQuickItem* scan = app.find("launch.startScan");
    REQUIRE(scan != nullptr);
    const QString english = scan->property("text").toString();
    CHECK(english == QLatin1String("Scan a cube"));
    CHECK(QGuiApplication::layoutDirection() == Qt::LeftToRight);
    // Primary action: leading half of the window, in a left-to-right language.
    const double english_x = scan->mapToScene(QPointF(scan->width() / 2, 0)).x();
    CHECK(english_x < app.window().width() / 2);

    // The pseudo-locale: the same control, now bracketed and accented.
    switch_to(app, "xx");
    scan = app.find("launch.startScan");
    REQUIRE(scan != nullptr);
    const QString pseudo = scan->property("text").toString();
    CHECK(pseudo != english);
    CHECK(pseudo.startsWith(QLatin1Char('[')));
    CHECK(pseudo.endsWith(QLatin1Char(']')));
    CHECK(QGuiApplication::layoutDirection() == Qt::LeftToRight);
    app.shot("language_launch_pseudo");

    // German is a draft: its own words, and the layout stays.
    switch_to(app, "de");
    scan = app.find("launch.startScan");
    REQUIRE(scan != nullptr);
    CHECK(scan->property("text").toString() != english);
    CHECK_FALSE(scan->property("text").toString().isEmpty());
    CHECK(QGuiApplication::layoutDirection() == Qt::LeftToRight);

    // Hebrew is a draft: its own words, in its own script; and the layout
    // mirrors.
    switch_to(app, "he");
    scan = app.find("launch.startScan");
    REQUIRE(scan != nullptr);
    const QString hebrew = scan->property("text").toString();
    CHECK(hebrew != english);
    REQUIRE_FALSE(hebrew.isEmpty());
    CHECK(hebrew.front().unicode() >= 0x0590);
    CHECK(hebrew.front().unicode() <= 0x05FF);
    CHECK(QGuiApplication::layoutDirection() == Qt::RightToLeft);
    CHECK(language(app).rtl());
    const double hebrew_x = scan->mapToScene(QPointF(scan->width() / 2, 0)).x();
    CHECK(hebrew_x > app.window().width() / 2);
    app.shot("language_launch_hebrew");

    // And back.
    switch_to(app, "en");
    scan = app.find("launch.startScan");
    REQUIRE(scan != nullptr);
    CHECK(scan->property("text").toString() == english);
    CHECK(QGuiApplication::layoutDirection() == Qt::LeftToRight);
    const double back_x = scan->mapToScene(QPointF(scan->width() / 2, 0)).x();
    CHECK(back_x == english_x);
}

TEST_CASE("the cube does not mirror when the layout around it does", "[system][language][cube]") {
    App& app = App::fresh();
    LanguageGuard guard(app);
    // Still, so that two pictures of it can be compared.
    REQUIRE(app.settings_state().setProperty("cubeAutoRotate", false));
    switch_to(app, "en");
    app.pump(500);
    app.settle();

    QQuickItem* cube = app.cube_in_view();
    REQUIRE(cube != nullptr);
    const auto cube_picture = [&]() {
        const QPointF at = cube->mapToScene(QPointF(0, 0));
        const QRect box(static_cast<int>(at.x()), static_cast<int>(at.y()),
                        static_cast<int>(cube->width()), static_cast<int>(cube->height()));
        return app.grab().copy(box);
    };
    // Fraction of pixels that differ between two pictures of the same size.
    const auto difference = [](const QImage& a, const QImage& b) {
        REQUIRE(a.size() == b.size());
        long differing = 0;
        for (int y = 0; y < a.height(); ++y) {
            for (int x = 0; x < a.width(); ++x) {
                const QColor p = a.pixelColor(x, y);
                const QColor q = b.pixelColor(x, y);
                if (std::abs(p.red() - q.red()) + std::abs(p.green() - q.green()) +
                        std::abs(p.blue() - q.blue()) >
                    40) {
                    ++differing;
                }
            }
        }
        return static_cast<double>(differing) / (a.width() * a.height());
    };

    const double english_x = cube->mapToScene(QPointF(cube->width() / 2, 0)).x();
    const QImage in_english = cube_picture();

    switch_to(app, "he");
    app.pump(500);
    app.settle();
    cube = app.cube_in_view();
    REQUIRE(cube != nullptr);
    const double hebrew_x = cube->mapToScene(QPointF(cube->width() / 2, 0)).x();
    // The pane it is in has changed sides...
    CHECK(english_x > app.window().width() / 2);
    CHECK(hebrew_x < app.window().width() / 2);

    // ...and the cube in it is the same cube, not its reflection.
    const QImage in_hebrew = cube_picture();
    // The control: a reflection of this cube is a visibly different picture.
    CHECK(difference(in_english, reflected(in_english)) > 0.05);
    CHECK(difference(in_english, in_hebrew) < 0.02);
}

TEST_CASE(
    "the solution screen follows the layout's direction: forward is the other arrow, "
    "the cube's pane the other side",
    "[system][language][solution]") {
    App& app = App::fresh(App::Start::elsewhere);
    LanguageGuard guard(app);
    app.resize(cube::systest::kDesktop);
    have_a_solution(app);
    switch_to(app, "en");
    app.go("solution");
    app.settle();

    const auto position = [&]() { return app.screen_item()->property("position").toInt(); };
    const auto cube_x = [&]() {
        QQuickItem* cube = app.find("solution.cube");
        REQUIRE(cube != nullptr);
        return cube->mapToScene(QPointF(cube->width() / 2, 0)).x();
    };
    const auto list_x = [&]() {
        QQuickItem* list = app.find("solution.list");
        REQUIRE(list != nullptr);
        return list->mapToScene(QPointF(list->width() / 2, 0)).x();
    };

    // English: the cube's pane leads, which is the left; the right arrow is
    // forward.
    CHECK(cube_x() < app.window().width() / 2);
    CHECK(list_x() > app.window().width() / 2);
    app.key(Qt::Key_Right);
    REQUIRE(app.wait_for([&]() { return position() == 1; }, 15'000));
    app.key(Qt::Key_Left);
    REQUIRE(app.wait_for([&]() { return position() == 0; }, 15'000));

    // Hebrew: the panes change sides, and so does forward. (That the cube in
    // its pane is not a reflection is cube_i18n_screen_tests's.)
    switch_to(app, "he");
    app.settle();
    CHECK(cube_x() > app.window().width() / 2);
    CHECK(list_x() < app.window().width() / 2);
    app.key(Qt::Key_Left);
    REQUIRE(app.wait_for([&]() { return position() == 1; }, 15'000));
    app.key(Qt::Key_Right);
    REQUIRE(app.wait_for([&]() { return position() == 0; }, 15'000));
    CHECK(app.screen_item()->property("mirrored").toBool());

    // Home and End are the ends, in either direction.
    app.key(Qt::Key_End);
    CHECK(position() == app.screen_item()->property("total").toInt());
    app.key(Qt::Key_Home);
    CHECK(position() == 0);
}

namespace {

// The converted screens of the real window, in `language` at one layout:
// nothing cut off, off the window or out of its pane. The pseudo-locale is the
// proxy for a real translation; the real drafts (Japanese, Chinese, German, French,
// Spanish, Hebrew, Arabic, Yiddish) run the same. `more` names screens to visit after
// the usual three.
void nothing_is_cut_off_in(const char* language, Layout layout,
                           std::vector<const char*> more = {}) {
    App& app = App::fresh();
    LanguageGuard guard(app);
    app.resize(layout == Layout::wide ? cube::systest::kDesktop : cube::systest::kPhone);
    switch_to(app, language);

    const auto check_screen = [&](const char* screen) {
        INFO(language << ", screen " << screen);
        QStringList cut = strings_cut_off(app);
        for (const QString& s : cut) {
            FAIL_CHECK("cut off: " << str(s));
        }
    };

    const QString which = QLatin1String(language) == QLatin1String("xx")
                              ? QStringLiteral("pseudo")
                              : QString::fromLatin1(language);
    const auto shoot = [&](const char* screen) {
        app.shot(QStringLiteral("language_%1_%2_%3")
                     .arg(QString::fromLatin1(screen), which,
                          QString::fromLatin1(cube::systest::to_string(layout))));
    };

    check_screen("launch");
    shoot("launch");
    app.click("launch.settings");
    REQUIRE(app.wait_for([&]() { return app.screen() == "settings"; }));
    app.settle();
    check_screen("settings");
    shoot("settings");
    app.click("settings.back");
    REQUIRE(app.wait_for([&]() { return app.screen() == "launch"; }));
    have_a_solution(app);
    std::vector<const char*> screens = {"permission", "complete", "solution"};
    screens.insert(screens.end(), more.begin(), more.end());
    for (const char* screen : screens) {
        app.go(QString::fromLatin1(screen));
        app.settle();
        check_screen(screen);
        shoot(screen);
    }
    app.go("launch");
}

}  // namespace

TEST_CASE("the pseudo-locale cuts nothing off on the converted screens",
          "[system][language][pseudo]") {
    const Layout layout = GENERATE(Layout::wide, Layout::narrow);
    DYNAMIC_SECTION("in the " << cube::systest::to_string(layout) << " layout") {
        nothing_is_cut_off_in("xx", layout);
    }
}

TEST_CASE(
    "the real Japanese, Chinese, Hebrew, Arabic and Yiddish cut nothing off on the "
    "converted screens",
    "[system][language][cjk]") {
    // The drafts, not their proxy. The three right-to-left ones are mirrored
    // as well, which the pseudo-locale (left to right) never is.
    const char* language = GENERATE("ja", "zh-CN", "he", "ar", "yi");
    const Layout layout = GENERATE(Layout::wide, Layout::narrow);
    DYNAMIC_SECTION(language << " in the " << cube::systest::to_string(layout) << " layout") {
        nothing_is_cut_off_in(language, layout);
    }
}

TEST_CASE("choosing a language in Settings switches it at once and keeps it",
          "[system][language][settings]") {
    App& app = App::fresh();
    LanguageGuard guard(app);
    switch_to(app, "en");

    app.click("launch.settings");
    REQUIRE(app.wait_for([&]() { return app.screen() == "settings"; }));
    app.settle();

    // 0 follows the device; then the nine in the order of the list: en de fr
    // es ja zh-CN he ar yi.
    QQuickItem* hebrew = app.find("settings.language.7");
    REQUIRE(hebrew != nullptr);
    app.click(hebrew);
    REQUIRE(app.wait_for([&]() { return QGuiApplication::layoutDirection() == Qt::RightToLeft; }));
    CHECK(language(app).currentLanguage() == QLatin1String("he"));
    CHECK(app.screen() == "settings");

    const auto stored = []() {
        QSettings store;
        store.sync();
        return store.value("language/code").toString();
    };
    CHECK(stored() == QLatin1String("he"));

    // English again, from the same screen, which never left.
    QQuickItem* english = app.find("settings.language.1");
    REQUIRE(english != nullptr);
    app.click(english);
    REQUIRE(app.wait_for([&]() { return QGuiApplication::layoutDirection() == Qt::LeftToRight; }));
    CHECK(language(app).currentLanguage() == QLatin1String("en"));
    CHECK(stored() == QLatin1String("en"));

    // "Same as this device" forgets the choice.
    app.click("settings.language.0");
    REQUIRE(app.wait_for([&]() { return stored().isEmpty(); }));
    CHECK_FALSE(language(app).hasOverride());
    CHECK(app.screen() == "settings");
}

TEST_CASE(
    "in Hebrew, Arabic and Yiddish every control on every screen is inside the window, bar the "
    "known few",
    "[system][language][rtl][layout]") {
    // tests/system/layout_test.cpp's rule, in a right-to-left language, where
    // what was pushed past the right edge is now pushed past the left. A
    // screen that was laid out with `left` and `right` written into its
    // anchors, or a number worked out for one direction, shows here. Each of
    // the three drafts is its own run: Arabic is the widest of them.
    const char* language = GENERATE("he", "ar", "yi");
    //
    // The known few are listed as they are, so that a new one fails and so
    // does the day one of these is fixed. Owner of each is named.
    const std::set<std::string> known = {
        // As in layout_test.cpp: the Complete screen's buttons below the fold
        // of a large window (experience lane, E3d).
        "complete.review @ 1280x800",
        "complete.scanAnother @ 1280x800",
    };

    const std::vector<QSize> sizes = {{375, 667}, {390, 844}, {719, 640},
                                      {720, 640}, {900, 640}, {1280, 800}};
    const char* screens[] = {"launch",   "permission", "scan",     "solve",
                             "settings", "history",    "complete", "solution"};

    std::set<std::string> overflowing;
    std::set<std::string> on_show;
    for (const char* name : screens) {
        // One reset and one switch of language per screen, then the sizes by
        // resizing (as layout_test.cpp does): the switch re-translates the
        // whole scene and the launch and solve screens draw a 3D cube, and on
        // a software renderer each of those is seconds - this case was the
        // longest of the suite, at some twenty minutes, doing it per size.
        App& app = App::fresh(QString::fromLatin1(name) == QLatin1String("launch")
                                  ? App::Start::launch
                                  : App::Start::elsewhere);
        LanguageGuard guard(app);
        switch_to(app, language);
        if (QString::fromLatin1(name) == QLatin1String("solve") ||
            QString::fromLatin1(name) == QLatin1String("solution")) {
            have_a_solution(app);
        }
        app.go(QString::fromLatin1(name));
        REQUIRE(app.screen() == QString::fromLatin1(name));
        app.settle();
        for (const QSize& size : sizes) {
            app.resize(size);
            app.pump(80);

            const QRect window(QPoint(0, 0), app.window().size());
            for (QQuickItem* control : app.visible_controls()) {
                bool in_flickable = false;
                for (QQuickItem* up = control->parentItem(); up != nullptr; up = up->parentItem()) {
                    in_flickable = in_flickable || up->inherits("QQuickFlickable");
                }
                if (in_flickable) {
                    continue;
                }
                const std::string key =
                    (app.control_key(control) + " @ " + QString::number(size.width()) + "x" +
                     QString::number(size.height()))
                        .toStdString();
                on_show.insert(key);
                const QRect box = control->mapRectToScene(control->boundingRect()).toAlignedRect();
                if (!window.contains(box)) {
                    overflowing.insert(key);
                    UNSCOPED_INFO(key << " is at " << box.x() << "," << box.y() << " "
                                      << box.width() << "x" << box.height());
                }
            }
        }
    }

    for (const std::string& key : overflowing) {
        INFO("a control outside the window, in " << language
                                                 << ", that is not in the known list: " << key);
        CHECK(known.count(key) == 1);
    }
    for (const std::string& key : known) {
        // Said, not failed: see layout_test.cpp. The list is of controls that
        // overflow somewhere; one that fits on this platform is not a fix.
        if (on_show.count(key) == 1 && overflowing.count(key) == 0) {
            WARN(
                "fits in its window on this platform; remove it from the known list when it does "
                "on all of them: "
                << key);
        }
    }
}

TEST_CASE("the real German, French and Spanish cut nothing off on the converted screens",
          "[system][language][latin]") {
    // German is the long one ("Vierteldrehung", "Geräteeinstellungen",
    // "Fortgeschritten" in a row of three method buttons): the solve screen is
    // visited as well, where those buttons are, and the history, whose rows
    // and dialogs carry the longest of the new strings.
    const char* language = GENERATE("de", "fr", "es");
    const Layout layout = GENERATE(Layout::wide, Layout::narrow);
    DYNAMIC_SECTION(language << " in the " << cube::systest::to_string(layout) << " layout") {
        nothing_is_cut_off_in(language, layout, {"solve", "history"});
    }
}
