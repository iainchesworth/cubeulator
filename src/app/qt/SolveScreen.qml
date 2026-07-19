import QtQuick
import Cubeulator

Rectangle {
    id: root
    color: Theme.paper
    readonly property bool wide: width >= Theme.wideBreakpoint

    // One entry per cube::solver::Stage (Cross, WhiteCorners, MiddleEdges,
    // Oll, Pll, in that order) - display copy lives here rather than in
    // the GUI-free solver lib, matching SolveController's own "no UI copy"
    // contract; SolveController.stageRanges gives the structural (index
    // range) data this pairs with.
    readonly property var stageCopy: [
        { title: "White cross", detail: "Bring the four white edges home, matching each side centre." },
        { title: "White corners", detail: "Insert the four white corners to complete the first layer." },
        { title: "Middle layer", detail: "Send the middle-layer edges home." },
        { title: "Orient last layer", detail: "Orient the last layer's edges and corners so the top face reads solid." },
        { title: "Permute last layer", detail: "Permute the last layer's pieces into their solved positions." }
    ]

    function colorHex(name) {
        switch (name) {
        case "White": return Theme.faceU;
        case "Yellow": return Theme.faceD;
        case "Green": return Theme.faceF;
        case "Blue": return Theme.faceB;
        case "Orange": return Theme.faceL;
        case "Red": return Theme.faceR;
        }
        return Theme.paper;
    }
    function colorsHex(names) {
        var out = [];
        for (var i = 0; i < names.length; i++)
            out.push(colorHex(names[i]));
        return out;
    }

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
            property var copy: null
            readonly property var range: SolveController.stageRanges[idx]
            readonly property bool isCurrent: SolveController.currentStageIndex === idx
            readonly property bool isDone: range && SolveController.moveIndex >= range.endIndex
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
                        text: copy ? copy.title : ""
                        font.family: Theme.fontHeading; font.weight: Font.ExtraBold; font.pixelSize: 15
                        color: Theme.ink
                    }
                    Text {
                        visible: isCurrent
                        text: copy ? copy.detail : ""
                        font.family: Theme.fontHeading; font.pixelSize: 12
                        color: Qt.rgba(0.125, 0.117, 0.113, 0.6)
                        width: parent.width; wrapMode: Text.WordWrap
                    }
                    Text {
                        visible: isCurrent
                        text: SolveController.currentMoveNotation
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
                text: SolveController.moveIndex + " / " + SolveController.totalMoves + " mv"
                font.family: Theme.fontMono; font.pixelSize: 12; color: Theme.ink
            }
        }
        Rectangle {
            width: parent.width; height: 1; color: Theme.divider
        }

        Item {
            width: parent.width; height: 230
            CubeView {
                id: narrowCubeView
                width: 200; height: 200
                anchors.centerIn: parent
                topColors: root.colorsHex(SolveController.topColors)
                rightColors: root.colorsHex(SolveController.rightColors)
                leftColors: root.colorsHex(SolveController.leftColors)
                showArrow: !SolveController.atEnd
                arrowOpacity: root.arrowOpacity
                currentMoveFace: SolveController.currentMoveFace
                currentMoveTurns: SolveController.currentMoveTurns
            }
            Text {
                anchors.top: parent.top; anchors.left: parent.left; anchors.margins: Theme.space3
                text: SolveController.currentMoveNotation
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

            readonly property var copy: root.stageCopy[SolveController.currentStageIndex]

            Text {
                text: "STAGE " + (SolveController.currentStageIndex + 1) + " OF " + root.stageCopy.length
                color: Theme.accent; font.family: Theme.fontHeading; font.pixelSize: 11; font.letterSpacing: 1.4
            }
            Text { text: parent.copy ? parent.copy.title : ""; font.family: Theme.fontHeading; font.weight: Font.ExtraBold; font.pixelSize: 20; color: Theme.ink }
            Text {
                text: parent.copy ? parent.copy.detail : ""
                font.family: Theme.fontHeading; font.pixelSize: 13
                color: Qt.rgba(0.125, 0.117, 0.113, 0.6)
                width: parent.width - Theme.space8; wrapMode: Text.WordWrap
            }
            Item { width: 1; height: Theme.space4 }
            Row {
                width: parent.width - Theme.space8
                spacing: Theme.space2
                AppButton { height: 44; variant: "secondary"; text: "Previous"; enabled: !SolveController.atStart; onClicked: { SolveController.previousMove(); narrowCubeView.animateMove(); } }
                AppButton { height: 44; width: parent.width - 100 - Theme.space2; variant: "primary"; text: SolveController.atEnd ? "Solved!" : "Next move"; enabled: !SolveController.atEnd; onClicked: { SolveController.nextMove(); narrowCubeView.animateMove(); } }
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
            Text { text: AppState.elapsedLabel() + " · " + SolveController.moveIndex + "/" + SolveController.totalMoves; font.family: Theme.fontMono; font.pixelSize: 13; color: Theme.ink }
        }
        Rectangle { width: parent.width; height: 2; color: Theme.divider }

        Row {
            width: parent.width
            height: parent.height - 46

            Item {
                width: parent.width - 320
                height: parent.height

                CubeView {
                    id: wideCubeView
                    width: 300; height: 300
                    anchors.centerIn: parent
                    topColors: root.colorsHex(SolveController.topColors)
                    rightColors: root.colorsHex(SolveController.rightColors)
                    leftColors: root.colorsHex(SolveController.leftColors)
                    showArrow: !SolveController.atEnd
                    arrowOpacity: root.arrowOpacity
                    currentMoveFace: SolveController.currentMoveFace
                    currentMoveTurns: SolveController.currentMoveTurns
                }
                Column {
                    anchors.top: parent.top; anchors.left: parent.left; anchors.margins: Theme.space4
                    spacing: 4
                    Text { text: "STAGE " + (SolveController.currentStageIndex + 1) + " OF " + root.stageCopy.length; color: Theme.accent; font.family: Theme.fontHeading; font.pixelSize: 11; font.letterSpacing: 1.4 }
                    Text { text: SolveController.currentMoveNotation; font.family: Theme.fontMono; font.pixelSize: 30; font.letterSpacing: 1; color: Theme.ink }
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
                    text: "Orthographic · arrow shows the exact turn"
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
                    text: "STAGES"
                    font.family: Theme.fontHeading; font.pixelSize: 11; font.letterSpacing: 1.4
                    color: Qt.rgba(0.125, 0.117, 0.113, 0.6)
                }
                Rectangle { width: parent.width; height: 2; color: Theme.divider }

                ListView {
                    width: parent.width
                    height: parent.height - 46 - 62
                    clip: true
                    model: root.stageCopy
                    delegate: Loader {
                        sourceComponent: stepRow
                        onLoaded: { item.idx = index; item.copy = modelData; }
                    }
                }

                Rectangle { width: parent.width; height: 2; color: Theme.divider }
                Row {
                    padding: Theme.space3
                    spacing: Theme.space2
                    AppButton { height: 42; variant: "secondary"; text: "Previous"; enabled: !SolveController.atStart; onClicked: { SolveController.previousMove(); wideCubeView.animateMove(); } }
                    AppButton { height: 42; width: 320 - 100 - Theme.space2 - Theme.space6; variant: "primary"; text: SolveController.atEnd ? "Solved!" : "Next move"; enabled: !SolveController.atEnd; onClicked: { SolveController.nextMove(); wideCubeView.animateMove(); } }
                }
            }
        }
    }
}
