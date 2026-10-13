// KitButton: the first kit component, and the pattern the others follow. It
// is held to every line of the gate (docs/app-experience-plan.md, E2): it is
// reachable by keyboard, has a name and a role, meets its target size, holds
// its contrast in both themes in every state, and truncates nothing when its
// label is 40% longer or the text is at the largest scale.
#include <catch2/catch_test_macros.hpp>

#include "kit_fixture.hpp"

#include <QSignalSpy>
#include <QString>

using namespace cube::test;

namespace {

std::unique_ptr<QQuickItem> button(const QVariantMap& properties) {
    QVariantMap withDefaults = properties;
    if (!withDefaults.contains("text")) {
        withDefaults.insert("text", "Scan a cube");
    }
    return create("KitButton", withDefaults);
}

QQuickItem* labelOf(QQuickItem* root) {
    QQuickItem* label = find(root, "label");
    REQUIRE(label != nullptr);
    return label;
}

// The colour the page behind a control is: what shows through a transparent
// fill. Over the camera that is the worst picture a panel can sit on.
QColor pageBehind(bool onOverlay) {
    if (onOverlay) {
        QObject* overlay = get("Theme", "overlay").value<QObject*>();
        return over(colour(overlay->property("panel")), Qt::white);
    }
    return colour(get("Theme", "paper"));
}

}  // namespace

TEST_CASE("A button has a name, a role, an action and a place in the tab order", "[kit][button][a11y]") {
    resetKitState();
    Hosted host(button({}));

    const Accessibility info = accessibilityOf(host.item());
    REQUIRE(info.present);
    CHECK(info.name == "Scan a cube");
    CHECK(info.role == QAccessible::PushButton);
    CHECK(info.state.focusable);
    CHECK_FALSE(info.state.disabled);
    CHECK(info.actions.contains(QAccessibleActionInterface::pressAction()));
}

TEST_CASE("A button's accessible name can say more than its label", "[kit][button][a11y]") {
    resetKitState();
    Hosted host(button({{"text", "Back"}, {"accessibleName", "Back to home"}, {"accessibleDescription", "Leaves the solve"}}));

    const Accessibility info = accessibilityOf(host.item());
    CHECK(info.name == "Back to home");
    CHECK(info.description == "Leaves the solve");
}

TEST_CASE("A disabled button says so and cannot be activated", "[kit][button][a11y]") {
    resetKitState();
    Hosted host(button({{"enabled", false}}));
    QSignalSpy clicked(host.item(), SIGNAL(clicked()));

    CHECK(accessibilityOf(host.item()).state.disabled);

    host.click(host.item());
    host.key(Qt::Key_Space);
    host.key(Qt::Key_Return);
    CHECK(clicked.count() == 0);
}

