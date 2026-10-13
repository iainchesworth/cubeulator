// Theme.qml is a hand port of docs/design/tokens.json. These tests are what
// keeps the two from drifting: every colour in both themes, the overlay and
// cube colours, spacing, targets, the type scale, motion and the size
// classes are read back out of the running Theme and compared with the JSON,
// and every contrast pairing the design gates on is recomputed from the
// values Theme actually answers (the arithmetic of
// docs/design/tools/check_tokens.py).
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "kit_fixture.hpp"

#include <QFile>
#include <QJSValue>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaObject>
#include <QVariantMap>

using namespace cube::test;

namespace {

QJsonObject loadTokens() {
    QFile file(QStringLiteral(CUBE_KIT_TOKENS_JSON));
    REQUIRE(file.open(QIODevice::ReadOnly));
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    REQUIRE(document.isObject());
    return document.object();
}

// A token's name in tokens.json is its property name on Theme, except the one
// that cannot be: see the "onAccent" test below.
QString propertyFor(const QString& token) {
    return token == "onAccent" ? QStringLiteral("accentOn") : token;
}

QString hex(const QColor& c) {
    return c.name(QColor::HexRgb).toUpper();
}

QVariantMap toMap(const QVariant& v) {
    if (v.metaType() == QMetaType::fromType<QJSValue>()) {
        return v.value<QJSValue>().toVariant().toMap();
    }
    return v.toMap();
}

QVariant themeValue(const char* property) {
    return get("Theme", property);
}

QVariant call(const char* function, const QVariant& argument) {
    return invoke(singleton("Theme"), function, {argument});
}

QObject* nested(const char* property) {
    auto* object = themeValue(property).value<QObject*>();
    REQUIRE(object != nullptr);
    return object;
}

// Names the kit uses where tokens.json has a dotted key: "target.min" is
// Theme.targetMin, "space.4" is Theme.space4 and so on.
QString camel(const QString& group, const QString& key) {
    return group + key.at(0).toUpper() + key.mid(1);
}

}  // namespace

TEST_CASE("Theme answers every colour token of tokens.json in both themes", "[kit][tokens]") {
    resetKitState();
    const QJsonObject colours = loadTokens().value("color").toObject();
    REQUIRE(colours.size() == 20);

    for (const char* theme : {"light", "dark"}) {
        set("SettingsState", "themeMode", theme);
        for (auto it = colours.begin(); it != colours.end(); ++it) {
            const QString expected = it.value().toObject().value(theme).toString().toUpper();
            const QString actual = hex(colour(themeValue(propertyFor(it.key()).toLatin1().constData())));
            INFO(theme << " " << it.key().toStdString());
            CHECK(actual == expected);
        }
    }
    resetKitState();
}

TEST_CASE("Theme answers the camera overlay tokens of tokens.json", "[kit][tokens]") {
    resetKitState();
    const QJsonObject overlay = loadTokens().value("overlay").toObject();
    QObject* theme = nested("overlay");

    const auto colourOf = [&](const char* property) { return hex(colour(theme->property(property))); };

    CHECK(colourOf("panelColor") == overlay.value("panel").toObject().value("value").toString().toUpper());
    CHECK(theme->property("panelAlpha").toDouble() == overlay.value("panel").toObject().value("alpha").toDouble());
    for (const char* name : {"ink", "inkSecondary", "ok", "caution", "outline"}) {
        INFO(name);
        CHECK(colourOf(name) == overlay.value(name).toObject().value("value").toString().toUpper());
    }
    CHECK(colourOf("scrimColor") == overlay.value("scrim").toObject().value("value").toString().toUpper());
    CHECK(theme->property("scrimAlpha").toDouble() == overlay.value("scrim").toObject().value("alpha").toDouble());

    // The same in both themes: the picture behind is not themed.
    set("SettingsState", "themeMode", "dark");
    CHECK(colourOf("ink") == overlay.value("ink").toObject().value("value").toString().toUpper());
    resetKitState();
}

