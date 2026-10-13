import QtQuick
import Cubeulator

// Notice (docs/design/system.md, "Notice"): something the person should know,
// in place. A surface fill, a 2 px outline edge, a leading icon, a bold first
// line, a sentence, and optional buttons.
//
// The kind is told by the icon, never by colour alone: information has a
// circled "i", caution a triangle, positive a check. Screen readers are told
// when one appears (docs/design/accessibility.md).
Rectangle {
    id: root

    // information | caution | positive
    property string kind: "information"
    // The bold first line.
    property string heading: ""
    // The sentence. `text` rather than `body` so a notice reads like the
    // kit's other things with words.
    property string text: ""
    // Say it to a screen reader when it appears. Assertive only for
    // something that stops the person.
    property bool announce: true
    property bool assertive: false
    // Buttons, if any: up to two, set `block` so they fill the notice.
    default property alias actions: actionRow.data

    readonly property string iconName: kind === "caution" ? "alert" : (kind === "positive" ? "check" : "info")
    readonly property color iconColor: kind === "caution" ? Theme.caution : (kind === "positive" ? Theme.ok : Theme.ink)

    readonly property var contrastPairs: [
        { name: "heading", fg: Theme.ink, bg: Theme.surface, target: 4.5 },
        { name: "text", fg: Theme.ink, bg: Theme.surface, target: 4.5 },
        { name: "icon", fg: iconColor, bg: Theme.surface, target: kind === "information" ? 4.5 : 4.5 },
        { name: "edge", fg: Theme.outline, bg: Theme.paper, target: 3.0 }
    ]

    objectName: "kitNotice"
    implicitWidth: 320
    implicitHeight: content.implicitHeight + 2 * Theme.space4 + 2 * Theme.borderRule
    color: Theme.surface
    border.width: Theme.borderRule
    border.color: Theme.outline
    radius: Theme.radius

    Row {
        id: content
        x: Theme.space4 + Theme.borderRule
        y: Theme.space4 + Theme.borderRule
        width: root.width - 2 * (Theme.space4 + Theme.borderRule)
        spacing: Theme.space3

        KitIcon {
            id: icon
            name: root.iconName
            size: 24
            color: root.iconColor
        }

        Column {
            width: parent.width - icon.width - parent.spacing
            spacing: Theme.space2

            KitText {
                objectName: "heading"
                width: parent.width
                text: root.heading
                role: "bodyStrong"
                visible: root.heading !== ""
                Accessible.ignored: true
            }
            KitText {
                objectName: "text"
                width: parent.width
                text: root.text
                role: "body"
                visible: root.text !== ""
                Accessible.ignored: true
            }
            // Stacked, one under another, each as wide as the notice (give them
            // `block`): beside each other they would not fit at large text
            // sizes or in a long translation.
            Column {
                id: actionRow
                width: parent.width
                spacing: Theme.space2
            }
        }
    }

    function spoken() {
        return heading !== "" && text !== "" ? heading + ". " + text : heading + text;
    }

    Component.onCompleted: {
        if (announce)
            Announcer.announce(root, spoken(), assertive);
    }

    Accessible.role: Accessible.AlertMessage
    Accessible.name: spoken()
}
