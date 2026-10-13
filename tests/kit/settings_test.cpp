// The settings the design adds (docs/design/strings.json, settings.*; item 7
// of the E2a brief): each is persisted the moment it changes, applies live,
// and is still set after a restart. A "restart" here is a second QML engine,
// with its own SettingsState, reading the store the first one wrote.
#include <catch2/catch_test_macros.hpp>

#include "kit_fixture.hpp"

#include <QCoreApplication>
#include <QLocale>
#include <QQmlEngine>
#include <QSettings>
#include <QTemporaryDir>

#include <memory>
#include <utility>

using namespace cube::test;

namespace {

struct Change {
    const char* name;
    QVariant defaultValue;
    QVariant changedValue;
};

const QList<Change>& changes() {
    static const QList<Change> list{
        {"themeMode", "system", "dark"},
        {"language", "", "de"},
        {"marks", true, false},
        {"textSize", "system", "larger"},
        {"motion", "system", "reduced"},
        {"advance", "tap", "timer"},
        {"pace", "steady", "slow"},
        {"explainStages", true, false},
        {"notation", false, true},
        {"sound", true, false},
        {"haptics", true, false},
        {"keepHistory", true, false},
    };
    return list;
}

QObject* stateIn(QQmlEngine& engine) {
    QObject* state = engine.singletonInstance<QObject*>("Cubeulator", "SettingsState");
    REQUIRE(state != nullptr);
    return state;
}

// Gives the run's settings back when the test is over, whatever happens.
class StoreInTemporaryFolder {
public:
    StoreInTemporaryFolder() {
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, folder_.path());
    }
    ~StoreInTemporaryFolder() {
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsRoot());
    }
    StoreInTemporaryFolder(const StoreInTemporaryFolder&) = delete;
    StoreInTemporaryFolder& operator=(const StoreInTemporaryFolder&) = delete;

private:
    QTemporaryDir folder_;
};

}  // namespace

TEST_CASE("A fresh install has the settings the design calls for", "[kit][settings]") {
    StoreInTemporaryFolder fresh;
    auto engine = std::make_unique<QQmlEngine>();
    QObject* state = stateIn(*engine);

    for (const Change& change : changes()) {
        INFO(change.name);
        CHECK(state->property(change.name) == change.defaultValue);
    }
}

TEST_CASE("Every setting is still set after a restart", "[kit][settings]") {
    StoreInTemporaryFolder fresh;
    {
        auto first = std::make_unique<QQmlEngine>();
        QObject* state = stateIn(*first);
        for (const Change& change : changes()) {
            INFO(change.name);
            REQUIRE(state->setProperty(change.name, change.changedValue));
        }
        QCoreApplication::processEvents();
        // The engine going away is the app closing: settings flush on the way.
    }
    {
        auto second = std::make_unique<QQmlEngine>();
        QObject* state = stateIn(*second);
        for (const Change& change : changes()) {
            INFO(change.name);
            CHECK(state->property(change.name) == change.changedValue);
        }
    }
}

TEST_CASE("Each setting is stored under the key the design names", "[kit][settings]") {
    // docs/design/screens/settings.md, "Persistence": group/name keys, named
    // before the first release so that none needs a migration after it.
    StoreInTemporaryFolder fresh;
    {
        auto engine = std::make_unique<QQmlEngine>();
        QObject* state = stateIn(*engine);
        for (const Change& change : changes()) {
            REQUIRE(state->setProperty(change.name, change.changedValue));
        }
        QCoreApplication::processEvents();
    }
    const QSettings store(QSettings::IniFormat, QSettings::UserScope, QStringLiteral("Cubeulator"),
                          QStringLiteral("Cubeulator-kit-tests"));
    const QList<std::pair<QString, QVariant>> expected{
        {"language/code", "de"},
        {"appearance/theme", "dark"},
        {"appearance/textSize", "larger"},
        {"appearance/motion", "reduced"},
        {"appearance/marks", false},
        {"solving/advance", "timer"},
        {"solving/pace", "slow"},
        {"solving/explain", false},
        {"solving/notation", true},
        {"solving/sound", false},
        {"solving/haptics", false},
        {"history/keep", false},
    };
    for (const auto& [key, value] : expected) {
        INFO(key.toStdString());
        REQUIRE(store.contains(key));
        CHECK(store.value(key) == value);
    }
}

TEST_CASE("A theme stored under the old key is carried over once", "[kit][settings][legacy]") {
    StoreInTemporaryFolder fresh;
    {
        // What an install from before the design named its keys left behind.
        QSettings old(QSettings::IniFormat, QSettings::UserScope, QStringLiteral("Cubeulator"),
                      QStringLiteral("Cubeulator-kit-tests"));
        old.setValue("appearance/themeMode", "dark");
        old.sync();
    }
    {
        auto first = std::make_unique<QQmlEngine>();
        QObject* state = stateIn(*first);
        CHECK(state->property("themeMode").toString() == "dark");
        // The person then goes back to following the device.
        REQUIRE(state->setProperty("themeMode", "system"));
        QCoreApplication::processEvents();
    }
    {
        // It must not be carried over again over their choice.
        auto second = std::make_unique<QQmlEngine>();
        CHECK(stateIn(*second)->property("themeMode").toString() == "system");
    }
}

TEST_CASE("A language stored as \"system\" before the key was named means the device's", "[kit][settings][legacy]") {
    resetKitState();
    set("SettingsState", "language", "system");
    CHECK_FALSE(get("Theme", "languageTag").toString().isEmpty());
    CHECK(get("Theme", "languageTag").toString() == QLocale().name());
    set("SettingsState", "language", "");
    CHECK(get("Theme", "languageTag").toString() == QLocale().name());
    resetKitState();
}

TEST_CASE("A setting applies live, in the tokens, with no restart", "[kit][settings]") {
    resetKitState();

    set("SettingsState", "themeMode", "dark");
    CHECK(get("Theme", "effectiveDark").toBool());
    set("SettingsState", "themeMode", "light");
    CHECK_FALSE(get("Theme", "effectiveDark").toBool());

    set("SettingsState", "textSize", "large");
    CHECK(get("Theme", "textScale").toDouble() == 1.25);
    set("SettingsState", "textSize", "system");
    CHECK(get("Theme", "textScale").toDouble() == 1.0);

    set("SettingsState", "motion", "reduced");
    CHECK(get("Theme", "reducedMotion").toBool());
    set("SettingsState", "motion", "full");
    CHECK_FALSE(get("Theme", "reducedMotion").toBool());

    set("SettingsState", "marks", false);
    CHECK_FALSE(get("Theme", "marksOn").toBool());

    set("SettingsState", "language", "ar");
    CHECK(get("Theme", "rtl").toBool());
    CHECK(get("Theme", "script").toString() == "arabic");
    resetKitState();
}

TEST_CASE("The old settings are still there and still persist", "[kit][settings][legacy]") {
    StoreInTemporaryFolder fresh;
    {
        auto first = std::make_unique<QQmlEngine>();
        QObject* state = stateIn(*first);
        REQUIRE(state->setProperty("graphicsBackend", "vulkan"));
        QCoreApplication::processEvents();
    }
    {
        auto second = std::make_unique<QQmlEngine>();
        CHECK(stateIn(*second)->property("graphicsBackend").toString() == "vulkan");
    }
}
