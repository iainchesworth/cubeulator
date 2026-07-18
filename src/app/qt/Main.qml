import QtQuick
import Cubeulator
import QtQuick.Controls

ApplicationWindow {
    id: root
    width: 900
    height: 640
    visible: true
    title: "Cubeulator"
    color: Theme.paper

    Loader {
        anchors.fill: parent
        sourceComponent: {
            switch (AppState.screen) {
            case "permission": return permissionScreen;
            case "scan": return scanScreen;
            case "solve": return solveScreen;
            case "complete": return completeScreen;
            default: return launchScreen;
            }
        }
    }

    Component { id: launchScreen; LaunchScreen {} }
    Component { id: permissionScreen; PermissionScreen {} }
    Component { id: scanScreen; ScanScreen {} }
    Component { id: solveScreen; SolveScreen {} }
    Component { id: completeScreen; CompleteScreen {} }
}
