import QtQuick
import Cubeulator

Rectangle {
    id: root
    color: "#2D2B2B"
    readonly property bool wide: width >= Theme.wideBreakpoint

    readonly property int sc: AppState.scanned
    readonly property var curFace: AppState.faceDefs[Math.min(sc, 5)]

    property var progressCube: ({
        top: sc >= 1 ? Array(9).fill(Theme.faceU) : null,
        right: sc >= 2 ? Array(9).fill(Theme.faceR) : null,
        left: sc >= 3 ? Array(9).fill(Theme.faceF) : null
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
                    Text { text: "Hold steady — reading colours"; color: "white"; font.pixelSize: 12; font.family: Theme.fontHeading }
                }
            }
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
                sourceComponent: missingHint
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
                        Rectangle { width: (parent.width - 8) / 3; height: 6; color: "#C07C12" }
                        Rectangle { width: (parent.width - 8) / 3; height: 6; color: "#C07C12" }
                        Rectangle { width: (parent.width - 8) / 3; height: 6; color: "#4B4747" }
                    }
                    Text { text: "shaky"; color: "#C07C12"; font.family: Theme.fontMono; font.pixelSize: 11; anchors.verticalCenter: parent.verticalCenter }
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

            Text {
                anchors.top: parent.top; anchors.left: parent.left; anchors.margins: Theme.space4
                text: "Show the " + root.curFace.name + " face"
                color: "#F3F2F2"; font.family: Theme.fontMono; font.pixelSize: 13
            }
            Loader { anchors.centerIn: parent; sourceComponent: reticle
                onLoaded: item.size = 250 }
            Loader {
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.bottom: parent.bottom
                anchors.bottomMargin: Theme.space4
                sourceComponent: missingHint
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
                    Rectangle { width: (parent.width - 10) / 3; height: 8; color: "#C07C12" }
                    Rectangle { width: (parent.width - 10) / 3; height: 8; color: "#C07C12" }
                    Rectangle { width: (parent.width - 10) / 3; height: 8; color: "#4B4747" }
                }
                Text {
                    text: "Current face reading shaky — steady the cube."
                    color: "#C07C12"; font.pixelSize: 12; font.family: Theme.fontHeading
                    width: parent.width; wrapMode: Text.WordWrap
                }
            }
        }
    }
}