TEST_CASE("Theme answers the six cube colours and their marks", "[kit][tokens]") {
    resetKitState();
    const QJsonObject cube = loadTokens().value("cube").toObject();
    QObject* colours = nested("cube");

    int seen = 0;
    for (auto it = cube.begin(); it != cube.end(); ++it) {
        if (it.key().startsWith('$')) {
            continue;
        }
        ++seen;
        const QJsonObject spec = it.value().toObject();
        INFO(it.key().toStdString());
        CHECK(hex(colour(colours->property(it.key().toLatin1().constData()))) == spec.value("value").toString().toUpper());
        CHECK(call("cubeMark", it.key()).toString() == spec.value("mark").toString());
        CHECK(hex(colour(call("cubeMarkInk", it.key()))) == spec.value("markInk").toString().toUpper());
    }
    CHECK(seen == 6);
}

TEST_CASE("Theme keeps the old cube-side names pointing at the new cube colours", "[kit][tokens][legacy]") {
    resetKitState();
    QObject* colours = nested("cube");
    // U white, D yellow, F green, B blue, L orange, R red: the app's own mapping.
    CHECK(colour(themeValue("faceU")) == colour(colours->property("white")));
    CHECK(colour(themeValue("faceD")) == colour(colours->property("yellow")));
    CHECK(colour(themeValue("faceF")) == colour(colours->property("green")));
    CHECK(colour(themeValue("faceB")) == colour(colours->property("blue")));
    CHECK(colour(themeValue("faceL")) == colour(colours->property("orange")));
    CHECK(colour(themeValue("faceR")) == colour(colours->property("red")));
}

TEST_CASE("Theme answers spacing, rules and target sizes", "[kit][tokens]") {
    resetKitState();
    const QJsonObject tokens = loadTokens();

    const QJsonObject space = tokens.value("space").toObject();
    REQUIRE(space.size() == 8);
    for (auto it = space.begin(); it != space.end(); ++it) {
        INFO("space " << it.key().toStdString());
        CHECK(themeValue(("space" + it.key()).toLatin1().constData()).toDouble() == it.value().toDouble());
    }
    for (const char* group : {"target", "border"}) {
        const QJsonObject values = tokens.value(group).toObject();
        for (auto it = values.begin(); it != values.end(); ++it) {
            const QString name = camel(group, it.key() == "handsFull" ? QStringLiteral("handsFull") : it.key());
            INFO(name.toStdString());
            CHECK(themeValue(name.toLatin1().constData()).toDouble() == it.value().toDouble());
        }
    }
    CHECK(themeValue("radius").toDouble() == tokens.value("radius").toObject().value("none").toDouble());
}

TEST_CASE("Theme answers the type scale", "[kit][tokens]") {
    resetKitState();
    const QJsonObject roles = loadTokens().value("type").toObject();
    const QVariantMap actual = toMap(themeValue("typeRoles"));

    int seen = 0;
    for (auto it = roles.begin(); it != roles.end(); ++it) {
        if (it.key().startsWith('$')) {
            continue;
        }
        ++seen;
        const QJsonObject spec = it.value().toObject();
        REQUIRE(actual.contains(it.key()));
        const QVariantMap role = actual.value(it.key()).toMap();
        INFO("role " << it.key().toStdString());
        for (auto field = spec.begin(); field != spec.end(); ++field) {
            INFO("field " << field.key().toStdString());
            REQUIRE(role.contains(field.key()));
            if (field.value().isString()) {
                CHECK(role.value(field.key()).toString() == field.value().toString());
            } else if (field.value().isBool()) {
                CHECK(role.value(field.key()).toBool() == field.value().toBool());
            } else {
                CHECK(role.value(field.key()).toDouble() == field.value().toDouble());
            }
        }
    }
    CHECK(seen == actual.size());
}

