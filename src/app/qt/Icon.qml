import QtQuick
import Cubeulator

// Minimal line-icon set (2px stroke, no fill) matching the Lucide icons
// used in the design handoff. Coordinates are in a 24x24 viewBox, scaled
// to fill the item.
Canvas {
    id: root
    property string name: ""
    property color color: Theme.ink
    property real strokeWidth: 2

    onNameChanged: requestPaint()
    onColorChanged: requestPaint()
    onWidthChanged: requestPaint()
    onHeightChanged: requestPaint()

    onPaint: {
        var ctx = getContext("2d");
        ctx.reset();
        ctx.clearRect(0, 0, width, height);
        if (width <= 0 || height <= 0)
            return;

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
        function circle(cx, cy, r) {
            ctx.beginPath();
            ctx.arc(cx, cy, r, 0, 2 * Math.PI);
            ctx.stroke();
        }
        function polyline(pts) {
            ctx.beginPath();
            ctx.moveTo(pts[0][0], pts[0][1]);
            for (var i = 1; i < pts.length; i++)
                ctx.lineTo(pts[i][0], pts[i][1]);
            ctx.stroke();
        }

        switch (root.name) {
        case "camera":
            ctx.beginPath();
            ctx.moveTo(14.5, 4); ctx.lineTo(9.5, 4); ctx.lineTo(7, 7); ctx.lineTo(4, 7);
            ctx.bezierCurveTo(2.9, 7, 2, 7.9, 2, 9);
            ctx.lineTo(2, 18);
            ctx.bezierCurveTo(2, 19.1, 2.9, 20, 4, 20);
            ctx.lineTo(20, 20);
            ctx.bezierCurveTo(21.1, 20, 22, 19.1, 22, 18);
            ctx.lineTo(22, 9);
            ctx.bezierCurveTo(22, 7.9, 21.1, 7, 20, 7);
            ctx.lineTo(17, 7); ctx.closePath();
            ctx.stroke();
            circle(12, 13, 3.2);
            break;
        case "chevron-right":
            polyline([[9, 6], [15, 12], [9, 18]]);
            break;
        case "check":
            polyline([[20, 6], [9, 17], [4, 12]]);
            break;
        case "globe":
            circle(12, 12, 9);
            line(3, 12, 21, 12);
            ctx.beginPath();
            ctx.moveTo(12, 3);
            ctx.bezierCurveTo(14.5, 5.4, 15.8, 8.6, 15.8, 12);
            ctx.bezierCurveTo(15.8, 15.4, 14.5, 18.6, 12, 21);
            ctx.bezierCurveTo(9.5, 18.6, 8.2, 15.4, 8.2, 12);
            ctx.bezierCurveTo(8.2, 8.6, 9.5, 5.4, 12, 3);
            ctx.closePath();
            ctx.stroke();
            break;
        case "menu":
            line(4, 7, 20, 7);
            line(4, 12, 20, 12);
            line(4, 17, 20, 17);
            break;
        case "alert-triangle":
            ctx.beginPath();
            ctx.moveTo(12, 3); ctx.lineTo(2, 20); ctx.lineTo(22, 20); ctx.closePath();
            ctx.stroke();
            line(12, 10, 12, 14);
            line(12, 17, 12, 17.01);
            break;
        case "sun":
            circle(12, 12, 4);
            [[12, 2, 12, 5], [12, 19, 12, 22], [2, 12, 5, 12], [19, 12, 22, 12],
             [5, 5, 7, 7], [17, 17, 19, 19], [19, 5, 17, 7], [7, 17, 5, 19]].forEach(function (p) {
                line(p[0], p[1], p[2], p[3]);
            });
            break;
        case "help-circle":
            circle(12, 12, 9);
            ctx.beginPath();
            ctx.moveTo(9.5, 9);
            ctx.bezierCurveTo(9.5, 7, 12, 6.3, 13, 8);
            ctx.bezierCurveTo(13.7, 9.2, 13, 9.8, 12, 10.7);
            ctx.bezierCurveTo(11.3, 11.3, 11, 11.7, 11, 12.7);
            ctx.stroke();
            line(12, 17, 12, 17.01);
            break;
        case "refresh":
            ctx.beginPath();
            ctx.arc(12, 12, 9, -Math.PI / 2, Math.PI * 1.15, false);
            ctx.stroke();
            polyline([[21, 4], [21, 9], [16, 9]]);
            break;
        }
    }
}
