import QtQuick
import Cubeulator

// The solve screen: the cube as it stands, in 3D, with the layer the next
// move turns nudging the way it has to go.
//
// The 3D cube on screen is the cube in the hand: never mirrored, and turned the
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
    // The three stages the other two methods have (cube::solver::Stage::F2l,
    // Reduce and Finish, which follow the five above in value). Worded through
    // ids; the five above are the solve lane's, and are worded by them.
    readonly property var extraStageCopy: [
        { title: qsTrId("stage.f2l.title"), detail: qsTrId("stage.f2l.what") },
        { title: qsTrId("stage.reduce.title"), detail: qsTrId("stage.reduce.what") },
        { title: qsTrId("stage.finish.title"), detail: qsTrId("stage.finish.what") }
    ]
    // The stages a cube that is not a 3x3 has before it is one, which follow in value
    // (cube::nxn::StageId: parity 8, centres 9, edges 10, and a 2x2's corners 11).
    readonly property var reductionStageCopy: [
        { title: qsTrId("stage.parity.title"), detail: qsTrId("stage.parity.what") },
        { title: qsTrId("stage.centres.title"), detail: qsTrId("stage.centres.what") },
        { title: qsTrId("stage.edges.title"), detail: qsTrId("stage.edges.what") },
        { title: qsTrId("stage.corners.title"), detail: qsTrId("stage.corners.what") }
    ]
    // The words for a stage value (cube::nxn::StageId, which numbers the 3x3's
    // stages as cube::solver::Stage does). The methods have different stages, so a
    // stage's value says what it is and nothing about its position: that is
    // stagePosition below.
    function copyFor(stage) {
        if (stage >= stageCopy.length + extraStageCopy.length)
            return reductionStageCopy[stage - stageCopy.length - extraStageCopy.length];
        return stage < stageCopy.length ? stageCopy[stage] : extraStageCopy[stage - stageCopy.length];
    }
    readonly property var currentCopy: copyFor(SolveController.currentStageIndex)

    // The stages of this solution in the order they are done, as Stage values;
    // fixed for the length of a solution, so it is set when one lands and not
    // on every move (a model that changed each step would rebuild the list).
    property var stageOrder: []
    function updateStageOrder() {
        // A 3x3 is shown every stage its method has. A bigger cube's reduction has
        // stages that a given cube does not need (no parity to set up, say): those
        // are left out, so that what is listed is what there is to do.
        var every = SolveController.size === 3;
        stageOrder = SolveController.stageRanges
            .filter(function (range) { return every || range.endIndex > range.startIndex; })
            .map(function (range) { return range.stageIndex; });
    }
    // Where a stage is in this solution's moves, by its value; nothing before the
    // solve has landed.
    function rangeOf(stage) {
        var ranges = SolveController.stageRanges;
        for (var i = 0; i < ranges.length; i++) {
            if (ranges[i].stageIndex === stage)
                return ranges[i];
        }
        return undefined;
    }
    Connections {
        target: SolveController
        function onSolutionChanged() { root.updateStageOrder(); }
    }
    // Where the stage being worked on is among them, and how many there are.
    readonly property int stagePosition: Math.max(0, stageOrder.indexOf(SolveController.currentStageIndex))
    readonly property int stageCount: stageOrder.length > 0 ? stageOrder.length : stageCopy.length

    // A method picked here is the method from now on: it is kept (so the next
    // cube is solved the same way) and the cube in hand is solved on from where
    // it stands. SettingsState keeps it and tells the controller.
    function chooseMethod(method) {
        SettingsState.solveMethod = method;
    }

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

    // A side is named by the colour of its centre, which never moves. A cube with
    // no centre sticker (an even size) is named by what its corners say.
    function centreColor(side) {
        return SolveController.size === 3 ? SolveController.stickers[side][4]
                                          : SolveController.sideColours[side];
    }

    // How the cube is held, in words: every move is shown on the cube as
    // held this way. An instruction until the camera has seen the cube, a
    // statement of what it saw after.
    readonly property string holdWords: (HeldCube.seen ? "Holding " : "Hold ")
        + centreColor(HeldCube.up).toLowerCase() + " on top, "
        + centreColor(HeldCube.front).toLowerCase() + " facing you"

    // ---- the camera -----------------------------------------------------------
    // The solve screen keeps the camera to see how the cube is held. A side
    // held square to it is matched against the cube as it should stand, and
    // the cube on screen turns to match the one in the hand.
    readonly property color good: "#3FE05A"
    readonly property color sightColor: {
        switch (HeldCube.sight) {
        case "matched": return good;
        case "unsure": return "#FFB347";
        case "wrong":
        case "unknown": return Theme.accent;
        }
        return Theme.divider;
    }
    readonly property string cameraWords: {
        if (DirectScan.cameraError !== "")
            return "Camera unavailable";
        if (DirectScan.frameSeq === 0)
            return DirectScan.starting ? "Starting the camera…" : "";
        if (!DirectScan.complete)
            return ""; // no scan, so nothing to match a side against
        switch (HeldCube.sight) {
        case "matched":
            if (!guiding)
                return "Matched to your cube";
            // The side facing the holder is the one side the camera never
            // sees change; say so rather than leave them waiting.
            return movePlace === "front"
                ? "Matched. The camera cannot see the side facing you: press Next after this move."
                : "Matched. Make the move and it steps on by itself.";
        case "unsure": return "That side looks the same turned. Show another to tell which way up.";
        case "wrong": return "Not the move that was asked for";
        case "unknown": return "Not the side that was expected";
        }
        return HeldCube.seen ? "Show a side again if you turn the cube over"
                             : "Hold any side square to the camera and the cube here turns to match";
    }

    // What the camera makes of a cube that is not as the solution has it.
    // When one turn explains what it sees, the side that was turned is
    // named; otherwise all it can say is that something is off. Either way
    // the buttons still step, and the solution does not move by itself.
    readonly property string mistakeTitle: HeldCube.sight === "wrong" ? "That was not the move"
        : HeldCube.sight === "unknown" ? "That side is not expected" : ""
    readonly property string mistakeWords: {
        if (HeldCube.sight === "wrong") {
            var side = HeldCube.wrongSide;
            var name = "The " + centreColor(side).toLowerCase() + " side";
            if (side === moveSide)
                return name + " was turned, but not the way shown. Turn it back and try again.";
            return name + " (" + placeWords[HeldCube.places[side]].toLowerCase()
                + ") was turned instead. Turn it back, then make the move shown.";
        }
        if (HeldCube.sight === "unknown")
            return "The side facing the camera is not what the solution has there. If the last move went wrong and cannot be put right, rescan.";
        return "";
    }

    // The camera has seen the move made: take the step without being asked.
    // If a press got there first and the layer is already turning, that
    // turn is this move - a second step would be one too many.
    Connections {
        target: HeldCube
        function onMoveSeen() {
            moveSeenNote.restart();
            if (root.pendingStep === 0)
                root.next();
        }
    }

    readonly property string moveSide: guiding ? SolveController.currentMoveFace : ""
    readonly property string moveColor: moveSide !== "" ? centreColor(moveSide) : ""
    // Where that side is for the person holding the cube: "top", "bottom",
    // "left", "right", "front", "back", or "" when there is no move.
    readonly property string movePlace: moveSide !== "" ? HeldCube.places[moveSide] : ""
    readonly property var placeWords: ({
        top: "Top", bottom: "Bottom", left: "Left side", right: "Right side", front: "Front", back: "Back"
    })
    readonly property string turnWords: SolveController.currentMoveTurns === 2 ? "half turn" : "quarter turn"
    readonly property bool outerMove: SolveController.currentMoveFirst === 0
                                      && SolveController.currentMoveLast === 0
    readonly property string moveWords: {
        if (moveSide === "")
            return "";
        if (outerMove)
            return placeWords[movePlace] + " · " + turnWords;
        // A layer inside the cube is counted from the side the move is named for.
        if (SolveController.currentMoveFirst === SolveController.currentMoveLast)
            return qsTrId("solve.move.layer").arg(placeWords[movePlace])
                .arg(SolveController.currentMoveFirst + 1).arg(turnWords);
        return qsTrId("solve.move.layers").arg(placeWords[movePlace])
            .arg(SolveController.currentMoveFirst + 1).arg(SolveController.currentMoveLast + 1).arg(turnWords);
    }

    // ---- where the cube is looked at from ------------------------------------
    // Normally from above and to the right, which shows the top, the front
    // and the right side - and two of the three rows of stickers round the
    // left and bottom layers, enough to see those turn. The back layer is
    // the one that turns out of sight, so a move on the back swings the
    // view round behind the cube, and it swings home afterwards. It is the
    // view that turns: the cube in the hand stays as it is, and the screen
    // says so while it is turned.
    readonly property var views: ({
        home: { pitch: 24, yaw: -32, words: "" },
        back: { pitch: 24, yaw: -140, words: "Seen from behind" }
    })
    readonly property var view: views[movePlace] || views.home
    readonly property bool viewTurned: view.words !== ""
    // The note's words outlast the turn, so it does not empty as it fades.
    property string viewNoteWords: view.words
    onViewChanged: if (viewTurned) viewNoteWords = view.words

    // ---- stepping ----------------------------------------------------------
    // A step is shown before it is taken: the layer turns all the way on
    // screen, and when it lands the solution moves on and the cube is
    // repainted as it now stands - which, the turn having been the right
    // one, changes nothing on screen.
    property int pendingStep: 0

    // The cube on screen: the 3x3's own view, or the one for any other size.
    readonly property bool threeByThree: SolveController.size === 3
    readonly property Item cube: cubeLoader.item

    // Turns the layers of a move on the cube on screen: all the way, then the
    // cube says so (movePlayed).
    function playOnCube(face, first, last, turns) {
        if (threeByThree)
            cube.playMove(face, turns);
        else
            cube.playMove(face, first, last, turns);
    }

    function next() {
        if (SolveController.isSolving || cube === null)
            return;
        cube.finishPlaying(); // a turn still in flight lands first
        if (SolveController.atEnd)
            return;
        pendingStep = 1;
        playOnCube(SolveController.currentMoveFace, SolveController.currentMoveFirst,
                   SolveController.currentMoveLast, SolveController.currentMoveTurns);
    }

    function previous() {
        if (SolveController.isSolving || cube === null)
            return;
        cube.finishPlaying();
        if (SolveController.atStart)
            return;
        // Going back is the last move undone.
        var last = SolveController.moves[SolveController.moveIndex - 1];
        pendingStep = -1;
        playOnCube(last.face, last.first, last.last, 4 - last.turns);
    }

    Connections {
        target: root.cube
        function onMovePlayed() {
            var step = root.pendingStep;
            root.pendingStep = 0;
            if (step > 0)
                SolveController.nextMove();
            else if (step < 0)
                SolveController.previousMove();
        }
    }

    // The cube is solved: on to the screen that says so. Only the button does
    // this - a key held down to step quickly must not run off the end of the
    // solution and out of the screen.
    function finish() {
        if (SolveController.atEnd && !SolveController.isSolving)
            AppState.screen = "complete";
    }

    // Hands are on the cube, not the mouse.
    focus: true
    Component.onCompleted: {
        forceActiveFocus();
        updateStageOrder();
    }
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
            anchors.right: progress.left
            // Wide, the way-out button sits between this and the count.
            anchors.rightMargin: Theme.space3 + (root.wide ? leave.width + Theme.space3 : 0)
            anchors.verticalCenter: parent.verticalCenter
            text: root.holdWords
            elide: Text.ElideRight
            font.family: Theme.fontMono; font.pixelSize: 12; color: Theme.ink
        }
        Text {
            id: progress
            anchors.right: parent.right; anchors.rightMargin: Theme.space3
            anchors.verticalCenter: parent.verticalCenter
            text: (root.wide ? AppState.elapsedLabel() + " · " : "")
                + SolveController.moveIndex + "/" + SolveController.totalMoves
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
        // Narrow, what the panel under the stage needs (the method row, the
        // stage's name and what it asks, the way out and the stepping
        // buttons) is kept, and the cube is given the rest - up to the most
        // it can use, which is its own width and what sits above and below.
        // On a short window that is what the stage always had (55%).
        readonly property real panelNeed: 240
        height: root.wide ? parent.height - header.height
                          : Math.max(230, Math.min(Math.max((parent.height - header.height) * 0.55,
                                                            parent.height - header.height - panelNeed),
                                                   cubeTop + width + corner + 44))

        // ---- the corners, and so the room the cube has -------------------
        // The move label is top left with the camera's words under it, and
        // the camera's preview top right. The cube is a square that has to
        // be clear of all of them, whatever the move or the words say, so
        // what they take is reserved rather than read off the text: a cube
        // that resized with every move would not be a cube anyone could
        // follow. (The cube's box has air round the cube itself, which it
        // needs to turn a layer in; a box that is clear is a cube that is.)
        readonly property real corner: Theme.space4
        // The camera reads a 3x3; a bigger cube has none to show yet.
        readonly property bool cameraOn: AppState.solveCamera && root.threeByThree
        readonly property real insetWidth: root.wide ? 184 : 124
        readonly property real insetHeight: Math.round(insetWidth * 3 / 4)
        // The label's column, and the words wrap in it. Room is kept for
        // the words at three lines, the most there are of them at this
        // width in English.
        readonly property real labelMax: 220
        readonly property real labelWidth: Math.max(0, Math.min(labelMax,
            width - (cameraOn ? insetWidth : 0) - 3 * corner))
        readonly property real wordsLineHeight: 14
        readonly property real wordsRoom: 3 * wordsLineHeight
        readonly property real cornersBottom: corner + Math.max(
            cameraOn ? insetHeight : 0,
            moveLabel.implicitHeight + (cameraOn ? Theme.space1 + wordsRoom : 0))
        // Under the corners, full width...
        readonly property real cubeTop: cornersBottom + Theme.space1
        // ...or, where the stage is wide enough for it, between them, which
        // is where the cube was before it was asked to keep clear of them.
        readonly property real sideRoom: corner + Math.max(labelMax, cameraOn ? insetWidth : 0) + Theme.space2
        // Under the cube: wide, the hint in the bottom corner; narrow, the
        // note about a turned view, which gives way when the stage is so
        // short that the cube would be left under 150 for it.
        readonly property real footRoom: root.wide
            ? 2 * corner
            : corner + Math.max(0, Math.min(44, height - cubeTop - corner - 150))
        readonly property real underSide: Math.min(width, height - cubeTop - footRoom)
        readonly property real betweenSide: Math.min(width - 2 * sideRoom, height - corner - footRoom)

        // The cube: the 3x3's own view, or the one that draws any other size.
        Loader {
            id: cubeLoader
            readonly property bool between: stage.betweenSide > stage.underSide
            readonly property real side: Math.max(0, between ? stage.betweenSide : stage.underSide)
            readonly property real boxTop: between ? stage.corner : stage.cubeTop
            width: side; height: side
            anchors.horizontalCenter: parent.horizontalCenter
            y: boxTop + (parent.height - stage.footRoom - boxTop - side) / 2
            sourceComponent: root.threeByThree ? cube3Component : cubeNComponent
        }

        Component {
            id: cube3Component
            Cube3DView {
                // Drag it to look round the back; let go and it comes home. The
                // layer about to move keeps nudging all the while.
                interactive: true
                dragTurnsView: true
                autoRotate: false
                HoverHandler { cursorShape: dragging ? Qt.ClosedHandCursor : Qt.OpenHandCursor }
                // The colours here are matched against a real cube.
                evenLight: true
                shaderStyle: SettingsState.shaderStyleIndex
                showStats: SettingsState.statsForNerds

                objectName: "solve.cube"

                // Turned the way the cube is held, and seen from wherever shows
                // the side about to move.
                homeRotation: Qt.vector3d(0, 0, 0)
                holdRotation: HeldCube.rotation
                viewPitch: root.view.pitch
                viewYaw: root.view.yaw

                faceletColors: root.painted(SolveController.stickers)
                guideFace: root.moveSide
                guideTurns: SolveController.currentMoveTurns
            }
        }

        Component {
            id: cubeNComponent
            CubeNView {
                interactive: true
                dragTurnsView: true
                autoRotate: false
                HoverHandler { cursorShape: dragging ? Qt.ClosedHandCursor : Qt.OpenHandCursor }
                evenLight: true
                showStats: SettingsState.statsForNerds

                objectName: "solve.cube"

                // The cube is shown as it is, not as a camera would see it held.
                homeRotation: Qt.vector3d(0, 0, 0)
                viewPitch: root.view.pitch
                viewYaw: root.view.yaw

                faceletColors: root.painted(SolveController.stickers)
                guideFace: root.moveSide
                guideFirst: SolveController.currentMoveFirst
                guideLast: SolveController.currentMoveLast
                guideTurns: SolveController.currentMoveTurns
            }
        }

        Column {
            id: moveLabel
            objectName: "solve.move"
            anchors.top: parent.top; anchors.left: parent.left; anchors.margins: Theme.space4
            width: stage.labelWidth
            spacing: 4

            Text {
                text: "STAGE " + (root.stagePosition + 1) + " OF " + root.stageCount
                color: Theme.accent; font.family: Theme.fontHeading; font.pixelSize: 11; font.letterSpacing: 1.4
            }
            Text {
                text: SolveController.atEnd ? "Solved" : SolveController.currentMoveNotation
                font.family: Theme.fontMono; font.pixelSize: root.wide ? 30 : 22; font.letterSpacing: 1
                color: Theme.ink
            }
            // The move in words: which side, named by where it is and by the
            // colour of its centre, and how far. It keeps its place when
            // there is no move to say (at the end), so the cube does not
            // move for it; and it shrinks to its column rather than run
            // under the preview ("Right side" is the longest name).
            //
            // A cube bigger than 3x3 names layers too ("Left side · layer 2 ·
            // half turn"), which is more than one line holds in this column; the
            // room for a second is kept whatever the move says, for the reason
            // above, and the words are made to fit the two lines (or the one) as
            // they are given.
            Row {
                opacity: root.moveWords !== "" ? 1 : 0
                height: root.threeByThree ? 20 : 36
                spacing: Theme.space2
                Rectangle {
                    y: 3
                    width: 14; height: 14
                    color: root.colorHex(root.moveColor)
                    border.width: 1; border.color: Theme.divider
                }
                Text {
                    width: Math.max(0, stage.labelWidth - 14 - Theme.space2)
                    height: parent.height
                    topPadding: 1.5
                    text: root.moveWords
                    wrapMode: Text.Wrap
                    elide: Text.ElideRight
                    fontSizeMode: Text.Fit; minimumPixelSize: 10
                    font.family: Theme.fontHeading; font.pixelSize: 14; font.weight: Font.DemiBold
                    color: Theme.ink
                }
            }
        }

        // What the camera makes of what it sees, in words: under the move
        // label, where the preview does not reach and the cube is kept
        // clear of. (It was under the preview, a column 124 wide on a phone,
        // and ran across the cube.) At most three lines; room is kept for
        // all three whatever it says.
        Text {
            objectName: "solve.words"
            anchors.top: moveLabel.bottom; anchors.topMargin: Theme.space1
            anchors.left: moveLabel.left
            width: stage.labelWidth
            visible: text !== "" && stage.cameraOn
            text: root.cameraWords
            wrapMode: Text.WordWrap
            maximumLineCount: 3
            elide: Text.ElideRight
            lineHeight: stage.wordsLineHeight; lineHeightMode: Text.FixedHeight
            font.family: Theme.fontHeading; font.pixelSize: 11
            color: HeldCube.sight === "unknown" || HeldCube.sight === "wrong"
                ? Theme.accent : Theme.inkMuted(0.75)
        }

        // That the cube can be picked up and looked round is not obvious
        // from a cube that is busy showing a move.
        Text {
            visible: root.wide
            anchors.right: parent.right; anchors.bottom: parent.bottom; anchors.margins: Theme.space4
            text: "Drag the cube to look round it"
            font.family: Theme.fontHeading; font.pixelSize: 11
            color: Theme.inkMuted(0.5)
        }

        // What the camera sees of the cube, with a dot on each sticker of the
        // side it has found. The picture is shown the way a mirror shows it
        // (CameraController.previewMirrored), as on the scan screen, so the
        // cube moves on it the way the hand moves it. That is only how it is
        // painted: the frame that is read, and so what the camera makes of
        // the cube, is the camera's own, because a mirror image of a side
        // never matches the side.
        //
        // The preview is small, so it does not show the whole picture. It
        // closes in on the cube and follows it about the frame, so that what
        // is being shown to the camera can actually be seen. The camera does
        // not move - this is only which part of its picture is on show - and
        // small movements are not chased, so the view sits still while the
        // cube is roughly where it was.
        Item {
            id: inset
            visible: stage.cameraOn
            anchors.top: parent.top; anchors.right: parent.right
            anchors.margins: stage.corner
            width: stage.insetWidth
            height: preview.height

            readonly property var points: DirectScan.stickerPoints
            readonly property bool found: points.length === 9
            readonly property real frameW: Math.max(1, DirectScan.frameWidth)
            readonly property real frameH: Math.max(1, DirectScan.frameHeight)
            // The window on the picture, inside the border.
            readonly property real viewW: preview.width - 4
            readonly property real viewH: preview.height - 4
            // Screen pixels per frame pixel with the whole picture in view
            // (filling the window, as the scan screen fills the screen)...
            readonly property real wholeScale: Math.max(viewW / frameW, viewH / frameH)
            // ...and how much of the window's shorter side the cube is given
            // once it has been found.
            readonly property real cubeShare: 0.6

            // Where the view is heading: the frame pixel at its centre, and
            // its scale. Changed only when the cube has moved or changed
            // size by enough to matter.
            property real aimX: frameW / 2
            property real aimY: frameH / 2
            property real aimScale: wholeScale
            // How long without a side in view before the view lets go of
            // where the cube was and opens out to the whole picture.
            property int lostAfter: 2500
            // Whether the picture is shown the way a mirror shows it: the
            // camera faces the user, so it is, unless it is a rear one. A
            // property, not just a read of the controller, so the inset can
            // be seen both ways without a camera of each kind.
            property bool mirrored: CameraController.previewMirrored

            function dist(a, b) { return Math.hypot(a.x - b.x, a.y - b.y); }
            function follow() {
                // Asked of the points themselves: `found` may not have caught
                // up with them yet when this runs.
                if (points.length !== 9) {
                    if (!lost.running)
                        lost.start();
                    return;
                }
                lost.stop();
                // Corner stickers are two spacings apart; the side is three.
                var side = 1.5 * Math.max(dist(points[0], points[2]), dist(points[6], points[8]),
                                          dist(points[0], points[6]), dist(points[2], points[8]));
                if (side <= 0)
                    return;
                var wanted = Math.max(wholeScale,
                                      Math.min(4 * wholeScale, cubeShare * Math.min(viewW, viewH) / side));
                var slack = 0.1 * Math.min(viewW, viewH) / aimScale; // in frame pixels
                if (Math.abs(points[4].x - aimX) > slack || Math.abs(points[4].y - aimY) > slack
                        || Math.abs(wanted / aimScale - 1) > 0.15) {
                    aimX = points[4].x;
                    aimY = points[4].y;
                    aimScale = wanted;
                }
            }
            onPointsChanged: follow()
            onWholeScaleChanged: if (points.length !== 9) aimScale = wholeScale
            Timer {
                id: lost
                interval: inset.lostAfter
                onTriggered: {
                    inset.aimX = inset.frameW / 2;
                    inset.aimY = inset.frameH / 2;
                    inset.aimScale = inset.wholeScale;
                }
            }

            // What is on show now: eased towards the aim, and never past the
            // edge of the picture.
            property real shownScale: aimScale
            property real shownX: Math.max(viewW / (2 * shownScale),
                                           Math.min(frameW - viewW / (2 * shownScale), aimX))
            property real shownY: Math.max(viewH / (2 * shownScale),
                                           Math.min(frameH - viewH / (2 * shownScale), aimY))
            Behavior on shownScale { SmoothedAnimation { duration: 350; velocity: -1 } }
            Behavior on shownX { SmoothedAnimation { duration: 350; velocity: -1 } }
            Behavior on shownY { SmoothedAnimation { duration: 350; velocity: -1 } }

            Rectangle {
                id: preview
                objectName: "solve.camera"
                // What the tests read: where a frame pixel lands in here.
                readonly property real pictureX: previewImage.x
                readonly property real pictureY: previewImage.y
                readonly property real pictureScale: inset.shownScale
                readonly property real wholeScale: inset.wholeScale
                property alias lostAfter: inset.lostAfter
                property alias mirrored: inset.mirrored

                anchors.right: parent.right
                width: parent.width
                // Squarer than the camera's picture: it is the cube that
                // has to fit, not the room.
                height: Math.round(width * 3 / 4)
                color: "#141312"
                clip: true

                // The picture and the dots on it are one picture and flip
                // together, about the middle of the window, so a dot stays on
                // its sticker. Everything above works in the camera's own
                // frame pixels and is flipped as a whole afterwards; the flash,
                // the frame and the words are not part of it.
                Item {
                    id: picture
                    objectName: "solve.picture"
                    anchors.fill: parent
                    transform: Scale {
                        origin.x: picture.width / 2
                        xScale: inset.mirrored ? -1 : 1
                    }

                    Image {
                        id: previewImage
                        objectName: "solve.image"
                        x: 2 + inset.viewW / 2 - inset.shownX * inset.shownScale
                        y: 2 + inset.viewH / 2 - inset.shownY * inset.shownScale
                        width: inset.frameW * inset.shownScale
                        height: inset.frameH * inset.shownScale
                        fillMode: Image.Stretch
                        cache: false
                        // Nothing to show until the camera has delivered a frame.
                        source: DirectScan.frameSeq > 0 ? "image://scanFrame/latest#" + DirectScan.frameSeq : ""
                    }
                    Repeater {
                        model: 9
                        delegate: Rectangle {
                            objectName: "solve.dot"
                            readonly property bool there: inset.points.length === 9
                            readonly property var point: there ? inset.points[index] : Qt.point(0, 0)
                            visible: there && DirectScan.frameWidth > 0
                            width: 7; height: 7; radius: 3.5
                            x: previewImage.x + point.x * inset.shownScale - width / 2
                            y: previewImage.y + point.y * inset.shownScale - height / 2
                            color: HeldCube.sight === "matched" ? root.good : "#FFD21F"
                            border.width: 1; border.color: "#141312"
                        }
                    }
                }
                // The camera saw the move made.
                Rectangle {
                    id: moveSeenFlash
                    objectName: "solve.flash"
                    anchors.fill: parent
                    color: root.good
                    opacity: 0
                    SequentialAnimation {
                        id: moveSeenNote
                        NumberAnimation { target: moveSeenFlash; property: "opacity"; to: 0.55; duration: 90 }
                        NumberAnimation { target: moveSeenFlash; property: "opacity"; to: 0; duration: 420 }
                    }
                }
                // The frame, drawn over the picture, in the colour of what
                // the camera makes of the cube.
                Rectangle {
                    objectName: "solve.frame"
                    anchors.fill: parent
                    color: "transparent"
                    border.width: 2
                    border.color: root.sightColor
                    Behavior on border.color { ColorAnimation { duration: 180 } }
                }
            }
        }

        // The view has turned to show a side that faces away. Said plainly,
        // because a cube turning on screen looks like an instruction to turn
        // the one in the hand.
        Column {
            id: notes
            anchors.left: parent.left; anchors.bottom: parent.bottom; anchors.margins: Theme.space4
            width: parent.width - 2 * Theme.space4
            spacing: Theme.space2

        // The cube is not as the solution has it. Says what the camera
        // made of it, and offers the way back.
        Rectangle {
            id: mistakeNote
            visible: root.mistakeTitle !== ""
            width: Math.min(380, parent.width)
            height: mistakeNoteText.implicitHeight + 2 * Theme.space2
            color: Theme.accent100
            border.width: 1; border.color: Theme.accent
            Rectangle { width: 4; height: parent.height; color: Theme.accent }
            Column {
                id: mistakeNoteText
                x: Theme.space3 + 4; y: Theme.space2
                width: parent.width - 2 * Theme.space3 - 4
                spacing: 2
                Text {
                    text: root.mistakeTitle
                    font.family: Theme.fontHeading; font.pixelSize: 14; font.weight: Font.ExtraBold
                    color: Theme.accent
                }
                Text {
                    width: parent.width
                    text: root.mistakeWords
                    wrapMode: Text.WordWrap
                    font.family: Theme.fontHeading; font.pixelSize: 12
                    color: Theme.ink
                }
                AppButton {
                    objectName: "solve.rescanFromNote"
                    height: 30
                    variant: "ghost"; text: "Rescan the cube and carry on from there"
                    onClicked: AppState.rescan()
                }
            }
        }

        Rectangle {
            id: viewNote
            width: Math.min(viewNoteText.implicitWidth + 2 * Theme.space3, parent.width)
            height: viewNoteText.implicitHeight + 2 * Theme.space2
            color: Theme.accent100
            border.width: 1; border.color: Theme.accent
            opacity: root.viewTurned ? 1 : 0
            visible: opacity > 0
            Behavior on opacity { NumberAnimation { duration: 180 } }

            Column {
                id: viewNoteText
                x: Theme.space3; y: Theme.space2
                spacing: 2
                Text {
                    text: root.viewNoteWords
                    font.family: Theme.fontHeading; font.pixelSize: 14; font.weight: Font.ExtraBold
                    color: Theme.ink
                }
                Text {
                    text: "Only the view has turned. Keep your cube as it is."
                    font.family: Theme.fontHeading; font.pixelSize: 12
                    color: Theme.inkMuted(0.75)
                }
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
            property int stage: 0
            property var copy: null
            readonly property var range: root.rangeOf(stage)
            readonly property bool isCurrent: range !== undefined
                && SolveController.currentStageIndex === range.stageIndex
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

        // How the cube is being solved. The same row on both layouts; wide it
        // also says what the chosen method is, narrow there is no room. Choosing
        // a different one solves on from the cube as it stands.
        Item {
            id: methodBox
            anchors.top: parent.top
            width: parent.width
            height: methodPicker.height + 2 * Theme.space3

            SolveMethodPicker {
                id: methodPicker
                objectName: "solve.method"
                x: Theme.space3; y: Theme.space3
                width: parent.width - 2 * Theme.space3
                showNote: root.wide
                // A big cube is reduced to a 3x3 first; the methods are for that 3x3.
                core: SolveController.size >= 4
                current: SolveController.method
                choosable: !SolveController.isSolving
                onSelected: (method) => root.chooseMethod(method)
            }
            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 2; color: Theme.divider }
        }

        // A person who can reduce a big cube themselves may jump over the reduction:
        // the cube on screen goes to the 3x3 it should have become, and the
        // steps carry on from there.
        Item {
            id: skipBox
            objectName: "solve.skip"
            anchors.bottom: buttons.top
            width: parent.width
            visible: SolveController.canSkipToCore && !SolveController.isSolving
            height: visible ? skipColumn.implicitHeight + 2 * Theme.space3 : 0

            Rectangle { width: parent.width; height: 2; color: Theme.divider }
            Column {
                id: skipColumn
                x: Theme.space3; y: Theme.space3
                width: parent.width - 2 * Theme.space3
                spacing: Theme.space1
                AppButton {
                    objectName: "solve.skip.button"
                    height: 34
                    variant: "secondary"; text: qsTrId("solve.skipToCore")
                    onClicked: SolveController.skipToCore()
                }
                Text {
                    visible: root.wide
                    width: parent.width
                    text: qsTrId("solve.skipToCore.note")
                    wrapMode: Text.WordWrap
                    font.family: Theme.fontHeading; font.pixelSize: 12
                    color: Theme.inkMuted(0.6)
                }
            }
        }

        // Wide: every stage, the current one opened out.
        Column {
            visible: root.wide
            anchors.top: methodBox.bottom; anchors.bottom: skipBox.top
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
                model: root.stageOrder
                delegate: Loader {
                    sourceComponent: stepRow
                    onLoaded: { item.idx = index; item.stage = modelData; item.copy = root.copyFor(modelData); }
                }
            }
        }

        // Narrow: the current stage only.
        Column {
            visible: !root.wide
            anchors.top: methodBox.bottom; anchors.bottom: skipBox.top
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
                objectName: "solve.previous"
                // Anchored, and not placed at an x: an x does not mirror, and the
                // button beside it, which is anchored to this one, would be
                // mirrored away from it.
                anchors.left: parent.left; anchors.leftMargin: Theme.space3
                y: Theme.space3 + 1
                height: 42
                variant: "secondary"; text: "Previous"
                enabled: !SolveController.atStart
                onClicked: root.previous()
            }
            // The same button steps and, once there is nothing left to step,
            // leaves for the screen that says so.
            AppButton {
                objectName: "solve.next"
                anchors.left: previousButton.right; anchors.leftMargin: Theme.space2
                anchors.right: parent.right; anchors.rightMargin: Theme.space3
                y: Theme.space3 + 1
                height: 42
                variant: "primary"; text: SolveController.atEnd ? qsTrId("solve.finish") : qsTrId("solve.next")
                enabled: !SolveController.isSolving
                onClicked: SolveController.atEnd ? root.finish() : root.next()
            }
        }
    }

    // The way back when the cube and the solution have parted company: read
    // the cube again as it is now and solve it from there. Whatever is
    // already done stays done - the new solution starts from the cube as it
    // stands. In the header when there is room for it beside the hold line;
    // narrow, the hold line needs the whole header, and this sits above the
    // stepping buttons.
    AppButton {
        id: leave
        objectName: "solve.rescan"
        // Only a 3x3 is read by the camera; a bigger cube is entered, not scanned.
        visible: root.threeByThree
        x: root.wide ? progress.x - width - Theme.space3 : root.width - width - Theme.space3
        y: root.wide ? (header.height - height) / 2 : root.height - buttons.height - height - Theme.space1
        height: 30
        variant: "ghost"; text: "Out of step? Rescan"
        onClicked: AppState.rescan()
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
