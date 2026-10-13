import QtQuick
import Cubeulator

// Empty state (docs/design/system.md, "Empty state"): a `title`, one
// sentence, and the button that would fill the screen. Never stranded: an
// empty list always says what to do about it.
Item {
    id: root

    property string title: ""
    // The one sentence.
    property string text: ""
    // The button that would fill the screen. Empty: no button.
    property string actionText: ""

    signal actionClicked

    readonly property var contrastPairs: [
        { name: "title", fg: Theme.ink, bg: Theme.paper, target: 4.5 },
        { name: "text", fg: Theme.ink, bg: Theme.paper, target: 4.5 }
    ]

    objectName: "kitEmptyState"
    implicitWidth: 320
    implicitHeight: column.implicitHeight

    Column {
        id: column
        width: Math.min(parent.width, Theme.measure)
        anchors.horizontalCenter: parent.horizontalCenter
        spacing: Theme.space4

        KitText {
            objectName: "title"
            width: parent.width
            text: root.title
            role: "title"
            horizontalAlignment: Text.AlignHCenter
        }
        KitText {
            objectName: "text"
            width: parent.width
            text: root.text
            role: "body"
            horizontalAlignment: Text.AlignHCenter
        }
        Item {
            width: parent.width
            height: action.height
            visible: root.actionText !== ""

            KitButton {
                id: action
                objectName: "action"
                text: root.actionText
                size: "thumb"
                anchors.horizontalCenter: parent.horizontalCenter
                // A long label wraps inside the column rather than past it.
                width: Math.min(implicitWidth, parent.width)
                onClicked: root.actionClicked()
            }
        }
    }

    Accessible.role: Accessible.Grouping
    Accessible.name: title
}
