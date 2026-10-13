// What each kit component does, one behaviour at a time. The gate over the
// whole kit is in gallery_test.cpp; this is the part that is about a
// component's own rules: which key does what, what a screen reader is told,
// what follows the layout's direction and what never does.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "kit_fixture.hpp"

#include <QAccessible>
#include <QMap>
#include <QSignalSpy>

#include <algorithm>
#include <cmath>
#include <utility>

using namespace cube::test;
using Catch::Approx;

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

// Listens for announcements for as long as it lives.
class Listening {
public:
    Listening() : previous_(QAccessible::installUpdateHandler(captureAnnouncement)) {
        g_announcements = 0;
        g_lastAnnouncement.clear();
        g_lastAssertive = false;
    }
    ~Listening() { QAccessible::installUpdateHandler(previous_); }
    Listening(const Listening&) = delete;
    Listening& operator=(const Listening&) = delete;

private:
    QAccessible::UpdateHandler previous_;
};

QQuickItem* child(QQuickItem* root, const char* name) {
    QQuickItem* found = find(root, QString::fromLatin1(name));
    REQUIRE(found != nullptr);
    return found;
}

}  // namespace

// ---- Icon button ---------------------------------------------------------------

TEST_CASE("An icon button is named by its text and is a 56 px target", "[kit][iconbutton]") {
    resetKitState();
    Hosted host(create("KitIconButton", {{"iconName", "chevron-left"}, {"text", "Back"}}));

    const Accessibility info = accessibilityOf(host.item());
    CHECK(info.name == "Back");
    CHECK(info.role == QAccessible::PushButton);
    CHECK(host.item()->width() == 56);
    CHECK(host.item()->height() == 56);

    QSignalSpy clicked(host.item(), SIGNAL(clicked()));
    REQUIRE(host.tab() == host.item());
    host.key(Qt::Key_Space);
    CHECK(clicked.count() == 1);
}

// ---- Switch ----------------------------------------------------------------------

TEST_CASE("A switch toggles, tells a screen reader it is checked, and moves its thumb", "[kit][switch]") {
    resetKitState();
    Hosted host(create("KitSwitch", {{"text", "Sound"}, {"note", "A soft click"}, {"width", 320}}));
    QQuickItem* thumb = child(host.item(), "thumb");
    QSignalSpy toggled(host.item(), SIGNAL(toggled()));

    Accessibility info = accessibilityOf(host.item());
    CHECK(info.name == "Sound");
    CHECK(info.description == "A soft click");
    CHECK(info.role == QAccessible::CheckBox);
    CHECK_FALSE(info.state.checked);
    const double offX = thumb->x();

    REQUIRE(host.tab() == host.item());
    host.key(Qt::Key_Space);
    CHECK(toggled.count() == 1);
    CHECK(host.item()->property("checked").toBool());
    info = accessibilityOf(host.item());
    CHECK(info.state.checked);

    // On is at the trailing end: further along than off, in a left-to-right layout.
    REQUIRE(waitFor([&] { return std::abs(thumb->x() - 24) < 0.5; }));
    CHECK(thumb->x() > offX);

    // Click toggles it back.
    host.click(host.item());
    CHECK_FALSE(host.item()->property("checked").toBool());
}

TEST_CASE("A switch's thumb sits at the leading end, which is the other side in right-to-left", "[kit][switch][rtl]") {
    resetKitState();
    set("SettingsState", "language", "he");
    auto root = createFromSource(R"(
        import QtQuick
        import Cubeulator
        Item {
            width: 400; height: 100
            LayoutMirroring.enabled: Theme.rtl
            LayoutMirroring.childrenInherit: true
            KitSwitch { objectName: "sw"; width: 320; text: "Sound" }
        }
    )");
    Hosted host(std::move(root));
    QQuickItem* sw = child(host.item(), "sw");
    QQuickItem* thumb = child(sw, "thumb");

    // Off, leading end: in right-to-left that is the right-hand end.
    CHECK(thumb->x() == Approx(52 - 24 - 4).margin(0.5));
    sw->setProperty("checked", true);
    REQUIRE(waitFor([&] { return std::abs(thumb->x() - 4) < 0.5; }));
    resetKitState();
}

TEST_CASE("A switch grows to hold its words at twice the text size", "[kit][switch][textscale]") {
    resetKitState();
    invoke(singleton("SystemTextScale"), "setOverride", {2.0});
    auto sw = create("KitSwitch", {{"text", "Marks on cube colours marks"},
                                   {"note", "A shape on each colour, so they can be told apart without hue."},
                                   {"width", 320}});
    QQuickItem* label = child(sw.get(), "label");
    QQuickItem* note = child(sw.get(), "note");
    const QRectF labelRect = sw->mapRectFromItem(label, QRectF(0, 0, label->width(), label->height()));
    const QRectF noteRect = sw->mapRectFromItem(note, QRectF(0, 0, note->width(), note->height()));

    INFO("switch height " << sw->height() << ", label " << labelRect.top() << ".." << labelRect.bottom()
                          << ", note " << noteRect.top() << ".." << noteRect.bottom() << ", implicitHeight "
                          << sw->implicitHeight());
    CHECK(noteRect.bottom() <= sw->height());
    CHECK(labelRect.top() >= 0);
    CHECK(note->height() > 100);   // it did wrap
    resetKitState();
}

// ---- Choice ----------------------------------------------------------------------

