pragma Singleton
import QtQuick
import Cubeulator

// Screen-routing state, delegating to real controllers for each phase:
// CameraController/DirectScan own the scan phase, SolveController owns the
// solve phase (moves, stickers, stage progress) via a real
// cube::solver::solve() fed the cube DirectScan read.
QtObject {
    id: state

    property string screen: "launch" // launch | permission | scan | solve | settings | complete | solution

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
        if (screen === "scan") {
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

    // The solution screen reviews a solve turn by turn. It is opened for the
    // solve just finished (SolveController), or for any object shaped like it
    // (SolutionScreen.qml lists the shape) - the history's entry for a solve
    // from another day. `subtitle` is a second line under its title, the
    // date. Back goes to the screen it was opened from.
    property var solutionSource: SolveController
    property string solutionSubtitle: ""
    property string solutionBack: "complete"

    function openSolution(source, subtitle) {
        solutionSource = source === undefined ? SolveController : source;
        solutionSubtitle = subtitle === undefined ? "" : subtitle;
        // Settings opened from the review comes back to it, and a review
        // opened from there must not make itself the way back.
        if (screen !== "settings")
            solutionBack = screen;
        screen = "solution";
    }

    function closeSolution() {
        screen = solutionBack;
    }

    // Part-way through a solve the cube and the solution no longer agree:
    // read the cube again as it now is and solve it from there. The same
    // solve as far as the clock is concerned, so the time keeps running.
    function rescan() {
        screen = "scan";
    }
}
