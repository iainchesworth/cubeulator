// The navigation stack and the rules for back (docs/design/journey.md, "Rules
// that hold everywhere" and "What 'back' does"), and the window plumbing that
// binds every platform's back to it (docs/design/platforms.md, "Back").
#include <catch2/catch_test_macros.hpp>

#include "kit_fixture.hpp"

#include <QMouseEvent>
#include <QSignalSpy>
#include <QTest>

using namespace cube::test;

namespace {

QObject* nav() {
    return singleton("Navigator");
}

QString current() {
    return get("Navigator", "current").toString();
}

bool back() {
    return invoke(nav(), "back").toBool();
}

void reset(const QString& id) {
    invoke(nav(), "reset", {id});
}

void push(const QString& id) {
    invoke(nav(), "push", {id});
}

QString journey() {
    QStringList ids;
    for (const QVariant& entry : get("Navigator", "stack").toList()) {
        ids << entry.toMap().value("id").toString();
    }
    return ids.join(" > ");
}

}  // namespace

TEST_CASE("Back goes where the person came from", "[kit][navigation]") {
    reset("home");
    push("settings");
    push("about");
    push("licences");
    CHECK(journey() == "home > settings > about > licences");

    CHECK(back());
    CHECK(current() == "about");
    CHECK(back());
    CHECK(current() == "settings");
    CHECK(back());
    CHECK(current() == "home");
}

TEST_CASE("Back at the root does nothing and says so", "[kit][navigation]") {
    reset("home");
    QSignalSpy atRoot(nav(), SIGNAL(backAtRoot()));

    CHECK_FALSE(back());
    CHECK(current() == "home");
    CHECK(atRoot.count() == 1);
    CHECK_FALSE(get("Navigator", "canGoBack").toBool());
}

TEST_CASE("Back from complete goes home, not into the finished solve", "[kit][navigation]") {
    reset("home");
    push("scan");
    push("solve");
    push("complete");

    CHECK(back());
    CHECK(current() == "home");
    CHECK(journey() == "home");
}

TEST_CASE("Back from solve goes home, whatever came before", "[kit][navigation]") {
    reset("home");
    push("settings");
    push("scan");
    push("solve");

    CHECK(back());
    CHECK(current() == "home");
}

TEST_CASE("Back from a solve the app resumed straight into still reaches home", "[kit][navigation]") {
    // After a restart the stack holds only the solve: home is not beneath it.
    reset("solve");
    CHECK(back());
    CHECK(current() == "home");
    CHECK(journey() == "home");
}

TEST_CASE("Back from scan after a camera failure goes home, not to where it was", "[kit][navigation]") {
    reset("home");
    push("settings");
    push("scan");

    // Without a failure: back to settings, where the person came from.
    // With one: the way they came is not a better place to be.
    invoke(nav(), "setBackTarget", {"home"});
    CHECK(back());
    CHECK(current() == "home");
}

TEST_CASE("A screen replaced by another leaves no back entry for itself", "[kit][navigation]") {
    reset("home");
    push("scan");
    invoke(nav(), "replace", {"solve"});

    CHECK(journey() == "home > solve");
}

TEST_CASE("An open dialog takes back first and chooses its safe option", "[kit][navigation]") {
    reset("home");
    push("settings");
    auto dialog = createFromSource(R"(
        import QtQuick
        Item {
            property int safeChosen: 0
            function requestBack() { safeChosen += 1 }
        }
    )");

    invoke(nav(), "registerOverlay", {QVariant::fromValue<QObject*>(dialog.get())});
    CHECK(back());
    CHECK(dialog->property("safeChosen").toInt() == 1);
    // The screen beneath was not left.
    CHECK(current() == "settings");

    invoke(nav(), "unregisterOverlay", {QVariant::fromValue<QObject*>(dialog.get())});
    CHECK(back());
    CHECK(current() == "home");
}

TEST_CASE("The innermost overlay is the one back closes", "[kit][navigation]") {
    reset("home");
    auto outer = createFromSource(R"(import QtQuick
Item { property int n: 0; function requestBack() { n += 1 } })");
    auto inner = createFromSource(R"(import QtQuick
Item { property int n: 0; function requestBack() { n += 1 } })");
    invoke(nav(), "registerOverlay", {QVariant::fromValue<QObject*>(outer.get())});
    invoke(nav(), "registerOverlay", {QVariant::fromValue<QObject*>(inner.get())});

    CHECK(back());
    CHECK(inner->property("n").toInt() == 1);
    CHECK(outer->property("n").toInt() == 0);

    invoke(nav(), "unregisterOverlay", {QVariant::fromValue<QObject*>(inner.get())});
    invoke(nav(), "unregisterOverlay", {QVariant::fromValue<QObject*>(outer.get())});
}