namespace {
constexpr const char* kChoiceSource = R"(
    import QtQuick
    import Cubeulator
    Item {
        width: 420; height: 220
        property int activations: 0
        property int lastIndex: -1
        property alias choice: theChoice
        KitButton { objectName: "before"; text: "Before"; x: 0; y: 0 }
        KitChoice {
            id: theChoice
            objectName: "choice"
            x: 0; y: 70; width: 400
            label: "Theme"
            options: ["Same as device", "Light", "Dark"]
            currentIndex: 0
            onActivated: index => { parent.activations += 1; parent.lastIndex = index }
        }
        KitButton { objectName: "after"; text: "After"; x: 0; y: 150 }
    }
)";
}  // namespace

TEST_CASE("A choice is one tab stop, and the arrow keys move and choose", "[kit][choice][keyboard]") {
    resetKitState();
    Hosted host(createFromSource(kChoiceSource));
    QQuickItem* before = child(host.item(), "before");
    QQuickItem* after = child(host.item(), "after");
    QQuickItem* choice = child(host.item(), "choice");
    QQuickItem* option0 = child(choice, "option0");
    QQuickItem* option1 = child(choice, "option1");
    QQuickItem* option2 = child(choice, "option2");

    // Tab enters the group at the chosen option, once, and leaves it once.
    REQUIRE(host.tab() == before);
    CHECK(host.tab() == option0);
    CHECK(host.tab() == after);

    // Back into the group, then the arrow keys.
    host.tab(true);
    REQUIRE(host.window()->activeFocusItem() == option0);
    host.key(Qt::Key_Right);
    CHECK(choice->property("currentIndex").toInt() == 1);
    CHECK(host.window()->activeFocusItem() == option1);
    CHECK(host.item()->property("lastIndex").toInt() == 1);
    host.key(Qt::Key_Down);
    CHECK(choice->property("currentIndex").toInt() == 2);
    CHECK(host.window()->activeFocusItem() == option2);
    // The last one stops there.
    host.key(Qt::Key_Right);
    CHECK(choice->property("currentIndex").toInt() == 2);
    host.key(Qt::Key_Left);
    CHECK(choice->property("currentIndex").toInt() == 1);

    // The tab stop followed the choice.
    QQuickItem* next = host.tab();
    INFO("Tab from the chosen option landed on '" << (next ? next->objectName().toStdString() : "nothing") << "'");
    CHECK(next == after);
}

