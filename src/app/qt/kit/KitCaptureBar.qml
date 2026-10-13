import QtQuick
import Cubeulator

// Capture bar (docs/design/system.md, "Progress"): the scan screen's 6 px
// bar along the bottom of the next chip, filling as the side is held. It is
// drawn over the camera, so it takes the overlay's colours.
Item {
    id: root

    // 0 to 1.
    property real value: 0
    // What it reads as to a screen reader.
    property string text: ""

    readonly property var contrastPairs: [
        { name: "fill", fg: Theme.overlay.ok, bg: Theme.overlay.worstCase, target: 3.0 },
        { name: "track", fg: Theme.overlay.outline, bg: Theme.overlay.worstCase, target: 3.0 }
    ]

    objectName: "kitCaptureBar"
    implicitWidth: 120
    implicitHeight: 6

    Rectangle {
        anchors.fill: parent
        color: Theme.overlay.outline
        Accessible.ignored: true

        Rectangle {
            objectName: "fill"
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            width: parent.width * Math.max(0, Math.min(1, root.value))
            color: Theme.overlay.ok
            Accessible.ignored: true
        }
    }

    Accessible.role: Accessible.ProgressBar
    Accessible.name: text
}
