pragma Singleton
import QtQuick
import Cubeulator

// Screen-routing and scan-mock state for the scan -> solve -> complete
// flow. Solve-stage state (moves, stickers, stage progress) now lives in
// SolveController, which wraps a real cube::solver::solve() - see
// solve_controller.hpp. The real camera/vision pipeline is still not
// implemented (see docs/architecture.md - Spec 00 skeleton).
QtObject {
    id: state

    property string screen: "launch" // launch | permission | scan | solve | complete
    property bool cameraGranted: false

    property int scanned: 0
    readonly property int totalFaces: 6

    readonly property var faceDefs: [
        { code: "U", name: "top", color: Theme.faceU },
        { code: "R", name: "right", color: Theme.faceR },
        { code: "F", name: "front", color: Theme.faceF },
        { code: "D", name: "bottom", color: Theme.faceD },
        { code: "L", name: "left", color: Theme.faceL },
        { code: "B", name: "back", color: Theme.faceB }
    ]

    // Thin passthrough so CompleteScreen.qml's move-count readout doesn't
    // need its own SolveController binding.
    readonly property int moveCount: SolveController.totalMoves

    property int elapsedSeconds: 0

    property var scanTimer: Timer {
        interval: 1300; repeat: true; running: state.screen === "scan"
        onTriggered: {
            if (state.scanned < state.totalFaces)
                state.scanned += 1;
            if (state.scanned >= state.totalFaces) {
                stop();
                state.screen = "solve";
            }
        }
    }

    property var clockTimer: Timer {
        interval: 1000; repeat: true
        running: state.screen === "scan" || state.screen === "solve"
        onTriggered: state.elapsedSeconds += 1
    }

    function elapsedLabel() {
        var m = Math.floor(state.elapsedSeconds / 60);
        var s = state.elapsedSeconds % 60;
        return (m < 10 ? "0" + m : m) + ":" + (s < 10 ? "0" + s : s);
    }

    function goLaunch() { screen = "launch"; }
    function requestPermission() { screen = "permission"; }

    function beginScan() {
        if (cameraGranted) {
            scanned = 0;
            elapsedSeconds = 0;
            screen = "scan";
        } else {
            screen = "permission";
        }
    }

    function grantCamera() {
        cameraGranted = true;
        scanned = 0;
        elapsedSeconds = 0;
        screen = "scan";
    }

    function restart() {
        scanned = 0;
        elapsedSeconds = 0;
        screen = "launch";
    }

    // Cycles through a small set of seeds confirmed (during this feature's
    // development) to solve quickly - solve_pll's search can occasionally
    // take a very long time on an arbitrary scramble (seed 3, notably -
    // see pll.cpp's history), and there's no real vision pipeline yet to
    // source a scramble from anyway, so picking from known-fast seeds
    // avoids freezing the UI on solve()'s still-synchronous call.
    property int nextScrambleSeed: 1

    function scanAnother() {
        scanned = 0;
        elapsedSeconds = 0;
        SolveController.restart(nextScrambleSeed);
        nextScrambleSeed = (nextScrambleSeed + 1) % 3;
        screen = "scan";
    }
}
