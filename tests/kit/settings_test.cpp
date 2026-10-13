// The settings the design adds (docs/design/strings.json, settings.*; item 7
// of the E2a brief): each is persisted the moment it changes, applies live,
// and is still set after a restart. A "restart" here is a second QML engine,
// with its own SettingsState, reading the store the first one wrote.
#include <catch2/catch_test_macros.hpp>

#include "kit_fixture.hpp"

#include <QCoreApplication>
#include <QQmlEngine>
#include <QSettings>
#include <QTemporaryDir>

#include <memory>

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
        {"language", "system", "de"},
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
