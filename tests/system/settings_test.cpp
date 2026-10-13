// Settings: opening and closing the screen, and what the controls on it do.

#include "app.hpp"

#include "app_runtime.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <QColor>
#include <QCoreApplication>
#include <QGuiApplication>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSettings>
#include <QStyleHints>

namespace {

using cube::systest::App;
using cube::systest::Layout;

void open_settings(App& app, const QString& from_control = "launch.settings") {
    app.click(from_control);
    REQUIRE(app.wait_for([&]() { return app.screen() == "settings"; }));
    app.settle();
}

[[nodiscard]] bool theme_is_dark(App& app) {
    return app.theme().property("effectiveDark").toBool();
}

}  // namespace

TEST_CASE("settings open from launch and close back to it", "[system][settings]") {
    const Layout layout = GENERATE(Layout::wide, Layout::narrow);
    DYNAMIC_SECTION("in the " << cube::systest::to_string(layout) << " layout") {
        App& app = App::fresh();
        app.resize(layout == Layout::wide ? cube::systest::kDesktop : cube::systest::kPhone);

        open_settings(app);
        CHECK(app.find_text(qtTrId("common.settings")) != nullptr);
        CHECK(app.find("settings.back") != nullptr);
        CHECK(app.find("settings.theme.0") != nullptr);

        app.click("settings.back");
        REQUIRE(app.wait_for([&]() { return app.screen() == "launch"; }));
        CHECK(app.find("launch.startScan") != nullptr);
    }
}

TEST_CASE("the language button opens the settings too", "[system][settings]") {
    // Only English exists, so "English" on the launch screen is a shortcut to
    // the same screen. (When languages arrive it becomes a picker.)
    App& app = App::fresh();
    open_settings(app, "launch.language");
    CHECK(app.screen() == "settings");
    app.click("settings.back");
    CHECK(app.wait_for([&]() { return app.screen() == "launch"; }));
}

TEST_CASE("the theme switches live, and the choice is kept", "[system][settings][theme]") {
    App& app = App::fresh();
    open_settings(app);

    const QColor light_paper("#F3F2F2");
    const QColor dark_paper("#1A1918");
    CHECK_FALSE(theme_is_dark(app));
    CHECK(app.window().color() == light_paper);

    // Dark.
    app.click("settings.theme.2");
    REQUIRE(app.wait_for([&]() { return theme_is_dark(app); }));
    CHECK(app.window().color() == dark_paper);
    CHECK(app.settings_state().property("themeMode").toString() == "dark");
    // Still on the same screen, which now draws dark.
    CHECK(app.screen() == "settings");
    app.settle();
    const QImage dark = app.grab();
    CHECK(dark.pixelColor(2, 2).lightness() < 80);

    // The choice reached the store the next launch reads. The QML Settings
    // type writes a moment after a change (500 ms), not at once.
    const auto stored = []() {
        QSettings store;
        store.sync();
        return store.value("appearance/themeMode").toString();
    };
    CHECK(app.wait_for([&]() { return stored() == "dark"; }, 5000));

    // Light.
    app.click("settings.theme.1");
    REQUIRE(app.wait_for([&]() { return !theme_is_dark(app); }));
    CHECK(app.window().color() == light_paper);
    app.settle();
    CHECK(app.grab().pixelColor(2, 2).lightness() > 180);
    CHECK(app.wait_for([&]() { return stored() == "light"; }, 5000));

    // System follows the operating system's own setting.
    app.click("settings.theme.0");
    REQUIRE(app.wait_for(
        [&]() { return app.settings_state().property("themeMode").toString() == "system"; }));
    const bool os_dark = QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark;
    CHECK(theme_is_dark(app) == os_dark);
}

TEST_CASE("a second launch comes up with the theme the first one left",
          "[.restart][system][settings][theme]") {
    // Run by its own ctest entry with CUBE_SYSTEM_TEST_THEME=dark in the
    // environment: main() has put that into the store before the app existed,
    // which is what a relaunch looks like to the app.
    REQUIRE(qEnvironmentVariable("CUBE_SYSTEM_TEST_THEME") == "dark");

    // Not fresh(): that resets to the baseline. This is the baseline, as
    // start-up read it.
    App& app = App::launched();
    REQUIRE(app.wait_for([&]() { return theme_is_dark(app); }));
    CHECK(app.settings_state().property("themeMode").toString() == "dark");
    CHECK(app.window().color() == QColor("#1A1918"));
    app.settle();
    CHECK(app.grab().pixelColor(2, 2).lightness() < 80);
}
