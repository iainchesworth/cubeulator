pragma Singleton
import QtQuick
import Cubeulator

// Screen-routing state, delegating to real controllers for each phase:
// CameraController/ScanSession own the scan phase, SolveController owns the
// solve phase (moves, stickers, stage progress) via a real
// cube::solver::solve() fed the actual scanned cube (ScanSession's
// free-form-assembled cube - see solve_controller.hpp).
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

    // SolveController reports solveReady once a completed scan's assembled
    // cube has its solve queued, or scanRejected if its legacy fallback
    // conversion failed - ScanSession's own scanCompleted isn't enough on
    // its own, since on the fallback path a completed scan isn't
    // necessarily a valid one (see solve_controller.cpp).
    property var solveControllerConnections: Connections {
        target: SolveController
        function onSolveReady() {
            scanError = "";
            scanRetryMessage = "";
            state.screen = "solve";
        }
        function onScanRejected(reason) {
            scanError = reason;
        }
    }

    // Set by onScanRejected above; ScanScreen.qml shows it and clears it on
    // the next successful solveReady. The scan has already restarted (see
    // SolveController::onScanCompleted's restartScan() call) - the user
    // stays on the scan screen and just keeps showing faces.
    property string scanError: ""

    // Free-form recovery: six captured faces failed to resolve/assemble
    // into a buildable cube, ScanSession dropped the most-suspect slot, and
    // scanning continues at 5 of 6. Slots carry no face identity, so the
    // banner can't name WHICH face was misread - the wording stays generic.
    // Transient: cleared by the timer (the count in the scan prompt already
    // shows the dropped slot) or by the next successful solve.
    property string scanRetryMessage: ""
    property var scanSessionConnections: Connections {
        target: ScanSession
        function onScanNeedsRetry(reason) {
            scanRetryMessage = "One face looked misread — keep scanning, show faces again";
            scanRetryTimer.restart();
        }
        // A layer was turned mid-scan: every capture predating it described
        // a sticker arrangement that no longer exists, so ScanSession
        // cleared ALL slots (facesScanned is back to 0). Same transient
        // banner strip; the wording explains the reset so the emptied
        // progress row doesn't read as a bug.
        function onScanInvalidated(reason) {
            scanRetryMessage = "Cube changed mid-scan — starting the capture over";
            scanRetryTimer.restart();
        }
    }
    property var scanRetryTimer: Timer {
        interval: 6000
        onTriggered: state.scanRetryMessage = ""
    }

    readonly property int scanned: ScanSession.facesScanned

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

    function scanAnother() {
        elapsedSeconds = 0;
        screen = "scan";
    }
}
