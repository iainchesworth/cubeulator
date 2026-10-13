import QtQuick
import Cubeulator

// A key's name ("Space") in numeral type inside a 1 px box, shown beside the
// solve screen's main button on desktop as a reminder. Always left to right,
// whatever the layout (docs/design/system.md, "Key name").
Rectangle {
    id: root

    property string text: ""
    // The colour of the box and its text. Not `color`: that is the Rectangle's fill.
    property color ink: Theme.ink

    implicitWidth: label.implicitWidth + 2 * Theme.space2
    implicitHeight: label.implicitHeight + 2 * Theme.space1

    color: "transparent"
    border.width: Theme.borderHair
    border.color: root.ink
    radius: Theme.radius

    LayoutMirroring.enabled: false
    LayoutMirroring.childrenInherit: true

    KitText {
        id: label
        anchors.centerIn: parent
        text: root.text
        role: "numeral"
        color: root.ink
        wrapMode: Text.NoWrap
        // The box is decoration for the key's name; the control it sits in
        // names itself.
        Accessible.ignored: true
    }

    Accessible.ignored: true
}
