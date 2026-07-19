pragma Singleton
import QtQuick
import Cubeulator

// Scan-phase state delegates to CameraController/ScanSession. Solve phase
// (step/steps/elapsedSeconds/etc.) stays a demo mock - solving itself isn't
// implemented yet (see docs/architecture.md - Spec 00 skeleton).
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

    property int step: 0
    readonly property var steps: [
        { title: "White cross", detail: "Bring the four white edges to the top face, each matching its side centre.", move: "F R U R' U' F'" },
        { title: "White corners", detail: "Insert the four white corners to complete the first layer.", move: "R U R' U'" },
        { title: "Middle layer", detail: "Send the middle-layer edges home with the right-hand insert.", move: "U R U' R' U' F' U F" },
        { title: "Yellow cross", detail: "Orient the last-layer edges into a cross on the yellow face.", move: "F R U R' U' F'" },
        { title: "Yellow face", detail: "Orient the last four corners so the whole top reads yellow.", move: "R U R' U R U2 R'" },
        { title: "Final layer", detail: "Permute the last-layer pieces into their solved positions.", move: "R U' R U R U R U' R' U' R2" }
    ]

    property int moveCount: 42
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

    function nextStep() {
        if (step < steps.length - 1)
            step += 1;
        else
            screen = "complete";
    }

    function prevStep() {
        if (step > 0)
            step -= 1;
    }

    function restart() {
        step = 0;
        elapsedSeconds = 0;
        screen = "launch";
    }

    function scanAnother() {
        step = 0;
        elapsedSeconds = 0;
        screen = "scan";
    }
}