TEST_CASE("In a right-to-left layout the horizontal arrows swap with the layout", "[kit][choice][rtl]") {
    resetKitState();
    set("SettingsState", "language", "he");
    Hosted host(createFromSource(QByteArray(R"(
        import QtQuick
        import Cubeulator
        Item {
            LayoutMirroring.enabled: Theme.rtl
            LayoutMirroring.childrenInherit: true
            width: 420; height: 100
            KitChoice { objectName: "choice"; width: 400; label: "Theme"
                        options: ["A", "B", "C"]; currentIndex: 1 }
        }
    )")));
    QQuickItem* choice = child(host.item(), "choice");

    REQUIRE(host.tab() == child(choice, "option1"));
    host.key(Qt::Key_Right);   // in right-to-left, "right" is back
    CHECK(choice->property("currentIndex").toInt() == 0);
    host.key(Qt::Key_Left);
    CHECK(choice->property("currentIndex").toInt() == 1);
    resetKitState();
}

TEST_CASE("A choice says which option is chosen, and by more than colour", "[kit][choice][a11y]") {
    resetKitState();
    Hosted host(createFromSource(kChoiceSource));
    QQuickItem* choice = child(host.item(), "choice");

    CHECK(accessibilityOf(choice).role == QAccessible::Grouping);
    CHECK(accessibilityOf(choice).name == "Theme");

    const Accessibility chosen = accessibilityOf(child(choice, "option0"));
    const Accessibility other = accessibilityOf(child(choice, "option1"));
    CHECK(chosen.role == QAccessible::RadioButton);
    CHECK(chosen.name == "Same as device");
    CHECK(chosen.state.checked);
    CHECK_FALSE(other.state.checked);
}

TEST_CASE("A choice whose labels do not fit on a line becomes a column", "[kit][choice][textfit]") {
    resetKitState();
    Hosted host(createFromSource(R"(
        import QtQuick
        import Cubeulator
        Item {
            width: 420; height: 300
            KitChoice { objectName: "wide"; width: 400
                        options: ["Same", "Light", "Dark"]; currentIndex: 0 }
            KitChoice { objectName: "narrow"; y: 120; width: 160
                        options: ["Same", "Light", "Dark"]; currentIndex: 0 }
        }
    )"));
    QQuickItem* wide = child(host.item(), "wide");
    QQuickItem* narrow = child(host.item(), "narrow");

    CHECK_FALSE(wide->property("stacked").toBool());
    CHECK(narrow->property("stacked").toBool());
    // Stacked, each option is as wide as the group and the group is taller.
    CHECK(child(narrow, "option0")->width() == Approx(narrow->width()).margin(1));
    CHECK(narrow->height() > wide->height() * 2);
    // In a row the options share the group's width and together fill it.
    const double sum = child(wide, "option0")->width() + child(wide, "option1")->width() +
                       child(wide, "option2")->width() - 2 * 2;
    CHECK(sum == Approx(wide->width()).margin(1.5));
}

// ---- List row ------------------------------------------------------------------------

TEST_CASE("A list row is a button that reads its value after its label", "[kit][listrow]") {
    resetKitState();
    Hosted host(create("KitListRow", {{"text", "Language"}, {"value", "English"}, {"width", 360}}));
    QSignalSpy clicked(host.item(), SIGNAL(clicked()));

    const Accessibility info = accessibilityOf(host.item());
    CHECK(info.name == "Language, English");
    CHECK(info.role == QAccessible::PushButton);
    CHECK(host.item()->height() >= 56);

    host.click(host.item());
    CHECK(clicked.count() == 1);
}

// ---- Top bar --------------------------------------------------------------------------

TEST_CASE("A top bar's title is a heading and its back control is named", "[kit][topbar]") {
    resetKitState();
    Hosted host(create("KitTopBar", {{"title", "Settings"}, {"backText", "Back"}, {"width", 420}}));
    QSignalSpy back(host.item(), SIGNAL(backClicked()));

    const Accessibility title = accessibilityOf(child(host.item(), "title"));
    CHECK(title.name == "Settings");
    CHECK(title.role == QAccessible::Heading);

    QQuickItem* backButton = child(host.item(), "back");
    CHECK(accessibilityOf(backButton).name == "Back");
    CHECK(backButton->width() == 56);

    host.click(backButton);
    CHECK(back.count() == 1);
    // 56 high, and the rule beneath.
    CHECK(host.item()->height() >= 56);
}

TEST_CASE("The back chevron points the other way in a right-to-left layout", "[kit][topbar][rtl]") {
    resetKitState();
    auto bar = [](const char* language) {
        set("SettingsState", "language", language);
        auto root = createFromSource(R"(
            import QtQuick
            import Cubeulator
            Item {
                width: 420; height: 100
                LayoutMirroring.enabled: Theme.rtl
                LayoutMirroring.childrenInherit: true
                KitTopBar { objectName: "bar"; width: 400; title: "T"; backText: "Back" }
            }
        )");
        return root;
    };
    {
        auto root = bar("en");
        QQuickItem* icon = child(child(root.get(), "back"), "icon");
        CHECK_FALSE(icon->property("mirrored").toBool());
    }
    {
        auto root = bar("he");
        QQuickItem* icon = child(child(root.get(), "back"), "icon");
        CHECK(icon->property("mirrored").toBool());
    }
    resetKitState();
}

// ---- Dialog ----------------------------------------------------------------------------

namespace {
constexpr const char* kDialogSource = R"(
    import QtQuick
    import Cubeulator
    Item {
        id: top
        width: 480; height: 320
        property int safe: 0
        property int other: 0
        property alias dialog: dlg
        KitButton { id: opener; objectName: "opener"; text: "Open"; x: 20; y: 20; onClicked: dlg.open() }
        KitDialog {
            id: dlg
            objectName: "dlg"
            title: "Leave the scan?"
            body: "The sides you have shown will be forgotten."
            safeText: "Keep scanning"
            otherText: "Leave"
            onSafeChosen: top.safe += 1
            onOtherChosen: top.other += 1
        }
    }
)";

struct DialogFixture {
    Hosted host;
    QQuickItem* opener;
    QObject* dialog;

    DialogFixture() : host(createFromSource(kDialogSource)) {
        opener = child(host.item(), "opener");
        dialog = host.item()->findChild<QObject*>("dlg");
        REQUIRE(dialog != nullptr);
    }

    bool isOpen() const { return dialog->property("opened").toBool(); }
    // Closed is when the exit transition has finished, not when it starts.
    bool isClosed() const { return !dialog->property("visible").toBool(); }
    void open() {
        host.tab();   // onto the opener
        host.key(Qt::Key_Space);
        REQUIRE(waitFor([&] { return isOpen(); }));
    }
    QQuickItem* focused() { return host.window()->activeFocusItem(); }
};
}  // namespace

TEST_CASE("A dialog opens with its safe choice focused, and Esc chooses it", "[kit][dialog][keyboard]") {
    resetKitState();
    DialogFixture f;
    f.open();

    REQUIRE(f.focused() != nullptr);
    CHECK(f.focused()->objectName() == "safe");
    CHECK(f.focused()->property("visualFocus").toBool());

    f.host.key(Qt::Key_Escape);
    REQUIRE(waitFor([&] { return f.isClosed(); }));
    CHECK(f.host.item()->property("safe").toInt() == 1);
    CHECK(f.host.item()->property("other").toInt() == 0);
}

TEST_CASE("Focus stays inside a dialog while it is open and returns afterwards", "[kit][dialog][keyboard]") {
    resetKitState();
    DialogFixture f;
    f.open();

    auto* content = f.dialog->property("contentItem").value<QQuickItem*>();
    REQUIRE(content != nullptr);
    for (int i = 0; i < 6; ++i) {
        QQuickItem* now = f.host.tab();
        REQUIRE(now != nullptr);
        INFO("after " << (i + 1) << " tabs, on " << now->objectName().toStdString());
        CHECK(content->isAncestorOf(now));
    }

    f.host.key(Qt::Key_Escape);
    REQUIRE(waitFor([&] { return f.isClosed(); }));
    // Back where it was.
    REQUIRE(waitFor([&] { return f.focused() == f.opener; }));
}

TEST_CASE("The other choice is the ghost button, second", "[kit][dialog]") {
    resetKitState();
    DialogFixture f;
    f.open();

    // Safe first, other second, in reading order.
    f.host.tab();   // from the safe choice onwards
    CHECK(f.focused()->objectName() == "other");
    f.host.key(Qt::Key_Space);
    REQUIRE(waitFor([&] { return f.isClosed(); }));
    CHECK(f.host.item()->property("other").toInt() == 1);
    CHECK(f.host.item()->property("safe").toInt() == 0);
}

TEST_CASE("A platform's back reaches the dialog before the screen beneath", "[kit][dialog][navigation]") {
    resetKitState();
    invoke(singleton("Navigator"), "reset", {"home"});
    invoke(singleton("Navigator"), "push", {"scan"});
    DialogFixture f;
    f.open();

    CHECK(invoke(singleton("Navigator"), "back").toBool());
    REQUIRE(waitFor([&] { return f.isClosed(); }));
    // The safe choice, and the screen it was over was not left.
    CHECK(f.host.item()->property("safe").toInt() == 1);
    CHECK(get("Navigator", "current").toString() == "scan");
    CHECK(get("Navigator", "overlays").toList().isEmpty());
}

TEST_CASE("A dialog is announced as a dialog with its title", "[kit][dialog][a11y]") {
    resetKitState();
    DialogFixture f;
    // A popup is not an item; what a reader is given is its content item.
    auto* content = f.dialog->property("contentItem").value<QQuickItem*>();
    REQUIRE(content != nullptr);
    const Accessibility info = accessibilityOf(content);
    CHECK(info.name == "Leave the scan?");
    CHECK(info.role == QAccessible::Dialog);
    CHECK(info.description == "The sides you have shown will be forgotten.");
}

// ---- Sheet ------------------------------------------------------------------------------

TEST_CASE("A sheet slides up from the bottom on compact and is centred from medium", "[kit][sheet]") {
    resetKitState();
    Hosted host(createFromSource(R"(
        import QtQuick
        import Cubeulator
        Item {
            width: 400; height: 600
            KitSheet {
                id: sheet
                objectName: "sheet"
                closeText: "Close"
                KitButton { text: "Help"; block: true; size: "thumb"; variant: "ghost" }
            }
            Component.onCompleted: sheet.open()
        }
    )"), QSize(400, 600));
    QObject* sheet = host.item()->findChild<QObject*>("sheet");
    REQUIRE(sheet != nullptr);
    REQUIRE(waitFor([&] { return sheet->property("opened").toBool(); }));

    set("Theme", "windowWidth", 400);
    CHECK(sheet->property("bottomSheet").toBool());
    const double top = sheet->property("y").toDouble();
    const double height = sheet->property("height").toDouble();
    CHECK(top + height == Approx(host.window()->height()).margin(1));

    set("Theme", "windowWidth", 900);
    CHECK_FALSE(sheet->property("bottomSheet").toBool());
    CHECK(sheet->property("width").toDouble() <= 440);
    resetKitState();
}

TEST_CASE("Esc closes a sheet and back does too", "[kit][sheet][keyboard][navigation]") {
    resetKitState();
    invoke(singleton("Navigator"), "reset", {"home"});
    Hosted host(createFromSource(R"(
        import QtQuick
        import Cubeulator
        Item {
            id: top
            width: 400; height: 600
            property int closedByPerson: 0
            KitSheet {
                id: sheet
                objectName: "sheet"
                closeText: "Close"
                onClosedByPerson: top.closedByPerson += 1
            }
            function show() { sheet.open() }
        }
    )"), QSize(400, 600));
    QObject* sheet = host.item()->findChild<QObject*>("sheet");

    invoke(host.item(), "show");
    REQUIRE(waitFor([&] { return sheet->property("opened").toBool(); }));
    host.key(Qt::Key_Escape);
    REQUIRE(waitFor([&] { return !sheet->property("visible").toBool(); }));

    invoke(host.item(), "show");
    REQUIRE(waitFor([&] { return sheet->property("opened").toBool(); }));
    invoke(singleton("Navigator"), "back");
    REQUIRE(waitFor([&] { return !sheet->property("visible").toBool(); }));
    CHECK(host.item()->property("closedByPerson").toInt() >= 1);
}

// ---- Notice, card -------------------------------------------------------------------------

TEST_CASE("A notice is an alert that reads its heading then its sentence", "[kit][notice][a11y]") {
    resetKitState();
    Hosted host(create("KitNotice", {{"kind", "caution"}, {"heading", "No camera found"},
                                     {"text", "Plug one in, then look again."}, {"announce", false}, {"width", 360}}));
    const Accessibility info = accessibilityOf(host.item());
    CHECK(info.role == QAccessible::AlertMessage);
    CHECK(info.name == "No camera found. Plug one in, then look again.");
}

TEST_CASE("A notice announces itself when it appears, assertively only if told to", "[kit][notice][a11y]") {
    resetKitState();
    Listening listening;
    {
        auto notice = create("KitNotice", {{"kind", "information"}, {"heading", "Nothing is saved"},
                                           {"text", "No picture is kept."}});
        CHECK(g_announcements == 1);
        CHECK(g_lastAnnouncement == "Nothing is saved. No picture is kept.");
        CHECK_FALSE(g_lastAssertive);
    }
    {
        auto notice = create("KitNotice", {{"kind", "caution"}, {"heading", "Camera lost"}, {"assertive", true}});
        CHECK(g_announcements == 2);
        CHECK(g_lastAssertive);
    }
    {
        auto silent = create("KitNotice", {{"heading", "Hello"}, {"announce", false}});
        CHECK(g_announcements == 2);
    }
}

TEST_CASE("A notice's kind is its icon, not just its colour", "[kit][notice]") {
    resetKitState();
    for (const auto& [kind, icon] : {std::pair{"information", "info"}, std::pair{"caution", "alert"},
                                     std::pair{"positive", "check"}}) {
        auto notice = create("KitNotice", {{"kind", kind}, {"announce", false}});
        INFO(kind);
        CHECK(notice->property("iconName").toString() == icon);
    }
}

// ---- Progress ------------------------------------------------------------------------------

TEST_CASE("Solve progress fills from the leading edge and marks its stage boundaries", "[kit][progress]") {
    resetKitState();
    Hosted host(create("KitSolveProgress", {{"width", 400}, {"value", 0.25}, {"text", "Turn 25 of 100"},
                                           {"boundaries", QVariantList{0.2, 0.5, 0.8}}}));
    QQuickItem* fill = child(host.item(), "fill");
    CHECK(fill->width() == Approx(100).margin(0.5));
    CHECK(accessibilityOf(host.item()).name == "Turn 25 of 100");
    CHECK(accessibilityOf(host.item()).role == QAccessible::ProgressBar);

    // Three ticks besides the fill and the track.
    int ticks = 0;
    for (QQuickItem* item : host.item()->childItems()) {
        if (item->metaObject()->indexOfProperty("modelData") >= 0 || item->width() == 2) {
            ++ticks;
        }
    }
    CHECK(ticks >= 3);
}

TEST_CASE("Solve progress fills from the right in a right-to-left layout", "[kit][progress][rtl]") {
    resetKitState();
    set("SettingsState", "language", "he");
    Hosted host(createFromSource(R"(
        import QtQuick
        import Cubeulator
        Item {
            width: 420; height: 60
            LayoutMirroring.enabled: Theme.rtl
            LayoutMirroring.childrenInherit: true
            KitSolveProgress { objectName: "p"; width: 400; value: 0.25 }
        }
    )"));
    QQuickItem* progress = child(host.item(), "p");
    QQuickItem* fill = child(progress, "fill");
    const QPointF right = progress->mapFromItem(fill, QPointF(fill->width(), 0));
    CHECK(right.x() == Approx(400).margin(1));
    resetKitState();
}

TEST_CASE("The working indicator is still under reduced motion and says what is happening", "[kit][progress][motion]") {
    resetKitState();
    set("SettingsState", "motion", "reduced");
    auto item = create("KitWorking", {{"text", "Working out the solution"}, {"announce", false}});
    CHECK(item->property("still").toBool());
    CHECK(accessibilityOf(item.get()).name == "Working out the solution");
    set("SettingsState", "motion", "full");
    CHECK_FALSE(item->property("still").toBool());
    resetKitState();
}

TEST_CASE("The countdown drains over its duration, and paused is full and still", "[kit][progress]") {
    resetKitState();
    auto item = create("KitCountdown", {{"duration", 150}, {"width", 300}});
    QSignalSpy finished(item.get(), SIGNAL(finished()));

    CHECK(item->property("remaining").toDouble() == 1.0);
    item->setProperty("running", true);
    REQUIRE(waitFor([&] { return item->property("remaining").toDouble() < 0.9; }));
    // Paused: back to full, and it stays there.
    item->setProperty("paused", true);
    CHECK(item->property("remaining").toDouble() == 1.0);
    CHECK(finished.count() == 0);

    item->setProperty("paused", false);
    REQUIRE(waitFor([&] { return finished.count() == 1; }, 1500));
    CHECK(item->property("remaining").toDouble() == Approx(0).margin(0.01));
}

TEST_CASE("The capture bar fills with how long the side has been held", "[kit][progress]") {
    resetKitState();
    auto item = create("KitCaptureBar", {{"width", 100}, {"value", 0.3}});
    CHECK(child(item.get(), "fill")->width() == Approx(30).margin(0.5));
    item->setProperty("value", 2.0);   // out of range is held to the bar
    CHECK(child(item.get(), "fill")->width() == Approx(100).margin(0.5));
}

// ---- Sticker, swatch --------------------------------------------------------------------------

TEST_CASE("A sticker carries its mark unless marks are off, it is too small, or it is white", "[kit][sticker]") {
    resetKitState();
    const auto marked = [](const char* colour, double size) {
        auto s = create("KitSticker", {{"colourName", colour}, {"size", size}});
        return s->property("showMark").toBool();
    };

    for (const char* colour : {"yellow", "orange", "red", "green", "blue"}) {
        INFO(colour);
        CHECK(marked(colour, 48));
        CHECK(marked(colour, 12));
        CHECK_FALSE(marked(colour, 11));
    }
    CHECK_FALSE(marked("white", 48));   // plain is white's mark

    set("SettingsState", "marks", false);
    CHECK_FALSE(marked("red", 48));
    resetKitState();
}

TEST_CASE("Each colour has its own mark", "[kit][sticker]") {
    resetKitState();
    const QMap<QString, QString> expected{{"white", "none"}, {"yellow", "ring"}, {"orange", "slash"},
                                          {"red", "plus"},   {"green", "dot"},   {"blue", "bar"}};
    for (auto it = expected.begin(); it != expected.end(); ++it) {
        auto s = create("KitSticker", {{"colourName", it.key()}});
        INFO(it.key().toStdString());
        CHECK(s->property("mark").toString() == it.value());
    }
}

TEST_CASE("A sticker is named only when it has a name to give", "[kit][sticker][a11y]") {
    resetKitState();
    Hosted host(create("KitSticker", {{"colourName", "red"}, {"accessibleName", "Red"}}));
    CHECK(accessibilityOf(host.item()).name == "Red");
    CHECK(accessibilityOf(host.item()).role == QAccessible::StaticText);
}

TEST_CASE("A swatch is a line of text high and follows the text size", "[kit][swatch][textscale]") {
    resetKitState();
    auto swatch = create("KitSwatch", {{"colourName", "yellow"}});
    CHECK(swatch->width() == 16);
    invoke(singleton("SystemTextScale"), "setOverride", {2.0});
    CHECK(swatch->width() == 32);
    CHECK(swatch->property("edgeWidth").toDouble() == 2);
    resetKitState();
}

// ---- Side chip ----------------------------------------------------------------------------------

TEST_CASE("A side chip has stickers only once a side is read, and is a stop only then", "[kit][sidechip]") {
    resetKitState();
    const QVariantList nine{"red", "white", "blue", "green", "orange", "yellow", "red", "green", "blue"};
    for (const char* state : {"empty", "filling"}) {
        auto chip = create("KitSideChip", {{"chipState", state}, {"colours", nine}});
        INFO(state);
        CHECK_FALSE(chip->property("hasStickers").toBool());
        CHECK_FALSE(chip->property("activeFocusOnTab").toBool());
    }
    for (const char* state : {"read", "inView", "confirmed"}) {
        auto chip = create("KitSideChip", {{"chipState", state}, {"colours", nine}});
        INFO(state);
        CHECK(chip->property("hasStickers").toBool());
        CHECK(chip->property("activeFocusOnTab").toBool());
    }
}

TEST_CASE("A side chip's edge says its state by weight, not only colour", "[kit][sidechip]") {
    resetKitState();
    const auto edge = [](const char* state) {
        auto chip = create("KitSideChip", {{"chipState", state}});
        return chip->property("edgeWidth").toDouble();
    };
    CHECK(edge("empty") == 1);
    CHECK(edge("read") == 1);
    CHECK(edge("inView") == 3);
    CHECK(edge("confirmed") == 3);
}

TEST_CASE("Activating a side chip reads its colours to a screen reader", "[kit][sidechip][a11y]") {
    resetKitState();
    Listening listening;
    Hosted host(create("KitSideChip", {{"chipState", "read"},
                                       {"colours", QVariantList{"red", "white", "blue", "green", "orange", "yellow",
                                                                "red", "green", "blue"}},
                                       {"accessibleName", "Side 3: read. Red side."},
                                       {"description", "Top row: red, white, blue."}}));
    const Accessibility info = accessibilityOf(host.item());
    CHECK(info.name == "Side 3: read. Red side.");
    CHECK(info.role == QAccessible::PushButton);

    host.click(host.item());
    CHECK(g_announcements == 1);
    CHECK(g_lastAnnouncement == "Top row: red, white, blue.");
}

TEST_CASE("The stickers of a chip are in the same places in either direction", "[kit][sidechip][rtl]") {
    resetKitState();
    const auto firstStickerX = [](const char* language) {
        set("SettingsState", "language", language);
        auto root = createFromSource(R"(
            import QtQuick
            import Cubeulator
            Item {
                width: 200; height: 200
                LayoutMirroring.enabled: Theme.rtl
                LayoutMirroring.childrenInherit: true
                KitSideChip { objectName: "chip"; chipState: "read"; size: 90
                              colours: ["red", "white", "blue", "green", "orange", "yellow", "red", "green", "blue"] }
            }
        )");
        QQuickItem* chip = child(root.get(), "chip");
        QList<QQuickItem*> stickers;
        for (QQuickItem* item : allItems(chip)) {
            if (item->objectName() == "kitSticker") {
                stickers.push_back(item);
            }
        }
        REQUIRE(stickers.size() == 9);
        double minX = 1e9;
        for (QQuickItem* s : stickers) {
            minX = std::min(minX, chip->mapFromItem(s, QPointF(0, 0)).x());
        }
        return minX;
    };
    CHECK(firstStickerX("en") == Approx(firstStickerX("he")).margin(0.01));
    resetKitState();
}

// ---- Colour picker ------------------------------------------------------------------------------------

namespace {
constexpr const char* kPickerSource = R"(
    import QtQuick
    import Cubeulator
    Item {
        width: 520; height: 200
        property string lastChosen: ""
        property int count: 0
        KitButton { objectName: "before"; text: "Before"; x: 0; y: 0 }
        KitColourPicker {
            objectName: "picker"
            x: 0; y: 70; width: 500
            title: "Paint with"
            labels: ["White", "Yellow", "Orange", "Red", "Green", "Blue"]
            current: "orange"
            onChosen: colour => { parent.lastChosen = colour; parent.count += 1 }
        }
        KitButton { objectName: "after"; text: "After"; x: 0; y: 170 }
    }
)";
}  // namespace

TEST_CASE("Keys 1 to 6 choose a colour, in the order shown", "[kit][colourpicker][keyboard]") {
    resetKitState();
    Hosted host(createFromSource(kPickerSource));
    QQuickItem* picker = child(host.item(), "picker");

    host.tab();   // before
    REQUIRE(host.tab() == child(picker, "colour/orange"));

    const char* expected[] = {"white", "yellow", "orange", "red", "green", "blue"};
    const Qt::Key keys[] = {Qt::Key_1, Qt::Key_2, Qt::Key_3, Qt::Key_4, Qt::Key_5, Qt::Key_6};
    for (int i = 0; i < 6; ++i) {
        host.key(keys[i]);
        INFO("key " << (i + 1));
        CHECK(picker->property("current").toString() == expected[i]);
    }
    // 3 was already chosen when 3 was pressed after 2? it was not: each press changed it.
    CHECK(host.item()->property("lastChosen").toString() == "blue");
}

TEST_CASE("A colour picker is one tab stop, and the arrows move the choice", "[kit][colourpicker][keyboard]") {
    resetKitState();
    Hosted host(createFromSource(kPickerSource));
    QQuickItem* picker = child(host.item(), "picker");

    host.tab();
    REQUIRE(host.tab() == child(picker, "colour/orange"));
    CHECK(host.tab() == child(host.item(), "after"));
    host.tab(true);
    REQUIRE(host.window()->activeFocusItem() == child(picker, "colour/orange"));

    host.key(Qt::Key_Right);
    CHECK(picker->property("current").toString() == "red");
    host.key(Qt::Key_Left);
    host.key(Qt::Key_Left);
    CHECK(picker->property("current").toString() == "yellow");
}

TEST_CASE("The colours keep their order and their arrow keys in a right-to-left layout", "[kit][colourpicker][rtl]") {
    resetKitState();
    set("SettingsState", "language", "he");
    Hosted host(createFromSource(R"(
        import QtQuick
        import Cubeulator
        Item {
            width: 520; height: 120
            LayoutMirroring.enabled: Theme.rtl
            LayoutMirroring.childrenInherit: true
            KitColourPicker { objectName: "picker"; width: 500; title: "T"
                              labels: ["W", "Y", "O", "R", "G", "B"]; current: "orange" }
        }
    )"));
    QQuickItem* picker = child(host.item(), "picker");

    // White is still at the leading-in-LTR end: left.
    const double white = picker->mapFromItem(child(picker, "colour/white"), QPointF(0, 0)).x();
    const double blue = picker->mapFromItem(child(picker, "colour/blue"), QPointF(0, 0)).x();
    CHECK(white < blue);

    REQUIRE(host.tab() == child(picker, "colour/orange"));
    host.key(Qt::Key_Right);
    CHECK(picker->property("current").toString() == "red");
    resetKitState();
}

TEST_CASE("A colour picker says which colour is chosen to a screen reader", "[kit][colourpicker][a11y]") {
    resetKitState();
    Hosted host(createFromSource(kPickerSource));
    QQuickItem* picker = child(host.item(), "picker");

    CHECK(accessibilityOf(picker).role == QAccessible::Grouping);
    CHECK(accessibilityOf(picker).name == "Paint with");
    const Accessibility orange = accessibilityOf(child(picker, "colour/orange"));
    CHECK(orange.name == "Orange");
    CHECK(orange.role == QAccessible::RadioButton);
    CHECK(orange.state.checked);
    CHECK_FALSE(accessibilityOf(child(picker, "colour/red")).state.checked);
}

// ---- Status panel ---------------------------------------------------------------------------------------

TEST_CASE("The status panel says what it now says, once, and urgent things assertively", "[kit][statuspanel][a11y]") {
    resetKitState();
    Listening listening;
    auto panel = create("KitStatusPanel", {{"headline", "Show the next side"}, {"body", "Hold it still."}});
    REQUIRE(waitFor([] { return g_announcements == 1; }));
    CHECK(g_lastAnnouncement == "Show the next side. Hold it still.");
    CHECK_FALSE(g_lastAssertive);

    // A headline and a body that change together are one change.
    panel->setProperty("headline", "Side 3 of 6 read");
    panel->setProperty("body", "Show the next side.");
    REQUIRE(waitFor([] { return g_announcements == 2; }));
    CHECK(g_lastAnnouncement == "Side 3 of 6 read. Show the next side.");
    // And only once.
    waitFor([] { return false; }, 150);
    CHECK(g_announcements == 2);

    // The same words again within two seconds are not said again.
    panel->setProperty("body", "Something else.");
    panel->setProperty("body", "Show the next side.");
    waitFor([] { return false; }, 150);
    CHECK(g_announcements == 2);

    panel->setProperty("urgent", true);
    panel->setProperty("headline", "Camera not found");
    panel->setProperty("body", "Plug one in.");
    REQUIRE(waitFor([] { return g_announcements == 3; }));
    CHECK(g_lastAssertive);
}

TEST_CASE("A caution or complete status has an icon as well as a colour", "[kit][statuspanel]") {
    resetKitState();
    CHECK(create("KitStatusPanel", {{"kind", "normal"}, {"live", false}})->property("iconName").toString().isEmpty());
    CHECK(create("KitStatusPanel", {{"kind", "caution"}, {"live", false}})->property("iconName").toString() == "alert");
    CHECK(create("KitStatusPanel", {{"kind", "complete"}, {"live", false}})->property("iconName").toString() == "check");
}

// ---- Instruction, stage row, empty state -----------------------------------------------------------------------

TEST_CASE("The instruction block reads as one sentence", "[kit][instruction][a11y]") {
    resetKitState();
    Hosted host(create("KitInstruction", {{"side", "Right"}, {"sideColour", "orange"}, {"direction", "downwards"},
                                          {"amount", "Quarter turn"}, {"width", 300}}));
    CHECK(accessibilityOf(host.item()).name == "Right downwards. Quarter turn");
}

TEST_CASE("The instruction block's lines wrap rather than overflow at twice the text size", "[kit][instruction][textscale]") {
    resetKitState();
    invoke(singleton("SystemTextScale"), "setOverride", {2.0});
    auto item = create("KitInstruction", {{"side", "Front"}, {"sideColour", "green"}, {"direction", "anticlockwise"},
                                          {"directionIcon", "turn-anticlockwise"}, {"amount", "Quarter turn"},
                                          {"notation", "F'"}, {"width", 300}});
    for (const char* name : {"side", "direction", "amount"}) {
        QQuickItem* text = child(item.get(), name);
        const QRectF inside = item->mapRectFromItem(text, QRectF(0, 0, text->width(), text->height()));
        INFO(name);
        CHECK(inside.right() <= item->width() + 1);
    }
    resetKitState();
}

TEST_CASE("A stage row's three states differ by number, fill and check", "[kit][stagerow]") {
    resetKitState();
    auto row = [](const char* status) {
        return create("KitStageRow", {{"number", 2}, {"name", "White corners"}, {"explanation", "Put the corners in."},
                                      {"status", status}, {"stateText", status}, {"width", 360}});
    };
    auto todo = row("todo");
    auto current = row("current");
    auto done = row("done");

    // Explanation only for the current stage.
    CHECK_FALSE(child(todo.get(), "explanation")->isVisible());
    CHECK(child(current.get(), "explanation")->isVisible());
    CHECK_FALSE(child(done.get(), "explanation")->isVisible());

    // The current row is tinted; the box is filled.
    CHECK(colour(current->property("color")) != colour(todo->property("color")));
    CHECK(colour(current->property("boxFill")).alpha() == 255);
    CHECK(colour(todo->property("boxFill")).alpha() == 0);

    // A screen reader hears the number, the name and the state.
    CHECK(accessibilityOf(current.get()).name == "2. White corners. current");
    CHECK(accessibilityOf(current.get()).role == QAccessible::ListItem);
}

TEST_CASE("An empty state offers the button that would fill the screen", "[kit][emptystate]") {
    resetKitState();
    Hosted host(create("KitEmptyState", {{"title", "No solves yet"}, {"text", "Scan a cube to start."},
                                         {"actionText", "Scan a cube"}, {"width", 360}}));
    QSignalSpy action(host.item(), SIGNAL(actionClicked()));
    QQuickItem* button = child(host.item(), "action");
    CHECK(button->isVisible());
    host.click(button);
    CHECK(action.count() == 1);

    auto none = create("KitEmptyState", {{"title", "T"}, {"width", 360}});
    CHECK_FALSE(child(none.get(), "action")->isVisible());
}

// ---- Page -------------------------------------------------------------------------------------------------------

TEST_CASE("A page's margins are the size class's plus the safe area's", "[kit][page][sizeclass]") {
    resetKitState();
    Hosted host(createFromSource(R"(
        import QtQuick
        import Cubeulator
        Item {
            width: 400; height: 600
            KitPage { objectName: "page" }
        }
    )"), QSize(400, 600));
    QQuickItem* page = child(host.item(), "page");

    set("Theme", "windowWidth", 400);
    CHECK(page->property("marginLeading").toDouble() == 24);
    CHECK(page->property("marginTop").toDouble() == 24);

    // A notch adds to the top margin only.
    invoke(singleton("SystemInsets"), "setOverride", {44, 0, 0, 34});
    CHECK(page->property("marginTop").toDouble() == 24 + 44);
    CHECK(page->property("marginBottom").toDouble() == 24 + 34);
    CHECK(page->property("marginLeading").toDouble() == 24);

    // And the side insets are leading and trailing, so they mirror.
    invoke(singleton("SystemInsets"), "setOverride", {0, 20, 30, 0});
    CHECK(page->property("marginLeading").toDouble() == 24 + 20);
    CHECK(page->property("marginTrailing").toDouble() == 24 + 30);
    set("SettingsState", "language", "he");
    CHECK(page->property("marginLeading").toDouble() == 24 + 30);
    resetKitState();
}

TEST_CASE("A page centres a column at medium and expanded sizes", "[kit][page][sizeclass]") {
    resetKitState();
    Hosted host(createFromSource(R"(
        import QtQuick
        import Cubeulator
        Item {
            width: 1000; height: 600
            KitPage { objectName: "page" }
        }
    )"), QSize(1000, 600));
    QQuickItem* page = child(host.item(), "page");

    set("Theme", "windowWidth", 700);   // medium
    CHECK(page->property("contentWidth").toDouble() == Approx(560).margin(0.5));
    set("Theme", "windowWidth", 360);   // compact: the whole width less margins
    CHECK(page->property("contentWidth").toDouble() > 300);
    resetKitState();
}

