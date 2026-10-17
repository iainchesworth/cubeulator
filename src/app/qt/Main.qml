import QtQuick
import Cubeulator
import QtQuick.Controls

ApplicationWindow {
    id: root
    width: 900
    height: 640
    visible: true
    title: qsTrId("app.name")
    color: Theme.paper

    Loader {
        id: screens
        anchors.fill: parent
        // Right-to-left languages mirror the layout - what is first goes last,
        // the back button and the chevrons change sides. The cube, the camera
        // picture and anything that describes the object in the hand opt out
        // where they are drawn (docs/i18n.md).
        LayoutMirroring.enabled: LanguageManager.rtl
        LayoutMirroring.childrenInherit: true
        // Each screen fades in rather than replacing the last in one frame.
        onLoaded: screenFade.restart()
        sourceComponent: {
            switch (AppState.screen) {
            case "entry": return entryScreen;
            case "colours": return coloursScreen;
            case "permission": return permissionScreen;
            case "scan": return scanScreen;
            case "solve": return solveScreen;
            case "settings": return settingsScreen;
            case "history": return historyScreen;
            case "complete": return completeScreen;
            case "solution": return solutionScreen;
            default: return launchScreen;
            }
        }
    }

    NumberAnimation {
        id: screenFade
        target: screens; property: "opacity"
        from: 0; to: 1; duration: 280; easing.type: Easing.OutCubic
    }

    // The diagnostics channel is never silent: while it writes, every screen
    // says so, and where (docs/diagnostics.md). Nothing is shown when it is
    // off, which is how the app starts.
    Rectangle {
        objectName: "diagnostics.marker"
        visible: Diagnostics.enabled
        z: 1000
        anchors.left: parent.left; anchors.bottom: parent.bottom
        anchors.margins: Theme.space2
        width: Math.min(markerText.implicitWidth + 2 * Theme.space3, parent.width - 2 * Theme.space2)
        height: markerText.implicitHeight + 2 * Theme.space1
        radius: 6
        color: Qt.rgba(0.08, 0.075, 0.07, 0.78)
        // The camera picture and the cube are not mirrored, and neither is a
        // path.
        LayoutMirroring.enabled: false

        Text {
            id: markerText
            anchors.centerIn: parent
            width: parent.width - 2 * Theme.space3
            elide: Text.ElideMiddle
            text: Diagnostics.framesEnabled
                  ? qsTrId("diagnostics.markerFrames").arg(Diagnostics.directory)
                  : qsTrId("diagnostics.marker").arg(Diagnostics.path)
            color: "#F3F2F2"
            font.family: Theme.fontMono; font.pixelSize: 11
        }
    }

    // E4a: desktop shortcuts, the macOS menu bar and the placeholder About.
    // Not on a phone: it needs the desktop shell's singletons (KeepAwake,
    // AppInfo), which AppRuntime registers only where that shell is built.
    Loader {
        active: !AppState.isMobile
        sourceComponent: DesktopShell { window: root }
    }

    Component { id: launchScreen; LaunchScreen {} }
    Component { id: entryScreen; EntryScreen {} }
    Component { id: coloursScreen; ColoursScreen {} }
    Component { id: permissionScreen; PermissionScreen {} }
    Component { id: scanScreen; DirectScanScreen {} }
    Component { id: solveScreen; SolveScreen {} }
    Component { id: settingsScreen; SettingsScreen {} }
    Component { id: historyScreen; HistoryScreen {} }
    Component { id: completeScreen; CompleteScreen {} }
    Component {
        id: solutionScreen
        SolutionScreen {
            source: AppState.solutionSource
            subtitle: AppState.solutionSubtitle
        }
    }
}
