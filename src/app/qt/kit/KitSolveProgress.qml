import QtQuick
import Cubeulator

// Solve progress (docs/design/system.md, "Progress"): a 6 px bar, a divider
// track, the accent filling from the leading edge, and a 2 px ink tick at
// each stage boundary. It mirrors in a right-to-left layout, because it is a
// sequence.
Item {
    id: root

    // 0 to 1: how far through the solve.
    property real value: 0
    // Where each stage ends, as a fraction of the whole (0 to 1, exclusive).
    property var boundaries: []
    // What it reads as to a screen reader ("Turn 10 of 62").
    property string text: ""

    readonly property real barHeight: 6
    readonly property real tickWidth: Theme.borderRule
    readonly property real tickHeight: 12

    readonly property var contrastPairs: [
        { name: "fill", fg: Theme.accent, bg: Theme.paper, target: 3.0 },
        { name: "tick", fg: Theme.ink, bg: Theme.paper, target: 3.0 }
    ]

    objectName: "kitSolveProgress"
    implicitWidth: 320
    implicitHeight: tickHeight

    Rectangle {
        id: track
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        height: root.barHeight
        color: Theme.divider
        Accessible.ignored: true

        Rectangle {
            id: fill
            objectName: "fill"
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            width: parent.width * Math.max(0, Math.min(1, root.value))
            color: Theme.accent
            Behavior on width {
                NumberAnimation {
                    duration: Theme.motionBase
                    easing.type: Easing.OutCubic
                }
            }
            Accessible.ignored: true
        }
    }

    Repeater {
        model: root.boundaries
        Rectangle {
            required property real modelData
            // x is physical; a mirrored layout counts from the other edge.
            x: (root.LayoutMirroring.enabled ? (1 - modelData) : modelData) * root.width - width / 2
            anchors.verticalCenter: parent.verticalCenter
            width: root.tickWidth
            height: root.tickHeight
            color: Theme.ink
            Accessible.ignored: true
        }
    }

    Accessible.role: Accessible.ProgressBar
    Accessible.name: text
}
