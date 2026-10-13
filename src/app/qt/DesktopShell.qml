import QtQuick
import QtQuick.Controls
import Cubeulator

// Desktop manners (workstream E4a, docs/design/platforms.md):
//
//   * the keyboard shortcuts of docs/design/accessibility.md, "Everywhere":
//     Settings, Help, a new scan from Home, full screen, quit;
//   * on macOS, the native menu bar of platforms.md, "Menus (macOS)";
//   * a placeholder About, until E3b builds the About screen.
//
// Hooked in from Main.qml with one line:
//
//     DesktopShell { window: root }
//
// Back (Esc, Alt+Left, Cmd+[, the mouse's back button) is not here: it belongs
// to the navigation stack E2a is building.
//
// Every action calls the navigation functions AppState has today. E2a
// repoints them. Where there is nothing to call yet - the Help screen (E3a),
// entering colours by hand (R3) - the action is disabled rather than absent,
// as macOS expects of menus and as "no button that does nothing" requires;
// set `helpAvailable` / `handEntryAvailable` when the screen exists and
// handle the matching signal.
Item {
    id: shell

    required property ApplicationWindow window

    // The platform has a menu bar for the whole app and the standard window
    // commands in it, and its own chord for full screen (macOS). Asked of the
    // platform, never derived from the OS name here.
    readonly property bool nativeMenuBar: AppInfo.nativeMenuBar

    // New Scan is offered from Home and, once solved, from Complete. Never
    // mid-scan or mid-solve, where it would throw the person's place away.
    readonly property bool onHome: AppState.screen === "launch" || AppState.screen === "complete"

    property bool helpAvailable: false
    property bool handEntryAvailable: false
    signal helpRequested()
    signal handEntryRequested()

    function showAbout() {
        aboutPlaceholder.open();
    }

    function toggleFullScreen() {
        window.visibility = window.visibility === Window.FullScreen ? Window.AutomaticVisibility : Window.FullScreen;
    }

    // ---- actions: one object each, shared by the shortcuts and the menus ----

    Action {
        id: settingsAction
        text: qsTrId("menu.settings")
        shortcut: "Ctrl+,"
        enabled: AppState.screen !== "settings"
        onTriggered: AppState.openSettings()
    }

    Action {
        id: newScanAction
        text: qsTrId("menu.newScan")
        shortcut: StandardKey.New
        enabled: shell.onHome
        onTriggered: AppState.beginScan()
    }

    Action {
        id: enterByHandAction
        text: qsTrId("menu.enterByHand")
        shortcut: "Shift+Ctrl+N"
        enabled: shell.handEntryAvailable && shell.onHome
        onTriggered: shell.handEntryRequested()
    }

    Action {
        id: helpAction
        text: qsTrId("menu.helpItem")
        // F1 on Windows and Linux, Cmd+? on macOS.
        shortcut: StandardKey.HelpContents
        enabled: shell.helpAvailable
        onTriggered: shell.helpRequested()
    }

    Action {
        id: aboutAction
        text: qsTrId("about.title")
        onTriggered: shell.showAbout()
    }

    Action {
        id: fullScreenAction
        text: shell.window.visibility === Window.FullScreen ? qsTrId("menu.exitFullScreen") : qsTrId("menu.enterFullScreen")
        // Ctrl+Cmd+F on macOS (Qt's Ctrl is Cmd there, Meta is Control).
        shortcut: shell.nativeMenuBar ? "Ctrl+Meta+F" : "F11"
        onTriggered: shell.toggleFullScreen()
    }

    Action {
        id: quitAction
        text: qsTrId("menu.quit")
        shortcut: "Ctrl+Q"
        onTriggered: Qt.quit()
    }

    // ---- macOS only: Window menu items, which exist nowhere else ----

    Action {
        id: closeWindowAction
        text: qsTrId("menu.closeWindow")
        shortcut: "Ctrl+W"
        enabled: shell.nativeMenuBar
        onTriggered: shell.window.close()
    }

    Action {
        id: minimiseAction
        text: qsTrId("menu.minimise")
        shortcut: "Ctrl+M"
        enabled: shell.nativeMenuBar
        onTriggered: shell.window.showMinimized()
    }

    Action {
        id: zoomAction
        text: qsTrId("menu.zoom")
        enabled: shell.nativeMenuBar
        onTriggered: shell.window.visibility = shell.window.visibility === Window.Maximized
                                              ? Window.Windowed : Window.Maximized
    }

    Action {
        id: bringAllToFrontAction
        text: qsTrId("menu.bringAllToFront")
        enabled: shell.nativeMenuBar
        onTriggered: {
            shell.window.raise();
            shell.window.requestActivate();
        }
    }

    // ---- the native menu bar: macOS only ----
    //
    // Qt 6.8's MenuBar becomes the system menu bar when its menus are
    // Popup.Native. The application menu is Qt's own: About, Settings and
    // Quit are placed there by their wording, and Services, Hide, Hide Others
    // and Show All are the system's. Items that cannot act where the person
    // is are disabled, not hidden.
    Component {
        id: macMenuBar

        MenuBar {
            Menu {
                title: qsTrId("menu.file")
                popupType: Popup.Native
                MenuItem { action: newScanAction }
                MenuItem { action: enterByHandAction }
                MenuSeparator {}
                MenuItem { action: closeWindowAction }
                MenuSeparator {}
                MenuItem { action: settingsAction }
                MenuItem { action: quitAction }
            }
            Menu {
                title: qsTrId("menu.view")
                popupType: Popup.Native
                MenuItem { action: fullScreenAction }
            }
            Menu {
                title: qsTrId("menu.window")
                popupType: Popup.Native
                MenuItem { action: minimiseAction }
                MenuItem { action: zoomAction }
                MenuSeparator {}
                MenuItem { action: bringAllToFrontAction }
            }
            Menu {
                title: qsTrId("menu.help")
                popupType: Popup.Native
                MenuItem { action: helpAction }
                MenuItem { action: aboutAction }
            }
        }
    }

    Component.onCompleted: {
        if (shell.nativeMenuBar) {
            shell.window.menuBar = macMenuBar.createObject(shell.window.contentItem);
        }
    }

    // About, until E3b: the name and the version, which is what the
    // packages promise. It is replaced, not extended.
    //
    // A Popup rather than a Dialog: the style's dialog brings a header and
    // footer of its own, in a look the rest of the app does not have.
    Popup {
        id: aboutPlaceholder
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        width: Math.min(320, Overlay.overlay ? Overlay.overlay.width - 32 : 320)
        padding: 24

        background: Rectangle {
            color: Theme.paper
            border.width: 1
            border.color: Theme.ink
        }

        // Accessible attaches to an Item, so it sits on the content item
        // rather than on the Popup.
        contentItem: Column {
            objectName: "about.placeholder"
            spacing: 8
            Accessible.role: Accessible.Dialog
            Accessible.name: qsTrId("about.title")
            Text {
                text: AppInfo.name
                font.pixelSize: 28
                font.weight: Font.ExtraBold
                color: Theme.ink
            }
            Text {
                objectName: "about.version"
                text: qsTrId("about.version").arg(AppInfo.version)
                font.pixelSize: 16
                color: Theme.ink
            }
            Item { width: 1; height: 8 }
            AppButton {
                objectName: "about.close"
                text: qsTrId("common.close")
                variant: "secondary"
                onClicked: aboutPlaceholder.close()
            }
        }
    }

    Connections {
        target: AppInfo
        function onAboutRequested() { shell.showAbout(); }
    }
}