TEST_CASE("A screen's guard can take back, as leaving a scan asks first", "[kit][navigation]") {
    reset("home");
    push("scan");
    auto screen = createFromSource(R"(
        import QtQuick
        import Cubeulator
        Item {
            property int asked: 0
            property bool handled: true
            readonly property var fn: function() { asked += 1; return handled }
            function install() { Navigator.addGuard(fn) }
            function remove() { Navigator.removeGuard(fn) }
        }
    )");

    invoke(screen.get(), "install");
    CHECK(back());
    CHECK(screen->property("asked").toInt() == 1);
    CHECK(current() == "scan");   // the guard handled it; the screen stays

    screen->setProperty("handled", false);
    CHECK(back());
    CHECK(screen->property("asked").toInt() == 2);
    CHECK(current() == "home");   // it declined, so back went on

    invoke(screen.get(), "remove");
}

TEST_CASE("popTo goes to the nearest entry with that id", "[kit][navigation]") {
    reset("home");
    push("settings");
    push("about");
    push("licences");

    CHECK(invoke(nav(), "popTo", {"settings"}).toBool());
    CHECK(journey() == "home > settings");
    CHECK_FALSE(invoke(nav(), "popTo", {"nowhere"}).toBool());
}

// ---- The window's back bindings -----------------------------------------------

namespace {
std::unique_ptr<QQuickItem> bindingsFor(Hosted& host) {
    // The property is a Window; give it one as that exact type.
    auto bindings = create("KitWindowBindings");
    REQUIRE(bindings->setProperty("window", QVariant::fromValue<QObject*>(host.window())));
    return bindings;
}
}  // namespace

TEST_CASE("Esc, Alt+Left and the Back key are back; the mouse's back button too", "[kit][navigation][keyboard]") {
    resetKitState();
    Hosted host(create("KitText", {{"text", "page"}}));
    QQuickItem* bindings = host.adopt(bindingsFor(host));
    REQUIRE(bindings != nullptr);

    const auto fromScan = [] {
        reset("home");
        push("settings");
    };

    fromScan();
    host.key(Qt::Key_Escape);
    CHECK(current() == "home");

    fromScan();
    host.key(Qt::Key_Left, Qt::AltModifier);
    CHECK(current() == "home");

    fromScan();
    host.key(Qt::Key_Back);
    CHECK(current() == "home");

    fromScan();
    QTest::mouseClick(host.window(), Qt::BackButton, Qt::NoModifier, QPoint(10, 10));
    QCoreApplication::processEvents();
    CHECK(current() == "home");
}

TEST_CASE("In a right-to-left layout back is Alt+Right, and Alt+Left is not", "[kit][navigation][keyboard]") {
    resetKitState();
    set("SettingsState", "language", "he");
    Hosted host(create("KitText", {{"text", "page"}}));
    host.adopt(bindingsFor(host));

    reset("home");
    push("settings");
    host.key(Qt::Key_Left, Qt::AltModifier);
    CHECK(current() == "settings");
    host.key(Qt::Key_Right, Qt::AltModifier);
    CHECK(current() == "home");
    resetKitState();
}

TEST_CASE("The window publishes its size to Theme, so the size class follows it", "[kit][sizeclass]") {
    resetKitState();
    Hosted host(create("KitText", {{"text", "page"}}), QSize(400, 700));
    host.adopt(bindingsFor(host));
    CHECK(get("Theme", "sizeClass").toString() == "compact");
    CHECK(get("Theme", "heightClass").toString() == "regular");

    host.window()->resize(900, 400);
    REQUIRE(waitFor([] { return get("Theme", "sizeClass").toString() == "expanded"; }));
    CHECK(get("Theme", "heightClass").toString() == "short");

    host.window()->resize(700, 700);
    REQUIRE(waitFor([] { return get("Theme", "sizeClass").toString() == "medium"; }));
    resetKitState();
}

TEST_CASE("A right-to-left language mirrors the window's content", "[kit][mirroring]") {
    resetKitState();
    Hosted host(createFromSource(R"(
        import QtQuick
        Item { property bool mirrored: LayoutMirroring.enabled }
    )"));
    host.adopt(bindingsFor(host));

    CHECK_FALSE(host.item()->property("mirrored").toBool());
    set("SettingsState", "language", "he");
    CHECK(host.item()->property("mirrored").toBool());
    set("SettingsState", "language", "en");
    CHECK_FALSE(host.item()->property("mirrored").toBool());
    resetKitState();
}
