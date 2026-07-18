import QtQuick
import Cubeulator

// Orthographic technical-drawing cube: top, right and left faces only,
// projected with pt(i,j,k) = ((i-j)*h, (i+j)*0.5*u + k*u), h = cos(30deg)*u.
// Ported directly from the design handoff's support.js Component.cube().
Canvas {
    id: root
    renderStrategy: Canvas.Cooperative

    property var topColors: null    // array of 9 colors, or null to hide the face
    property var rightColors: null
    property var leftColors: null
    property bool showArrow: false
    property real arrowOpacity: 1.0

    readonly property color ink: Theme.ink
    readonly property color emptyStroke: Theme.neutral500
    readonly property color arrowColor: Theme.accent

    onTopColorsChanged: requestPaint()
    onRightColorsChanged: requestPaint()
    onLeftColorsChanged: requestPaint()
    onShowArrowChanged: requestPaint()
    onArrowOpacityChanged: requestPaint()
    onWidthChanged: requestPaint()
    onHeightChanged: requestPaint()

    onPaint: {
            var ctx = getContext("2d");
            ctx.reset();
            ctx.clearRect(0, 0, width, height);
            if (width <= 0 || height <= 0)
                return;

            var cos30 = 0.8660254;
            var vbW = 6 * cos30 + 1.9;   // per unit u=1
            var vbH = 6 + 1.9;
            var u = Math.min(width / vbW, height / vbH);
            if (u <= 0)
                return;
            var h = cos30 * u;
            var pad = u * 0.95;
            var ht = 6 * u;

            var originX = width / 2;
            var originY = (height - (ht + 2 * pad)) / 2 + pad;

            function P(i, j, k) {
                return [originX + (i - j) * h, originY + (i + j) * 0.5 * u + k * u];
            }

            function poly(pts) {
                ctx.beginPath();
                ctx.moveTo(pts[0][0], pts[0][1]);
                for (var n = 1; n < pts.length; n++)
                    ctx.lineTo(pts[n][0], pts[n][1]);
                ctx.closePath();
            }

            function cell(pts, color) {
                poly(pts);
                if (color) {
                    ctx.fillStyle = color;
                    ctx.fill();
                    ctx.setLineDash([]);
                    ctx.strokeStyle = root.ink;
                    ctx.lineWidth = 1;
                    ctx.stroke();
                } else {
                    ctx.setLineDash([3, 3]);
                    ctx.strokeStyle = root.emptyStroke;
                    ctx.lineWidth = 1;
                    ctx.stroke();
                }
            }

            function outline(pts) {
                poly(pts);
                ctx.setLineDash([]);
                ctx.strokeStyle = root.ink;
                ctx.lineWidth = 2.4;
                ctx.stroke();
            }

            // Hidden edges (dashed) from the rear-bottom vertex.
            var Hv = P(0, 0, 3);
            ctx.setLineDash([4, 4]);
            ctx.strokeStyle = root.ink;
            ctx.lineWidth = 1.3;
            ctx.globalAlpha = 0.6;
            [P(0, 0, 0), P(3, 0, 3), P(0, 3, 3)].forEach(function (a) {
                ctx.beginPath();
                ctx.moveTo(Hv[0], Hv[1]);
                ctx.lineTo(a[0], a[1]);
                ctx.stroke();
            });
            ctx.globalAlpha = 1;

            var top = root.topColors, right = root.rightColors, left = root.leftColors;
            for (var i = 0; i < 3; i++)
                for (var j = 0; j < 3; j++)
                    cell([P(i, j, 0), P(i + 1, j, 0), P(i + 1, j + 1, 0), P(i, j + 1, 0)], top ? top[j * 3 + i] : null);
            for (j = 0; j < 3; j++)
                for (var k = 0; k < 3; k++)
                    cell([P(3, j, k), P(3, j + 1, k), P(3, j + 1, k + 1), P(3, j, k + 1)], right ? right[k * 3 + j] : null);
            for (i = 0; i < 3; i++)
                for (k = 0; k < 3; k++)
                    cell([P(i, 3, k), P(i + 1, 3, k), P(i + 1, 3, k + 1), P(i, 3, k + 1)], left ? left[k * 3 + i] : null);

            outline([P(0, 0, 0), P(3, 0, 0), P(3, 3, 0), P(0, 3, 0)]);
            outline([P(3, 0, 0), P(3, 3, 0), P(3, 3, 3), P(3, 0, 3)]);
            outline([P(0, 3, 0), P(3, 3, 0), P(3, 3, 3), P(0, 3, 3)]);

            if (root.showArrow) {
                var cy = 1.5 * u, rx = 2.4 * h, ry = 2.4 * 0.5 * u;
                var s = [originX - rx, originY + cy];
                var en = [originX + rx * 0.15, originY + cy - ry];

                // SVG endpoint-to-center arc parameterization (rotation = 0,
                // large-arc-flag = 1, sweep-flag = 1) so the arc matches the
                // design's "A rx ry 0 1 1 x y" path exactly.
                var dx = (s[0] - en[0]) / 2, dy = (s[1] - en[1]) / 2;
                var rxSq = rx * rx, rySq = ry * ry, dxSq = dx * dx, dySq = dy * dy;
                var num = rxSq * rySq - rxSq * dySq - rySq * dxSq;
                var den = rxSq * dySq + rySq * dxSq;
                var co = Math.sqrt(Math.max(0, num / den));
                // large-arc-flag == sweep-flag (both 1) => negative sign
                var cxp = -co * (rx * dy / ry);
                var cyp = -co * (-ry * dx / rx);
                var cx = cxp + (s[0] + en[0]) / 2;
                var ccy = cyp + (s[1] + en[1]) / 2;

                function angle(ux, uy, vx, vy) {
                    var dot = ux * vx + uy * vy;
                    var len = Math.sqrt((ux * ux + uy * uy) * (vx * vx + vy * vy));
                    var ang = Math.acos(Math.max(-1, Math.min(1, dot / len)));
                    if (ux * vy - uy * vx < 0)
                        ang = -ang;
                    return ang;
                }
                var startAngle = angle(1, 0, (s[0] - cx) / rx, (s[1] - ccy) / ry);
                var deltaAngle = angle((s[0] - cx) / rx, (s[1] - ccy) / ry, (en[0] - cx) / rx, (en[1] - ccy) / ry);
                if (deltaAngle < 0)
                    deltaAngle += 2 * Math.PI; // sweep-flag = 1 => positive direction
                var endAngle = startAngle + deltaAngle;

                ctx.save();
                ctx.globalAlpha = root.arrowOpacity;
                ctx.setLineDash([]);
                ctx.strokeStyle = root.arrowColor;
                ctx.lineWidth = 3.2;
                ctx.lineCap = "round";
                ctx.beginPath();
                ctx.ellipse(cx, ccy, rx, ry, 0, startAngle, endAngle, false);
                ctx.stroke();

                // Arrowhead at the arc's end point.
                ctx.beginPath();
                ctx.moveTo(en[0] - 7, en[1] + 1);
                ctx.lineTo(en[0] + 7, en[1] - 5);
                ctx.lineTo(en[0] + 1, en[1] + 9);
                ctx.closePath();
                ctx.fillStyle = root.arrowColor;
                ctx.fill();
                ctx.restore();
            }
        }
    }
