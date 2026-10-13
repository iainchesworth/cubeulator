import QtQuick
import QtQuick.Controls.Basic
import Cubeulator

// Sheet (docs/design/system.md, "Sheet"): a short list of actions that does
// not need answering. Slides up from the bottom edge on compact; a centred
// panel at most 440 wide from medium up. Same edge and scrim as a dialog. A
// tap on the scrim, Esc or back closes it. Rows are block buttons, 56 high;
// the last row is Close.
//
// Put the rows in as children:
//
//   KitSheet { closeText: "Close"
//       KitButton { text: "Help"; block: true; size: "thumb"; variant: "ghost" } }
Popup {
    id: root

    // The last row. Required: the kit holds no user-visible words.
    property string closeText: ""
    // The sheet's name to a screen reader ("More").
    property string title: ""
    default property alias rows: rowsColumn.data

    signal closedByPerson

    readonly property bool bottomSheet: Theme.sizeClass === "compact"
    readonly property real overlayWidth: Overlay.overlay ? Overlay.overlay.width : 400
    readonly property real overlayHeight: Overlay.overlay ? Overlay.overlay.height : 600

    objectName: "kitSheet"
    parent: Overlay.overlay
    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    padding: 0

    width: bottomSheet ? overlayWidth : Math.min(440, overlayWidth - 2 * Theme.space4)
    x: Math.round((overlayWidth - width) / 2)
    // Bottom edge on compact; centred above.
    y: bottomSheet ? overlayHeight - height : Math.round((overlayHeight - height) / 2)

    readonly property var contrastPairs: [
        { name: "close", fg: Theme.ink, bg: Theme.paper, target: 4.5 },
        { name: "edge", fg: Theme.ink, bg: Theme.paper, target: 3.0 }
    ]

    function requestBack() {
        root.close();
        root.closedByPerson();
    }

    onAboutToShow: Navigator.registerOverlay(root)
    onClosed: Navigator.unregisterOverlay(root)
    // A popup taken away while it is open must not leave back pointing at it.
    Component.onDestruction: Navigator.unregisterOverlay(root)
    onOpened: closeButton.forceActiveFocus(Qt.TabFocusReason)

    Overlay.modal: Rectangle {
        color: Theme.scrim
    }

    background: Rectangle {
        color: Theme.paper
        border.width: Theme.borderRule
        border.color: Theme.ink
        radius: Theme.radius
    }

    enter: Transition {
        NumberAnimation {
            property: "opacity"
            from: 0
            to: 1
            duration: Theme.motionBase
            easing.type: Easing.OutCubic
        }
    }
    exit: Transition {
        NumberAnimation {
            property: "opacity"
            from: 1
            to: 0
            duration: Theme.motionBase
            easing.type: Easing.OutCubic
        }
    }

    contentItem: FocusScope {
        implicitHeight: column.implicitHeight
        implicitWidth: column.implicitWidth

        Keys.onEscapePressed: event => {
            root.requestBack();
            event.accepted = true;
        }

        Accessible.role: Accessible.Dialog
        Accessible.name: root.title

        Column {
            id: column
            width: parent.width

            Column {
                id: rowsColumn
                width: parent.width
            }
            KitButton {
                id: closeButton
                objectName: "close"
                width: parent.width
                size: "thumb"
                variant: "ghost"
                block: true
                text: root.closeText
                onClicked: root.requestBack()
            }
        }
    }

}
