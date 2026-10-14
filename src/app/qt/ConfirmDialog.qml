import QtQuick
import Cubeulator

// A question with a safe answer, over the screen beneath it: a title, a line
// of body if there is one, and two buttons - keep (the safe choice, first in
// reading order, and what Esc, back and a press outside the panel choose) and
// the one that does it.
//
// The dialog's own objectName names the whole of it, and its buttons are that
// name plus ".keep" and ".confirm" (history.deleteDialog.keep, ...). It fills
// its parent, so it is declared last in the screen that asks.
Item {
    id: root

    property bool open: false
    property string title: ""
    property string body: ""
    property string keepText: ""
    property string confirmText: ""
    // Where keyboard focus goes back to when the dialog closes.
    property Item restoreFocusTo: null

    // The safe choice was made (the button, Esc, back, or a press outside).
    signal kept()
    // The thing asked about was chosen.
    signal confirmed()

    anchors.fill: parent
    visible: open
    focus: open
    z: 10

    Accessible.role: Accessible.Dialog
    Accessible.name: root.title

    onOpenChanged: {
        if (open)
            forceActiveFocus();
        else if (restoreFocusTo)
            restoreFocusTo.forceActiveFocus();
    }

    Keys.onEscapePressed: root.kept()
    Keys.onBackPressed: root.kept()

    // The screen beneath is dimmed and does not take presses.
    Rectangle {
        anchors.fill: parent
        color: Theme.ink
        opacity: 0.45
    }
    MouseArea {
        anchors.fill: parent
        onClicked: root.kept()
    }

    Rectangle {
        id: panel
        anchors.centerIn: parent
        width: Math.min(root.width - Theme.space6 * 2, 420)
        height: content.implicitHeight + Theme.space6 * 2
        color: Theme.paper
        border.width: 2
        border.color: Theme.divider

        // Presses on the panel stay on the panel.
        MouseArea { anchors.fill: parent }

        Column {
            id: content
            x: Theme.space6; y: Theme.space6
            width: parent.width - Theme.space6 * 2
            spacing: Theme.space4

            Text {
                width: parent.width
                text: root.title
                wrapMode: Text.WordWrap
                font.family: Theme.fontHeading; font.weight: Font.ExtraBold; font.pixelSize: 20
                color: Theme.ink
                Accessible.ignored: true
            }
            Text {
                visible: root.body !== ""
                width: parent.width
                text: root.body
                wrapMode: Text.WordWrap
                font.family: Theme.fontHeading; font.pixelSize: 14
                color: Theme.inkMuted(0.7)
                Accessible.ignored: true
            }
            Flow {
                width: parent.width
                spacing: Theme.space3
                AppButton {
                    objectName: root.objectName + ".keep"
                    variant: "primary"
                    maxWidth: parent.width
                    text: root.keepText
                    onClicked: root.kept()
                }
                AppButton {
                    objectName: root.objectName + ".confirm"
                    variant: "secondary"
                    maxWidth: parent.width
                    text: root.confirmText
                    onClicked: root.confirmed()
                }
            }
        }
    }
}
