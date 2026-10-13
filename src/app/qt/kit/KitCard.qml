import QtQuick
import Cubeulator

// Card (docs/design/system.md, "Card"): the one object on a screen that
// needs deciding, such as the unfinished solve on Home. A 2 px ink edge, no
// fill, content and up to two buttons. There is at most one on a screen.
Rectangle {
    id: root

    // Optional heading above the content.
    property string heading: ""
    // The content between the heading and the buttons.
    default property alias content: contentColumn.data
    // The buttons: up to two.
    property alias actions: actionColumn.data

    readonly property var contrastPairs: [
        { name: "heading", fg: Theme.ink, bg: Theme.paper, target: 4.5 },
        { name: "edge", fg: Theme.ink, bg: Theme.paper, target: 3.0 }
    ]

    objectName: "kitCard"
    implicitWidth: 320
    implicitHeight: column.implicitHeight + 2 * (Theme.space4 + Theme.borderRule)
    color: "transparent"
    border.width: Theme.borderRule
    border.color: Theme.ink
    radius: Theme.radius

    Column {
        id: column
        x: Theme.space4 + Theme.borderRule
        y: Theme.space4 + Theme.borderRule
        width: root.width - 2 * (Theme.space4 + Theme.borderRule)
        spacing: Theme.space3

        KitText {
            objectName: "heading"
            width: parent.width
            text: root.heading
            role: "headline"
            visible: root.heading !== ""
        }
        Column {
            id: contentColumn
            width: parent.width
            spacing: Theme.space2
        }
        Column {
            id: actionColumn
            width: parent.width
            spacing: Theme.space2
        }
    }

    Accessible.role: Accessible.Grouping
    Accessible.name: heading
}
