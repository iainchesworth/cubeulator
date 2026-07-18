import QtQuick
import Cubeulator

Rectangle {
    id: root
    color: Theme.paper
    readonly property bool wide: width >= Theme.wideBreakpoint

    readonly property int step: AppState.step
    readonly property var curStep: AppState.steps[step]

    readonly property var scrambled: ({
        top: ["#F2F0EC", "#F2F0EC", "#F0C94A", "#3A9D5D", "#F2F0EC", "#DF7A2B", "#F2F0EC", "#2F6FAE", "#F2F0EC"],
        right: ["#D1372A", "#DF7A2B", "#D1372A", "#2F6FAE", "#D1372A", "#3A9D5D", "#D1372A", "#F2F0EC", "#F0C94A"],
        left: ["#3A9D5D", "#F0C94A", "#3A9D5D", "#F2F0EC", "#3A9D5D", "#D1372A", "#DF7A2B", "#3A9D5D", "#2F6FAE"]
    })

    property real arrowOpacity: 1.0
    SequentialAnimation on arrowOpacity {
        loops: Animation.Infinite
        NumberAnimation { from: 0.3; to: 1.0; duration: 800; easing.type: Easing.InOutQuad }
        NumberAnimation { from: 1.0; to: 0.3; duration: 800; easing.type: Easing.InOutQuad }
    }

    Component {
        id: stepRow
        Rectangle {
            property int idx: 0
            property var s: null
            readonly property bool isCurrent: idx === root.step
            readonly property bool isDone: idx < root.step
            width: ListView.view ? ListView.view.width : 320
            height: isCurrent ? (detailCol.implicitHeight + Theme.space3 * 2) : 56
            color: isCurrent ? Theme.accent100 : "transparent"
            opacity: isDone ? 0.5 : 1.0

            Rectangle {
                anchors.bottom: parent.bottom
                width: parent.width; height: 1
                color: Theme.divider
            }

            Row {
                anchors.fill: parent
                anchors.margins: Theme.space3
                spacing: Theme.space3

                Rectangle {
                    width: 30; height: 30
                    color: isCurrent ? Theme.accent : "transparent"
                    border.width: isCurrent ? 0 : 1
                    border.color: Theme.divider
                    Text {
                        anchors.centerIn: parent
                        text: isDone ? "✓" : (idx + 1).toString().padStart(2, "0")
                        font.family: Theme.fontMono
                        font.pixelSize: 13
                        color: isCurrent ? Theme.paper : (isDone ? Theme.neutral600 : Theme.ink)
                    }
                }

                Column {
                    id: detailCol
                    width: parent.width - 42
                    spacing: 4
                    Text {
                        text: s ? s.title : ""
                        font.family: Theme.fontHeading; font.weight: Font.ExtraBold; font.pixelSize: 15
                        color: Theme.ink
                    }
                    Text {
                        visible: isCurrent
                        text: s ? s.detail : ""
                        font.family: Theme.fontHeading; font.pixelSize: 12
                        color: Qt.rgba(0.125, 0.117, 0.113, 0.6)
                        width: parent.width; wrapMode: Text.WordWrap
                    }
                    Text {
                        visible: isCurrent
                        text: s ? s.move : ""
                        font.family: Theme.fontMono; font.pixelSize: 13
                        color: Theme.accent700
                    }
                }
            }
        }
    }

    // ---- narrow: fixed animation pane + drawer ----
    Column {
        visible: !root.wide
        anchors.fill: parent

        Row {
            width: parent.width
            padding: Theme.space3
            Text {
                text: "⌚ " + AppState.elapsedLabel()
                font.family: Theme.fontMono; font.pixelSize: 12; color: Theme.ink
                width: parent.width - 100
            }
            Text {
                text: AppState.step + " / 42 mv"
                font.family: Theme.fontMono; font.pixelSize: 12; color: Theme.ink
            }
        }
        Rectangle {
            width: parent.width; height: 1; color: Theme.divider
        }

        Item {
            width: parent.width; height: 230
            CubeView {
                width: 200; height: 200
                anchors.centerIn: parent
                topColors: root.scrambled.top
                rightColors: root.scrambled.right
                leftColors: root.scrambled.left
                showArrow: true
                arrowOpacity: root.arrowOpacity
            }
            Text {
                anchors.top: parent.top; anchors.left: parent.left; anchors.margins: Theme.space3
                text: root.curStep.move
                font.family: Theme.fontMono; font.pixelSize: 22; font.letterSpacing: 1
                color: Theme.ink
            }
            Text {
                anchors.bottom: parent.bottom; anchors.right: parent.right; anchors.margins: Theme.space3
                text: "orthographic · no perspective"
                font.family: Theme.fontHeading; font.pixelSize: 11
                color: Qt.rgba(0.125, 0.117, 0.113, 0.55)
            }
        }
        Rectangle { width: parent.width; height: 2; color: Theme.divider }

        Column {
            width: parent.width
            padding: Theme.space4
            spacing: Theme.space1

            Text {
                text: "STEP " + (root.step + 1) + " OF " + AppState.steps.length
                color: Theme.accent; font.family: Theme.fontHeading; font.pixelSize: 11; font.letterSpacing: 1.4
            }
            Text { text: root.curStep.title; font.family: Theme.fontHeading; font.weight: Font.ExtraBold; font.pixelSize: 20; color: Theme.ink }
            Text {
                text: root.curStep.detail
                font.family: Theme.fontHeading; font.pixelSize: 13
                color: Qt.rgba(0.125, 0.117, 0.113, 0.6)
                width: parent.width - Theme.space8; wrapMode: Text.WordWrap
            }
            Item { width: 1; height: Theme.space4 }
            Row {
                width: parent.width - Theme.space8
                spacing: Theme.space2
                AppButton { height: 44; variant: "secondary"; text: "Previous"; onClicked: AppState.prevStep() }
                AppButton { height: 44; width: parent.width - 100 - Theme.space2; variant: "primary"; text: "Next step"; onClicked: AppState.nextStep() }
            }
            Row {
                topPadding: Theme.space3
                spacing: Theme.space2
                Rectangle {
                    width: 16; height: 16; border.width: 1.5; border.color: Theme.divider
                    Icon { anchors.centerIn: parent; name: "check"; color: Theme.accent; width: 12; height: 12; strokeWidth: 3 }
                }
                Text { text: "Auto-advance from camera"; font.pixelSize: 12; color: Qt.rgba(0.125, 0.117, 0.113, 0.6); font.family: Theme.fontHeading }
            }
        }
    }

    // ---- wide: true split ----
    Column {
        visible: root.wide
        anchors.fill: parent

        Row {
            width: parent.width
            padding: Theme.space3
            Text { text: "Solving — beginner method"; font.family: Theme.fontMono; font.pixelSize: 12; color: Qt.rgba(0.125, 0.117, 0.113, 0.6); width: parent.width - 160 }
            Text { text: AppState.elapsedLabel() + " · " + root.step + "/42"; font.family: Theme.fontMono; font.pixelSize: 13; color: Theme.ink }
        }
        Rectangle { width: parent.width; height: 2; color: Theme.divider }

        Row {
            width: parent.width
            height: parent.height - 46

            Item {
                width: parent.width - 320
                height: parent.height

                CubeView {
                    width: 300; height: 300
                    anchors.centerIn: parent
                    topColors: root.scrambled.top
                    rightColors: root.scrambled.right
                    leftColors: root.scrambled.left
                    showArrow: true
                    arrowOpacity: root.arrowOpacity
                }
                Column {
                    anchors.top: parent.top; anchors.left: parent.left; anchors.margins: Theme.space4
                    spacing: 4
                    Text { text: "STEP " + (root.step + 1) + " OF " + AppState.steps.length; color: Theme.accent; font.family: Theme.fontHeading; font.pixelSize: 11; font.letterSpacing: 1.4 }
                    Text { text: root.curStep.move; font.family: Theme.fontMono; font.pixelSize: 30; font.letterSpacing: 1; color: Theme.ink }
                }
                Rectangle {
                    anchors.bottom: parent.bottom; anchors.right: parent.right; anchors.margins: Theme.space4
                    width: 120; height: 82
                    color: "#2D2B2B"; border.width: 2; border.color: Theme.ink
                    Row {
                        anchors.bottom: parent.bottom; anchors.left: parent.left; anchors.right: parent.right
                        anchors.margins: 6
                        Text { text: "AUTO"; color: "#BAB6B6"; font.family: Theme.fontMono; font.pixelSize: 10; width: parent.width - 10 }
                        Rectangle { width: 8; height: 8; radius: 4; color: Theme.detected }
                    }
                }
                Text {
                    anchors.bottom: parent.bottom; anchors.left: parent.left; anchors.margins: Theme.space4
                    text: "Orthographic · arrows show the exact turn"
                    font.pixelSize: 12; color: Qt.rgba(0.125, 0.117, 0.113, 0.55); font.family: Theme.fontHeading
                }
                Rectangle { anchors.right: parent.right; width: 2; height: parent.height; color: Theme.divider }
            }

            Column {
                width: 320
                height: parent.height

                Text {
                    width: parent.width
                    padding: Theme.space3
                    text: "STEPS"
                    font.family: Theme.fontHeading; font.pixelSize: 11; font.letterSpacing: 1.4
                    color: Qt.rgba(0.125, 0.117, 0.113, 0.6)
                }
                Rectangle { width: parent.width; height: 2; color: Theme.divider }

                ListView {
                    width: parent.width
                    height: parent.height - 46 - 62
                    clip: true
                    model: AppState.steps
                    delegate: Loader {
                        sourceComponent: stepRow
                        onLoaded: { item.idx = index; item.s = modelData; }
                    }
                }

                Rectangle { width: parent.width; height: 2; color: Theme.divider }
                Row {
                    padding: Theme.space3
                    spacing: Theme.space2
                    AppButton { height: 42; variant: "secondary"; text: "Previous"; onClicked: AppState.prevStep() }
                    AppButton { height: 42; width: 320 - 100 - Theme.space2 - Theme.space6; variant: "primary"; text: "Next step"; onClicked: AppState.nextStep() }
                }
            }
        }
    }
}
