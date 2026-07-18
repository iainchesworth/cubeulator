import QtQuick
import Cubeulator

Rectangle {
    id: root
    color: Theme.paper
    readonly property bool wide: width >= Theme.wideBreakpoint

    property var solvedCube: ({ top: Array(9).fill(Theme.faceU), right: Array(9).fill(Theme.faceR), left: Array(9).fill(Theme.faceF) })

    // ---- narrow (phone) ----
    Column {
        visible: !root.wide
        anchors.fill: parent
        anchors.margins: Theme.space6
        spacing: Theme.space4

        Text {
            text: "SCAN · SOLVE · IN YOUR HANDS"
            color: Theme.accent
            font.family: Theme.fontHeading
            font.pixelSize: 11
            font.letterSpacing: 1.6
        }

        Item {
            width: parent.width
            height: root.height - 360
            CubeView {
                width: 180; height: 190
                anchors.centerIn: parent
                topColors: root.solvedCube.top
                rightColors: root.solvedCube.right
                leftColors: root.solvedCube.left
            }
        }

        Column {
            width: parent.width
            spacing: Theme.space2

            AppButton {
                width: parent.width; height: 46
                variant: "primary"; block: true
                text: "Start scan"; trailingIcon: "chevron-right"
                onClicked: AppState.beginScan()
            }
            AppButton {
                width: parent.width; height: 44
                variant: "secondary"; block: true
                text: "Solve history"; trailingIcon: "chevron-right"
            }
            Rectangle { width: parent.width; height: 1; color: Theme.divider }
            Item {
                width: parent.width
                height: 40
                AppButton {
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    variant: "ghost"; text: "English"; leadingIcon: "globe"
                }
                AppButton {
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    variant: "ghost"; text: "Settings"; leadingIcon: "menu"
                }
            }
        }
    }

    // ---- wide (tablet/desktop) ----
    Row {
        visible: root.wide
        anchors.fill: parent

        Column {
            width: parent.width / 2
            height: parent.height
            padding: Theme.space8 + Theme.space2

            Text {
                text: "CUBEULATOR"
                color: Theme.accent
                font.family: Theme.fontHeading
                font.pixelSize: 11
                font.letterSpacing: 1.8
            }

            Item { width: 1; height: parent.height - 340 }

            Column {
                spacing: Theme.space3
                Text {
                    text: "Scan. Solve.\nIn your hands."
                    color: Theme.ink
                    font.family: Theme.fontHeading
                    font.weight: Font.ExtraBold
                    font.pixelSize: 44
                    lineHeight: 0.98
                    lineHeightMode: Text.ProportionalHeight
                }
                Text {
                    text: "Point the camera, turn the cube through six faces, follow the moves."
                    color: Qt.rgba(0.125, 0.117, 0.113, 0.55)
                    font.family: Theme.fontHeading
                    font.pixelSize: 15
                    width: 340
                    wrapMode: Text.WordWrap
                }
                Row {
                    spacing: Theme.space3
                    topPadding: Theme.space3
                    AppButton {
                        height: 46; variant: "primary"
                        text: "Start scan"; trailingIcon: "chevron-right"
                        onClicked: AppState.beginScan()
                    }
                    AppButton { height: 46; variant: "secondary"; text: "Solve history" }
                }
            }

            Item { width: 1; height: 40 }

            Row {
                spacing: Theme.space6
                AppButton { variant: "ghost"; text: "English"; leadingIcon: "globe" }
                AppButton { variant: "ghost"; text: "Settings"; leadingIcon: "menu" }
            }
        }

        Rectangle {
            width: parent.width / 2
            height: parent.height
            color: Theme.surface

            Rectangle { width: 2; height: parent.height; color: Theme.divider }

            CubeView {
                width: 300; height: 320
                anchors.centerIn: parent
                topColors: root.solvedCube.top
                rightColors: root.solvedCube.right
                leftColors: root.solvedCube.left
            }
        }
    }
}