TEST_CASE("Type sizes are in rem and follow the text scale", "[kit][tokens][textscale]") {
    resetKitState();
    // 1 rem is 16 px at scale 1.
    CHECK(call("typeSize", "body").toDouble() == 16.0);
    CHECK(call("typeSize", "eyebrow").toDouble() == 13.0);

    invoke(singleton("SystemTextScale"), "setOverride", {2.0});
    CHECK(themeValue("textScale").toDouble() == 2.0);
    CHECK(themeValue("rem").toDouble() == 32.0);
    CHECK(call("typeSize", "body").toDouble() == 32.0);
    resetKitState();
}

TEST_CASE("Text scale is the system's times the app's, clamped to 0.85 - 2.0", "[kit][textscale]") {
    resetKitState();
    QObject* system = singleton("SystemTextScale");
    const auto scaleOf = [&](qreal os, const char* app) {
        invoke(system, "setOverride", {os});
        set("SettingsState", "textSize", app);
        return themeValue("textScale").toDouble();
    };

    CHECK(scaleOf(1.0, "system") == 1.0);
    CHECK(scaleOf(1.0, "large") == 1.25);
    CHECK(scaleOf(1.0, "larger") == 1.5);
    CHECK(scaleOf(1.5, "large") == 1.875);
    CHECK(scaleOf(2.25, "larger") == 2.0);   // clamped above
    CHECK(scaleOf(0.5, "system") == 0.85);   // clamped below
    // A value the store should not hold is read as the default.
    CHECK(scaleOf(1.0, "enormous") == 1.0);
    resetKitState();
}

TEST_CASE("Reduced motion follows the system unless the person chose", "[kit][motion]") {
    resetKitState();
    QObject* system = singleton("SystemMotion");

    invoke(system, "setOverride", {true});
    set("SettingsState", "motion", "system");
    CHECK(themeValue("reducedMotion").toBool());
    set("SettingsState", "motion", "full");
    CHECK_FALSE(themeValue("reducedMotion").toBool());

    invoke(system, "setOverride", {false});
    set("SettingsState", "motion", "system");
    CHECK_FALSE(themeValue("reducedMotion").toBool());
    set("SettingsState", "motion", "reduced");
    CHECK(themeValue("reducedMotion").toBool());
    resetKitState();
}

TEST_CASE("Motion durations are tokens.json's, full and reduced", "[kit][tokens][motion]") {
    resetKitState();
    const QJsonObject motion = loadTokens().value("motion").toObject();

    for (const char* setting : {"full", "reduced"}) {
        set("SettingsState", "motion", setting);
        for (auto it = motion.begin(); it != motion.end(); ++it) {
            if (it.key().startsWith('$')) {
                continue;
            }
            const QString property = camel("motion", it.key());
            const int expected = it.value().toObject().value(setting).toInt();
            INFO(setting << " " << property.toStdString());
            CHECK(themeValue(property.toLatin1().constData()).toInt() == expected);
        }
    }
    resetKitState();
}

TEST_CASE("Size classes change at tokens.json's widths and heights", "[kit][tokens][sizeclass]") {
    resetKitState();
    const QJsonObject classes = loadTokens().value("sizeClass").toObject();
    const QJsonObject width = classes.value("width").toObject();
    const QJsonObject height = classes.value("height").toObject();

    const auto widthClassAt = [](int w) {
        set("Theme", "windowWidth", w);
        return themeValue("sizeClass").toString();
    };
    const auto heightClassAt = [](int h) {
        set("Theme", "windowHeight", h);
        return themeValue("heightClass").toString();
    };

    for (auto it = width.begin(); it != width.end(); ++it) {
        const QJsonArray range = it.value().toArray();
        const int low = range.at(0).toInt();
        INFO("width class " << it.key().toStdString());
        CHECK(widthClassAt(low) == it.key());
        if (!range.at(1).isNull()) {
            CHECK(widthClassAt(range.at(1).toInt()) == it.key());
        } else {
            CHECK(widthClassAt(4000) == it.key());
        }
    }
    for (auto it = height.begin(); it != height.end(); ++it) {
        const QJsonArray range = it.value().toArray();
        INFO("height class " << it.key().toStdString());
        CHECK(heightClassAt(range.at(0).toInt()) == it.key());
        if (!range.at(1).isNull()) {
            CHECK(heightClassAt(range.at(1).toInt()) == it.key());
        } else {
            CHECK(heightClassAt(4000) == it.key());
        }
    }
    resetKitState();
}

