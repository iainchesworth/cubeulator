import QtQuick
import Cubeulator

// Orthographic technical-drawing cube: top, right and left faces only,
// projected with pt(i,j,k) = ((i-j)*h, (i+j)*0.5*u + k*u), h = cos(30deg)*u.
// Ported directly from the design handoff's support.js Component.cube().
//
// Face mapping (confirmed for the solve-visualization work): top=U,
// right=R, left=F - the standard "look at the cube from above/front-right"
// iso view. D/L/B are never drawn; a move on one of them shows the hidden-
// face badge below instead of a (misleading) on-cube arrow.
Canvas {
    id: root
    renderStrategy: Canvas.Cooperative

    property var topColors: null    // array of 9 colors, or null to hide the face
    property var rightColors: null
    property var leftColors: null
    property bool showArrow: false
    property real arrowOpacity: 1.0
    // The move currently being illustrated: a face letter (U/D/L/R/F/B, or
    // "" for none) and a turn count (1 = CW quarter, 2 = half, 3 = CCW
    // quarter), matching cube::solver::Move's convention exactly (see
    // SolveController).
    property string currentMoveFace: ""
    property int currentMoveTurns: 1

    readonly property color ink: Theme.ink
    readonly property color emptyStroke: Theme.neutral500
    readonly property color arrowColor: Theme.accent

    // Seam for the future 3D-cube worktree: a move currently just snaps
    // (sticker props update, requestPaint() redraws instantly - see the
    // solve-visualization plan's "lightweight/snap for now" call). Call
    // this alongside advancing SolveController so callers don't need to
    // change when real rotation animation lands here later; today it's a
    // brief opacity flash, not a rotation.
    function animateMove() {
        flashAnimation.restart();
    }
    SequentialAnimation {
        id: flashAnimation
        NumberAnimation { target: root; property: "opacity"; to: 0.55; duration: 60 }
        NumberAnimation { target: root; property: "opacity"; to: 1.0; duration: 120 }
    }

    onTopColorsChanged: requestPaint()
    onRightColorsChanged: requestPaint()
    onLeftColorsChanged: requestPaint()
    onShowArrowChanged: requestPaint()
    onArrowOpacityChanged: requestPaint()
    onCurrentMoveFaceChanged: requestPaint()
    onCurrentMoveTurnsChanged: requestPaint()
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

            if (!root.showArrow || root.currentMoveFace === "")
                return;

            // Screen-space delta per unit step along each of the cube's 3
            // axes, at this scale - the same partial derivatives of P()
            // used to project cells above, reused here to build an arc
            // that follows each visible face's own (skewed, isometric)
            // orientation rather than a plain axis-aligned ellipse.
            var dI = [h, 0.5 * u];
            var dJ = [-h, 0.5 * u];
            var dK = [0, u];

            // basisA/basisB order matters, not just which two axes: the arc
            // below walks cos(t)*basisA + sin(t)*basisB, and swapping the
            // pair mirrors its screen-space sweep direction. dK points
            // straight down while dI/dJ both slope outward-and-down, so
            // (dI,dJ) and (dI,dK) share one screen-rotation handedness but
            // (dJ,dK) has the other - R uses (dK,dJ), not (dJ,dK), so that
            // the same clockwise/CCW sweep sign below actually reads as the
            // same on-screen direction across all three visible faces.
            function faceGeometry(faceLetter) {
                if (faceLetter === "U")
                    return {center: P(1.5, 1.5, 0), basisA: dI, basisB: dJ};
                if (faceLetter === "R")
                    return {center: P(3, 1.5, 1.5), basisA: dK, basisB: dJ};
                if (faceLetter === "F")
                    return {center: P(1.5, 3, 1.5), basisA: dI, basisB: dK};
                return null;
            }

            var geometry = faceGeometry(root.currentMoveFace);

            if (geometry === null) {
                // Hidden-face fallback: a move on D/L/B can't be shown on
                // this camera-fixed 3-face view without misleading the
                // user, so a distinct dashed badge (not an arrow) marks
                // that the move happens on a face not currently in frame -
                // the move-notation text shown alongside CubeView is the
                // actual instruction.
                var bx = width / 2, by = height - u * 0.65;
                var br = u * 0.5;
                ctx.save();
                ctx.globalAlpha = root.arrowOpacity;
                ctx.setLineDash([3, 3]);
                ctx.strokeStyle = root.emptyStroke;
                ctx.lineWidth = 1.5;
                ctx.beginPath();
                ctx.arc(bx, by, br, 0.4, Math.PI * 1.7);
                ctx.stroke();

                var endAngle = Math.PI * 1.7;
                var hx = bx + br * Math.cos(endAngle), hy = by + br * Math.sin(endAngle);
                var tx = -Math.sin(endAngle), ty = Math.cos(endAngle);
                ctx.setLineDash([]);
                ctx.beginPath();
                ctx.moveTo(hx + tx * 6, hy + ty * 6);
                ctx.lineTo(hx - ty * 5, hy + tx * 5);
                ctx.lineTo(hx + ty * 5, hy - tx * 5);
                ctx.closePath();
                ctx.fillStyle = root.emptyStroke;
                ctx.fill();

                ctx.fillStyle = root.ink;
                ctx.font = "bold " + Math.round(br * 0.7) + "px sans-serif";
                ctx.textAlign = "center";
                ctx.textBaseline = "middle";
                ctx.fillText(root.currentMoveFace, bx, by);
                ctx.restore();
                return;
            }

            var clockwise = root.currentMoveTurns !== 3;  // turns: 1=CW, 2=half(shown CW+"2"), 3=CCW
            var startAngle = -0.35;
            var sweep = (clockwise ? 1 : -1) * 4.5;  // ~258 degrees, matching the design's large-arc look
            var endA = startAngle + sweep;
            var steps = 40;
            var pts = [];
            for (var s = 0; s <= steps; s++) {
                var t = startAngle + (endA - startAngle) * (s / steps);
                var ct = Math.cos(t), st = Math.sin(t);
                pts.push([
                    geometry.center[0] + 0.62 * (ct * geometry.basisA[0] + st * geometry.basisB[0]),
                    geometry.center[1] + 0.62 * (ct * geometry.basisA[1] + st * geometry.basisB[1])
                ]);
            }

            ctx.save();
            ctx.globalAlpha = root.arrowOpacity;
            ctx.setLineDash([]);
            ctx.strokeStyle = root.arrowColor;
            ctx.lineWidth = 3.2;
            ctx.lineCap = "round";
            ctx.beginPath();
            ctx.moveTo(pts[0][0], pts[0][1]);
            for (s = 1; s < pts.length; s++)
                ctx.lineTo(pts[s][0], pts[s][1]);
            ctx.stroke();

            var tail = pts[pts.length - 2], head = pts[pts.length - 1];
            var dx = head[0] - tail[0], dy = head[1] - tail[1];
            var len = Math.sqrt(dx * dx + dy * dy) || 1;
            dx /= len; dy /= len;
            var nx = -dy, ny = dx;
            var hs = 9;
            ctx.beginPath();
            ctx.moveTo(head[0] + dx * hs, head[1] + dy * hs);
            ctx.lineTo(head[0] - dx * hs * 0.3 + nx * hs * 0.7, head[1] - dy * hs * 0.3 + ny * hs * 0.7);
            ctx.lineTo(head[0] - dx * hs * 0.3 - nx * hs * 0.7, head[1] - dy * hs * 0.3 - ny * hs * 0.7);
            ctx.closePath();
            ctx.fillStyle = root.arrowColor;
            ctx.fill();

            if (root.currentMoveTurns === 2) {
                ctx.fillStyle = root.arrowColor;
                ctx.font = "bold 14px sans-serif";
                ctx.textAlign = "center";
                ctx.textBaseline = "middle";
                ctx.fillText("2×", geometry.center[0], geometry.center[1]);
            }
            ctx.restore();
        }
    }
