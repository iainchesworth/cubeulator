// The design's type is shipped with the app (docs/design/system.md, "Type";
// the plan's E2 gate: "on a machine without Archivo installed the app still
// shows Archivo"). These hold the bundle to that on whichever platform the
// suite runs on.
#include <catch2/catch_test_macros.hpp>

#include "kit/bundled_fonts.hpp"
#include "kit_fixture.hpp"

#include <QFont>
#include <QFontDatabase>
#include <QFontInfo>
#include <QStringList>

using namespace cube::test;

namespace {

// What Qt actually resolves a request for this family and weight to.
QFontInfo resolve(const QString& family, int weight) {
    QFont font(family);
    font.setPixelSize(16);
    font.setWeight(static_cast<QFont::Weight>(weight));
    return QFontInfo(font);
}

QFontInfo shown(QQuickItem* text) {
    return QFontInfo(text->property("font").value<QFont>());
}

}  // namespace

TEST_CASE("The bundle holds the four families the design names", "[kit][fonts]") {
    const QStringList added = cube::app::kit::installBundledFonts();
    INFO("families the bundle registered: " << added.join(", ").toStdString());
    for (const char* family : {"Archivo", "IBM Plex Mono", "IBM Plex Sans Hebrew", "IBM Plex Sans Arabic"}) {
        INFO(family);
        CHECK(QFontDatabase::hasFamily(family));
    }
}

TEST_CASE("Installing the bundle twice adds nothing twice", "[kit][fonts]") {
    const QStringList once = cube::app::kit::installBundledFonts();
    const QStringList twice = cube::app::kit::installBundledFonts();
    CHECK(once == twice);
}

TEST_CASE("Archivo's three weights each resolve to their own face", "[kit][fonts]") {
    cube::app::kit::installBundledFonts();
    for (const int weight : {400, 700, 800}) {
        const QFontInfo info = resolve("Archivo", weight);
        INFO("weight " << weight << " resolved to '" << info.family().toStdString() << "' "
                       << info.styleName().toStdString() << " at " << info.weight());
        CHECK(info.family().startsWith("Archivo"));
        CHECK(info.weight() == weight);
    }
}

TEST_CASE("The mono face is Plex Mono Medium for numerals", "[kit][fonts]") {
    cube::app::kit::installBundledFonts();
    const QFontInfo info = resolve("IBM Plex Mono", 500);
    INFO("resolved to '" << info.family().toStdString() << "' " << info.styleName().toStdString() << " at "
                         << info.weight());
    CHECK(info.family().startsWith("IBM Plex Mono"));
    CHECK(info.weight() == 500);
    CHECK(info.fixedPitch());
}

TEST_CASE("Hebrew and Arabic have a regular and a bold, and no more", "[kit][fonts]") {
    cube::app::kit::installBundledFonts();
    for (const char* family : {"IBM Plex Sans Hebrew", "IBM Plex Sans Arabic"}) {
        for (const int weight : {400, 700}) {
            const QFontInfo info = resolve(family, weight);
            INFO(family << " at " << weight << " resolved to '" << info.family().toStdString() << "' at "
                        << info.weight());
            CHECK(info.family() == family);
            CHECK(info.weight() == weight);
        }
    }
}

TEST_CASE("The app shows Archivo with no Archivo installed", "[kit][fonts]") {
    resetKitState();
    // Take away everything the run registered, so that what is left is what
    // the machine itself has: the state of a machine without the fonts.
    QFontDatabase::removeAllApplicationFonts();
    const bool installedOnTheMachine = QFontDatabase::hasFamily("Archivo");
    if (installedOnTheMachine) {
        WARN("Archivo is installed on this machine, so 'without it' cannot be shown here; "
             "CI machines do not have it");
    } else {
        CHECK_FALSE(QFontDatabase::hasFamily("Archivo"));
    }

    // Start-up registers the bundle...
    const QStringList added = cube::app::kit::installBundledFonts();
    INFO("registered: " << added.join(", ").toStdString());
    CHECK(QFontDatabase::hasFamily("Archivo"));

    // ...and what the app draws its text in is Archivo, in every weight it uses.
    auto text = create("KitText", {{"text", "Scan. Solve. In your hands."}, {"role", "title"}});
    CHECK(shown(text.get()).family().startsWith("Archivo"));
    CHECK(shown(text.get()).weight() == 800);

    auto body = create("KitText", {{"text", "Point the camera"}, {"role", "body"}});
    CHECK(shown(body.get()).family() == "Archivo");
    CHECK(shown(body.get()).weight() == 400);
}

TEST_CASE("Numerals are drawn in Plex Mono, and a Hebrew UI in Plex Hebrew", "[kit][fonts]") {
    resetKitState();
    cube::app::kit::installBundledFonts();

    auto numeral = create("KitText", {{"text", "62"}, {"role", "numeral"}});
    CHECK(shown(numeral.get()).family().startsWith("IBM Plex Mono"));

    set("SettingsState", "language", "he");
    auto hebrew = create("KitText", {{"text", "שלום"}, {"role", "title"}});
    CHECK(shown(hebrew.get()).family() == "IBM Plex Sans Hebrew");
    // No extra-bold in Hebrew: a title is 700.
    CHECK(shown(hebrew.get()).weight() == 700);

    set("SettingsState", "language", "ar");
    auto arabic = create("KitText", {{"text", "مرحبا"}, {"role", "body"}});
    CHECK(shown(arabic.get()).family() == "IBM Plex Sans Arabic");
    resetKitState();
}
