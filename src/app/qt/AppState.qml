pragma Singleton
import QtQuick
import Cubeulator

// Screen-routing state, delegating to real controllers for each phase:
// CameraController/DirectScan own the scan phase, SolveController owns the
// solve phase (moves, stickers, stage progress) via a real
// cube::solver::solve() fed the cube DirectScan read.
QtObject {
    id: state

    property string screen: "launch" // launch | permission | scan | solve | settings | history | complete

    // Camera-access screens exist only for mobile platforms, where the OS
    // requires an explicit in-app permission prompt. Desktop platforms grant
    // camera access via their own OS-level dialog the first time the device
    // is opened (see docs/architecture.md - Spec 00 skeleton), so "Start
    // scan" goes straight to the scan screen there.
    readonly property bool isMobile: PlatformFacts.isMobile

    // Qt::PermissionStatus::Granted - see CameraPermission.hpp.
    readonly property bool cameraGranted: CameraPermission.status === 2

    property string previousScreen: "launch"

    // Kicks off device enumeration immediately at startup rather than
    // waiting for a "grant access" button press: on desktop, beginScan()
    // goes straight to "scan" without ever visiting the permission screen
    // (see isMobile above), so nothing else would trigger it in time for
    // DirectScan.start() to have a selectedDeviceId ready.
    Component.onCompleted: CameraController.refreshDevices()

    // Whether the solve screen keeps the camera. It does, to see how the
    // cube is being held; the solve-screen demo (CUBE_SOLVE_DEMO) turns it
    // off, having no scan for the camera to be matched against.
    property bool solveCamera: true

    // Starts/stops the live capture to match the screen shown.
    onScreenChanged: {
        if (screen !== "scan") {
            scanComplete = false;
            completeBeat.stop();
        }
        // The solve is over: the history entry for it is finished, with the
        // time this screen is about to show.
        if (screen === "complete") {
            SolveHistory.finish();
        }
        if (screen === "scan") {
            // A scan precedes whatever solve follows, so the camera has
            // something to be matched against again (resumeSolve turned it
            // off, having none).
            solveCamera = true;
            DirectScan.start(CameraController.selectedDeviceId);
        } else if (screen === "solve" && solveCamera) {
            // The camera the scan used stays as it is, exposure and white
            // balance still fixed, so the cube's colours measure what they
            // measured during the scan.
            DirectScan.watch(CameraController.selectedDeviceId);
        } else if (DirectScan.running || DirectScan.starting) {
            DirectScan.stop();
        }
    }

    // A finished scan stays on screen for a beat: long enough to see the
    // sixth side arrive and all six confirmed. The solve starts the moment
    // the scan completes and runs behind that, so the wait is spent looking
    // at the result rather than at a spinner. The solve screen follows when
    // the beat has passed AND the solution is ready, whichever comes later.
    property bool scanComplete: false
    property var directScanConnections: Connections {
        target: DirectScan
        function onScanCompleted() {
            state.scanComplete = true;
            completeBeat.restart();
        }
    }
    property var completeBeat: Timer {
        interval: 1400
        onTriggered: state.showSolutionWhenReady()
    }
    function showSolutionWhenReady() {
        if (scanComplete && !completeBeat.running && !SolveController.isSolving && screen === "scan") {
            scanComplete = false;
            screen = "solve";
        }
    }

    // The solve lands on its worker and announces itself through
    // stateChanged; that is one of the two things showSolutionWhenReady
    // waits for.
    property var solveControllerConnections: Connections {
        target: SolveController
        function onStateChanged() {
            state.showSolutionWhenReady();
        }
    }

    readonly property int scanned: DirectScan.sidesRead

    // Thin passthrough so CompleteScreen.qml's move-count readout doesn't
    // need its own SolveController binding.
    readonly property int moveCount: SolveController.totalMoves

    property int elapsedSeconds: 0

    property var clockTimer: Timer {
        interval: 1000; repeat: true
        running: state.screen === "scan" || state.screen === "solve"
        onTriggered: state.elapsedSeconds += 1
    }

    // The clock the history entry takes its time from: what the complete
    // screen shows is what the history lists.
    property var historyClock: Binding {
        target: SolveHistory
        property: "elapsedSeconds"
        value: state.elapsedSeconds
    }

    // A duration as minutes and seconds, "12:40": the complete screen's form,
    // and the history's.
    function durationLabel(seconds) {
        var m = Math.floor(seconds / 60);
        var s = seconds % 60;
        return (m < 10 ? "0" + m : m) + ":" + (s < 10 ? "0" + s : s);
    }

    function elapsedLabel() {
        return durationLabel(state.elapsedSeconds);
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

    // The list of solves kept on this device. Entered from Home; back goes
    // there. (Settings opened from it comes back to it: openSettings keeps
    // where it was opened from.)
    function openHistory() {
        screen = "history";
    }

    function closeHistory() {
        screen = "launch";
    }

    // Carries on the unfinished solve the history holds, from the turn it was
    // left at. The cube is solved again with the method it was started with
    // (which becomes the method from now on, as choosing it on the solve
    // screen would), and the clock carries on from the time it had. There is no
    // scan this session for the camera to be matched against, so the solve
    // screen keeps it off, as the solve demo does. False if there is nothing
    // that can be carried on under that id.
    function resumeSolve(id) {
        var entry = SolveHistory.entry(id);
        if (!entry.carryOn) {
            return false;
        }
        SettingsState.solveMethod = entry.method;
        if (!SolveHistory.resume(id)) {
            return false;
        }
        elapsedSeconds = entry.seconds;
        solveCamera = false;
        screen = "solve";
        return true;
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

    // Part-way through a solve the cube and the solution no longer agree:
    // read the cube again as it now is and solve it from there. The same
    // solve as far as the clock is concerned, so the time keeps running.
    function rescan() {
        screen = "scan";
    }
}
