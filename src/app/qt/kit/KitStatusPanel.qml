import QtQuick
import Cubeulator

// Status panel (docs/design/system.md, "Status panel"): the scan screen's
// headline and guidance, on an overlay panel at the top leading corner, at
// most 520 wide. `headline` then `body`. Caution and complete add a leading
// icon. It is a live region: a change is announced politely, the same words
// twice within two seconds are said once, and a camera failure is assertive.
//
// Drawn over the camera, so it is the same in both themes.
Rectangle {
    id: root

    property string headline: ""
    property string body: ""
    // normal | caution | complete
    property string kind: "normal"
    // Interrupts what is being read. For a failure that stops the person.
    property bool urgent: false
    // Tell a screen reader about changes.
    property bool live: true

    readonly property real maxWidth: 520
    readonly property string iconName: kind === "caution" ? "alert" : (kind === "complete" ? "check" : "")
    readonly property color iconColor: kind === "caution" ? Theme.overlay.caution : Theme.overlay.ok

    readonly property var contrastPairs: [
        { name: "headline", fg: Theme.overlay.ink, bg: Theme.overlay.worstCase, target: 4.5 },
        { name: "body", fg: Theme.overlay.inkSecondary, bg: Theme.overlay.worstCase, target: 4.5 },
        { name: "icon", fg: iconColor, bg: Theme.overlay.worstCase, target: 4.5 }
    ]

    objectName: "kitStatusPanel"
    implicitWidth: Math.min(maxWidth, 520)
    implicitHeight: content.implicitHeight + 2 * Theme.space4
    color: Theme.overlay.panel
    radius: Theme.radius

    Row {
        id: content
        x: Theme.space4
        y: Theme.space4
        width: root.width - 2 * Theme.space4
        spacing: Theme.space3

        KitIcon {
            id: icon
            visible: root.iconName !== ""
            name: root.iconName
            size: 24
            color: root.iconColor
        }
        Column {
            width: parent.width - (icon.visible ? icon.width + parent.spacing : 0)
            spacing: Theme.space1

            KitText {
                objectName: "headline"
                width: parent.width
                text: root.headline
                role: "headline"
                onOverlay: true
                Accessible.ignored: true
            }
            KitText {
                objectName: "body"
                width: parent.width
                visible: root.body !== ""
                text: root.body
                role: "body"
                color: Theme.overlay.inkSecondary
                Accessible.ignored: true
            }
        }
    }

    // ---- Live region -----------------------------------------------------------------
    // What was last said and when, so that the same words twice within two
    // seconds are said once.
    property string lastSpoken: ""
    property double lastSpokenAt: 0
    function spoken() {
        return body !== "" ? headline + ". " + body : headline;
    }
    function speak() {
        if (!live)
            return;
        const words = spoken();
        const now = Date.now();
        if (words === "" || (words === lastSpoken && now - lastSpokenAt < 2000))
            return;
        lastSpoken = words;
        lastSpokenAt = now;
        Announcer.announce(root, words, urgent);
    }
    // A headline and a body that change together are one change: speak what
    // they have become, not each step on the way.
    Timer {
        id: settle
        interval: 40
        onTriggered: root.speak()
    }
    onHeadlineChanged: settle.restart()
    onBodyChanged: settle.restart()
    Component.onCompleted: settle.restart()

    // Name and guidance together: when it is read rather than announced, one
    // read says all of it.
    Accessible.role: Accessible.Section
    Accessible.name: spoken()
    Accessible.description: body
}
