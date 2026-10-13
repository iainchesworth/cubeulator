// The gate of docs/app-experience-plan.md, E2, held over the whole kit at once
// by walking the developer gallery, which shows every component in every
// state: every interactive component is reachable by keyboard, has a name and
// a role, meets its target size and holds its contrast in both themes; labels
// 40% longer, and text at twice the size, truncate nothing; and the layout
// holds in a right-to-left language.
#include <catch2/catch_test_macros.hpp>

#include "kit_fixture.hpp"

#include <QDir>
#include <QImage>
#include <QQuickItem>
#include <QSet>

#include <algorithm>
#include <cmath>

using namespace cube::test;

namespace {

// Every object named "specimen/...": the items, which are in the visual tree,
// and the popups, which are QObject children of whatever declared them.
QList<QObject*> specimens(QQuickItem* gallery) {
    QList<QObject*> found;
    QSet<QObject*> seen;
    const auto note = [&](QObject* object) {
        if (object->objectName().startsWith("specimen/") && !seen.contains(object)) {
            seen.insert(object);
            found.push_back(object);
        }
    };
    for (QQuickItem* item : allItems(gallery)) {
        note(item);
        for (QObject* object : item->children()) {
            note(object);
        }
    }
    return found;
}

bool isInteractive(QObject* object) {
    auto* item = qobject_cast<QQuickItem*>(object);
    if (item == nullptr) {
        return false;
    }
    const Accessibility info = accessibilityOf(item);
    return info.present && (info.role == QAccessible::PushButton || info.role == QAccessible::CheckBox ||
                            info.role == QAccessible::RadioButton);
}

// Every item that is itself a stop or a choice within a group.
QList<QQuickItem*> interactiveItems(QQuickItem* gallery) {
    QList<QQuickItem*> found;
    for (QQuickItem* item : allItems(gallery)) {
        if (!item->isVisible()) {
            continue;
        }
        const Accessibility info = accessibilityOf(item);
        if (!info.present) {
            continue;
        }
        const bool interactiveRole = info.role == QAccessible::PushButton || info.role == QAccessible::CheckBox ||
                                     info.role == QAccessible::RadioButton;
        if (interactiveRole) {
            found.push_back(item);
        }
    }
    return found;
}

// The text items under a component.
QList<QQuickItem*> textsUnder(QQuickItem* root) {
    QList<QQuickItem*> found;
    for (QQuickItem* item : allItems(root)) {
        if (item->metaObject()->indexOfProperty("contentWidth") >= 0 &&
            item->metaObject()->indexOfProperty("truncated") >= 0) {
            found.push_back(item);
        }
    }
    return found;
}

struct GalleryFixture {
    Hosted host;
    QQuickItem* gallery;

    GalleryFixture() : host(create("KitGallery"), QSize(900, 640)), gallery(host.item()) {}
};

}  // namespace

TEST_CASE("The gallery shows every kit component", "[kit][gallery]") {
    resetKitState();
    GalleryFixture fixture;
    const auto shown = specimens(fixture.gallery);

    QSet<QString> kinds;
    for (QObject* object : shown) {
        kinds.insert(object->objectName().split('/').value(1));
    }
    // The components system.md lists under "Components".
    for (const char* kind : {"button", "iconbutton", "switch", "choice", "listrow", "topbar", "dialog", "sheet",
                             "notice", "card", "progress", "sticker", "swatch", "sidechip", "colourpicker",
                             "statuspanel", "instruction", "stagerow", "keyname", "emptystate"}) {
        INFO(kind);
        CHECK(kinds.contains(kind));
    }
}

TEST_CASE("Every component's colours hold the contrast tokens.json sets, in both themes", "[kit][gallery][contrast]") {
    resetKitState();
    GalleryFixture fixture;
    const auto shown = specimens(fixture.gallery);

    int checked = 0;
    for (const char* theme : {"light", "dark"}) {
        set("SettingsState", "themeMode", theme);
        for (QObject* object : shown) {
            for (const ContrastPair& pair : contrastPairsOf(object)) {
                const double ratio = contrast(pair.foreground, pair.background);
                INFO(theme << " " << object->objectName().toStdString() << " / " << pair.name.toStdString() << ": "
                           << ratio << " against a target of " << pair.target);
                CHECK(ratio >= pair.target);
                ++checked;
            }
        }
    }
    // Not an empty pass: components do declare what they draw.
    CHECK(checked > 300);
    resetKitState();
}

