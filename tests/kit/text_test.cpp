// KitText: text in the design's type roles. What matters to a screen reader
// is that it is read as what it is (a heading, or plain text), reads its own
// words, and is not a place the keyboard stops.
#include <catch2/catch_test_macros.hpp>

#include "kit_fixture.hpp"

using namespace cube::test;

TEST_CASE("Running text is static text that says its words and is not a focus stop", "[kit][text][a11y]") {
    resetKitState();
    for (const char* role : {"body", "small", "eyebrow", "numeral", "label", "direction", "instruction"}) {
        Hosted host(create("KitText", {{"text", "Hold the cube like this"}, {"role", role}}));
        const Accessibility info = accessibilityOf(host.item());
        INFO(role);
        REQUIRE(info.present);
        CHECK(info.name == "Hold the cube like this");
        CHECK(info.role == QAccessible::StaticText);
        // Qt tells a reader static text is focusable so it can be reached; the
        // keyboard's Tab never lands on it, which is what matters here.
        CHECK_FALSE(host.item()->activeFocusOnTab());
    }
}

TEST_CASE("Titles are headings", "[kit][text][a11y]") {
    resetKitState();
    for (const char* role : {"display", "title", "headline"}) {
        Hosted host(create("KitText", {{"text", "Camera access"}, {"role", role}}));
        const Accessibility info = accessibilityOf(host.item());
        INFO(role);
        CHECK(info.name == "Camera access");
        CHECK(info.role == QAccessible::Heading);
        CHECK_FALSE(host.item()->activeFocusOnTab());
    }
}

TEST_CASE("Text is in the role's size, scaled, and wraps rather than elides", "[kit][text][textscale]") {
    resetKitState();
    auto item = create("KitText", {{"text", "A line long enough to need wrapping in a narrow column"},
                                   {"role", "body"}, {"width", 120}});
    CHECK(item->property("font").value<QFont>().pixelSize() == 16);
    CHECK(item->property("truncated").toBool() == false);
    CHECK(item->property("contentHeight").toDouble() > item->property("font").value<QFont>().pixelSize() * 2);

    invoke(singleton("SystemTextScale"), "setOverride", {2.0});
    CHECK(item->property("font").value<QFont>().pixelSize() == 32);
    CHECK_FALSE(item->property("truncated").toBool());
    resetKitState();
}

TEST_CASE("The eyebrow is capitals by capitalisation, not by typing", "[kit][text]") {
    resetKitState();
    auto item = create("KitText", {{"text", "Scan, solve, in your hands"}, {"role", "eyebrow"}});
    CHECK(item->property("text").toString() == "Scan, solve, in your hands");
    CHECK(item->property("font").value<QFont>().capitalization() == QFont::AllUppercase);

    // Scripts without capitals set it as written.
    set("SettingsState", "language", "he");
    CHECK(item->property("font").value<QFont>().capitalization() == QFont::MixedCase);
    resetKitState();
}
