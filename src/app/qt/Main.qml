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

    // Component kit (E2a): publishes this window's size and safe area to Theme,
    // makes the content follow the language's direction, and binds back per
    // platform to Navigator. It fills the window and sits beneath everything
    // that follows. Navigator is not routing the screens yet (docs/kit.md,
    // "Proposed split of AppState.qml"), so back does nothing until it is.
    KitWindowBindings { window: root }

    Loader {
        id: screens
        anchors.fill: parent
        // Each screen fades in rather than replacing the last in one frame.
        onLoaded: screenFade.restart()
        sourceComponent: {
            // Component kit (E2a): the gallery, developer builds only.
            if (BuildInfo.galleryRequested)
                return galleryScreen;
            switch (AppState.screen) {
            case "permission": return permissionScreen;
            case "scan": return scanScreen;
            case "solve": return solveScreen;
            case "settings": return settingsScreen;
            case "complete": return completeScreen;
            default: return launchScreen;
            }
        }
    }

    NumberAnimation {
        id: screenFade
        target: screens; property: "opacity"
        from: 0; to: 1; duration: 280; easing.type: Easing.OutCubic
    }

    Component { id: launchScreen; LaunchScreen {} }
    Component { id: permissionScreen; PermissionScreen {} }
    Component { id: scanScreen; DirectScanScreen {} }
    Component { id: solveScreen; SolveScreen {} }
    Component { id: settingsScreen; SettingsScreen {} }
    Component { id: completeScreen; CompleteScreen {} }
    // Component kit (E2a).
    Component { id: galleryScreen; KitGallery {} }
}