TEST_CASE("Every interactive component has a name, a role and a target of at least 48", "[kit][gallery][a11y][target]") {
    resetKitState();
    GalleryFixture fixture;
    const auto controls = interactiveItems(fixture.gallery);
    REQUIRE(controls.size() > 40);

    for (QQuickItem* control : controls) {
        const Accessibility info = accessibilityOf(control);
        INFO(control->objectName().toStdString());
        CHECK_FALSE(info.name.isEmpty());
        CHECK(control->width() >= 48 - 0.01);
        CHECK(control->height() >= 48 - 0.01);
    }
}

TEST_CASE("Tab reaches every enabled control in the gallery", "[kit][gallery][keyboard]") {
    resetKitState();
    GalleryFixture fixture;
    const auto controls = interactiveItems(fixture.gallery);

    QSet<QQuickItem*> visited;
    QQuickItem* first = nullptr;
    for (int i = 0; i < 700; ++i) {
        QQuickItem* focused = fixture.host.tab();
        if (focused == nullptr) {
            break;
        }
        if (first == nullptr) {
            first = focused;
        } else if (focused == first) {
            break;
        }
        visited.insert(focused);
    }
    REQUIRE(visited.size() > 40);

    // Only controls take the keyboard: text and pictures are never a stop.
    for (QQuickItem* stop : visited) {
        const Accessibility info = accessibilityOf(stop);
        INFO(stop->objectName().toStdString());
        CHECK((info.role == QAccessible::PushButton || info.role == QAccessible::CheckBox ||
               info.role == QAccessible::RadioButton));
    }

    for (QQuickItem* control : controls) {
        const Accessibility info = accessibilityOf(control);
        if (info.state.disabled || !info.state.focusable) {
            continue;
        }
        // A group is one stop: its chosen option. The rest of the group is
        // reached by arrow keys.
        const bool inGroup = control->objectName().startsWith("option") || control->objectName().startsWith("colour/");
        if (inGroup) {
            if (!info.state.checked) {
                continue;
            }
            // A picker with nothing chosen is entered at its first colour.
        }
        INFO(control->objectName().toStdString());
        CHECK(visited.contains(control));
    }
}

TEST_CASE("At twice the text size and labels 40% longer, no text is cut off or overflows its control",
          "[kit][gallery][textfit]") {
    resetKitState();
    invoke(singleton("SystemTextScale"), "setOverride", {2.0});
    GalleryFixture fixture;
    fixture.host.settle();
    // Labels 40% longer, as the gallery's own switch does it (the switch itself
    // can be off the bottom of a window this small at twice the text size).
    REQUIRE(fixture.gallery->setProperty("longLabels", true));
    fixture.host.settle();

    int texts = 0;
    for (QObject* object : specimens(fixture.gallery)) {
        auto* component = qobject_cast<QQuickItem*>(object);
        if (component == nullptr) {
            continue;
        }
        for (QQuickItem* text : textsUnder(component)) {
            if (!text->isVisible() || text->width() <= 0) {
                continue;
            }
            ++texts;
            INFO(component->objectName().toStdString());
            CHECK_FALSE(text->property("truncated").toBool());
            // The words are within the control that holds them, to a pixel.
            const QRectF inComponent = component->mapRectFromItem(text, QRectF(0, 0, text->width(), text->height()));
            CHECK(inComponent.right() <= component->width() + 1.5);
            CHECK(inComponent.bottom() <= component->height() + 1.5);
            CHECK(inComponent.left() >= -1.5);
        }
    }
    CHECK(texts > 80);
    resetKitState();
}

