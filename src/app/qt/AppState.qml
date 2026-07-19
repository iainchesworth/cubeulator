pragma Singleton
import QtQuick
import Cubeulator

// Screen-routing state, delegating to real controllers for each phase:
// CameraController/ScanSession own the scan phase, SolveController owns the
// solve phase (moves, stickers, stage progress) via a real
// cube::solver::solve() - see solve_controller.hpp. The two aren't wired
// together yet - SolveController still solves a random_scramble()'d cube
// rather than what ScanSession actually reads (see docs/architecture.md).
QtObject {
    id: state

    property string screen: "launch" // launch | permission | scan | solve | settings | complete

    // Camera-access screens exist only for mobile platforms, where the OS
    // requires an explicit in-app permission prompt. Desktop platforms grant
    // camera access via their own OS-level dialog the first time the device
    // is opened (see docs/architecture.md - Spec 00 skeleton), so "Start
    // scan" goes straight to the scan screen there.
    readonly property bool isMobile: Qt.platform.os === "android" || Qt.platform.os === "ios"

    // Qt::PermissionStatus::Granted - see CameraPermission.hpp.
    readonly property bool cameraGranted: CameraPermission.status === 2

    property string previousScreen: "launch"

    // Kicks off device enumeration immediately at startup rather than
    // waiting for a "grant access" button press: on desktop, beginScan()
    // goes straight to "scan" without ever visiting the permission screen
    // (see isMobile above), so nothing else would trigger it in time for
    // ScanSession.start() to have a selectedDeviceId ready.
    Component.onCompleted: CameraController.refreshDevices()

    // Starts/stops the live capture to match the screen shown.
    onScreenChanged: {
        if (screen === "scan") {
            ScanSession.start(CameraController.selectedDeviceId);
        } else if (ScanSession.running || ScanSession.starting) {
            ScanSession.stop();
        }
    }

    // ScanSession reports scanCompleted once all 6 faces are confirmed
    // (manually via confirmFace(), or automatically when autoAdvance is on).
    property var scanSessionConnections: Connections {
        target: ScanSession
        function onScanCompleted() {
            state.screen = "solve";
        }
    }

    readonly property int scanned: ScanSession.facesScanned

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

    function openSettings() {
        previousScreen = screen;
        screen = "settings";
    }

    function closeSettings() {
        screen = previousScreen;
    }

    function beginScan() {
        if (cameraGranted || !isMobile) {
            elapsedSeconds = 0;
            screen = "scan";
        } else {
            screen = "permission";
        }
    }

    // Called once CameraPermission reports Granted while the permission
    // screen is up (see its Connections block) - moves on to scanning the
    // same way beginScan() does when permission was already granted.
    function proceedToScan() {
        elapsedSeconds = 0;
        screen = "scan";
    }

    function restart() {
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
        elapsedSeconds = 0;
        SolveController.restart(nextScrambleSeed);
        nextScrambleSeed = (nextScrambleSeed + 1) % 3;
        screen = "scan";
    }
}
