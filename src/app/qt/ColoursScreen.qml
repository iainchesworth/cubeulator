import QtQuick
import Cubeulator

// A cube typed in by hand, a colour at a time, at any size from 2 to 9
// (docs/design/screens/review.md, "A cube of another size").
//
// Hold the cube any way up: the side facing up is the top and the side facing
// you the front. One side is shown at a time, large, as it looks from outside;
// the sides next to each of its edges are named round it, so which way up the
// grid goes is never a guess, and on a wide screen the cube is drawn beside it
// and turned to show that side. Pick a colour, then tap or drag over the
// stickers that have it. Whatever six colours there are are the cube's: the
// library says whether they can be those of a cube (ColourEntry.status).
Rectangle {
    id: root
    color: Theme.paper
    readonly property bool wide: width >= Theme.wideBreakpoint

    readonly property int size: ColourEntry.size
    // The side being painted, as a side letter, and the colour in the brush
    // (0 to 5 as the palette lists them; -1 unpaints).
    property string side: "F"
    property int brush: 0
    readonly property var status: ColourEntry.status

    // The sides in the order a person goes round a cube: the top, the front, then
    // the right, back and left, and the bottom.
    readonly property var sideOrder: ["U", "F", "R", "B", "L", "D"]
    function sideName(letter) {
        switch (letter) {
        case "U": return qsTrId("side.top");
        case "D": return qsTrId("side.bottom");
        case "L": return qsTrId("side.left");
        case "R": return qsTrId("side.right");
        case "F": return qsTrId("side.front");
        }
        return qsTrId("side.back");
    }
    // The sides against the edges of a side's grid, as it looks from outside: the top
    // row's, the right column's, the bottom row's and the left column's.
    readonly property var edges: ({
        U: ["B", "R", "F", "L"], D: ["F", "R", "B", "L"],
        F: ["U", "R", "D", "L"], B: ["U", "L", "D", "R"],
        L: ["U", "F", "D", "B"], R: ["U", "B", "D", "F"]
    })
    // Where the cube on show is looked from to show a side: { pitch, yaw }.
    readonly property var views: ({
        F: { pitch: 18, yaw: -24 }, R: { pitch: 18, yaw: -64 }, B: { pitch: 18, yaw: 156 },
        L: { pitch: 18, yaw: 66 }, U: { pitch: 62, yaw: -20 }, D: { pitch: -62, yaw: -20 }
    })

    // The colours a brush has, by cube::solver::Color: names QML maps to the palette.
    readonly property var palette: ["White", "Yellow", "Green", "Blue", "Orange", "Red"]
    function colourHex(name) {
        switch (name) {
        case "White": return Theme.faceU;
        case "Yellow": return Theme.faceD;
        case "Green": return Theme.faceF;
        case "Blue": return Theme.faceB;
        case "Orange": return Theme.faceL;
        case "Red": return Theme.faceR;
        }
        return "transparent";
    }
    function colourName(index) {
        switch (index) {
        case 0: return qsTrId("colour.white");
        case 1: return qsTrId("colour.yellow");
        case 2: return qsTrId("colour.green");
        case 3: return qsTrId("colour.blue");
        case 4: return qsTrId("colour.orange");
        case 5: return qsTrId("colour.red");
        }
        return qsTrId("colour.unset");
    }
    function digits(n) { return Qt.locale(Qt.uiLanguage).toString(n); }

    // The whole cube as the 3D view is painted, with what is not painted yet in grey.
    function painted(sides) {
        var out = {};
        for (var letter in sides) {
            var names = sides[letter];
            var colours = [];
            for (var i = 0; i < names.length; i++)
                colours.push(names[i] === "" ? "#6F6B6B" : colourHex(names[i]));
            out[letter] = colours;
        }
        return out;
    }

    // ---- what is the matter ---------------------------------------------------
    function countList(entries) {
        var parts = [];
        for (var i = 0; i < entries.length; i++) {
            parts.push(qsTrId("review.status.countItem")
                       .arg(colourName(palette.indexOf(entries[i].colour))).arg(digits(entries[i].count)));
        }
        return parts.join(", ");
    }
    function faultWords(fault) {
        switch (fault) {
        case "noScheme": return qsTrId("review.fault.noScheme");
        case "centreColours": return qsTrId("review.fault.centreColours");
        case "repeatedPiece": return qsTrId("review.fault.repeatedPiece");
        case "twistedCorner": return qsTrId("review.fault.twistedCorner");
        case "flippedEdge": return qsTrId("review.fault.flippedEdge");
        case "swappedPieces": return qsTrId("review.fault.swappedPieces");
        }
        return qsTrId("review.fault.unknownPiece");
    }
    function headlineOf(state) {
        switch (state) {
        case "empty": return qsTrId("review.status.emptyN").arg(digits(6 * size * size));
        case "partial": return qsTrId("review.status.partial", status.toGo);
        case "miscount": return qsTrId("review.status.miscount");
        case "impossible": return qsTrId("review.status.impossible");
        }
        return qsTrId("review.status.valid");
    }
    function detailOf(state) {
        var lines = [];
        if (state === "miscount") {
            if (status.over.length > 0)
                lines.push(qsTrId("review.status.miscountOver").arg(countList(status.over)));
            if (status.under.length > 0)
                lines.push(qsTrId("review.status.miscountUnder").arg(countList(status.under)));
        } else if (state === "impossible") {
            lines.push(faultWords(status.fault));
            lines.push(qsTrId("review.status.impossibleBody"));
        }
        return lines.join("\n");
    }
    readonly property string headline: headlineOf(status.state)
    readonly property string detail: detailOf(status.state)

    // The sticker to look at first when the cube is not one, where there is one.
    readonly property var suspect: status.state === "impossible" && status.suspect !== undefined
                                   ? status.suspect : null

    // Whether every sticker of a side is painted. Read off the sides themselves so
    // that what asks is told when one is painted.
    function complete(letter) {
        var stickers = ColourEntry.sides[letter];
        if (stickers === undefined)
            return false;
        for (var i = 0; i < stickers.length; i++) {
            if (stickers[i] === "")
                return false;
        }
        return true;
    }

    function paint(index) { ColourEntry.paint(side, index, brush); }

    // Leaving with colours entered asks first (and says where it was going).
    property bool confirmingLeave: false
    property bool leavingToScan: false
    function leave() {
        leavingToScan = false;
        if (ColourEntry.touched)
            confirmingLeave = true;
        else
            AppState.closeColours();
    }
    function scanInstead() {
        leavingToScan = true;
        if (ColourEntry.touched)
            confirmingLeave = true;
        else
            AppState.beginScan();
    }
    function solve() {
        AppState.solveColours();
    }

    focus: true
    Component.onCompleted: forceActiveFocus()
    Keys.onEscapePressed: root.leave()
    Keys.onBackPressed: root.leave()

    // ---- the screen -------------------------------------------------------------
    Item {
        id: bar
        width: parent.width
        height: 64
        AppButton {
            id: back
            objectName: "colours.back"
            anchors.left: parent.left; anchors.leftMargin: Theme.space3
            anchors.verticalCenter: parent.verticalCenter
            variant: "ghost"; leadingIcon: "chevron-left"
            text: qsTrId("common.back")
            onClicked: root.leave()
        }
        Text {
            objectName: "colours.title"
            anchors.left: back.right; anchors.leftMargin: Theme.space2
            anchors.right: parent.right; anchors.rightMargin: Theme.space3
            anchors.verticalCenter: parent.verticalCenter
            text: qsTrId("review.title.enter")
            elide: Text.ElideRight
            font.family: Theme.fontHeading; font.weight: Font.ExtraBold; font.pixelSize: 22
            color: Theme.ink
        }
        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 2; color: Theme.divider }
    }

    // The cube, beside the side being painted: wide screens only.
    Item {
        id: preview
        visible: root.wide
        anchors.top: bar.bottom; anchors.bottom: parent.bottom; anchors.left: parent.left
        width: root.wide ? Math.min(parent.width * 0.4, 420) : 0
        CubeNView {
            id: cube
            objectName: "colours.cube"
            anchors.centerIn: parent
            width: Math.min(parent.width, parent.height); height: width
            interactive: false
            autoRotate: false
            evenLight: true
            homeRotation: Qt.vector3d(0, 0, 0)
            size: root.size
            viewPitch: root.views[root.side].pitch
            viewYaw: root.views[root.side].yaw
            faceletColors: root.painted(ColourEntry.sides)
            LayoutMirroring.enabled: false
        }
        Rectangle { anchors.right: parent.right; width: 2; height: parent.height; color: Theme.divider }
    }

    Flickable {
        id: form
        anchors.top: bar.bottom; anchors.bottom: parent.bottom; anchors.right: parent.right
        anchors.left: preview.visible ? preview.right : parent.left
        contentWidth: width
        contentHeight: column.height
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        Column {
            id: column
            width: Math.min(form.width, 560)
            x: (form.width - width) / 2
            spacing: Theme.space3
            topPadding: Theme.space3
            bottomPadding: Theme.space4

            Text {
                x: Theme.space4
                width: parent.width - 2 * Theme.space4
                text: qsTrId("review.intro.enterN")
                wrapMode: Text.WordWrap
                font.family: Theme.fontHeading; font.pixelSize: 13
                color: Theme.inkMuted(0.7)
            }

            // The sides, to choose which is painted. A tick says a side is done.
            Flow {
                objectName: "colours.sides"
                x: Theme.space4
                width: parent.width - 2 * Theme.space4
                spacing: Theme.space2
                Accessible.name: qsTrId("review.sidePicker")
                Repeater {
                    model: root.sideOrder
                    delegate: AppButton {
                        required property string modelData
                        objectName: "colours.side." + modelData
                        height: 44
                        variant: modelData === root.side ? "primary" : "secondary"
                        text: root.sideName(modelData)
                        trailingIcon: root.complete(modelData) ? "check" : ""
                        onClicked: root.side = modelData
                    }
                }
            }

            // The side being painted, with the sides at its edges named round it.
            Item {
                id: sideBox
                x: Theme.space4
                width: parent.width - 2 * Theme.space4
                // The sides at the top and bottom are named above and below the grid,
                // those at the left and right up its sides (turned, so that they take
                // the width of a line and not of a word).
                readonly property real label: 22
                readonly property real gridSide: Math.min(width - 2 * label,
                    root.wide ? Math.min(380, Math.max(180, root.height - 470))
                              : Math.max(168, root.height - 480))
                height: gridSide + 2 * label

                Text {   // top
                    objectName: "colours.edge.top"
                    anchors.horizontalCenter: parent.horizontalCenter; anchors.top: parent.top
                    text: root.sideName(root.edges[root.side][0])
                    font.family: Theme.fontHeading; font.pixelSize: 11; color: Theme.inkMuted(0.6)
                }
                Text {   // bottom
                    objectName: "colours.edge.bottom"
                    anchors.horizontalCenter: parent.horizontalCenter; anchors.bottom: parent.bottom
                    text: root.sideName(root.edges[root.side][2])
                    font.family: Theme.fontHeading; font.pixelSize: 11; color: Theme.inkMuted(0.6)
                }
                Text {   // left
                    objectName: "colours.edge.left"
                    x: (parent.width - sideBox.gridSide) / 4 - width / 2
                    y: (parent.height - height) / 2
                    rotation: -90
                    text: root.sideName(root.edges[root.side][3])
                    font.family: Theme.fontHeading; font.pixelSize: 11; color: Theme.inkMuted(0.6)
                }
                Text {   // right
                    objectName: "colours.edge.right"
                    x: parent.width - (parent.width - sideBox.gridSide) / 4 - width / 2
                    y: (parent.height - height) / 2
                    rotation: 90
                    text: root.sideName(root.edges[root.side][1])
                    font.family: Theme.fontHeading; font.pixelSize: 11; color: Theme.inkMuted(0.6)
                }

                Rectangle {
                    id: gridBox
                    objectName: "colours.grid"
                    anchors.centerIn: parent
                    width: sideBox.gridSide; height: sideBox.gridSide
                    color: "#1B1A1A"
                    // The cube is the object in the hand: it does not mirror.
                    LayoutMirroring.enabled: false
                    LayoutMirroring.childrenInherit: true

                    readonly property real cell: (width - 2 * pad) / root.size
                    readonly property real pad: 4
                    readonly property var names: ColourEntry.sides[root.side]

                    Repeater {
                        model: root.size * root.size
                        delegate: Rectangle {
                            required property int index
                            objectName: "colours.sticker." + index
                            readonly property string colourOf: gridBox.names !== undefined ? gridBox.names[index] : ""
                            readonly property bool suspected: root.suspect !== null
                                && root.suspect.side === root.side && root.suspect.index === index
                            x: gridBox.pad + (index % root.size) * gridBox.cell + 1
                            y: gridBox.pad + Math.floor(index / root.size) * gridBox.cell + 1
                            width: gridBox.cell - 2; height: gridBox.cell - 2
                            color: colourOf === "" ? "#3A3737" : root.colourHex(colourOf)
                            border.width: suspected ? 3 : (colourOf === "" ? 1 : 0)
                            border.color: suspected ? Theme.accent : "#575353"
                            Accessible.role: Accessible.Button
                            Accessible.name: qsTrId("review.a11y.sticker")
                                .arg(root.sideName(root.side)).arg(root.digits(Math.floor(index / root.size) + 1))
                                .arg(root.digits(index % root.size + 1))
                                .arg(colourOf === "" ? qsTrId("colour.unset") : root.colourName(root.palette.indexOf(colourOf)))
                        }
                    }

                    // Tap or drag over the stickers to paint them.
                    MouseArea {
                        anchors.fill: parent
                        function stickerAt(x, y) {
                            var col = Math.floor((x - gridBox.pad) / gridBox.cell);
                            var row = Math.floor((y - gridBox.pad) / gridBox.cell);
                            if (col < 0 || row < 0 || col >= root.size || row >= root.size)
                                return -1;
                            return row * root.size + col;
                        }
                        onPressed: (mouse) => {
                            var i = stickerAt(mouse.x, mouse.y);
                            if (i >= 0) root.paint(i);
                        }
                        onPositionChanged: (mouse) => {
                            if (!pressed)
                                return;
                            var i = stickerAt(mouse.x, mouse.y);
                            if (i >= 0) root.paint(i);
                        }
                    }
                }
            }

            // The colours: choose one, then paint with it.
            Row {
                objectName: "colours.palette"
                x: Theme.space4
                width: parent.width - 2 * Theme.space4
                spacing: Theme.space2
                Accessible.name: qsTrId("review.palette")
                Repeater {
                    model: 7
                    delegate: Rectangle {
                        required property int index
                        // The seventh takes a colour away again.
                        readonly property int colourIndex: index < 6 ? index : -1
                        objectName: "colours.colour." + colourIndex
                        width: (parent.width - 6 * Theme.space2) / 7
                        height: 52
                        color: colourIndex < 0 ? "transparent" : root.colourHex(root.palette[colourIndex])
                        border.width: root.brush === colourIndex ? 3 : 1
                        border.color: root.brush === colourIndex ? Theme.accent : Theme.divider
                        Accessible.role: Accessible.RadioButton
                        Accessible.name: root.colourName(colourIndex)
                        Accessible.checked: root.brush === colourIndex
                        Icon {
                            visible: parent.colourIndex < 0
                            anchors.centerIn: parent
                            name: "cross"; width: 16; height: 16
                            color: Theme.inkMuted(0.7)
                        }
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: root.brush = parent.colourIndex
                        }
                    }
                }
            }
            Row {
                x: Theme.space4
                width: parent.width - 2 * Theme.space4
                spacing: Theme.space2
                Text {
                    width: Math.max(40, parent.width - fillSide.width - clearAll.width - 2 * Theme.space2)
                    anchors.verticalCenter: parent.verticalCenter
                    text: root.colourName(root.brush)
                    elide: Text.ElideRight
                    font.family: Theme.fontHeading; font.weight: Font.ExtraBold; font.pixelSize: 14
                    color: Theme.ink
                }
                AppButton {
                    id: fillSide
                    objectName: "colours.fillSide"
                    height: 34
                    variant: "ghost"
                    text: qsTrId("review.fillSide")
                    onClicked: ColourEntry.paintSide(root.side, root.brush)
                }
                AppButton {
                    id: clearAll
                    objectName: "colours.clearAll"
                    height: 34
                    variant: "ghost"
                    text: qsTrId("review.clearAll")
                    enabled: ColourEntry.touched
                    onClicked: ColourEntry.clearAll()
                }
            }

            // What is the matter, and the way on.
            Column {
                objectName: "colours.status"
                x: Theme.space4
                width: parent.width - 2 * Theme.space4
                spacing: Theme.space1
                Text {
                    objectName: "colours.status.headline"
                    width: parent.width
                    text: root.headline
                    wrapMode: Text.WordWrap
                    font.family: Theme.fontHeading; font.weight: Font.ExtraBold; font.pixelSize: 15
                    color: root.status.state === "valid" ? Theme.detected
                           : (root.status.state === "miscount" || root.status.state === "impossible"
                              ? Theme.accent : Theme.ink)
                }
                Text {
                    objectName: "colours.status.detail"
                    width: parent.width
                    visible: root.detail !== ""
                    text: root.detail
                    wrapMode: Text.WordWrap
                    font.family: Theme.fontHeading; font.pixelSize: 12
                    color: Theme.inkMuted(0.75)
                }
            }

            Row {
                x: Theme.space4
                width: parent.width - 2 * Theme.space4
                spacing: Theme.space2
                AppButton {
                    id: scanInstead
                    objectName: "colours.scan"
                    visible: root.size === 3
                    height: 46
                    variant: "secondary"
                    text: qsTrId("entry.scan")
                    onClicked: root.scanInstead()
                }
                AppButton {
                    objectName: "colours.solve"
                    width: parent.width - (scanInstead.visible ? scanInstead.width + Theme.space2 : 0)
                    height: 46
                    variant: "primary"; block: true
                    text: qsTrId("review.solve"); trailingIcon: "chevron-right"
                    enabled: root.status.state === "valid"
                    onClicked: root.solve()
                }
            }
        }
    }

    ConfirmDialog {
        objectName: "colours.discard"
        open: root.confirmingLeave
        restoreFocusTo: root
        title: qsTrId("review.discard.title")
        body: qsTrId("review.discard.body")
        keepText: qsTrId("review.discard.keep")
        confirmText: qsTrId("review.discard.confirm")
        onKept: root.confirmingLeave = false
        onConfirmed: {
            root.confirmingLeave = false;
            if (root.leavingToScan)
                AppState.beginScan();
            else
                AppState.closeColours();
        }
    }
}