TEST_CASE("The kit holds in a right-to-left language, in both themes", "[kit][gallery][rtl]") {
    resetKitState();
    set("SettingsState", "language", "he");
    GalleryFixture fixture;
    REQUIRE(get("Theme", "rtl").toBool());

    for (const char* theme : {"light", "dark"}) {
        set("SettingsState", "themeMode", theme);
        for (QObject* object : specimens(fixture.gallery)) {
            for (const ContrastPair& pair : contrastPairsOf(object)) {
                INFO(theme << " " << object->objectName().toStdString() << " / " << pair.name.toStdString());
                CHECK(contrast(pair.foreground, pair.background) >= pair.target);
            }
        }
    }
    resetKitState();
}

TEST_CASE("The cube is never mirrored, though the layout is", "[kit][gallery][rtl][mirroring]") {
    resetKitState();
    set("SettingsState", "language", "he");
    GalleryFixture fixture;

    // The page itself follows the language...
    QQuickItem* aButton = find(fixture.gallery, "specimen/button/primary/rest");
    REQUIRE(aButton != nullptr);
    CHECK(layoutMirrored(aButton));

    // ...but the sticker, the side chip, the instruction block, the colour
    // picker and a turn's direction icon set it off on themselves.
    for (const char* name : {"specimen/sticker/red", "specimen/sidechip/read", "specimen/instruction/leading",
                             "specimen/colourpicker/red", "specimen/swatch"}) {
        QQuickItem* item = find(fixture.gallery, name);
        INFO(name);
        REQUIRE(item != nullptr);
        CHECK_FALSE(layoutMirrored(item));
    }
    QQuickItem* arrow = find(fixture.gallery, "directionIcon");
    REQUIRE(arrow != nullptr);
    CHECK_FALSE(layoutMirrored(arrow));
    resetKitState();
}

// ---- A picture of the gallery, for people ------------------------------------------
//
// Hidden from the normal run. Renders the gallery with the software scene
// graph, a window's height at a time, in both themes and both directions and at
// twice the text size, into CUBE_KIT_SCREENSHOT_DIR:
//
//   CUBE_KIT_SCREENSHOT_DIR=some/folder cube_kit_tests "[.screenshots]"
//
// What E0a's screenshot tour does for whole screens, for the parts.
TEST_CASE("Render the gallery to pictures", "[.screenshots]") {
    const QString directory = qEnvironmentVariable("CUBE_KIT_SCREENSHOT_DIR");
    if (directory.isEmpty()) {
        SKIP("set CUBE_KIT_SCREENSHOT_DIR to a folder");
    }
    QDir().mkpath(directory);

    struct Variant {
        const char* name;
        const char* theme;
        const char* language;
        double textScale;
    };
    for (const Variant v : {Variant{"light-ltr", "light", "en", 1.0}, Variant{"dark-ltr", "dark", "en", 1.0},
                            Variant{"light-rtl", "light", "he", 1.0}, Variant{"light-2x", "light", "en", 2.0}}) {
        resetKitState();
        set("SettingsState", "themeMode", v.theme);
        set("SettingsState", "language", v.language);
        invoke(singleton("SystemTextScale"), "setOverride", {v.textScale});

        GalleryFixture fixture;
        fixture.host.window()->resize(900, 700);
        fixture.host.settle();

        QQuickItem* flick = nullptr;
        for (QQuickItem* item : allItems(fixture.gallery)) {
            if (item->inherits("QQuickFlickable")) {
                flick = item;
                break;
            }
        }
        REQUIRE(flick != nullptr);
        const double content = flick->property("contentHeight").toDouble();
        const double page = flick->height();
        const int steps = std::max(1, static_cast<int>(std::ceil(content / page)));
        for (int i = 0; i < steps; ++i) {
            flick->setProperty("contentY", std::min(i * page, std::max(0.0, content - page)));
            fixture.host.settle();
            const QImage image = fixture.host.window()->grabWindow();
            REQUIRE_FALSE(image.isNull());
            const QString file = QString("%1/%2-%3.png").arg(directory, v.name).arg(i, 2, 10, QChar('0'));
            REQUIRE(image.save(file));
        }
    }
    resetKitState();
}
