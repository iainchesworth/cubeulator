import QtQuick
import Cubeulator

// The solve screen: the cube as it stands, in 3D, with the layer the next
// move turns nudging the way it has to go.
//
// The cube on screen is the cube in the hand: never mirrored, and turned the
// way the hold line at the top says it is held. Every move is shown on it by
// moving the layer itself, so nobody has to read notation to follow along.
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
        { title: "Orient last layer", detail: "Orient the last layer's edges and corners so the top side reads solid." },
        { title: "Permute last layer", detail: "Permute the last layer's pieces into their solved positions." }
    ]
    readonly property var currentCopy: stageCopy[SolveController.currentStageIndex]

    // Theme's sticker palette is keyed by colour, under names left over from
    // a white-on-top scheme: faceU is white, faceD yellow, faceL orange,
    // faceR red.
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

    // SolveController.stickers with each colour name swapped for its colour:
    // what the 3D cube is painted from.
    function painted(stickers) {
        var out = {};
        for (var side in stickers) {
            var names = stickers[side];
            var colors = [];
            for (var i = 0; i < names.length; i++)
                colors.push(colorHex(names[i]));
            out[side] = colors;
        }
        return out;
    }

    readonly property bool guiding: !SolveController.isSolving && !SolveController.atEnd

    // Where each side is, as the cube is held: yellow on top, green facing
    // the person holding it.
    readonly property var sideWords: ({ U: "Top", D: "Bottom", L: "Left side", R: "Right side", F: "Front", B: "Back" })
    readonly property string moveSide: guiding ? SolveController.currentMoveFace : ""
    // A side is named by the colour of its centre, which never moves.
    readonly property string moveColor: moveSide !== "" ? SolveController.stickers[moveSide][4] : ""
    readonly property string moveWords: {
        if (moveSide === "")
            return "";
        return sideWords[moveSide] + " · "
            + (SolveController.currentMoveTurns === 2 ? "half turn" : "quarter turn");
    }

    // ---- stepping ----------------------------------------------------------
    // A step is shown before it is taken: the layer turns all the way on
    // screen, and when it lands the solution moves on and the cube is
    // repainted as it now stands - which, the turn having been the right
    // one, changes nothing on screen.
    property int pendingStep: 0

    function next() {
        if (SolveController.isSolving)
            return;
        cube.finishPlaying(); // a turn still in flight lands first
        if (SolveController.atEnd)
            return;
        pendingStep = 1;
        cube.playMove(SolveController.currentMoveFace, SolveController.currentMoveTurns);
    }

    function previous() {
        if (SolveController.isSolving)
            return;
        cube.finishPlaying();
        if (SolveController.atStart)
            return;
        // Going back is the last move undone.
        var last = SolveController.moves[SolveController.moveIndex - 1];
        pendingStep = -1;
        cube.playMove(last.face, 4 - last.turns);
    }

    Connections {
        target: cube
        function onMovePlayed() {
            var step = root.pendingStep;
            root.pendingStep = 0;
            if (step > 0)
                SolveController.nextMove();
            else if (step < 0)
                SolveController.previousMove();
        }
    }

    // Hands are on the cube, not the mouse.
    focus: true
    Component.onCompleted: forceActiveFocus()
    Keys.onRightPressed: next()
    Keys.onSpacePressed: next()
    Keys.onReturnPressed: next()
    Keys.onLeftPressed: previous()

    // ---- header -------------------------------------------------------------
    Item {
        id: header
        width: parent.width
        height: 44

        // Every move is named for a side as held this way; without it the
        // moves mean nothing.
        Text {
            anchors.left: parent.left; anchors.leftMargin: Theme.space3
            anchors.right: progress.left; anchors.rightMargin: Theme.space3
            anchors.verticalCenter: parent.verticalCenter
            text: "Hold yellow on top, green facing you"
            elide: Text.ElideRight
            font.family: Theme.fontMono; font.pixelSize: 12; color: Theme.ink
        }
        Text {
            id: progress
            anchors.right: parent.right; anchors.rightMargin: Theme.space3
            anchors.verticalCenter: parent.verticalCenter
            text: AppState.elapsedLabel() + " · " + SolveController.moveIndex + "/" + SolveController.totalMoves
            font.family: Theme.fontMono; font.pixelSize: 13; color: Theme.ink
        }
        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 2; color: Theme.divider }
    }

    // ---- the cube -----------------------------------------------------------
    Item {
        id: stage
        anchors.top: header.bottom
        anchors.left: parent.left
        width: root.wide ? parent.width - panel.width : parent.width
        height: root.wide ? parent.height - header.height
                          : Math.max(230, Math.min(parent.width, (parent.height - header.height) * 0.55))

        Cube3DView {
            id: cube
            readonly property real side: Math.min(parent.width, parent.height)
            width: side; height: side
            anchors.centerIn: parent
            anchors.verticalCenterOffset: root.wide ? 10 : 14

            interactive: false
            autoRotate: false
            shaderStyle: SettingsState.shaderStyleIndex
            showStats: SettingsState.statsForNerds

            // Yellow on top, green towards the viewer, seen from above and
            // to the right: the top, the front and the right side in view.
            homeRotation: Qt.vector3d(0, 0, 0)
            viewPitch: 24
            viewYaw: -32

            faceletColors: root.painted(SolveController.stickers)
            guideFace: root.moveSide
            guideTurns: SolveController.currentMoveTurns
        }

        Column {
            anchors.top: parent.top; anchors.left: parent.left; anchors.margins: Theme.space4
            spacing: 4

            Text {
                text: "STAGE " + (SolveController.currentStageIndex + 1) + " OF " + root.stageCopy.length
                color: Theme.accent; font.family: Theme.fontHeading; font.pixelSize: 11; font.letterSpacing: 1.4
            }
            Text {
                text: SolveController.atEnd ? "Solved" : SolveController.currentMoveNotation
                font.family: Theme.fontMono; font.pixelSize: root.wide ? 30 : 22; font.letterSpacing: 1
                color: Theme.ink
            }
            // The move in words: which side, named by where it is and by the
            // colour of its centre, and how far.
            Row {
                visible: root.moveWords !== ""
                spacing: Theme.space2
                Rectangle {
                    width: 14; height: 14
                    anchors.verticalCenter: parent.verticalCenter
                    color: root.colorHex(root.moveColor)
                    border.width: 1; border.color: Theme.divider
                }
                Text {
                    text: root.moveWords
                    font.family: Theme.fontHeading; font.pixelSize: 14; font.weight: Font.DemiBold
                    color: Theme.ink
                }
            }
        }

        Rectangle {
            visible: root.wide
            anchors.right: parent.right
            width: 2; height: parent.height
            color: Theme.divider
        }
        Rectangle {
            visible: !root.wide
            anchors.bottom: parent.bottom
            width: parent.width; height: 2
            color: Theme.divider
        }
    }

    // ---- stages and stepping -------------------------------------------------
    Component {
        id: stepRow
        Rectangle {
            property int idx: 0
            property var copy: null
            readonly property var range: SolveController.stageRanges[idx]
            readonly property bool isCurrent: SolveController.currentStageIndex === idx
            // No range yet while the solve is still running.
            readonly property bool isDone: range !== undefined && SolveController.moveIndex >= range.endIndex
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
                        color: Theme.inkMuted(0.6)
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

    Item {
        id: panel
        x: root.wide ? stage.width : 0
        y: root.wide ? stage.y : stage.y + stage.height
        width: root.wide ? 320 : parent.width
        height: parent.height - y

        // Wide: every stage, the current one opened out.
        Column {
            visible: root.wide
            anchors.top: parent.top; anchors.bottom: buttons.top
            width: parent.width

            Text {
                width: parent.width
                padding: Theme.space3
                text: "STAGES"
                font.family: Theme.fontHeading; font.pixelSize: 11; font.letterSpacing: 1.4
                color: Theme.inkMuted(0.6)
            }
            Rectangle { width: parent.width; height: 2; color: Theme.divider }
            ListView {
                width: parent.width
                height: parent.height - 46
                clip: true
                model: root.stageCopy
                delegate: Loader {
                    sourceComponent: stepRow
                    onLoaded: { item.idx = index; item.copy = modelData; }
                }
            }
        }

        // Narrow: the current stage only.
        Column {
            visible: !root.wide
            anchors.top: parent.top; anchors.bottom: buttons.top
            anchors.left: parent.left; anchors.right: parent.right
            anchors.margins: Theme.space4
            spacing: Theme.space1

            Text {
                text: root.currentCopy ? root.currentCopy.title : ""
                font.family: Theme.fontHeading; font.weight: Font.ExtraBold; font.pixelSize: 20; color: Theme.ink
            }
            Text {
                text: root.currentCopy ? root.currentCopy.detail : ""
                font.family: Theme.fontHeading; font.pixelSize: 13
                color: Theme.inkMuted(0.6)
                width: parent.width; wrapMode: Text.WordWrap
            }
        }

        Item {
            id: buttons
            anchors.bottom: parent.bottom
            width: parent.width
            height: 42 + 2 * Theme.space3

            Rectangle { width: parent.width; height: 2; color: Theme.divider }
            AppButton {
                id: previousButton
                x: Theme.space3; y: Theme.space3 + 1
                height: 42
                variant: "secondary"; text: "Previous"
                enabled: !SolveController.atStart
                onClicked: root.previous()
            }
            AppButton {
                anchors.left: previousButton.right; anchors.leftMargin: Theme.space2
                anchors.right: parent.right; anchors.rightMargin: Theme.space3
                y: Theme.space3 + 1
                height: 42
                variant: "primary"; text: SolveController.atEnd ? "Solved!" : "Next move"
                enabled: !SolveController.atEnd
                onClicked: root.next()
            }
        }
    }

    // ---- solving overlay: the solve runs off the UI thread (see
    // solve_controller.cpp) but there's nothing sensible to show until it
    // finishes - block interaction with a spinner rather than flashing the
    // previous solve's stale move data.
    Rectangle {
        anchors.fill: parent
        visible: SolveController.isSolving
        color: Theme.paper

        MouseArea { anchors.fill: parent }

        Column {
            anchors.centerIn: parent
            spacing: Theme.space3

            Rectangle {
                id: spinner
                anchors.horizontalCenter: parent.horizontalCenter
                width: 40; height: 40
                radius: 20
                color: "transparent"
                border.width: 4
                border.color: Theme.accent100

                Rectangle {
                    width: 10; height: 10; radius: 5
                    color: Theme.accent
                    anchors.top: parent.top
                    anchors.horizontalCenter: parent.horizontalCenter
                }

                RotationAnimation on rotation {
                    running: SolveController.isSolving
                    from: 0; to: 360
                    duration: 900
                    loops: Animation.Infinite
                }
            }
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: "Solving…"
                font.family: Theme.fontHeading; font.pixelSize: 14
                color: Theme.ink
            }
        }
    }
}
