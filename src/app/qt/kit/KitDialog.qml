import QtQuick
import QtQuick.Controls.Basic
import Cubeulator

// Dialog (docs/design/system.md, "Dialog"): a question that must be answered
// before going on. Centred, at most 400 wide, a 2 px ink edge, on a scrim.
//
//   * A title, one sentence of body, two buttons.
//   * The safe choice is first, secondary, focused when the dialog opens, and
//     what Esc and back choose.
//   * Focus stays inside until it closes, then returns to where it was.
//
// It registers with Navigator while open, so that a platform's back lands
// here before it lands on the screen beneath.
Popup {
    id: root

    property string title: ""
    property string body: ""
    // The safe choice: "Keep it", "Stay". First, secondary, the default.
    property string safeText: ""
    // The other: "Discard", "Leave". Ghost.
    property string otherText: ""

    // The safe choice was made (by button, by Esc, by back).
    signal safeChosen
    signal otherChosen

    readonly property real maxWidth: 400
    readonly property real windowWidth: Overlay.overlay ? Overlay.overlay.width : 400

    objectName: "kitDialog"
    parent: Overlay.overlay
    modal: true
    focus: true
    // Esc and back are handled below so that they choose the safe option
    // rather than just closing.
    closePolicy: Popup.NoAutoClose
    padding: Theme.space6

    x: Math.round(((Overlay.overlay ? Overlay.overlay.width : width) - width) / 2)
    y: Math.round(((Overlay.overlay ? Overlay.overlay.height : height) - height) / 2)
    width: Math.min(maxWidth, windowWidth - 2 * Theme.space4)

    // What it draws, for the contrast tests.
    readonly property var contrastPairs: [
        { name: "title", fg: Theme.ink, bg: Theme.paper, target: 4.5 },
        { name: "body", fg: Theme.ink, bg: Theme.paper, target: 4.5 },
        { name: "edge", fg: Theme.ink, bg: Theme.paper, target: 3.0 }
    ]

    // What back does here.
    function requestBack() {
        chooseSafe();
    }
    function chooseSafe() {
        root.close();
        root.safeChosen();
    }
    function chooseOther() {
        root.close();
        root.otherChosen();
    }

    onAboutToShow: Navigator.registerOverlay(root)
    onClosed: Navigator.unregisterOverlay(root)
    // A popup taken away while it is open must not leave back pointing at it.
    Component.onDestruction: Navigator.unregisterOverlay(root)
    // The safe choice takes focus as the dialog opens, by the keyboard's
    // reason so that its ring shows and the person can see where they are.
    onOpened: safeButton.forceActiveFocus(Qt.TabFocusReason)

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

        // Esc is the safe choice. Key handlers attach to Items, so it is here,
        // where the focus is, and not on the Popup.
        Keys.onEscapePressed: event => {
            root.requestBack();
            event.accepted = true;
        }

        // What a screen reader is given: a dialog, with its title and sentence.
        Accessible.role: Accessible.Dialog
        Accessible.name: root.title
        Accessible.description: root.body

        Column {
            id: column
            width: parent.width
            spacing: Theme.space4

            KitText {
                id: titleText
                objectName: "title"
                width: parent.width
                text: root.title
                role: "headline"
                Accessible.ignored: true
            }
            KitText {
                id: bodyText
                objectName: "body"
                width: parent.width
                text: root.body
                role: "body"
                visible: root.body !== ""
                Accessible.ignored: true
            }
            Column {
                width: parent.width
                spacing: Theme.space2

                KitButton {
                    id: safeButton
                    objectName: "safe"
                    width: parent.width
                    size: "thumb"
                    variant: "secondary"
                    block: true
                    text: root.safeText
                    onClicked: root.chooseSafe()
                }
                KitButton {
                    id: otherButton
                    objectName: "other"
                    width: parent.width
                    size: "thumb"
                    variant: "ghost"
                    block: true
                    text: root.otherText
                    onClicked: root.chooseOther()
                }
            }
        }
    }

}
