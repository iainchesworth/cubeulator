import QtQuick
import Cubeulator

// Countdown (docs/design/system.md, "Progress"): the paced solve's 8 px bar
// draining to the leading edge over the pace. Paused, it is full and still.
Item {
    id: root

    // How long the bar takes to drain, in milliseconds.
    property int duration: 4000
    property bool running: false
    property bool paused: false
    // What it reads as to a screen reader ("Next turn in 4 seconds").
    property string text: ""

    signal finished

    readonly property bool draining: running && !paused
    readonly property var contrastPairs: [
        { name: "fill", fg: Theme.accent, bg: Theme.paper, target: 3.0 }
    ]

    objectName: "kitCountdown"
    implicitWidth: 320
    implicitHeight: 8

    // 1 is full. Restarted from full each time `running` becomes true.
    property real remaining: 1

    onDrainingChanged: {
        if (draining) {
            drain.restart();
        } else {
            drain.stop();
            // Paused is full and still; stopped for any other reason is too.
            remaining = 1;
        }
    }

    NumberAnimation {
        id: drain
        target: root
        property: "remaining"
        from: 1
        to: 0
        duration: root.duration
        easing.type: Easing.Linear
        onFinished: root.finished()
    }

    Rectangle {
        anchors.fill: parent
        color: Theme.divider
        Accessible.ignored: true

        Rectangle {
            objectName: "fill"
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            width: parent.width * root.remaining
            color: Theme.accent
            Accessible.ignored: true
        }
    }

    Accessible.role: Accessible.ProgressBar
    Accessible.name: text
}
