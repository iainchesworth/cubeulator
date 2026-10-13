import QtQuick
import Cubeulator

// The scan screen: the camera, a dot on each sticker of the side in view,
// and a chip for every side read so far.
//
// Nothing here is mirrored. A chip has to match the cube in the user's hand
// when they turn it to look, and a mirror image of a side never matches the
// side itself in any orientation.
Item {
    id: root

    readonly property var sides: DirectScan.sides
    readonly property var points: DirectScan.stickerPoints
    readonly property bool cubeInView: points.length === 9

    // The preview fills the screen and crops what does not fit; sticker
    // points arrive in frame pixels and are mapped through the same crop.
    readonly property real frameScale: DirectScan.frameWidth > 0
        ? Math.max(width / DirectScan.frameWidth, height / DirectScan.frameHeight) : 1
    readonly property real frameLeft: (width - DirectScan.frameWidth * frameScale) / 2
    readonly property real frameTop: (height - DirectScan.frameHeight * frameScale) / 2

    readonly property string headline: {
        if (DirectScan.cameraError !== "")
            return "Camera unavailable";
        if (!DirectScan.running)
            return "Starting the camera…";
        return DirectScan.sidesRead + " of 6 sides read";
    }
    readonly property string guidance: {
        if (DirectScan.cameraError !== "")
            return DirectScan.cameraError;
        if (!DirectScan.running)
            return "";
        if (DirectScan.settingUp)
            return "Setting the camera's exposure and colour balance…";
        if (DirectScan.conflicted)
            return "These six sides do not make a valid cube yet. Show each side again, square to the camera.";
        if (DirectScan.reading)
            return "Reading this side — hold it there";
        if (cubeInView)
            return "Hold it still";
        if (DirectScan.sidesRead === 0)
            return "Hold one side of the cube square to the camera";
        return "Turn to a side that is not filled in below, square to the camera";
    }

    Rectangle { anchors.fill: parent; color: "#141312" }

    Image {
        anchors.fill: parent
        fillMode: Image.PreserveAspectCrop
        cache: false
        // Nothing to show until the camera has delivered a frame.
        source: DirectScan.frameSeq > 0 ? "image://scanFrame/latest#" + DirectScan.frameSeq : ""
    }

    // One dot per sticker: yellow when the side is seen, green while it counts.
    Repeater {
        model: 9
        delegate: Rectangle {
            readonly property var point: root.cubeInView ? root.points[index] : Qt.point(0, 0)
            visible: root.cubeInView
            width: 14; height: 14; radius: 7
            x: root.frameLeft + point.x * root.frameScale - width / 2
            y: root.frameTop + point.y * root.frameScale - height / 2
            color: DirectScan.reading ? "#3FE05A" : "#FFD21F"
            border.color: "#141312"; border.width: 2
        }
    }

    // Status, top left.
    Rectangle {
        x: Theme.space4; y: Theme.space4
        width: Math.min(root.width - 2 * Theme.space4, 520)
        height: statusColumn.height + 2 * Theme.space3
        radius: 8
        color: Qt.rgba(0.08, 0.075, 0.07, 0.78)

        Column {
            id: statusColumn
            x: Theme.space4; y: Theme.space3
            width: parent.width - 2 * Theme.space4
            spacing: Theme.space1

            Text {
                text: root.headline
                color: DirectScan.sidesRead === 6 && !DirectScan.conflicted ? "#3FE05A" : "#F3F2F2"
                font.family: Theme.fontHeading; font.pixelSize: 24; font.weight: Font.DemiBold
            }
            Text {
                width: parent.width
                visible: text !== ""
                text: root.guidance
                wrapMode: Text.WordWrap
                color: DirectScan.conflicted || DirectScan.cameraError !== "" ? "#FFB347" : "#D9D6D4"
                font.family: Theme.fontHeading; font.pixelSize: 15
            }
        }
    }

    // The sides read so far, in the order they were first shown, each as the
    // camera last saw it. Colours are the measured ones, not tidy swatches.
    Row {
        id: chips
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: buttons.top
        anchors.bottomMargin: Theme.space4
        spacing: Theme.space3

        readonly property real chip: Math.max(48, Math.min(96, (root.width - 2 * Theme.space4 - 5 * spacing) / 6))

        Repeater {
            model: 6
            delegate: Rectangle {
                id: chipFrame
                readonly property var side: index < root.sides.length ? root.sides[index] : null
                width: chips.chip; height: chips.chip
                radius: 6
                color: Qt.rgba(0.08, 0.075, 0.07, 0.85)
                border.width: side !== null && side.inView ? 3 : 1
                border.color: side !== null && side.inView ? "#F3F2F2" : Qt.rgba(1, 1, 1, 0.25)

                Grid {
                    anchors.centerIn: parent
                    columns: 3
                    spacing: 2
                    visible: chipFrame.side !== null
                    Repeater {
                        model: 9
                        delegate: Rectangle {
                            width: (chips.chip - 16) / 3; height: width
                            radius: 2
                            color: chipFrame.side !== null ? chipFrame.side.colors[index] : "transparent"
                        }
                    }
                }
            }
        }
    }

    Row {
        id: buttons
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: Theme.space4
        spacing: Theme.space3

        AppButton {
            height: 42
            variant: "secondary"; text: "Cancel"
            onClicked: AppState.goLaunch()
        }
        AppButton {
            height: 42
            variant: "secondary"; text: "Start over"
            enabled: DirectScan.sidesRead > 0
            onClicked: DirectScan.restart()
        }
    }
}
