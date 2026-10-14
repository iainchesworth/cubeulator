#include <catch2/catch_test_macros.hpp>

#include "controllers/platform_facts.hpp"

#include <QString>
#include <QStringList>

// What the platform says about itself, as QML reads it. The interface text is
// set in a named family for Japanese and Chinese (Theme.qml), so every
// platform has to name at least one, and a name has to be a name: a blank in
// the list would be matched against the installed families as "".
TEST_CASE("The platform names families for Japanese and Chinese", "[platform]") {
    const cube::app::PlatformFacts facts;

    for (const QStringList& faces : {facts.japaneseFaces(), facts.chineseFaces()}) {
        REQUIRE_FALSE(faces.isEmpty());
        for (const QString& face : faces) {
            INFO("face: '" << face.toStdString() << "'");
            REQUIRE_FALSE(face.trimmed().isEmpty());
        }
    }
}

TEST_CASE("The platform offers at least one graphics backend", "[platform]") {
    const cube::app::PlatformFacts facts;
    REQUIRE_FALSE(facts.graphicsBackends().isEmpty());
}
