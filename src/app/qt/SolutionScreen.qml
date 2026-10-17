import QtQuick
import Cubeulator

// The solution, every turn of it, grouped by stage, with the cube beside it
// at the turn that is selected (docs/design/screens/solution.md). Hands are
// free here, so it is where someone learning the method reads what each stage
// did and steps through it at their own speed.
//
// The cube is the solve screen's, driven by a position of its own: stepping
// here does not move the solve. It shows the cube *before* the selected turn,
// as it would be held at the start of the solve (yellow on top, green facing),
// which is the hold every turn's words are written for.
//
// What it reviews is `source`: SolveController, for the solve just finished.
// Anything with the same shape can be put there - the solve history's entry
// for a solve from another day - which is what AppState.openSolution() is for.
// The shape: `moves` (the list of { face, turns, notation, first, last }:
// the layers a move turns, counted from its side, 0 for the outer one),
// `stageRanges` (the list of { stageIndex, startIndex, endIndex }), `method` (a
// SolveController.Method), the invokable `stickersAt(turn)` (the cube after
// that many turns, as SolveController.stickers), and a `solutionChanged`
// signal for when the moves are replaced. A source of a cube that is not a 3x3
// also says its `size` and `sideColours` (a side's letter to the colour it is
// named by); one that says neither is a 3x3.
Rectangle {
    id: root
    objectName: "solution"
    color: Theme.paper

    property var source: SolveController
    // The cube reviewed: a 3x3 with the 3x3's own view, or any other size with
    // the one that draws them (CubeNView).
    readonly property int cubeSize: source.size !== undefined ? source.size : 3
    readonly property bool threeByThree: cubeSize === 3
    readonly property Item cube: cubeLoader.item
    // A second line under the title: the date of the solve, when it was opened
    // from the history.
    property string subtitle: ""

    // Two things the app has no setting for yet: Settings > Appearance >
    // Motion (Reduced turns each layer in no time and plays more slowly) and
    // Settings > Solving > Show cube notation (each stage's turns as letters,
    // the way cubers read an algorithm). The settings screens' rebuild
    // (docs/design/screens/settings.md) binds these two.
    property bool reducedMotion: false
    property bool showNotation: false

    // ---- layout --------------------------------------------------------------
    // Compact: one column, the list under the buttons. Expanded and short
    // (under 480 high, at any width): two panes, the list in the second.
    readonly property bool wide: width >= Theme.wideBreakpoint
    readonly property bool low: height < 480
    readonly property bool twoPane: wide || low
    // Whether the layout is mirrored (right to left). The cube, the swatches
    // and the arrows never are; everything round them is.
    readonly property bool mirrored: LayoutMirroring.enabled

    // ---- what is reviewed ------------------------------------------------------
    // Rebuilt when a solution lands. A step on the solve screen changes
    // neither list, and the controller's stageRanges is a new list on every one.
    property var turnList: []
    property int total: 0
    property int stageCount: 0
    // The list's rows in order: a stage, then its turns, then the next stage.
    property var rows: []
    // Where each turn is in `rows`.
    property var rowOfTurn: []
    // Side letter -> the colour of its centre, which never moves.
    property var centres: ({})
    // Counts the solutions that have landed, so that what is painted from
    // stickersAt() is painted again when one has.
    property int revision: 0

    // Theme's sticker palette is keyed by colour, under names left over from a
    // white-on-top scheme: faceU is white, faceD yellow, faceL orange, faceR
    // red. SolveScreen.qml keeps the same table.
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

    // stickersAt() with each colour name swapped for its colour: what the 3D
    // cube is painted from. A new object each time (see Cube3DView).
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

    // The cube at the position shown, painted; again when a solution lands.
    readonly property var paintedNow: {
        revision;
        return painted(source.stickersAt(position));
    }

    // Seen from above and to the right, and from behind for a turn of the back
    // side, which turns out of sight otherwise.
    readonly property real viewYaw: position < total && turnList[position]
                                    && turnList[position].face === "B" ? -140 : -32

    // The layers a move turns, counted from its side: the outer one unless the
    // source says otherwise.
    function firstLayer(move) { return move.first !== undefined ? move.first : 0; }
    function lastLayer(move) { return move.last !== undefined ? move.last : 0; }

    function rebuild() {
        var list = source.moves;
        // A 3x3 is listed every stage its method has. A bigger cube's reduction has
        // stages that a given cube does not need (no parity to set up, say):
        // those are left out, so that what is listed is what there is to do.
        var every = threeByThree;
        var ranges = source.stageRanges.filter(function (range) {
            return every || range.endIndex > range.startIndex;
        });
        var items = [];
        var places = [];
        for (var s = 0; s < ranges.length; ++s) {
            var range = ranges[s];
            items.push({ kind: "stage", position: s + 1, stage: range.stageIndex,
                         start: range.startIndex, end: range.endIndex });
            for (var i = range.startIndex; i < range.endIndex; ++i) {
                places[i] = items.length;
                items.push({ kind: "turn", index: i, number: i + 1,
                             face: list[i].face, turns: list[i].turns,
                             first: firstLayer(list[i]), last: lastLayer(list[i]) });
            }
        }
        // A side is named by the colour of its centre, which never moves; a cube
        // with no centre sticker (an even size) is named by what its corners say.
        var found = {};
        if (threeByThree) {
            var start = source.stickersAt(0);
            for (var side in start)
                found[side] = colorHex(start[side][4]);
        } else {
            var named = source.sideColours;
            for (var letter in named)
                found[letter] = colorHex(named[letter]);
        }

        turnList = list;
        total = list.length;
        stageCount = ranges.length;
        rows = items;
        rowOfTurn = places;
        centres = found;
        playing = false;
        pendingStep = 0;
        position = 0;
        revision += 1;
    }

    Connections {
        target: root.source
        function onSolutionChanged() { root.rebuild(); }
    }

    // ---- the words ------------------------------------------------------------
    // The five stages of the layer-by-layer method have a title, what it does
    // and why; the others (the Stage values after them: the intermediate
    // method's first two layers, the advanced method's two) have the first
    // two. A function over literal ids, so that the extractor sees each one.
    function stageWords(stage) {
        switch (stage) {
        case 0: return { title: qsTrId("stage.1.title"), what: qsTrId("stage.1.what"), why: qsTrId("stage.1.why") };
        case 1: return { title: qsTrId("stage.2.title"), what: qsTrId("stage.2.what"), why: qsTrId("stage.2.why") };
        case 2: return { title: qsTrId("stage.3.title"), what: qsTrId("stage.3.what"), why: qsTrId("stage.3.why") };
        case 3: return { title: qsTrId("stage.4.title"), what: qsTrId("stage.4.what"), why: qsTrId("stage.4.why") };
        case 4: return { title: qsTrId("stage.5.title"), what: qsTrId("stage.5.what"), why: qsTrId("stage.5.why") };
        case 5: return { title: qsTrId("stage.f2l.title"), what: qsTrId("stage.f2l.what"), why: "" };
        case 6: return { title: qsTrId("stage.reduce.title"), what: qsTrId("stage.reduce.what"), why: "" };
        case 7: return { title: qsTrId("stage.finish.title"), what: qsTrId("stage.finish.what"), why: "" };
        // The stages a cube bigger than a 3x3 has before it is one (cube::nxn::StageId).
        case 8: return { title: qsTrId("stage.parity.title"), what: qsTrId("stage.parity.what"), why: "" };
        case 9: return { title: qsTrId("stage.centres.title"), what: qsTrId("stage.centres.what"), why: "" };
        case 10: return { title: qsTrId("stage.edges.title"), what: qsTrId("stage.edges.what"), why: "" };
        case 11: return { title: qsTrId("stage.corners.title"), what: qsTrId("stage.corners.what"), why: "" };
        }
        return { title: "", what: "", why: "" };
    }

    // Which layers of a side a turn is, when it is not just the side's outer
    // one: "layer 2", "layers 1 to 2" (counted from the side, from 1).
    function layerWords(first, last) {
        if (!(first > 0 || last > 0))
            return "";
        if (first === last)
            return qsTrId("solution.layer").arg(number(first + 1));
        return qsTrId("solution.layers").arg(number(first + 1)).arg(number(last + 1));
    }

    // A turn as the parts of a row: side, layers (only for a layer inside the
    // cube), direction, amount.
    function turnLine(info) {
        return [sideWords(info.face), layerWords(info.first, info.last),
                directionWords(directionOf(info.face, info.turns)), amountWords(info.turns)]
            .filter(function (part) { return part !== ""; }).join(" · ");
    }

    // The whole turn as one sentence, for a screen reader: the sentence written
    // for the side's turn when it is the side that turns, else its parts.
    function turnSpeech(info) {
        if (!(info.first > 0 || info.last > 0))
            return turnSentence(info.face, info.turns);
        return [sideWords(info.face), layerWords(info.first, info.last),
                directionWords(directionOf(info.face, info.turns)), amountWords(info.turns)].join(", ");
    }

    // A move names a side by its letter; as the cube is held at the start of
    // the solve the sides are where their letters say.
    function sideWords(face) {
        switch (face) {
        case "U": return qsTrId("side.top");
        case "D": return qsTrId("side.bottom");
        case "L": return qsTrId("side.left");
        case "R": return qsTrId("side.right");
        case "F": return qsTrId("side.front");
        case "B": return qsTrId("side.back");
        }
        return "";
    }

    // Which way a turn goes, by the dir.* id and the arrow. Quarter turns are
    // clockwise as seen from outside the side (turns === 1), or the other way
    // (turns === 3); and the top, bottom, left and right sides are looked at
    // from different ends of their axes.
    function directionOf(face, turns) {
        if (turns === 2)
            return "either";
        var clockwise = turns === 1;
        switch (face) {
        case "U": return clockwise ? "left" : "right";
        case "D": return clockwise ? "right" : "left";
        case "R": return clockwise ? "up" : "down";
        case "L": return clockwise ? "down" : "up";
        case "F": return clockwise ? "cw" : "ccw";
        case "B": return clockwise ? "ccw" : "cw";
        }
        return "either";
    }

    function directionWords(direction) {
        switch (direction) {
        case "left": return qsTrId("dir.left");
        case "right": return qsTrId("dir.right");
        case "up": return qsTrId("dir.up");
        case "down": return qsTrId("dir.down");
        case "cw": return qsTrId("dir.cw");
        case "ccw": return qsTrId("dir.ccw");
        case "either": return qsTrId("dir.either");
        }
        return "";
    }

    function amountWords(turns) {
        return turns === 2 ? qsTrId("amount.half") : qsTrId("amount.quarter");
    }

    // The whole turn as one sentence: what a screen reader says, and what the
    // three parts of a row add up to.
    function turnSentence(face, turns) {
        switch (face + turns) {
        case "U1": return qsTrId("turn.U1");
        case "U3": return qsTrId("turn.U3");
        case "U2": return qsTrId("turn.U2");
        case "D1": return qsTrId("turn.D1");
        case "D3": return qsTrId("turn.D3");
        case "D2": return qsTrId("turn.D2");
        case "R1": return qsTrId("turn.R1");
        case "R3": return qsTrId("turn.R3");
        case "R2": return qsTrId("turn.R2");
        case "L1": return qsTrId("turn.L1");
        case "L3": return qsTrId("turn.L3");
        case "L2": return qsTrId("turn.L2");
        case "F1": return qsTrId("turn.F1");
        case "F3": return qsTrId("turn.F3");
        case "F2": return qsTrId("turn.F2");
        case "B1": return qsTrId("turn.B1");
        case "B3": return qsTrId("turn.B3");
        case "B2": return qsTrId("turn.B2");
        }
        return "";
    }

    function number(n) { return Qt.locale(Qt.uiLanguage).toString(n); }

    // The method's one line for someone learning: SolveController.Method.
    readonly property string methodWords: {
        switch (source.method) {
        case 1: return qsTrId("solution.method.intermediate");
        case 2: return qsTrId("solution.method.advanced");
        }
        return qsTrId("solution.method.beginner");
    }

    // ---- the position ---------------------------------------------------------
    // How many turns have been made on the cube shown: 0 is the cube as it was
    // scanned, `total` the cube solved. The selected row is the turn the cube
    // is about to make. Only the turn playing on the cube is ahead of it.
    property int position: 0
    property bool playing: false
    // +1 or -1 while a turn is being played on the cube: where `position`
    // goes when the layer lands.
    property int pendingStep: 0

    // The four states of docs/design/screens/solution.md, by the id after the
    // dot. (Not `state`: an Item has one.)
    readonly property string reviewState: playing ? "playing"
        : position >= total ? "end"
        : position === 0 ? "overview"
        : "stepping"

    readonly property string caption: position <= 0 ? qsTrId("solution.atStart")
        : position >= total ? qsTrId("solution.atEnd")
        : qsTrId("solve.progress").arg(number(position + 1)).arg(number(total))

    onPositionChanged: {
        if (playing && position >= total)
            playing = false;
        showSelection();
    }

    // Keeps the selected row in view, with the heading of its stage when it is
    // the stage's first turn.
    function showSelection() {
        if (total === 0 || rows.length === 0)
            return;
        if (position <= 0) {
            list.positionViewAtBeginning();
            return;
        }
        if (position >= total) {
            list.positionViewAtEnd();
            return;
        }
        var row = rowOfTurn[position];
        if (row > 0 && rows[row - 1].kind === "stage")
            list.positionViewAtIndex(row - 1, ListView.Contain);
        list.positionViewAtIndex(row, ListView.Contain);
    }

    // The turn plays on the cube and the position moves on when it lands.
    // With reduced motion there is nothing to see turn: the cube is repainted.
    function advance() {
        cube.finishPlaying(); // a turn still in flight lands first
        if (position >= total)
            return false;
        if (!root.reducedMotion) {
            var move = turnList[position];
            pendingStep = 1;
            if (playOnCube(move, move.turns) && cube.playing)
                return true;
            pendingStep = 0;
        }
        position += 1;
        return true;
    }

    function retreat() {
        cube.finishPlaying();
        if (position <= 0)
            return false;
        if (!root.reducedMotion) {
            // Going back is the last turn undone.
            var move = turnList[position - 1];
            pendingStep = -1;
            if (playOnCube(move, 4 - move.turns) && cube.playing)
                return true;
            pendingStep = 0;
        }
        position -= 1;
        return true;
    }

    // Turns the layers of a move on the cube on screen; false if it refuses.
    function playOnCube(move, turns) {
        return threeByThree ? cube.playMove(move.face, turns)
                            : cube.playMove(move.face, firstLayer(move), lastLayer(move), turns);
    }

    Connections {
        target: cube
        function onMovePlayed() {
            var step = root.pendingStep;
            root.pendingStep = 0;
            root.position += step;
        }
    }

    // Goes to a position without turning anything. A turn in flight lands
    // first, so the picture is of a cube and not of one mid-turn; the repaint
    // that follows puts every layer back.
    function jumpTo(index) {
        playing = false;
        cube.finishPlaying();
        pendingStep = 0;
        position = Math.max(0, Math.min(total, index));
    }

    function next() {
        playing = false;
        advance();
    }

    function previous() {
        playing = false;
        retreat();
    }

    function play() {
        if (position >= total)
            return;
        playing = true;
        if (!advance())
            playing = false;
    }

    function pause() { playing = false; }

    // The main button: Play, Pause, and once the cube is solved Start again.
    function toggle() {
        if (reviewState === "end")
            jumpTo(0);
        else if (playing)
            pause();
        else
            play();
    }

    function back() { AppState.closeSolution(); }

    // Turns one after another, about 700 ms apart (900 with reduced motion).
    Timer {
        interval: root.reducedMotion ? 900 : 700
        repeat: true
        running: root.playing
        onTriggered: {
            if (!root.advance())
                root.playing = false;
        }
    }

    // ---- keys -------------------------------------------------------------------
    // Forward and back follow the layout's direction: in a right-to-left
    // language forward is the left arrow.
    focus: true
    Component.onCompleted: {
        rebuild();
        forceActiveFocus();
    }
    Keys.onRightPressed: mirrored ? previous() : next()
    Keys.onLeftPressed: mirrored ? next() : previous()
    Keys.onSpacePressed: toggle()
    Keys.onEscapePressed: back()
    Keys.onBackPressed: back()
    // Keys has no signal of its own for these two.
    Keys.onPressed: (event) => {
        if (event.key === Qt.Key_Home) {
            jumpTo(0);
            event.accepted = true;
        } else if (event.key === Qt.Key_End) {
            jumpTo(total);
            event.accepted = true;
        }
    }

    // ---- the top bar ----------------------------------------------------------
    Item {
        id: topBar
        width: parent.width
        height: Math.max(56, titleColumn.implicitHeight + 2 * Theme.space2)

        AppButton {
            id: backButton
            objectName: "solution.back"
            anchors.left: parent.left; anchors.leftMargin: Theme.space2
            anchors.verticalCenter: parent.verticalCenter
            height: 44
            variant: "ghost"
            leadingIcon: "chevron-left"
            text: qsTrId("common.back")
            onClicked: root.back()
        }
        Column {
            id: titleColumn
            anchors.left: backButton.right; anchors.leftMargin: Theme.space2
            anchors.right: parent.right; anchors.rightMargin: Theme.space4
            anchors.verticalCenter: parent.verticalCenter
            Text {
                objectName: "solution.title"
                width: parent.width
                text: qsTrId("solution.title")
                wrapMode: Text.WordWrap
                font.family: Theme.fontHeading; font.weight: Font.ExtraBold; font.pixelSize: 20
                color: Theme.ink
                Accessible.role: Accessible.Heading
                Accessible.name: text
            }
            Text {
                objectName: "solution.subtitle"
                visible: root.subtitle !== ""
                width: parent.width
                text: root.subtitle
                wrapMode: Text.WordWrap
                font.family: Theme.fontHeading; font.pixelSize: 13
                color: Theme.inkMuted(0.75)
            }
        }
        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 2; color: Theme.divider }
    }

    // ---- the cube and its controls ----------------------------------------------
    Item {
        id: lead
        // Anchored, so that in a right-to-left layout the leading pane is the
        // one on the right.
        anchors.left: parent.left
        anchors.top: topBar.bottom
        readonly property real margin: Theme.space4
        readonly property real innerWidth: width - 2 * margin
        // Two panes: the cube gets what the summary, caption and buttons leave.
        // Expanded gives the list a column of its own and the cube the rest.
        width: root.twoPane ? (root.wide ? Math.max(root.width - Math.min(520, root.width / 2), root.width / 2)
                                         : Math.round(root.width / 2))
                            : root.width
        height: root.twoPane ? root.height - topBar.height
                             : buttonRow.y + buttonRow.height + Theme.space3 + 2

        Column {
            id: summaryBlock
            x: lead.margin; y: Theme.space3
            width: lead.innerWidth
            spacing: 2
            Text {
                objectName: "solution.summary"
                width: parent.width
                text: qsTrId("solution.summary", root.total).arg(root.number(root.stageCount))
                wrapMode: Text.WordWrap
                font.family: Theme.fontHeading; font.weight: Font.Bold; font.pixelSize: 16
                color: Theme.ink
            }
            Text {
                objectName: "solution.method"
                width: parent.width
                text: root.methodWords
                wrapMode: Text.WordWrap
                font.family: Theme.fontHeading; font.pixelSize: 14
                color: Theme.inkMuted(0.75)
            }
        }

        // The cube is never mirrored, whatever the layout round it does.
        Item {
            id: cubeBox
            y: summaryBlock.y + summaryBlock.height + Theme.space2
            width: lead.width
            height: root.twoPane ? Math.max(120, lead.height - y - captionText.height - 4
                                            - Theme.space3 - buttonRow.height - Theme.space3)
                                 : 210
            LayoutMirroring.enabled: false
            LayoutMirroring.childrenInherit: true

            // The 3x3's own view, or the one that draws any other size; the two
            // are painted and turned alike.
            Loader {
                id: cubeLoader
                readonly property real side: Math.max(0, Math.min(parent.width - 2 * lead.margin, parent.height))
                width: side; height: side
                anchors.centerIn: parent
                sourceComponent: root.threeByThree ? cube3Component : cubeNComponent
            }

            Component {
                id: cube3Component
                Cube3DView {
                    objectName: "solution.cube"

                    // Looked at, not handled: dragging it would be a control with
                    // no word for what it does.
                    interactive: false
                    autoRotate: false
                    evenLight: true
                    shaderStyle: SettingsState.shaderStyleIndex
                    showStats: SettingsState.statsForNerds

                    // As the cube is held at the start of the solve, and seen from
                    // above and to the right - from behind for a turn of the back
                    // side, which turns out of sight otherwise. It is the view
                    // that turns, not the cube.
                    homeRotation: Qt.vector3d(0, 0, 0)
                    viewPitch: 24
                    viewYaw: root.viewYaw

                    faceletColors: root.paintedNow

                    Accessible.role: Accessible.Graphic
                    Accessible.name: root.caption
                }
            }

            Component {
                id: cubeNComponent
                CubeNView {
                    objectName: "solution.cube"
                    interactive: false
                    autoRotate: false
                    evenLight: true
                    showStats: SettingsState.statsForNerds

                    homeRotation: Qt.vector3d(0, 0, 0)
                    viewPitch: 24
                    viewYaw: root.viewYaw

                    faceletColors: root.paintedNow

                    Accessible.role: Accessible.Graphic
                    Accessible.name: root.caption
                }
            }
        }

        Text {
            id: captionText
            objectName: "solution.caption"
            x: lead.margin
            y: cubeBox.y + cubeBox.height + 4
            width: lead.innerWidth
            text: root.caption
            wrapMode: Text.WordWrap
            horizontalAlignment: Text.AlignHCenter
            font.family: Theme.fontMono; font.weight: Font.Medium; font.pixelSize: 16
            color: Theme.inkMuted(0.75)
        }

        // A Row, which turns round in a right-to-left layout: Previous is then
        // on the right and Next on the left, as the arrow keys are.
        Row {
            id: buttonRow
            x: lead.margin
            y: captionText.y + captionText.height + Theme.space3
            width: lead.innerWidth
            height: 56
            spacing: Theme.space2
            readonly property real cell: (width - 2 * spacing) / 3

            AppButton {
                objectName: "solution.previous"
                width: buttonRow.cell; height: 56
                variant: "secondary"
                text: qsTrId("solution.previous")
                enabled: root.position > 0
                onClicked: root.previous()
            }
            // Play, Pause, and once the cube is solved Start again.
            AppButton {
                objectName: "solution.play"
                width: buttonRow.cell; height: 56
                variant: "primary"
                text: root.playing ? qsTrId("solution.pause")
                    : root.position >= root.total ? qsTrId("solution.startAgain")
                    : qsTrId("solution.play")
                enabled: root.total > 0
                onClicked: root.toggle()
            }
            AppButton {
                objectName: "solution.next"
                width: buttonRow.cell; height: 56
                variant: "secondary"
                text: qsTrId("solution.next")
                enabled: root.position < root.total
                onClicked: root.next()
            }
        }

        // Under the buttons, narrow; between the panes, wide.
        Rectangle {
            visible: !root.twoPane
            anchors.bottom: parent.bottom
            width: parent.width; height: 2
            color: Theme.divider
        }
        Rectangle {
            visible: root.twoPane
            anchors.right: parent.right
            width: 2; height: parent.height
            color: Theme.divider
        }
    }

    // ---- the list ------------------------------------------------------------------
    // A stage's heading, then its turns. Components of this file, not inline
    // components: they read the screen's own functions and position, which an
    // inline component's scope does not reach. What a row shows arrives in
    // `info` once its Loader has made it; until then every binding has
    // something harmless to read.
    Component {
        id: stageHeading
        Rectangle {
            id: heading
            property var info: ({ position: 0, stage: -1, start: 0, end: 0 })
            readonly property var words: root.stageWords(info.stage)
            readonly property int count: info.end - info.start

            objectName: "solution.stage" + info.position
            width: parent ? parent.width : 320
            height: headingColumn.implicitHeight + 2 * Theme.space3
            color: Theme.surface

            Accessible.role: Accessible.StaticText
            Accessible.name: words.title
            Accessible.description: words.what

            Rectangle {
                id: numberBox
                anchors.left: parent.left; anchors.leftMargin: Theme.space3
                y: Theme.space3
                width: 32; height: 32
                color: Theme.accent
                Text {
                    anchors.centerIn: parent
                    text: root.number(heading.info.position)
                    font.family: Theme.fontMono; font.pixelSize: 15
                    color: Theme.paper
                }
            }
            Column {
                id: headingColumn
                anchors.left: numberBox.right; anchors.leftMargin: Theme.space3
                anchors.right: parent.right; anchors.rightMargin: Theme.space3
                y: Theme.space3
                spacing: Theme.space1

                // The name and how many turns: on one line when there is room.
                Flow {
                    width: parent.width
                    spacing: Theme.space2
                    // Each wraps inside the line rather than past it.
                    Text {
                        width: Math.min(implicitWidth, headingColumn.width)
                        text: heading.words.title
                        wrapMode: Text.WordWrap
                        font.family: Theme.fontHeading; font.weight: Font.ExtraBold; font.pixelSize: 16
                        color: Theme.ink
                    }
                    Text {
                        width: Math.min(implicitWidth, headingColumn.width)
                        text: qsTrId("solution.stageTurns", heading.count)
                        wrapMode: Text.WordWrap
                        font.family: Theme.fontHeading; font.pixelSize: 14
                        color: Theme.inkMuted(0.75)
                    }
                }
                Text {
                    width: parent.width
                    text: heading.words.what
                    wrapMode: Text.WordWrap
                    font.family: Theme.fontHeading; font.pixelSize: 14
                    color: Theme.ink
                }
                Text {
                    visible: heading.words.why !== ""
                    width: parent.width
                    text: heading.words.why
                    wrapMode: Text.WordWrap
                    font.family: Theme.fontHeading; font.pixelSize: 14
                    color: Theme.inkMuted(0.75)
                }
                // The stage's turns as letters, left to right whatever the
                // language: the way an algorithm is written.
                Text {
                    objectName: "solution.notation"
                    visible: root.showNotation && heading.count > 0
                    width: parent.width
                    text: root.turnList.slice(heading.info.start, heading.info.end)
                              .map(function (move) { return move.notation; }).join(" ")
                    wrapMode: Text.WordWrap
                    horizontalAlignment: Text.AlignLeft
                    font.family: Theme.fontMono; font.pixelSize: 14
                    color: Theme.ink
                    LayoutMirroring.enabled: false
                }
            }
        }
    }

    Component {
        id: turnRow
        Rectangle {
            id: turn
            property var info: ({ index: -1, number: 0, face: "U", turns: 1, first: 0, last: 0 })
            readonly property bool selected: root.position === info.index
            readonly property string direction: root.directionOf(info.face, info.turns)

            objectName: "solution.turn" + info.number
            width: parent ? parent.width : 320
            height: Math.max(48, rowText.implicitHeight + 2 * Theme.space2)
            color: selected ? Theme.accent100 : "transparent"

            Accessible.role: Accessible.ListItem
            Accessible.name: qsTrId("solution.a11y.turn").arg(root.number(info.number))
                                 .arg(root.turnSpeech(info))
            Accessible.selectable: true
            Accessible.selected: selected

            Rectangle {
                visible: turn.selected
                anchors.left: parent.left
                width: 6; height: parent.height
                color: Theme.accent
            }
            Text {
                id: numberText
                anchors.left: parent.left; anchors.leftMargin: Theme.space4
                anchors.verticalCenter: parent.verticalCenter
                width: 36
                text: root.number(turn.info.number)
                font.family: Theme.fontMono; font.pixelSize: 14
                font.weight: turn.selected ? Font.Bold : Font.Normal
                color: Theme.inkMuted(0.75)
            }
            // The colour of the side's centre, which says which side without
            // naming it twice.
            Rectangle {
                id: swatch
                anchors.left: numberText.right; anchors.leftMargin: Theme.space2
                anchors.verticalCenter: parent.verticalCenter
                width: 16; height: 16
                color: root.centres[turn.info.face] !== undefined ? root.centres[turn.info.face] : Theme.paper
                border.width: 1; border.color: Theme.divider
            }
            TurnArrow {
                id: arrow
                anchors.right: parent.right; anchors.rightMargin: Theme.space4
                anchors.verticalCenter: parent.verticalCenter
                width: 24; height: 24
                kind: turn.direction
                color: Theme.ink
            }
            Column {
                id: rowText
                anchors.left: swatch.right; anchors.leftMargin: Theme.space3
                anchors.right: arrow.left; anchors.rightMargin: Theme.space3
                anchors.verticalCenter: parent.verticalCenter
                Text {
                    width: parent.width
                    text: root.turnLine(turn.info)
                    wrapMode: Text.WordWrap
                    font.family: Theme.fontHeading; font.pixelSize: 14
                    font.weight: turn.selected ? Font.Bold : Font.Normal
                    color: Theme.ink
                }
                // The back side cannot be seen from the front: say which way
                // round the clockwise is.
                Text {
                    visible: turn.info.face === "B" && turn.info.turns !== 2
                    width: parent.width
                    text: qsTrId("dir.seenFromFront")
                    wrapMode: Text.WordWrap
                    font.family: Theme.fontHeading; font.pixelSize: 12
                    color: Theme.inkMuted(0.75)
                }
            }
            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                onClicked: root.jumpTo(turn.info.index)
            }
        }
    }

    ListView {
        id: list
        objectName: "solution.list"
        // Beside the cube's pane, or under it.
        anchors.left: root.twoPane ? lead.right : parent.left
        anchors.right: parent.right
        anchors.top: root.twoPane ? topBar.bottom : lead.bottom
        anchors.bottom: parent.bottom
        clip: true
        model: root.rows
        boundsBehavior: Flickable.StopAtBounds
        // The screen's keys are the screen's: the list does not take its own.
        keyNavigationEnabled: false
        activeFocusOnTab: false
        delegate: Item {
            id: cell
            width: ListView.view.width
            height: content.item ? content.item.height : 0
            Loader {
                id: content
                width: parent.width
                sourceComponent: modelData.kind === "stage" ? stageHeading : turnRow
                onLoaded: item.info = modelData
            }
        }
    }
}