TEST_CASE("Page margins are 24 compact (16 under 360), 32 medium, 40 expanded", "[kit][sizeclass]") {
    resetKitState();
    set("Theme", "windowWidth", 400);
    CHECK(themeValue("pageMargin").toDouble() == 24);
    set("Theme", "windowWidth", 340);
    CHECK(themeValue("pageMargin").toDouble() == 16);
    set("Theme", "windowWidth", 700);
    CHECK(themeValue("pageMargin").toDouble() == 32);
    set("Theme", "windowWidth", 1000);
    CHECK(themeValue("pageMargin").toDouble() == 40);
    resetKitState();
}

TEST_CASE("Every contrast pairing tokens.json gates on holds, from the running Theme", "[kit][tokens][contrast]") {
    resetKitState();
    const QJsonObject tokens = loadTokens();
    const QJsonArray pairs = tokens.value("pairs").toArray();
    REQUIRE(pairs.size() > 20);

    for (const char* theme : {"light", "dark"}) {
        set("SettingsState", "themeMode", theme);
        for (const QJsonValue& entry : pairs) {
            const QJsonArray pair = entry.toArray();
            const QString foreground = pair.at(0).toString();
            const QString background = pair.at(1).toString();
            const double target = pair.at(2).toDouble();
            const double ratio = contrast(colour(themeValue(propertyFor(foreground).toLatin1().constData())),
                                          colour(themeValue(propertyFor(background).toLatin1().constData())));
            INFO(theme << ": " << foreground.toStdString() << " on " << background.toStdString() << " is " << ratio
                       << ", needs " << target << " (" << pair.at(3).toString().toStdString() << ")");
            CHECK(ratio >= target);
        }
    }
    resetKitState();
}

TEST_CASE("Over the camera, text and edges hold their contrast against the worst picture", "[kit][tokens][contrast]") {
    resetKitState();
    QObject* overlay = nested("overlay");
    // A pure white picture is the worst the camera can put behind a panel.
    const QColor worst = over(colour(overlay->property("panel")), Qt::white);

    const auto ratio = [&](const char* property) { return contrast(colour(overlay->property(property)), worst); };
    CHECK(ratio("ink") >= 4.5);
    CHECK(ratio("inkSecondary") >= 4.5);
    CHECK(ratio("ok") >= 4.5);
    CHECK(ratio("caution") >= 4.5);
    CHECK(ratio("outline") >= 3.0);
}

TEST_CASE("Each cube colour's mark holds 3:1 against its sticker", "[kit][tokens][contrast]") {
    resetKitState();
    QObject* colours = nested("cube");
    for (const QString name : {"yellow", "orange", "red", "green", "blue"}) {
        const QColor sticker = colour(colours->property(name.toLatin1().constData()));
        const QColor mark = colour(call("cubeMarkInk", name));
        INFO(name.toStdString());
        CHECK(contrast(mark, sticker) >= 3.0);
    }
}

