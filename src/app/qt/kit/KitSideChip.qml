import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Shapes
import Cubeulator

// Side chip (docs/design/system.md, "Sticker, swatch, side chip"): nine
// stickers in a 3 x 3 grid with 2 px gaps on a `cubeBody` ground. It sits on
// the scan screen over the camera, so its edges are the overlay's. The grid
// never mirrors.
//
//   empty      a 1 px edge and nothing in it
//   filling    empty, with the capture bar along the bottom
//   read       the nine stickers
//   inView     read, in view again: a 3 px edge and a pointer above it
//   confirmed  the scan is complete: a 3 px green edge and a check
//
// A chip is a control as well as a picture: activating it reads its nine
// colours aloud to a screen reader (`description`).
AbstractButton {
    id: root

    // Nine colour names, row by row from the top left.
    property var colours: []
    // empty | filling | read | inView | confirmed
    property string chipState: "empty"
    // 0 to 1, shown by the capture bar while filling.
    property real progress: 0
    // Side of the chip in pixels.
    property real size: 72
    // "Side 3: read. Red side."
    property string accessibleName: ""
    // "Red side. Row 1: ..." read when the chip is activated.
    property string description: ""
    // For the gallery: show the keyboard focus ring.
    property string previewState: ""

    readonly property bool hasStickers: chipState === "read" || chipState === "inView" || chipState === "confirmed"
    readonly property bool showFocus: previewState === "focus" || visualFocus
    readonly property real gap: 2
    readonly property real stickerSize: (size - 4 * gap) / 3
    readonly property color edgeColor: chipState === "confirmed" ? Theme.overlay.ok
        : (chipState === "inView" ? Theme.overlay.ink : Theme.overlay.outline)
    readonly property real edgeWidth: (chipState === "confirmed" || chipState === "inView") ? Theme.borderHeavy
                                                                                            : Theme.borderHair

    readonly property var contrastPairs: [
        { name: "edge", fg: edgeColor, bg: Theme.overlay.worstCase, target: 3.0 },
        { name: "check", fg: Theme.cubeBody, bg: Theme.overlay.ok, target: 3.0 }
    ]

    objectName: "kitSideChip"
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    // Nothing to read while empty or filling, so not a Tab stop then. Assigned
    // as the state changes rather than bound; see KitChoice.syncTabStops.
    Component.onCompleted: activeFocusOnTab = hasStickers
    onHasStickersChanged: activeFocusOnTab = hasStickers
    // A write of false while it holds focus is taken back (see KitChoice).
    onActiveFocusChanged: {
        if (!activeFocus)
            activeFocusOnTab = hasStickers;
    }
    implicitWidth: size
    implicitHeight: size

    // The grid is the cube; no layout may flip it.
    LayoutMirroring.enabled: false
    LayoutMirroring.childrenInherit: true

    background: Item {
        Rectangle {
            id: ground
            anchors.fill: parent
            color: root.hasStickers ? Theme.cubeBody : "transparent"
            border.width: root.edgeWidth
            border.color: root.edgeColor
            radius: Theme.radius
        }
    }

    contentItem: Item {
        Grid {
            visible: root.hasStickers
            columns: 3
            spacing: root.gap
            anchors.centerIn: parent

            Repeater {
                model: root.hasStickers ? 9 : 0
                KitSticker {
                    required property int index
                    colourName: index < root.colours.length ? root.colours[index] : "white"
                    size: root.stickerSize
                    onPage: false
                }
            }
        }

        KitCaptureBar {
            visible: root.chipState === "filling"
            value: root.progress
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
        }

        // The pointer above a chip that is in view again.
        Shape {
            visible: root.chipState === "inView"
            width: 16
            height: 8
            anchors.horizontalCenter: parent.horizontalCenter
            y: -height - Theme.space1
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                strokeColor: "transparent"
                fillColor: Theme.overlay.ink
                PathSvg {
                    path: "M0 0 L16 0 L8 8 Z"
                }
            }
        }

        // The tick on a chip of a finished scan.
        Rectangle {
            visible: root.chipState === "confirmed"
            width: 22
            height: 22
            anchors.right: parent.right
            anchors.top: parent.top
            color: Theme.overlay.ok
            KitIcon {
                anchors.centerIn: parent
                name: "check"
                size: 16
                strokeWidth: 3
                color: Theme.cubeBody
            }
        }
    }

    KitFocusRing {
        objectName: "focusRing"
        shown: root.showFocus
        onOverlay: true
    }

    HoverHandler {
        cursorShape: root.hasStickers ? Qt.PointingHandCursor : Qt.ArrowCursor
    }

    // Reading the chip: the nine colours, row by row, politely.
    onClicked: {
        if (hasStickers && description !== "")
            Announcer.announce(root, description, false);
    }
    Keys.onReturnPressed: event => {
        root.click();
        event.accepted = true;
    }
    Keys.onEnterPressed: event => {
        root.click();
        event.accepted = true;
    }

    Accessible.role: Accessible.Button
    Accessible.name: accessibleName
    Accessible.description: description
    Accessible.focusable: hasStickers
    Accessible.onPressAction: root.click()
}
