import QtQuick
import QtQuick.Shapes
import Cubeulator

// Sticker (docs/design/system.md, "Sticker, swatch, side chip"): a square in
// a cube colour, with its mark. The mark is what tells the colours apart
// without hue: nothing for white, a ring for yellow, a slash for orange, a
// plus for red, a dot for green, a bar for blue. A mark fills the middle 40%,
// is drawn in the ink that holds 3:1 against its sticker, and is on by default
// (Settings > Appearance > Marks on cube colours).
//
// A sticker never mirrors: it is the cube, not the layout.
Item {
    id: root

    // white | yellow | orange | red | green | blue
    property string colourName: "white"
    // Side of the square, in pixels.
    property real size: 48
    property bool marks: Theme.marksOn
    // On the page: a 1 px `cubeEdge` outline. On the plastic of a chip or the
    // cube: none.
    property bool onPage: true
    property real edgeWidth: onPage ? Theme.borderHair : 0
    // What a screen reader says (the colour's name, in the language). Empty:
    // the sticker is decoration inside something that names itself.
    property string accessibleName: ""

    // Below 12 px a sticker is too small to carry a mark and must not be the
    // only way a colour is shown.
    readonly property bool showMark: marks && size >= 12 && Theme.cubeMark(colourName) !== "none"
    readonly property string mark: Theme.cubeMark(colourName)
    readonly property color fillColor: Theme.cubeColour(colourName)
    readonly property color markInk: Theme.cubeMarkInk(colourName)

    readonly property var contrastPairs: {
        const pairs = [];
        if (showMark)
            pairs.push({ name: "mark", fg: markInk, bg: fillColor, target: 3.0 });
        if (onPage)
            pairs.push({ name: "edge", fg: Theme.cubeEdge, bg: Theme.paper, target: 3.0 });
        return pairs;
    }

    objectName: "kitSticker"
    implicitWidth: size
    implicitHeight: size

    LayoutMirroring.enabled: false
    LayoutMirroring.childrenInherit: true

    Rectangle {
        objectName: "face"
        anchors.fill: parent
        color: root.fillColor
        border.width: root.edgeWidth
        border.color: Theme.cubeEdge
        radius: Theme.radius
        Accessible.ignored: true
    }

    // The mark is drawn on a 24 grid and scaled into the middle 40%.
    Item {
        id: markBox
        objectName: "mark"
        visible: root.showMark
        width: root.size * 0.4
        height: root.size * 0.4
        anchors.centerIn: parent
        Accessible.ignored: true

        readonly property real scaleFactor: width / 24
        // About 1.6 px at the smallest, 6% of the sticker above that.
        readonly property real strokePx: Math.max(1.5, root.size * 0.06)

        Item {
            width: 24
            height: 24
            scale: markBox.scaleFactor
            transformOrigin: Item.TopLeft

            Shape {
                anchors.fill: parent
                preferredRendererType: Shape.CurveRenderer

                ShapePath {
                    // Strokes: ring, slash, plus, bar. The dot is a fill.
                    strokeColor: root.mark === "dot" ? "transparent" : root.markInk
                    strokeWidth: markBox.strokePx / markBox.scaleFactor
                    fillColor: root.mark === "dot" ? root.markInk : "transparent"
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin
                    PathSvg {
                        path: {
                            switch (root.mark) {
                            case "ring": return "M21 12 A9 9 0 1 1 3 12 A9 9 0 1 1 21 12";
                            case "dot": return "M20 12 A8 8 0 1 1 4 12 A8 8 0 1 1 20 12";
                            case "slash": return "M5 19 L19 5";
                            case "plus": return "M12 4 L12 20 M4 12 L20 12";
                            case "bar": return "M12 3 L12 21";
                            default: return "";
                            }
                        }
                    }
                }
            }
        }
    }

    Accessible.ignored: accessibleName === ""
    Accessible.role: Accessible.StaticText
    Accessible.name: accessibleName
}