TEST_CASE("The old Theme names still answer", "[kit][legacy]") {
    resetKitState();
    // Still read by the scan and solve screens, which this work must not edit.
    for (const char* name : {"ink", "paper", "surface", "divider", "accent", "accent100", "accent600", "accent700",
                             "detected", "warning", "neutral200", "neutral400", "neutral500", "neutral600",
                             "neutral900", "faceU", "faceD", "faceF", "faceB", "faceL", "faceR", "fontHeading",
                             "fontMono", "space1", "space2", "space3", "space4", "space6", "space8",
                             "wideBreakpoint", "effectiveDark"}) {
        INFO(name);
        CHECK(themeValue(name).isValid());
    }
    CHECK(themeValue("fontHeading").toString() == "Archivo");
    CHECK(themeValue("fontMono").toString() == "IBM Plex Mono");
    CHECK(themeValue("wideBreakpoint").toInt() == 720);

    const QColor muted = colour(call("inkMuted", 0.5));
    CHECK(muted.alphaF() == Catch::Approx(0.5).margin(0.001));
    CHECK(muted.rgb() == colour(themeValue("ink")).rgb());
}

TEST_CASE("Insets are leading and trailing, so they mirror", "[kit][insets]") {
    resetKitState();
    invoke(singleton("SystemInsets"), "setOverride", {10, 20, 30, 40});
    QObject* insets = nested("insets");
    CHECK(insets->property("top").toDouble() == 10);
    CHECK(insets->property("bottom").toDouble() == 40);
    CHECK(insets->property("leading").toDouble() == 20);
    CHECK(insets->property("trailing").toDouble() == 30);

    set("SettingsState", "language", "he");
    CHECK(themeValue("rtl").toBool());
    CHECK(insets->property("leading").toDouble() == 30);
    CHECK(insets->property("trailing").toDouble() == 20);
    resetKitState();
}

TEST_CASE("The language chooses the script, the direction and the type's families", "[kit][language]") {
    resetKitState();
    const auto scriptFor = [](const char* language) {
        set("SettingsState", "language", language);
        return themeValue("script").toString();
    };

    CHECK(scriptFor("en") == "latin");
    CHECK(scriptFor("de") == "latin");
    CHECK(scriptFor("he") == "hebrew");
    CHECK(themeValue("rtl").toBool());
    CHECK(scriptFor("ar") == "arabic");
    CHECK(themeValue("rtl").toBool());
    CHECK(scriptFor("ja") == "ja");
    CHECK_FALSE(themeValue("rtl").toBool());
    CHECK(scriptFor("zh-CN") == "zh");
    CHECK_FALSE(themeValue("rtl").toBool());

    // One family for the UI in each language, by script.
    CHECK(themeValue("headingFamily").toString() == "Archivo");
    set("SettingsState", "language", "he");
    CHECK(themeValue("headingFamily").toString() == "Noto Sans Hebrew");
    set("SettingsState", "language", "ar");
    CHECK(themeValue("headingFamily").toString() == "Noto Sans Arabic");
    // Yiddish is written in Hebrew script and runs right to left.
    set("SettingsState", "language", "yi");
    CHECK(themeValue("script").toString() == "hebrew");
    CHECK(themeValue("rtl").toBool());
    CHECK(themeValue("headingFamily").toString() == "Noto Sans Hebrew");
    resetKitState();
}

TEST_CASE("The label colour of a primary button is Theme.accentOn, because onAccent cannot be a property",
          "[kit][tokens]") {
    // tokens.json calls the token onAccent. A QML property named "on" and a
    // capital is taken for a signal handler and reads as undefined (6.8.3), so
    // Theme calls it accentOn and keeps the design's name only in its token
    // table. If a later Qt makes onAccent readable this test says so and the
    // alias can go.
    resetKitState();
    auto item = createFromSource(R"(
        import QtQuick
        import Cubeulator
        Item {
            property color viaName: Theme.accentOn
            property color viaTable: Theme.colour("onAccent", false)
            property var literal: Theme.onAccent
        }
    )");
    CHECK(hex(colour(item->property("viaName"))) == "#FFFFFF");
    CHECK(hex(colour(item->property("viaTable"))) == "#FFFFFF");
    CHECK_FALSE(item->property("literal").isValid());   // undefined: the reason for the alias
    set("SettingsState", "themeMode", "dark");
    CHECK(hex(colour(item->property("viaName"))) == "#1A1918");
    resetKitState();
}
