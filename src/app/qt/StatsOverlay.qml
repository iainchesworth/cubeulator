import QtQuick
import QtQuick.Controls
import Cubeulator

// "Stats for nerds": a small read-only HUD any rendered surface can drop in
// and toggle via SettingsState.statsForNerds - not just Cube3DView. Picking
// the rendering backend itself is a real setting (SettingsScreen's
// RENDERING section), not tucked away behind this debug toggle - this just
// reports what's actually running. Reparents itself into the
// ApplicationWindow's Overlay layer so it always sits in the actual
// window's bottom-right corner, regardless of where the Item that hosts it
// (e.g. a small cube preview) is laid out on screen. Frame rate comes from
// counting the host window's frameSwapped signal (the whole window shares
// one render loop; there is no meaningful per-Item frame rate in Qt Quick).
Item {
    id: root
    property bool active: false
    visible: active
    anchors.margins: 12
    width: content.implicitWidth + 16
    height: content.implicitHeight + 12

    property int frameCount: 0
    property real fps: 0

    readonly property string backendName: SettingsState.graphicsApiNames[GraphicsInfo.api] || ("API " + GraphicsInfo.api)

    // Reparent into the window's overlay layer (spans the whole window,
    // above everything) so anchors.right/bottom below land on the true
    // window edge instead of whatever local Item happens to host this.
    Component.onCompleted: {
        if (typeof Overlay !== "undefined" && Overlay.overlay)
            root.parent = Overlay.overlay;
    }
    onParentChanged: {
        if (parent) {
            anchors.right = parent.right;
            anchors.bottom = parent.bottom;
        }
    }

    Connections {
        target: root.Window.window
        function onFrameSwapped() { root.frameCount += 1; }
    }

    Timer {
        interval: 1000
        running: root.active
        repeat: true
        onTriggered: { root.fps = root.frameCount; root.frameCount = 0; }
    }

    Rectangle {
        anchors.fill: parent
        color: Qt.rgba(0, 0, 0, 0.35)
    }

    Column {
        id: content
        anchors.centerIn: parent
        spacing: 2
        Text { text: "FPS " + root.fps.toFixed(0); color: "#8CFF8C"; font.family: Theme.fontMono; font.pixelSize: 11 }
        Text { text: root.backendName; color: "#BAB6B6"; font.family: Theme.fontMono; font.pixelSize: 11 }
    }
}
