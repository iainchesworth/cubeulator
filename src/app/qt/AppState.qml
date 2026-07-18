pragma Singleton
import QtQuick
import Cubeulator

// Demo/mock state for the scan -> solve -> complete flow. The real
// camera/vision/solver pipeline is not implemented yet (see
// docs/architecture.md - Spec 00 skeleton); this drives the UI the same
// way the design prototype's own demo state did, so the screens are fully
// navigable ahead of that backend work landing.
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
        scanned = 0;
        step = 0;
        elapsedSeconds = 0;
        screen = "launch";
    }

    function scanAnother() {
        scanned = 0;
        step = 0;
        elapsedSeconds = 0;
        screen = "scan";
    }
}
