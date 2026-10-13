import QtQuick
import QtQuick.Controls.Basic
import Cubeulator

// Top bar (docs/design/system.md, "Top bar"): 56 high. Back, an icon button,
// at the leading edge; the screen's title in `label` type; an optional action
// at the trailing edge; a 2 px divider rule beneath. On two-pane screens the
// bar spans both.
//
// The title is the screen's heading: entering a screen moves a screen
// reader's focus to it (docs/design/accessibility.md).
Item {
    id: root

    // The screen's title.
    property string title: ""
    // The back control's name and tooltip ("Back"). Required to be set by the
    // screen, since the kit holds no user-visible words.
    property string backText: ""
    property bool showBack: true
    property bool onOverlay: false
    // Whatever sits at the trailing edge: an icon button, a ghost button.
    default property alias trailing: trailingRow.data

    signal backClicked

    readonly property color pageColor: onOverlay ? Theme.overlay.worstCase : Theme.paper
    readonly property var contrastPairs: [
        { name: "title", fg: onOverlay ? Theme.overlay.ink : Theme.ink, bg: pageColor, target: 4.5 },
        { name: "rule", fg: onOverlay ? Theme.overlay.outline : Theme.divider, bg: pageColor, target: 1.0 }
    ]

    objectName: "kitTopBar"
    implicitWidth: 320
    implicitHeight: Math.max(Theme.targetThumb, titleText.implicitHeight + 2 * Theme.space3) + Theme.borderRule

    KitIconButton {
        id: back
        objectName: "back"
        visible: root.showBack
        iconName: "chevron-left"
        text: root.backText
        onOverlay: root.onOverlay
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.topMargin: ((root.height - Theme.borderRule) - height) / 2
        onClicked: root.backClicked()
    }

    KitText {
        id: titleText
        objectName: "title"
        text: root.title
        role: "label"
        onOverlay: root.onOverlay
        anchors.left: back.visible ? back.right : parent.left
        anchors.leftMargin: back.visible ? Theme.space2 : Theme.space4
        anchors.right: trailingRow.left
        anchors.rightMargin: Theme.space2
        anchors.top: parent.top
        anchors.topMargin: ((root.height - Theme.borderRule) - height) / 2
        Accessible.role: Accessible.Heading
    }

    Row {
        id: trailingRow
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.topMargin: ((root.height - Theme.borderRule) - height) / 2
        spacing: Theme.space2
    }

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: Theme.borderRule
        color: root.onOverlay ? Theme.overlay.outline : Theme.divider
        Accessible.ignored: true
    }

    Accessible.role: Accessible.ToolBar
    Accessible.name: title
}
