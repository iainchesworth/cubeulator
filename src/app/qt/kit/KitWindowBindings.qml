import QtQuick
import Cubeulator

// What a window needs to be a kit window (E2a). Put one in the root window,
// first (it fills its parent, so it lies beneath everything after it):
//
//   ApplicationWindow {
//       KitWindowBindings { window: root }
//       ...
//
// It does four things and draws nothing:
//
//   * publishes the window's size to Theme, so Theme.sizeClass and
//     Theme.heightClass follow it;
//   * tells SystemInsets which window's screen to read the safe area from;
//   * makes the window's content follow the language's direction. Screens
//     never set mirroring themselves; the sticker, the side chip and the
//     direction icons switch it off on themselves;
//   * binds back (docs/design/platforms.md, "Back"): Esc on desktop; Alt+Left
//     on Windows and Linux, Alt+Right in right-to-left layouts; Cmd+[ on
//     macOS; the mouse's back button; the Android back key and a keyboard's
//     browser Back key; and an edge swipe on iOS. Every one calls
//     Navigator.back() and nothing else.
//
// Back never quits the app except from home on Android, where that is what
// Android does.
Item {
    id: root

    // The window, as any object: QML's Window and ApplicationWindow are
    // different private classes and a plain QQuickWindow is neither.
    property QtObject window: null

    anchors.fill: parent

    readonly property string os: Qt.platform.os
    readonly property bool mobile: os === "android" || os === "ios"
    readonly property bool mac: os === "osx"

    // ---- Size, safe area, direction -------------------------------------------
    Binding {
        target: Theme
        property: "windowWidth"
        value: root.window ? root.window.width : 900
    }
    Binding {
        target: Theme
        property: "windowHeight"
        value: root.window ? root.window.height : 640
    }
    Binding {
        target: root.window ? root.window.contentItem.LayoutMirroring : null
        property: "enabled"
        value: Theme.rtl
    }
    Binding {
        target: root.window ? root.window.contentItem.LayoutMirroring : null
        property: "childrenInherit"
        value: true
    }
    onWindowChanged: SystemInsets.watch(window)
    Component.onCompleted: SystemInsets.watch(window)

    // ---- Back ----------------------------------------------------------------------
    // Android: nothing left to go back to means the app has nowhere to go
    // either, so it closes, as Android's own apps do from their first screen.
    function goBack() {
        const handled = Navigator.back();
        if (!handled && os === "android" && window)
            window.close();
        return handled;
    }

    // Esc. A key, so it is desktop's; a phone has no Esc.
    Shortcut {
        sequences: ["Escape"]
        enabled: !root.mobile
        onActivated: root.goBack()
    }
    // Alt+Left, as a browser's. Right-to-left layouts have back on the other
    // side, so Alt+Right is theirs.
    Shortcut {
        sequences: [Theme.rtl ? "Alt+Right" : "Alt+Left"]
        enabled: !root.mobile && !root.mac
        onActivated: root.goBack()
    }
    // Qt maps Ctrl to Cmd on macOS.
    Shortcut {
        sequences: ["Ctrl+["]
        enabled: root.mac
        onActivated: root.goBack()
    }
    // Qt::Key_Back: the Android system back key and gesture, and the Back key
    // of a keyboard with browser keys.
    Shortcut {
        sequences: ["Back"]
        onActivated: root.goBack()
    }

    // The mouse's back button, wherever in the window it is pressed.
    TapHandler {
        acceptedButtons: Qt.BackButton
        acceptedDevices: PointerDevice.Mouse
        onTapped: root.goBack()
    }

    // The window being closed by the system while there is somewhere to go
    // back to: Android sends a close for the back key if nothing took it.
    Connections {
        target: root.window
        function onClosing(close) {
            if (root.os === "android" && Navigator.canGoBack) {
                close.accepted = false;
                Navigator.back();
            }
        }
    }

    // iOS: a drag from the leading edge, as the system's own apps allow. An
    // extra; the on-screen control is always there.
    Item {
        id: edge
        visible: root.os === "ios"
        width: 24 + Theme.insets.leading
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.left: parent.left

        DragHandler {
            target: null
            enabled: root.os === "ios"
            acceptedDevices: PointerDevice.TouchScreen
            yAxis.enabled: false
            onActiveChanged: {
                // Inward from the leading edge: to the right, or to the left
                // when the layout is mirrored.
                const inward = Theme.rtl ? -activeTranslation.x : activeTranslation.x;
                if (!active && inward > 72)
                    root.goBack();
            }
        }
    }
}
