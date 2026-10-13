// The desktop shell (E4a): what DesktopShell.qml adds to the app on a desktop.
//
// The shortcuts need key chords, which the harness's key() does not send; they
// were pressed by hand and are listed in docs/packaging.md. What can be driven
// here is the placeholder About, which is the way to About on Windows and
// Linux until E3b: reached from Settings, it names the app and its version and
// closes again.

#include "app.hpp"

#include <cube/version.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <QQuickItem>
#include <QString>

namespace {

using cube::systest::App;
using cube::systest::Layout;

}  // namespace

TEST_CASE("About opens from Settings, names the app and its version, and closes",
          "[system][desktop]") {
    const Layout layout = GENERATE(Layout::wide, Layout::narrow);
    DYNAMIC_SECTION("in the " << cube::systest::to_string(layout) << " layout") {
        App& app = App::fresh();
        app.resize(layout == Layout::wide ? cube::systest::kDesktop : cube::systest::kPhone);

        app.click("launch.settings");
        REQUIRE(app.wait_for([&]() { return app.screen() == "settings"; }));
        app.settle();
        CHECK(app.find("about.placeholder") == nullptr);  // not up until asked for

        app.click("settings.about");
        REQUIRE(app.wait_for([&]() { return app.find("about.placeholder") != nullptr; }));
        app.settle();

        // The version is the build's, not a string the QML makes up.
        const QString version =
            qtTrId("about.version")
                .arg(QString::fromUtf8(cube::version_string.data(),
                                       static_cast<qsizetype>(cube::version_string.size())));
        CHECK(app.find_text("Cubeulator") != nullptr);
        CHECK(app.find_text(version) != nullptr);

        app.click("about.close");
        REQUIRE(app.wait_for([&]() { return app.find("about.placeholder") == nullptr; }));
        CHECK(app.screen() == "settings");  // closing it leaves you where you were
    }
}
