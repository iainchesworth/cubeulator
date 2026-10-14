import QtQuick
import Cubeulator

// The arrow that says which way a turn goes: an arrow along the way for the
// top, bottom, left and right sides; a half circle for the front and back,
// clockwise or anticlockwise; and a half circle with a head at each end for a
// half turn, which has no direction.
//
// A direction here is a physical one - to the left is the left of the person
// holding the cube - so it is drawn as it is whatever the layout's direction.
// This is not an Icon, which turns the chevrons round in a right-to-left
// layout and has none of these.
Canvas {
    id: root

    // "left" | "right" | "up" | "down" | "cw" | "ccw" | "either": the dir.*
    // words, by the part after the dot.
    property string kind: "right"
    property color color: Theme.ink
    property real strokeWidth: 2

    onKindChanged: requestPaint()
    onColorChanged: requestPaint()
    onWidthChanged: requestPaint()
    onHeightChanged: requestPaint()

    onPaint: {
        var ctx = getContext("2d");
        ctx.reset();
        ctx.clearRect(0, 0, width, height);
        if (width <= 0 || height <= 0)
            return;

        // 24 x 24 box, scaled to fill the item.
        var s = Math.min(width, height) / 24;
        ctx.translate((width - 24 * s) / 2, (height - 24 * s) / 2);
        ctx.scale(s, s);
        ctx.strokeStyle = root.color;
        ctx.lineWidth = root.strokeWidth / s;
        ctx.lineCap = "round";
        ctx.lineJoin = "round";

        function line(x1, y1, x2, y2) {
            ctx.beginPath();
            ctx.moveTo(x1, y1);
            ctx.lineTo(x2, y2);
            ctx.stroke();
        }
        function polyline(points) {
            ctx.beginPath();
            ctx.moveTo(points[0][0], points[0][1]);
            for (var i = 1; i < points.length; i++)
                ctx.lineTo(points[i][0], points[i][1]);
            ctx.stroke();
        }
        // A half circle over the top, from its left end to its right end.
        function arch() {
            ctx.beginPath();
            ctx.arc(12, 12, 7, Math.PI, 2 * Math.PI, false);
            ctx.stroke();
        }
        // An end of the arch carries on downwards and ends in a head: the
        // turn goes round the circle that way.
        function headRight() {
            line(19, 12, 19, 19);
            polyline([[15, 15], [19, 19], [23, 15]]);
        }
        function headLeft() {
            line(5, 12, 5, 19);
            polyline([[1, 15], [5, 19], [9, 15]]);
        }

        switch (root.kind) {
        case "right":
            line(4, 12, 20, 12);
            polyline([[14, 6], [20, 12], [14, 18]]);
            break;
        case "left":
            line(4, 12, 20, 12);
            polyline([[10, 6], [4, 12], [10, 18]]);
            break;
        case "up":
            line(12, 20, 12, 4);
            polyline([[6, 10], [12, 4], [18, 10]]);
            break;
        case "down":
            line(12, 4, 12, 20);
            polyline([[6, 14], [12, 20], [18, 14]]);
            break;
        case "cw":
            // Over the top from the left to the right is clockwise.
            arch();
            headRight();
            break;
        case "ccw":
            arch();
            headLeft();
            break;
        case "either":
            arch();
            headRight();
            headLeft();
            break;
        }
    }
}
