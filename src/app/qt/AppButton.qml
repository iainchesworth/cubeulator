import QtQuick
import Cubeulator

// btn / btn-primary / btn-secondary / btn-ghost from the Modernist system:
// flat fills, zero radius, flush-left labels in block buttons.
Item {
    id: root

    property string text: ""
    property string variant: "primary" // primary | secondary | ghost
    property string trailingIcon: ""
    property string leadingIcon: ""
    property bool block: false // fill width; label flush-left instead of centered
    property bool enabled: true
    implicitHeight: 44
    implicitWidth: block ? 200 : (label.implicitWidth + (root.leadingIcon ? 26 : 0) + (root.trailingIcon ? 26 : 0) + Theme.space6)

    signal clicked()

    readonly property bool primary: variant === "primary"
    readonly property bool secondary: variant === "secondary"
    readonly property bool ghost: variant === "ghost"

    opacity: enabled ? 1.0 : 0.45

    Rectangle {
        anchors.fill: parent
        color: root.primary ? (mouse.pressed ? Theme.accent700 : (mouse.containsMouse ? Theme.accent600 : Theme.accent))
             : root.secondary ? (mouse.containsMouse ? Theme.inkMuted(0.07) : "transparent")
             : (mouse.containsMouse ? Qt.rgba(0.925, 0.188, 0.075, 0.10) : "transparent")
        border.width: root.secondary ? 1 : 0
        border.color: Theme.divider
    }

    Row {
        id: content
        anchors.verticalCenter: parent.verticalCenter
        anchors.left: root.block ? parent.left : undefined
        anchors.leftMargin: root.block ? Theme.space4 : 0
        anchors.horizontalCenter: root.block ? undefined : parent.horizontalCenter
        spacing: 8

        Icon {
            visible: root.leadingIcon !== ""
            name: root.leadingIcon
            width: 16; height: 16
            anchors.verticalCenter: parent.verticalCenter
            color: root.ghost ? Theme.accent : Theme.ink
        }

        Text {
            id: label
            text: root.text
            font.family: Theme.fontHeading
            font.weight: Font.ExtraBold
            font.pixelSize: 14
            anchors.verticalCenter: parent.verticalCenter
            color: root.primary ? Theme.paper : (root.ghost ? Theme.accent : Theme.ink)
        }
    }

    Icon {
        visible: root.trailingIcon !== "" && root.block
        name: root.trailingIcon
        width: 18; height: 18
        anchors.verticalCenter: parent.verticalCenter
        anchors.right: parent.right
        anchors.rightMargin: Theme.space4
        color: root.primary ? Theme.paper : Theme.ink
    }

    Icon {
        visible: root.trailingIcon !== "" && !root.block
        name: root.trailingIcon
        width: 18; height: 18
        anchors.verticalCenter: parent.verticalCenter
        anchors.left: content.right
        anchors.leftMargin: 8
        color: root.primary ? Theme.paper : Theme.ink
    }

    MouseArea {
        id: mouse
        anchors.fill: parent
        hoverEnabled: true
        enabled: root.enabled
        cursorShape: Qt.PointingHandCursor
        onClicked: root.clicked()
    }
}
