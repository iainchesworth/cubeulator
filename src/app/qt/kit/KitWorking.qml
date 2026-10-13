import QtQuick
import Cubeulator

// Working (docs/design/system.md, "Progress"): three 12 px accent squares
// pulsing in turn, with a `headline` saying what is being worked out. With
// reduced motion the squares are still and the words carry it.
Item {
    id: root

    // What is happening ("Working out the solution").
    property string text: ""
    property bool running: true
    property bool onOverlay: false
    // Tell a screen reader when it appears.
    property bool announce: true

    readonly property bool still: Theme.reducedMotion || !running
    readonly property color squareColor: Theme.accent
    readonly property color pageColor: onOverlay ? Theme.overlay.worstCase : Theme.paper
    readonly property var contrastPairs: [
        { name: "squares", fg: squareColor, bg: pageColor, target: 3.0 },
        { name: "text", fg: onOverlay ? Theme.overlay.ink : Theme.ink, bg: pageColor, target: 4.5 }
    ]

    objectName: "kitWorking"
    implicitWidth: Math.max(label.implicitWidth, row.implicitWidth)
    implicitHeight: row.implicitHeight + Theme.space3 + label.implicitHeight

    Row {
        id: row
        spacing: Theme.space2
        anchors.horizontalCenter: parent.horizontalCenter

        Repeater {
            model: 3
            Rectangle {
                id: square
                required property int index
                width: 12
                height: 12
                color: root.squareColor
                radius: Theme.radius
                opacity: 1
                Accessible.ignored: true

                // Each square dims in turn; with motion reduced it just
                // stays on. A square's pulse starts a third of a cycle after
                // the one before it.
                SequentialAnimation on opacity {
                    running: !root.still
                    loops: Animation.Infinite
                    onStopped: square.opacity = 1
                    PauseAnimation {
                        duration: square.index * 200
                    }
                    NumberAnimation {
                        from: 1
                        to: 0.25
                        duration: 300
                        easing.type: Easing.InOutQuad
                    }
                    NumberAnimation {
                        from: 0.25
                        to: 1
                        duration: 300
                        easing.type: Easing.InOutQuad
                    }
                    PauseAnimation {
                        duration: (2 - square.index) * 200
                    }
                }
            }
        }
    }

    KitText {
        id: label
        objectName: "label"
        text: root.text
        role: "headline"
        onOverlay: root.onOverlay
        horizontalAlignment: Text.AlignHCenter
        anchors.top: row.bottom
        anchors.topMargin: Theme.space3
        anchors.left: parent.left
        anchors.right: parent.right
        Accessible.ignored: true
    }

    Component.onCompleted: {
        if (announce && text !== "")
            Announcer.announce(root, text, false);
    }

    Accessible.role: Accessible.ProgressBar
    Accessible.name: text
}
