import QtQuick
import Cubeulator

Rectangle {
    id: root
    color: Theme.paper
    readonly property bool wide: width >= Theme.wideBreakpoint

    readonly property var solvedCube: ({ top: Array(9).fill(Theme.faceU), right: Array(9).fill(Theme.faceR), left: Array(9).fill(Theme.faceF) })

    component BobbingCube: CubeView {
        SequentialAnimation on y {
            loops: Animation.Infinite
            NumberAnimation { from: 0; to: -5; duration: 1300; easing.type: Easing.InOutQuad }
            NumberAnimation { from: -5; to: 0; duration: 1300; easing.type: Easing.InOutQuad }
        }
    }

    component SweepUnderline: Item {
        property real fullWidth: 120
        property real barHeight: 4
        implicitWidth: fullWidth
        implicitHeight: barHeight
        Rectangle {
            height: parent.barHeight
            width: parent.fullWidth * scaleX
            color: Theme.accent
            property real scaleX: 0
            SequentialAnimation on scaleX {
                loops: Animation.Infinite
                NumberAnimation { from: 0; to: 1; duration: 1300; easing.type: Easing.OutCubic }
                PauseAnimation { duration: 1100 }
                PropertyAction { value: 0 }
            }
        }
    }

    // ---- narrow ----
    Column {
        visible: !root.wide
        anchors.fill: parent
        anchors.margins: Theme.space6
        spacing: 0

        BobbingCube {
            width: 180; height: 190
            anchors.horizontalCenter: parent.horizontalCenter
            topColors: root.solvedCube.top
            rightColors: root.solvedCube.right
            leftColors: root.solvedCube.left
        }

        Column {
            topPadding: Theme.space2
            spacing: 4
            Text { text: qsTrId("complete.title"); font.family: Theme.fontHeading; font.weight: Font.ExtraBold; font.pixelSize: 44; color: Theme.ink; font.letterSpacing: -0.5 }
            SweepUnderline { fullWidth: 120 }
        }

        Rectangle {
            width: parent.width; height: 76
            border.width: 2; border.color: Theme.divider
            anchors.topMargin: Theme.space6
            Row {
                anchors.fill: parent
                Column {
                    width: parent.width / 2; height: parent.height
                    padding: Theme.space3
                    Text { text: qsTrId("complete.turns"); font.capitalization: Font.AllUppercase; font.pixelSize: 11; font.letterSpacing: 1; color: Theme.inkMuted(0.6); font.family: Theme.fontHeading }
                    Text { text: Qt.locale(Qt.uiLanguage).toString(AppState.moveCount); font.family: Theme.fontMono; font.pixelSize: 30; color: Theme.ink }
                }
                Rectangle { width: 2; height: parent.height; color: Theme.divider }
                Column {
                    width: parent.width / 2 - 2; height: parent.height
                    padding: Theme.space3
                    Text { text: qsTrId("complete.time"); font.capitalization: Font.AllUppercase; font.pixelSize: 11; font.letterSpacing: 1; color: Theme.inkMuted(0.6); font.family: Theme.fontHeading }
                    Text { text: AppState.elapsedLabel(); font.family: Theme.fontMono; font.pixelSize: 30; color: Theme.ink }
                }
            }
        }

        Item { width: 1; height: Math.max(20, root.height - 560) }

        Column {
            width: parent.width
            spacing: Theme.space2
            AppButton { width: parent.width; height: 46; variant: "primary"; block: true; objectName: "complete.review"; text: qsTrId("complete.review") }
            AppButton { width: parent.width; height: 44; variant: "secondary"; block: true; objectName: "complete.scanAnother"; text: qsTrId("complete.scanAnother"); onClicked: AppState.scanAnother() }
        }
    }

    // ---- wide ----
    Row {
        visible: root.wide
        anchors.fill: parent

        Rectangle {
            width: parent.width * 300 / 720
            height: parent.height
            color: Theme.surface

            BobbingCube {
                width: 250; height: 260
                anchors.centerIn: parent
                topColors: root.solvedCube.top
                rightColors: root.solvedCube.right
                leftColors: root.solvedCube.left
            }
        }

        Column {
            width: parent.width - parent.width * 300 / 720
            height: parent.height
            padding: Theme.space8 + Theme.space2 - 4

            Text { text: qsTrId("complete.eyebrow"); font.capitalization: Font.AllUppercase; color: Theme.accent; font.family: Theme.fontHeading; font.pixelSize: 11; font.letterSpacing: 1.6 }
            Item { width: 1; height: Math.max(20, parent.height - 380) }

            Column {
                spacing: 6
                Text { text: qsTrId("complete.title"); font.family: Theme.fontHeading; font.weight: Font.ExtraBold; font.pixelSize: 64; color: Theme.ink; font.letterSpacing: -1 }
                SweepUnderline { fullWidth: 180; barHeight: 5 }
            }

            Rectangle {
                width: parent.width - 88
                height: 96
                border.width: 2; border.color: Theme.divider
                anchors.topMargin: Theme.space8
                Row {
                    anchors.fill: parent
                    Column {
                        width: parent.width / 3; height: parent.height; padding: Theme.space4
                        Text { text: qsTrId("complete.turns"); font.capitalization: Font.AllUppercase; font.pixelSize: 11; font.letterSpacing: 1; color: Theme.inkMuted(0.6); font.family: Theme.fontHeading }
                        Text { text: Qt.locale(Qt.uiLanguage).toString(AppState.moveCount); font.family: Theme.fontMono; font.pixelSize: 40; color: Theme.ink }
                    }
                    Rectangle { width: 2; height: parent.height; color: Theme.divider }
                    Column {
                        width: parent.width / 3; height: parent.height; padding: Theme.space4
                        Text { text: qsTrId("complete.time"); font.capitalization: Font.AllUppercase; font.pixelSize: 11; font.letterSpacing: 1; color: Theme.inkMuted(0.6); font.family: Theme.fontHeading }
                        Text { text: AppState.elapsedLabel(); font.family: Theme.fontMono; font.pixelSize: 40; color: Theme.ink }
                    }
                    Rectangle { width: 2; height: parent.height; color: Theme.divider }
                    Column {
                        width: parent.width / 3 - 4; height: parent.height; padding: Theme.space4
                        Text { text: "METHOD"; font.pixelSize: 11; font.letterSpacing: 1; color: Theme.inkMuted(0.6); font.family: Theme.fontHeading }
                        Text { text: "Beginner"; font.family: Theme.fontMono; font.pixelSize: 18; color: Theme.ink; topPadding: 14 }
                    }
                }
            }

            Item { width: 1; height: Math.max(20, parent.height - 640) }

            // A Flow, not a Row: two buttons whose labels run to 40% more in
            // another language do not fit side by side in this pane, and one
            // that runs off the window cannot be pressed.
            Flow {
                width: parent.width - parent.padding * 2
                spacing: Theme.space3
                AppButton { height: 48; variant: "primary"; objectName: "complete.review"; text: qsTrId("complete.review") }
                AppButton { height: 48; variant: "secondary"; objectName: "complete.scanAnother"; text: qsTrId("complete.scanAnother"); onClicked: AppState.scanAnother() }
            }
        }
    }
}
