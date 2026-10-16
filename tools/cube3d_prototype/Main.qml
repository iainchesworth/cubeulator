import QtQuick
import QtQuick.Window
import CubePrototype

// Plays the player's moves on one of the two renderers, one after another, and
// reports what the frames cost while the cube is still and while it turns.
Window {
    id: win

    required property Player player
    required property FrameStats stats
    required property string mode
    required property int turnMs
    required property int idleMs
    required property string screenshot
    required property string screenshotMid
    required property bool solving

    visible: true
    width: 900
    height: 900
    // A measurement should not take the focus from whoever is at the machine.
    flags: Qt.Window | Qt.WindowDoesNotAcceptFocus | Qt.WindowStaysOnBottomHint
    title: "cube3d prototype - " + mode + " " + player.size + "x" + player.size

    Loader {
        id: scene
        anchors.fill: parent
        sourceComponent: win.mode === "naive" ? naive : lift
    }
    Component { id: lift; LiftCube { player: win.player; stats: win.stats; turnMs: win.turnMs } }
    Component { id: naive; NaiveCube { player: win.player; stats: win.stats; turnMs: win.turnMs } }

    property int played: 0
    property real startedAt: 0

    Component.onCompleted: stats.attach(win)

    // Let the scene draw once, watch it still for idleMs, then play.
    Timer {
        id: settle
        interval: 300
        running: scene.status === Loader.Ready
        onTriggered: {
            win.stats.phase("idle")
            watch.start()
        }
    }
    Timer {
        id: watch
        interval: win.idleMs
        onTriggered: {
            win.stats.phase("moving")
            win.startedAt = win.stats.nowMicros()
            win.step()
        }
    }

    property real moveStartedAt: 0
    function step() {
        moveStartedAt = stats.nowMicros()
        if (!scene.item.begin()) {
            finish()
        }
    }

    function finish() {
        stats.phase("")
        var elapsed = (stats.nowMicros() - startedAt) / 1000
        if (screenshot !== "") {
            // A frame of the cube as it stands, drawn before the picture is taken.
            Qt.callLater(function() { win.stats.grab(win.screenshot) })
        }
        Qt.callLater(function() {
            win.stats.finish({
                mode: win.mode,
                size: win.player.size,
                moves: win.played,
                solving: win.solving,
                turnMs: win.turnMs,
                playedMs: elapsed,
                objects: scene.item.objects,
                quads: scene.item.quads,
                builds: scene.item.builds
            })
        })
    }

    property bool grabbedMid: false
    Connections {
        target: scene.item
        function onAngleChanged() {
            if (win.screenshotMid !== "" && win.played === 3 && !win.grabbedMid
                    && Math.abs(scene.item.angle) > 35) {
                win.grabbedMid = true
                Qt.callLater(function() { win.stats.grab(win.screenshotMid) })
            }
        }
        function onFinished() {
            // What one move took on the clock, which a cube that cannot keep up
            // with its own animation shows as more than its turn time.
            win.stats.tally("moveWall", win.stats.nowMicros() - win.moveStartedAt)
            win.played++
            win.step()
        }
    }
}
