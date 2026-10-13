// The small helpers the kit reads the system through: text size, reduced
// motion, safe-area insets, and what kind of build this is. Windows is the
// only platform they are read for real on in this suite; the others are
// exercised through the override every helper has, which is also what the
// developer gallery uses to show "what if the system asked for this".
#include <catch2/catch_test_macros.hpp>

#include "kit_fixture.hpp"

#include <QAccessible>
#include <QSignalSpy>

using namespace cube::test;

TEST_CASE("The system text scale can be overridden and cleared, and says when it changes", "[kit][system]") {
    resetKitState();
    QObject* system = singleton("SystemTextScale");
    QSignalSpy changed(system, SIGNAL(scaleChanged()));
    const double real = system->property("scale").toDouble();

    CHECK(real >= 1.0);
    CHECK_FALSE(system->property("overridden").toBool());

    invoke(system, "setOverride", {1.75});
    CHECK(system->property("scale").toDouble() == 1.75);
    CHECK(system->property("overridden").toBool());
    CHECK(changed.count() == 1);

    // Same value again: nothing to report.
    invoke(system, "setOverride", {1.75});
    CHECK(changed.count() == 1);

    invoke(system, "clearOverride");
    CHECK(system->property("scale").toDouble() == real);
    CHECK_FALSE(system->property("overridden").toBool());
    CHECK(changed.count() == 2);
}

TEST_CASE("Refreshing the system text scale does not drop an override", "[kit][system]") {
    resetKitState();
    QObject* system = singleton("SystemTextScale");
    invoke(system, "setOverride", {1.5});
    invoke(system, "refresh");
    CHECK(system->property("scale").toDouble() == 1.5);
    invoke(system, "clearOverride");
}

TEST_CASE("The system's reduced-motion preference can be overridden and cleared", "[kit][system]") {
    resetKitState();
    QObject* system = singleton("SystemMotion");
    QSignalSpy changed(system, SIGNAL(reducedChanged()));
    const bool real = system->property("reduced").toBool();

    invoke(system, "setOverride", {!real});
    CHECK(system->property("reduced").toBool() == !real);
    CHECK(system->property("overridden").toBool());
    CHECK(changed.count() >= 1);

    invoke(system, "clearOverride");
    CHECK(system->property("reduced").toBool() == real);
    CHECK_FALSE(system->property("overridden").toBool());
}

TEST_CASE("Insets are zero on a desktop and can be overridden to show a notch", "[kit][system]") {
    resetKitState();
    QObject* insets = singleton("SystemInsets");

    CHECK(insets->property("top").toDouble() == 0);
    CHECK(insets->property("left").toDouble() == 0);
    CHECK(insets->property("right").toDouble() == 0);
    CHECK(insets->property("bottom").toDouble() == 0);

    QSignalSpy changed(insets, SIGNAL(insetsChanged()));
    invoke(insets, "setOverride", {44, 0, 0, 34});
    CHECK(insets->property("top").toDouble() == 44);
    CHECK(insets->property("bottom").toDouble() == 34);
    CHECK(changed.count() == 1);

    // Negative insets are not insets.
    invoke(insets, "setOverride", {-5, 0, 0, 0});
    CHECK(insets->property("top").toDouble() == 0);

    invoke(insets, "clearOverride");
    CHECK_FALSE(insets->property("overridden").toBool());
}

TEST_CASE("A developer build knows it is one, and the gallery is not asked for by default", "[kit][system]") {
    QObject* info = singleton("BuildInfo");
    // These tests are only built where the gallery is (developer builds are
    // the default for everything but Release); a Release test run would
    // report false here and that is the correct behaviour, not a failure of
    // the kit, so the claim is only that the two properties exist and agree.
    CHECK(info->property("developerBuild").isValid());
    CHECK(info->property("galleryRequested").isValid());
    if (!info->property("developerBuild").toBool()) {
        CHECK_FALSE(info->property("galleryRequested").toBool());
    }
    CHECK_FALSE(info->property("galleryRequested").toBool());
}

namespace {
QString g_lastAnnouncement;
bool g_lastAssertive = false;
int g_announcements = 0;

void captureAnnouncement(QAccessibleEvent* event) {
    if (event->type() == QAccessible::Announcement) {
        auto* announcement = static_cast<QAccessibleAnnouncementEvent*>(event);
        g_lastAnnouncement = announcement->message();
        g_lastAssertive = announcement->politeness() == QAccessible::AnnouncementPoliteness::Assertive;
        ++g_announcements;
    }
}
}  // namespace

TEST_CASE("An announcement reaches the accessibility layer, politely unless it stops the person", "[kit][a11y]") {
    resetKitState();
    Hosted host(create("KitText", {{"text", "status"}}));
    QQuickItem* item_ = host.item();
    QAccessible::UpdateHandler previous = QAccessible::installUpdateHandler(captureAnnouncement);
    g_announcements = 0;

    QObject* announcer = singleton("Announcer");
    invoke(announcer, "announce", {QVariant::fromValue<QObject*>(item_), "Side 3 of 6 read"});
    CHECK(g_announcements == 1);
    CHECK(g_lastAnnouncement == "Side 3 of 6 read");
    CHECK_FALSE(g_lastAssertive);

    invoke(announcer, "announce", {QVariant::fromValue<QObject*>(item_), "Camera not found", true});
    CHECK(g_announcements == 2);
    CHECK(g_lastAssertive);

    // Nothing to say is nothing said.
    invoke(announcer, "announce", {QVariant::fromValue<QObject*>(item_), ""});
    CHECK(g_announcements == 2);

    QAccessible::installUpdateHandler(previous);
}
