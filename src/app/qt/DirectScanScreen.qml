import QtQuick
import Cubeulator

// The scan screen: the camera, a dot on each sticker of the side in view,
// and a chip for every side read so far.
//
// The picture is shown the way a mirror shows it, so the cube moves on screen
// the way the hand moves it (CameraController.previewMirrored: a front or
// unknown camera, never a rear one). That is the only mirroring there is.
// The frame that is read is the camera's own, and the chips are made from it:
// a chip has to match the cube in the user's hand when they turn it to look,
// and a mirror image of a side never matches the side itself in any
// orientation.
Item {
    id: root

    readonly property var sides: DirectScan.sides
    readonly property var points: DirectScan.stickerPoints
    // The cube's size: a side is size x size stickers, nine for a 3x3.
    readonly property int size: DirectScan.size
    readonly property bool cubeInView: points.length === size * size
    // The cube is read. This screen stays up for a beat so the last side is
    // seen arriving and all six are seen confirmed; AppState moves on once
    // the solution is ready.
    readonly property bool complete: DirectScan.complete
    // The camera could not be started (refused, absent, in use) and nothing is
    // starting it again. Nothing is running to start over from; the way on is
    // to ask for the camera again.
    readonly property bool cameraFailed: DirectScan.cameraError !== "" && !DirectScan.starting

    readonly property color good: "#3FE05A"

    // Whether the picture is shown the way a mirror shows it: the camera
    // faces the user, so it is, unless it is a rear one. A property of the
    // screen, not just a read of the controller, so the screen can be seen
    // both ways without a camera of each kind.
    property bool mirrored: CameraController.previewMirrored

    // The preview fills the screen and crops what does not fit; sticker
    // points arrive in frame pixels and are mapped through the same crop.
    readonly property real frameScale: DirectScan.frameWidth > 0
        ? Math.max(width / DirectScan.frameWidth, height / DirectScan.frameHeight) : 1
    readonly property real frameLeft: (width - DirectScan.frameWidth * frameScale) / 2
    readonly property real frameTop: (height - DirectScan.frameHeight * frameScale) / 2

    // A cube colour, from the name the controller gives it.
    function colourName(name) {
        switch (name) {
        case "white": return qsTrId("colour.white");
        case "yellow": return qsTrId("colour.yellow");
        case "green": return qsTrId("colour.green");
        case "blue": return qsTrId("colour.blue");
        case "orange": return qsTrId("colour.orange");
        case "red": return qsTrId("colour.red");
        }
        return "";
    }

    // The corner to show, by the colours of its three sides, when the scan
    // knows one; empty otherwise.
    readonly property string settlingText: {
        const corner = DirectScan.settlingCorner;
        if (corner.length !== 3)
            return "";
        return qsTrId("scan.guide.ambiguousCorner")
            .arg(colourName(corner[0])).arg(colourName(corner[1])).arg(colourName(corner[2]));
    }

    readonly property string headline: {
        if (DirectScan.cameraError !== "")
            return "Camera unavailable";
        if (complete)
            return "Cube read";
        if (!DirectScan.running)
            return "Starting the camera…";
        return DirectScan.sidesRead + " of 6 sides read";
    }
    readonly property string guidance: {
        if (DirectScan.cameraError !== "")
            return DirectScan.cameraError;
        if (complete)
            return "All six sides make a valid cube. Working out the solution…";
        if (!DirectScan.running)
            return "";
        if (DirectScan.settingUp)
            return "Setting the camera's exposure and colour balance…";
        if (DirectScan.ambiguous) {
            // Said by colour when the cubes the six sides make differ at one
            // corner more than another: showing that corner settles it.
            if (settlingText !== "")
                return settlingText;
            return "These six sides fit together in more than one way. Turn the cube slowly so that two sides show at once.";
        }
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

    // The camera and the dots on it are one picture and flip together, about
    // its middle, so a dot stays on its sticker. The status, the chips and the
    // buttons are not part of it and are never flipped.
    Item {
        id: video
        objectName: "scan.video"
        anchors.fill: parent
        transform: Scale {
            origin.x: video.width / 2
            xScale: root.mirrored ? -1 : 1
        }

        Image {
            objectName: "scan.picture"
            anchors.fill: parent
            fillMode: Image.PreserveAspectCrop
            cache: false
            // Nothing to show until the camera has delivered a frame.
            source: DirectScan.frameSeq > 0 ? "image://scanFrame/latest#" + DirectScan.frameSeq : ""
        }

        // One dot per sticker: yellow when the side is seen, green while it
        // counts. Placed in frame pixels, as the container flips them.
        Repeater {
            model: root.size * root.size
            delegate: Rectangle {
                objectName: "scan.dot"
                readonly property var point: root.cubeInView ? root.points[index] : Qt.point(0, 0)
                visible: root.cubeInView
                // 14 for a 3x3, smaller for the smaller stickers of a bigger cube.
                width: Math.round(Math.max(8, 42 / root.size)); height: width; radius: width / 2
                x: root.frameLeft + point.x * root.frameScale - width / 2
                y: root.frameTop + point.y * root.frameScale - height / 2
                color: DirectScan.reading ? root.good : "#FFD21F"
                border.color: "#141312"; border.width: 2
            }
        }
    }

    // Status, top left.
    Rectangle {
        objectName: "scan.status"
        x: Theme.space4; y: Theme.space4
        width: Math.min(root.width - 2 * Theme.space4, 520)
        height: statusColumn.height + 2 * Theme.space3
        radius: 8
        color: Qt.rgba(0.08, 0.075, 0.07, 0.78)
        Behavior on height { NumberAnimation { duration: 160; easing.type: Easing.OutCubic } }

        Column {
            id: statusColumn
            x: Theme.space4; y: Theme.space3
            width: parent.width - 2 * Theme.space4
            spacing: Theme.space1

            Text {
                text: root.headline
                color: root.complete ? root.good : "#F3F2F2"
                font.family: Theme.fontHeading; font.pixelSize: 24; font.weight: Font.DemiBold
                Behavior on color { ColorAnimation { duration: 220 } }
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

    // A recording is never silent. This shows only when a developer has
    // turned recording on; the app stores nothing otherwise.
    Rectangle {
        visible: DirectScan.recordingRoot !== ""
        anchors.right: parent.right; anchors.top: parent.top
        anchors.margins: Theme.space4
        width: recordingRow.width + 2 * Theme.space3
        height: recordingRow.height + 2 * Theme.space2
        radius: 6
        color: Qt.rgba(0.08, 0.075, 0.07, 0.78)

        Row {
            id: recordingRow
            anchors.centerIn: parent
            spacing: Theme.space2
            Rectangle {
                width: 10; height: 10; radius: 5
                anchors.verticalCenter: parent.verticalCenter
                color: "#E5484D"
            }
            Text {
                text: "Recording this scan to " + DirectScan.recordingRoot
                color: "#F3F2F2"
                font.family: Theme.fontMono; font.pixelSize: 12
            }
        }
    }

    // The size of the cube being scanned: found from the cube in view (Auto, the
    // default) or chosen here, which stands until Auto is chosen again. A change
    // starts the scan over.
    Row {
        objectName: "scan.sizes"
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: chips.top
        anchors.bottomMargin: Theme.space3
        spacing: Theme.space2
        visible: !root.complete && !root.cameraFailed

        // The size is found from the cube until a size is chosen; this goes back to
        // finding it.
        AppButton {
            objectName: "scan.size.auto"
            height: 36
            variant: DirectScan.autoSize ? "primary" : "secondary"
            text: qsTrId("scan.size.auto")
            onClicked: DirectScan.useAutoSize()
        }
        Repeater {
            model: [2, 3, 4, 5]
            AppButton {
                objectName: "scan.size." + modelData
                height: 36
                variant: modelData === DirectScan.size ? "primary" : "secondary"
                text: qsTrId("entry.size.option").arg(modelData)
                onClicked: DirectScan.size = modelData
            }
        }
    }

    // The sides read so far, in the order they were first shown, each as the
    // camera last saw it. Colours are the measured ones, not tidy swatches.
    Row {
        id: chips
        objectName: "scan.chips"
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: buttons.top
        anchors.bottomMargin: Theme.space4
        spacing: Theme.space3

        readonly property real chip: Math.max(48, Math.min(96, (root.width - 2 * Theme.space4 - 5 * spacing) / 6))

        Repeater {
            model: 6
            delegate: Rectangle {
                id: chipFrame
                readonly property int slot: index
                readonly property var side: slot < root.sides.length ? root.sides[slot] : null
                readonly property bool filled: side !== null
                // The first empty chip is where the side being held will land.
                readonly property bool next: !filled && slot === root.sides.length
                readonly property bool inView: filled && side.inView && !root.complete

                width: chips.chip; height: chips.chip
                radius: 6
                color: Qt.rgba(0.08, 0.075, 0.07, 0.85)
                border.width: root.complete || inView ? 3 : 1
                border.color: root.complete ? root.good : inView ? "#F3F2F2" : Qt.rgba(1, 1, 1, 0.25)
                Behavior on border.color { ColorAnimation { duration: 220 } }

                // A side arriving.
                onFilledChanged: if (filled) arrive.restart()
                SequentialAnimation {
                    id: arrive
                    NumberAnimation { target: chipFrame; property: "scale"; from: 0.8; to: 1.08; duration: 130; easing.type: Easing.OutQuad }
                    NumberAnimation { target: chipFrame; property: "scale"; to: 1.0; duration: 110; easing.type: Easing.InOutQuad }
                }

                // All six confirmed: a ripple along the row.
                Connections {
                    target: root
                    function onCompleteChanged() {
                        if (root.complete)
                            confirm.restart();
                    }
                }
                SequentialAnimation {
                    id: confirm
                    PauseAnimation { duration: 260 + chipFrame.slot * 70 }
                    NumberAnimation { target: chipFrame; property: "scale"; to: 1.1; duration: 120; easing.type: Easing.OutQuad }
                    NumberAnimation { target: chipFrame; property: "scale"; to: 1.0; duration: 160; easing.type: Easing.InOutQuad }
                }

                Grid {
                    anchors.centerIn: parent
                    columns: root.size
                    spacing: root.size > 5 ? 1 : 2
                    visible: chipFrame.filled
                    Repeater {
                        model: root.size * root.size
                        delegate: Rectangle {
                            width: (chips.chip - 16 - (root.size - 3) * 2) / root.size; height: width
                            radius: 2
                            color: chipFrame.filled ? chipFrame.side.colors[index] : "transparent"
                        }
                    }
                }

                // The side being held, filling towards counting.
                Rectangle {
                    visible: chipFrame.next && DirectScan.captureProgress > 0
                    x: 6; y: parent.height - 6 - height
                    height: 5; radius: 2.5
                    width: (parent.width - 12) * DirectScan.captureProgress
                    color: root.good
                }
            }
        }
    }

    Row {
        id: buttons
        objectName: "scan.buttons"
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: Theme.space4
        spacing: Theme.space3

        AppButton {
            height: 42
            variant: "secondary"; text: "Cancel"
            onClicked: AppState.goLaunch()
        }
        // Six sides are in and they make no cube (or one the person doubts): look
        // at what was read and put right what the camera misread.
        AppButton {
            objectName: "scan.checkColours"
            height: 42
            visible: DirectScan.reviewable && !root.cameraFailed
            variant: DirectScan.conflicted ? "primary" : "secondary"
            text: qsTrId("scan.checkColours")
            onClicked: AppState.reviewScan()
        }
        AppButton {
            height: 42
            visible: !root.complete && !root.cameraFailed
            variant: "secondary"; text: "Start over"
            enabled: DirectScan.sidesRead > 0
            onClicked: DirectScan.restart()
        }
        // In place of Start over when the camera would not start: the same
        // start the screen made on entry (AppState.onScreenChanged), so a
        // permission now granted or a camera now free works without going
        // back to the launch screen.
        AppButton {
            objectName: "scan.retry"
            height: 42
            visible: root.cameraFailed
            variant: "primary"; text: qsTrId("common.tryAgain")
            onClicked: DirectScan.start(CameraController.selectedDeviceId)
        }
    }
}