TEST_CASE("Tab reaches a button and shows the focus ring; a click does not", "[kit][button][keyboard]") {
    resetKitState();
    Hosted host(createFromSource(R"(
        import QtQuick
        import Cubeulator
        Column {
            spacing: 24
            KitButton { objectName: "first"; text: "First" }
            KitButton { objectName: "second"; text: "Second" }
        }
    )"));
    QQuickItem* first = find(host.item(), "first");
    QQuickItem* second = find(host.item(), "second");
    REQUIRE(first != nullptr);
    REQUIRE(second != nullptr);
    QQuickItem* firstRing = first->findChild<QQuickItem*>("focusRing");
    QQuickItem* secondRing = second->findChild<QQuickItem*>("focusRing");
    REQUIRE(firstRing != nullptr);
    REQUIRE(secondRing != nullptr);
    CHECK_FALSE(firstRing->isVisible());

    // The keyboard: focus, and the ring.
    CHECK(host.tab() == first);
    CHECK(first->property("visualFocus").toBool());
    CHECK(firstRing->isVisible());

    // Tab again moves on and takes the ring with it.
    CHECK(host.tab() == second);
    CHECK_FALSE(firstRing->isVisible());
    CHECK(secondRing->isVisible());

    // The pointer: focus is moved to what was clicked, but it is not "keyboard
    // focus", so there is no ring.
    host.click(first);
    CHECK(first->hasActiveFocus());
    CHECK_FALSE(first->property("visualFocus").toBool());
    CHECK_FALSE(firstRing->isVisible());
    CHECK_FALSE(secondRing->isVisible());
}

TEST_CASE("Space, Enter and Return activate a focused button", "[kit][button][keyboard]") {
    resetKitState();
    Hosted host(button({}));
    QSignalSpy clicked(host.item(), SIGNAL(clicked()));

    REQUIRE(host.tab() == host.item());

    host.key(Qt::Key_Space);
    CHECK(clicked.count() == 1);
    host.key(Qt::Key_Return);
    CHECK(clicked.count() == 2);
    host.key(Qt::Key_Enter);
    CHECK(clicked.count() == 3);
}

TEST_CASE("A click or tap activates a button once", "[kit][button][pointer]") {
    resetKitState();
    Hosted host(button({}));
    QSignalSpy clicked(host.item(), SIGNAL(clicked()));

    host.click(host.item());
    CHECK(clicked.count() == 1);
}

TEST_CASE("Assistive technology can press a button by its action", "[kit][button][a11y]") {
    resetKitState();
    Hosted host(button({}));
    QSignalSpy clicked(host.item(), SIGNAL(clicked()));

    QAccessibleInterface* iface = QAccessible::queryAccessibleInterface(host.item());
    REQUIRE(iface != nullptr);
    QAccessibleActionInterface* actions = iface->actionInterface();
    REQUIRE(actions != nullptr);
    actions->doAction(QAccessibleActionInterface::pressAction());
    CHECK(clicked.count() == 1);
}

TEST_CASE("A button meets its target size, whatever its label", "[kit][button][target]") {
    resetKitState();
    struct Case {
        const char* size;
        double minHeight;
    };
    for (const Case c : {Case{"regular", 48}, Case{"thumb", 56}, Case{"giant", 96}}) {
        for (const char* text : {"OK", "Scan a cube"}) {
            auto item = button({{"size", c.size}, {"text", text}});
            INFO(c.size << " / " << text);
            CHECK(item->height() >= c.minHeight);
            CHECK(item->width() >= 48);
        }
    }
}

TEST_CASE("A label 40% longer wraps and nothing is cut off", "[kit][button][textfit]") {
    resetKitState();
    const QString source = "Enter the colours by hand";
    // 40% longer, as a translation might be: whole words, not a longer one.
    const QString longer = source + " " + source.left(int(source.size() * 0.4));

    for (const char* size : {"regular", "thumb", "giant"}) {
        for (const bool block : {false, true}) {
            auto item = button({{"text", longer}, {"size", size}, {"block", block}, {"leadingIcon", "camera"}});
            // A button in a column the width of a small phone.
            item->setWidth(240);
            QQuickItem* label = labelOf(item.get());

            INFO(size << (block ? " block" : ""));
            CHECK_FALSE(label->property("truncated").toBool());
            // The whole of the text is inside the button.
            CHECK(label->property("contentHeight").toDouble() <= item->height());
            CHECK(label->property("contentWidth").toDouble() <= item->width());
        }
    }
}

TEST_CASE("At twice the text size a button grows with its label and still fits", "[kit][button][textscale]") {
    resetKitState();
    auto item = button({{"text", "Enter the colours by hand"}, {"block", true}});
    item->setWidth(320);
    const double before = item->height();

    QMetaObject::invokeMethod(singleton("SystemTextScale"), "setOverride", Q_ARG(qreal, 2.0));
    QQuickItem* label = labelOf(item.get());

    CHECK(item->height() >= before);
    CHECK_FALSE(label->property("truncated").toBool());
    CHECK(label->property("contentHeight").toDouble() <= item->height());
    // Never sideways: the label stays inside the button.
    CHECK(label->property("contentWidth").toDouble() <= item->width());
    resetKitState();
}

TEST_CASE("In every state and both themes a button's label and edge hold their contrast", "[kit][button][contrast]") {
    resetKitState();
    struct Variant {
        const char* name;
        bool hasEdge;
    };

    for (const char* theme : {"light", "dark"}) {
        set("SettingsState", "themeMode", theme);
        for (const bool onOverlay : {false, true}) {
            for (const Variant v : {Variant{"primary", false}, Variant{"secondary", true}, Variant{"ghost", false}}) {
                for (const char* state : {"", "hover", "pressed"}) {
                    auto item = button({{"variant", v.name}, {"previewState", state}, {"onOverlay", onOverlay}});
                    const QColor page = pageBehind(onOverlay);
                    const QColor fill = colour(item->property("fillColor"));
                    const QColor label = colour(item->property("labelColor"));
                    const QColor behind = fill.alphaF() > 0 ? over(fill, page) : page;

                    INFO(theme << (onOverlay ? " over camera" : "") << " " << v.name << " [" << state << "]");
                    CHECK(contrast(label, behind) >= 4.5);

                    // A primary button's fill is itself a boundary against the page.
                    if (QString(v.name) == "primary") {
                        CHECK(contrast(behind, page) >= 3.0);
                    }
                    if (v.hasEdge) {
                        const QColor edge = colour(item->property("edgeColor"));
                        CHECK(contrast(edge, page) >= 3.0);
                    }
                }
            }
        }
    }
    resetKitState();
}

TEST_CASE("A disabled button's label is held at 3:1 even though it is exempt", "[kit][button][contrast]") {
    resetKitState();
    for (const char* theme : {"light", "dark"}) {
        set("SettingsState", "themeMode", theme);
        for (const char* variant : {"primary", "secondary", "ghost"}) {
            auto item = button({{"variant", variant}, {"enabled", false}});
            const QColor fill = colour(item->property("fillColor"));
            const QColor page = pageBehind(false);
            const QColor behind = fill.alphaF() > 0 ? over(fill, page) : page;
            INFO(theme << " " << variant);
            CHECK(contrast(colour(item->property("labelColor")), behind) >= 3.0);
        }
    }
    resetKitState();
}

TEST_CASE("A button's own colours follow the theme live", "[kit][button][theme]") {
    resetKitState();
    auto item = button({{"variant", "secondary"}});
    set("SettingsState", "themeMode", "light");
    const QColor light = colour(item->property("labelColor"));
    set("SettingsState", "themeMode", "dark");
    const QColor dark = colour(item->property("labelColor"));
    CHECK(light != dark);
    resetKitState();
}
