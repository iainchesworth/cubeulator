import QtQuick
import Cubeulator

Rectangle {
    id: root
    color: "#2D2B2B"
    readonly property bool wide: width >= Theme.wideBreakpoint

    readonly property int sc: AppState.scanned
    readonly property var curFace: AppState.faceDefs[Math.min(sc, 5)]

    // Predicted-but-unconfirmed centers render at reduced alpha so they
    // read as a hint, not a real reading - no CubeView changes needed
    // (that file is what the sibling ui-polish-flow worktree is about to
    // replace), a cell is just passed a translucent color instead of null.
    function withAlpha(hex, alpha) {
        if (!hex) return null;
        var a = Math.round(alpha * 255).toString(16).padStart(2, "0");
        return "#" + a + hex.substring(1);
    }

    function faceCells(index) {
        var color = ScanSession.progressCenters[index];
        if (!color) return null;
        var confirmed = ScanSession.progressCentersConfirmed[index];
        return Array(9).fill(confirmed ? color : root.withAlpha(color, 0.35));
    }

    // top/right/left = U/R/F: the only three faces this isometric view ever
    // shows, and (by construction) always the first three scanned in order.
    property var progressCube: ({
        top: root.faceCells(0),
        right: root.faceCells(1),
        left: root.faceCells(2)
    })

    Component {
        id: reticle
        Item {
            property real size: 170
            width: size; height: size
            SequentialAnimation on opacity {
                loops: Animation.Infinite
                NumberAnimation { from: 0.4; to: 0.95; duration: 900; easing.type: Easing.InOutQuad }
                NumberAnimation { from: 0.95; to: 0.4; duration: 900; easing.type: Easing.InOutQuad }
            }
            readonly property real bracket: size * 0.15
            Item { anchors.top: parent.top; anchors.left: parent.left; width: bracket; height: bracket
                Rectangle { width: parent.width; height: 3; color: "#F3F2F2" }
                Rectangle { width: 3; height: parent.height; color: "#F3F2F2" } }
            Item { anchors.top: parent.top; anchors.right: parent.right; width: bracket; height: bracket
                Rectangle { anchors.top: parent.top; width: parent.width; height: 3; color: "#F3F2F2" }
                Rectangle { anchors.right: parent.right; width: 3; height: parent.height; color: "#F3F2F2" } }
            Item { anchors.bottom: parent.bottom; anchors.left: parent.left; width: bracket; height: bracket
                Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 3; color: "#F3F2F2" }
                Rectangle { width: 3; height: parent.height; color: "#F3F2F2" } }
            Item { anchors.bottom: parent.bottom; anchors.right: parent.right; width: bracket; height: bracket
                Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 3; color: "#F3F2F2" }
                Rectangle { anchors.right: parent.right; width: 3; height: parent.height; color: "#F3F2F2" } }

            // Real per-cell grid overlay, tinted by classification and
            // dimmed by confidence - laid out over the same square the
            // reticle brackets frame, independent of whether OpenCV's own
            // detected grid lines up with it (this is a framing guide, not
            // a projection of the actual detected geometry).
            Grid {
                anchors.fill: parent
                anchors.margins: parent.bracket * 0.6
                columns: 3; rows: 3
                visible: ScanSession.cubeDetected
                Repeater {
                    model: 9
                    delegate: Rectangle {
                        readonly property string cellColor: ScanSession.cellColors[index] || "#00000000"
                        readonly property real cellConfidence: ScanSession.cellConfidence[index] || 0
                        width: (parent.width - 4) / 3
                        height: (parent.height - 4) / 3
                        color: cellColor
                        opacity: 0.25 + cellConfidence * 0.55
                        border.width: 1
                        border.color: Qt.rgba(1, 1, 1, 0.3)
                    }
                }
            }
        }
    }

    Component {
        id: missingHint
        Row {
            spacing: 6
            SequentialAnimation on opacity {
                loops: Animation.Infinite
                NumberAnimation { from: 0.55; to: 1.0; duration: 550 }
                NumberAnimation { from: 1.0; to: 0.55; duration: 550 }
            }
            Rectangle {
                width: chipRow.width + 24; height: 26
                color: Theme.warning
                Row {
                    id: chipRow
                    anchors.centerIn: parent
                    spacing: 6
                    Icon { name: "alert-triangle"; color: "white"; width: 14; height: 14; strokeWidth: 2.4 }
                    Text {
                        text: !ScanSession.cubeDetected ? "Show the whole cube"
                            : !ScanSession.isSteady ? "Hold steady — reading colours"
                            : "Move your hand out of frame"
                        color: "white"; font.pixelSize: 12; font.family: Theme.fontHeading
                    }
                }
            }
        }
    }

    Component {
        id: confirmRetakeRow
        Row {
            spacing: Theme.space2
            AppButton { variant: "secondary"; text: "Retake"; onClicked: ScanSession.retakeFace() }
            AppButton { variant: "primary"; text: "Confirm"; trailingIcon: "chevron-right"; onClicked: ScanSession.confirmFace() }
        }
    }

    Component {
        id: faceTile
        Column {
            property string label: ""
            property color fill: "transparent"
            property bool captured: false
            spacing: 5
            Rectangle {
                width: 22; height: 22
                color: captured ? fill : "transparent"
                border.width: captured ? 0 : 1
                border.color: "#9B9797"
            }
            Text { text: label; color: "#BAB6B6"; font.family: Theme.fontMono; font.pixelSize: 10; anchors.horizontalCenter: parent.horizontalCenter }
        }
    }

    // ---- narrow ----
    Column {
        visible: !root.wide
        anchors.fill: parent

        Row {
            width: parent.width
            padding: Theme.space3
            Text {
                text: "Show the " + root.curFace.name + " face"
                color: "#F3F2F2"; font.family: Theme.fontMono; font.pixelSize: 11
                width: parent.width - 60
            }
            Text {
                text: root.sc + " / 6"
                color: "#F3F2F2"; font.family: Theme.fontMono; font.pixelSize: 11; font.weight: Font.DemiBold
            }
        }

        Item {
            width: parent.width
            height: root.height - 40 - 130
            clip: true
            Image {
                anchors.fill: parent
                fillMode: Image.PreserveAspectCrop
                cache: false
                visible: ScanSession.running
                source: ScanSession.running ? "image://scanFrame/latest#" + ScanSession.frameSeq : ""
            }
            Text {
                anchors.centerIn: parent
                visible: ScanSession.starting
                text: "Connecting to camera…"
                color: "#F3F2F2"; font.family: Theme.fontMono; font.pixelSize: 13
            }
            Loader { anchors.centerIn: parent; sourceComponent: reticle }
            CubeView {
                width: 74; height: 78
                anchors.top: parent.top; anchors.right: parent.right
                anchors.margins: Theme.space2
                topColors: root.progressCube.top
                rightColors: root.progressCube.right
                leftColors: root.progressCube.left
            }
            Loader {
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.bottom: parent.bottom
                anchors.bottomMargin: Theme.space3
                sourceComponent: ScanSession.starting ? null
                    : (ScanSession.readyToConfirm && !ScanSession.autoAdvance) ? confirmRetakeRow
                    : (!ScanSession.cubeDetected || !ScanSession.isSteady || ScanSession.handPresent) ? missingHint
                    : null
            }
        }

        Rectangle {
            width: parent.width
            height: 130
            color: "#201E1D"

            Column {
                anchors.fill: parent
                anchors.margins: Theme.space3
                spacing: Theme.space3

                Row {
                    width: parent.width
                    Repeater {
                        model: AppState.faceDefs
                        delegate: Loader {
                            width: (parent.width) / 6
                            sourceComponent: faceTile
                            onLoaded: { item.label = modelData.code; item.fill = modelData.color; item.captured = index < root.sc; }
                        }
                    }
                }
                Row {
                    width: parent.width
                    spacing: 10
                    Text { text: "CONF"; color: "#BAB6B6"; font.family: Theme.fontMono; font.pixelSize: 11; anchors.verticalCenter: parent.verticalCenter }
                    Row {
                        width: parent.width - 90
                        spacing: 4
                        Repeater {
                            model: 3
                            delegate: Rectangle {
                                readonly property real threshold: (index + 1) / 3
                                width: (parent.width - 8) / 3; height: 6
                                color: ScanSession.overallConfidence >= threshold ? Theme.warning : "#4B4747"
                            }
                        }
                    }
                    Text {
                        text: !ScanSession.isSteady ? "shaky" : ScanSession.handPresent ? "hand in frame" : ""
                        color: "#C07C12"; font.family: Theme.fontMono; font.pixelSize: 11; anchors.verticalCenter: parent.verticalCenter
                    }
                }
            }
        }
    }

    // ---- wide ----
    Row {
        visible: root.wide
        anchors.fill: parent

        Item {
            width: parent.width - 250
            height: parent.height
            clip: true

            Image {
                anchors.fill: parent
                fillMode: Image.PreserveAspectCrop
                cache: false
                visible: ScanSession.running
                source: ScanSession.running ? "image://scanFrame/latest#" + ScanSession.frameSeq : ""
            }
            Text {
                anchors.top: parent.top; anchors.left: parent.left; anchors.margins: Theme.space4
                text: "Show the " + root.curFace.name + " face"
                color: "#F3F2F2"; font.family: Theme.fontMono; font.pixelSize: 13
            }
            Text {
                anchors.centerIn: parent
                visible: ScanSession.starting
                text: "Connecting to camera…"
                color: "#F3F2F2"; font.family: Theme.fontMono; font.pixelSize: 14
            }
            Loader { anchors.centerIn: parent; sourceComponent: reticle
                onLoaded: item.size = 250 }
            Loader {
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.bottom: parent.bottom
                anchors.bottomMargin: Theme.space4
                sourceComponent: ScanSession.starting ? null
                    : (ScanSession.readyToConfirm && !ScanSession.autoAdvance) ? confirmRetakeRow
                    : (!ScanSession.cubeDetected || !ScanSession.isSteady || ScanSession.handPresent) ? missingHint
                    : null
            }
        }

        Rectangle {
            width: 250
            height: parent.height
            color: "#201E1D"
            Rectangle { width: 2; height: parent.height; color: "#4B4747" }

            Column {
                anchors.fill: parent
                anchors.margins: Theme.space4
                spacing: Theme.space3

                Row {
                    width: parent.width
                    Text { text: "PROGRESS"; color: "#BAB6B6"; font.pixelSize: 11; font.letterSpacing: 1; font.family: Theme.fontHeading }
                    Item { width: parent.width - 120 }
                    Text { text: root.sc + "/6"; color: "#F3F2F2"; font.family: Theme.fontMono; font.pixelSize: 18 }
                }

                CubeView {
                    width: 150; height: 158
                    anchors.horizontalCenter: parent.horizontalCenter
                    topColors: root.progressCube.top
                    rightColors: root.progressCube.right
                    leftColors: root.progressCube.left
                }

                Grid {
                    columns: 3
                    spacing: 10
                    anchors.horizontalCenter: parent.horizontalCenter
                    Repeater {
                        model: AppState.faceDefs
                        delegate: Loader {
                            width: 46
                            sourceComponent: faceTile
                            onLoaded: { item.label = modelData.code; item.fill = modelData.color; item.captured = index < root.sc; }
                        }
                    }
                }

                Item { width: 1; height: parent.height - 320 }

                Text { text: "CONFIDENCE"; color: "#BAB6B6"; font.family: Theme.fontMono; font.pixelSize: 11 }
                Row {
                    width: parent.width
                    spacing: 5
                    Repeater {
                        model: 3
                        delegate: Rectangle {
                            readonly property real threshold: (index + 1) / 3
                            width: (parent.width - 10) / 3; height: 8
                            color: ScanSession.overallConfidence >= threshold ? Theme.warning : "#4B4747"
                        }
                    }
                }
                Text {
                    text: !ScanSession.cubeDetected ? "Show the whole cube to the camera."
                        : !ScanSession.isSteady ? "Current face reading shaky — steady the cube."
                        : ScanSession.handPresent ? "A hand is blocking the view — move it aside."
                        : ScanSession.readyToConfirm && ScanSession.autoAdvance ? "Reading confirmed — advancing…"
                        : ""
                    color: "#C07C12"; font.pixelSize: 12; font.family: Theme.fontHeading
                    width: parent.width; wrapMode: Text.WordWrap
                }
            }
        }
    }
}
