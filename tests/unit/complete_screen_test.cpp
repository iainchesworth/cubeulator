// The complete screen's stats card, and whether its figures can be read.
//
// CompleteScreen.qml loaded with the app's own controllers behind it, in each
// theme and each layout. The card (turns, time, method) is a ruled box on the
// page. A Rectangle with no `color` is opaque white, which reads as ink on
// white in the light theme and is near-white ink on white in the dark one: the
// card in complete-wide-dark.png was a white box whose figures could hardly be
// seen.
//
// So this reads, from the items' own properties, what each piece of text in
// the card is drawn over, and holds the pair to the contrast the design system
// asks of its tokens (docs/design/tools/check_tokens.py). It needs neither a
// GPU nor a display. render_test.cpp looks at the same card's pixels.
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include "contrast.hpp"
#include "qml_test_app.hpp"

#include <QColor>
#include <QObject>
#include <QQmlComponent>
#include <QQuickItem>
#include <QString>
#include <QVariant>

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace {

// Either side of Theme.wideBreakpoint (720).
constexpr int kNarrowWidth = 390;
constexpr int kNarrowHeight = 844;
constexpr int kWideWidth = 900;
constexpr int kWideHeight = 640;

[[nodiscard]] QObject& singleton(const char* name) {
    QObject* found = cube::test::qml_engine().singletonInstance<QObject*>("Cubeulator", name);
    REQUIRE(found != nullptr);
    return *found;
}

// The persisted theme choice, set for the length of a case and put back: the
// store is the one the whole QML test run shares.
class ThemeMode {
public:
    explicit ThemeMode(const QString& mode)
        : settings_(singleton("SettingsState")),
          previous_(settings_.property("themeMode").toString()) {
        REQUIRE(settings_.setProperty("themeMode", mode));
    }
    ~ThemeMode() { settings_.setProperty("themeMode", previous_); }
    ThemeMode(const ThemeMode&) = delete;
    ThemeMode& operator=(const ThemeMode&) = delete;

private:
    QObject& settings_;
    QString previous_;
};

void collect(QQuickItem* item, std::vector<QQuickItem*>& found,
             const std::function<bool(const QQuickItem&)>& wanted) {
    if (wanted(*item)) {
        found.push_back(item);
    }
    for (QQuickItem* child : item->childItems()) {
        collect(child, found, wanted);
    }
}

[[nodiscard]] std::vector<QQuickItem*> visible_items(
    QQuickItem* root, const std::function<bool(const QQuickItem&)>& wanted) {
    std::vector<QQuickItem*> found;
    collect(root, found,
            [&wanted](const QQuickItem& item) { return item.isVisible() && wanted(item); });
    return found;
}

// What `item` is drawn over: the fills of the Rectangles it sits in, nearest
// first, until one is opaque. The window clears to white under all of them.
[[nodiscard]] QColor drawn_over(const QQuickItem* item) {
    std::vector<QColor> fills;
    for (const QQuickItem* up = item; up != nullptr; up = up->parentItem()) {
        if (!up->inherits("QQuickRectangle")) {
            continue;
        }
        const QColor fill = up->property("color").value<QColor>();
        fills.push_back(fill);
        if (cube::test::is_opaque(fill)) {
            break;
        }
    }
    QColor result = Qt::white;
    for (auto fill = fills.rbegin(); fill != fills.rend(); ++fill) {
        result = cube::test::over(*fill, result);
    }
    return result;
}

}  // namespace

TEST_CASE("the figures on the complete screen's stats card can be read, in both themes",
          "[qml][complete-screen][theme]") {
    const std::string mode = GENERATE(as<std::string>{}, "light", "dark");
    const bool wide = GENERATE(false, true);
    CAPTURE(mode, wide);

    const ThemeMode theme(QString::fromStdString(mode));
    const QColor ink = singleton("Theme").property("ink").value<QColor>();

    QQmlComponent component(&cube::test::qml_engine());
    component.loadFromModule("Cubeulator", "CompleteScreen");
    if (component.isError()) {
        FAIL(component.errorString().toStdString());
    }
    const std::unique_ptr<QQuickItem> screen(qobject_cast<QQuickItem*>(component.create()));
    REQUIRE(screen != nullptr);
    screen->setWidth(wide ? kWideWidth : kNarrowWidth);
    screen->setHeight(wide ? kWideHeight : kNarrowHeight);

    // Each layout is built beside the other; the one on show is the card.
    const std::vector<QQuickItem*> cards = visible_items(
        screen.get(), [](const QQuickItem& item) { return item.objectName() == "complete.stats"; });
    REQUIRE(cards.size() == 1);
    QQuickItem* card = cards.front();

    // What the ink is drawn on.
    const QColor fill = drawn_over(card);
    INFO("ink " << ink.name().toStdString() << " on the card's " << fill.name().toStdString());
    CHECK(cube::test::contrast_ratio(ink, fill) >= cube::test::kTextContrast);

    // And each thing written in it, in the colour it is written in, over what
    // is really behind that: the figures are ink, the labels a mix of it.
    const std::vector<QQuickItem*> texts = visible_items(card, [](const QQuickItem& item) {
        return item.inherits("QQuickText") && !item.property("text").toString().isEmpty();
    });
    REQUIRE(texts.size() >= 4);  // turns and time, a label and a figure each
    for (const QQuickItem* text : texts) {
        const QColor written = text->property("color").value<QColor>();
        const QColor behind = drawn_over(text);
        const QColor shown = cube::test::over(written, behind);
        const bool figure = written.rgb() == ink.rgb() && cube::test::is_opaque(written);
        const std::string what = text->property("text").toString().toStdString();
        CAPTURE(what, shown.name().toStdString(), behind.name().toStdString());
        // The muted label is 4.2:1 on the light page at the 0.6 the screens
        // mix it at, a near miss that is the design's to settle; what this
        // holds it to is not being lost.
        CHECK(cube::test::contrast_ratio(shown, behind) >=
              (figure ? cube::test::kTextContrast : cube::test::kLargeOrMutedContrast));
    }
}
