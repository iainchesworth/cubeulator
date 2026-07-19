import QtQuick
import Cubeulator

Rectangle {
    id: root
    color: "#2D2B2B"
    readonly property bool wide: width >= Theme.wideBreakpoint

    readonly property int sc: AppState.scanned

    // Free-form scanning: the user shows faces in ANY order, at ANY
    // rotation - identity is worked out from centre colours when the sixth
    // distinct face lands, so the prompt never names a face or a holding
    // convention, it only reports the running count.
    readonly property string promptPrimary: ScanSession.cornerDetected
        ? "Hold steady — reading three faces…"
        : "Show me any face — " + sc + " of 6 captured"
    readonly property string promptSecondary: ScanSession.cornerDetected ? ""
        : "or show a corner — three faces at once"

    // Paint map for the 3D progress cube: slotSummaries lists each captured
    // slot as {face, colors} (identity keyed off the centre colour). Faces
    // not yet captured stay absent from the map, so the skeleton Cube3DView
    // renders them as dim placeholder sockets - the "empty cube filling in"
    // progress metaphor, replacing the old flat isometric sketch.
    readonly property var scanFaceletColors: {
        var map = {};
        var slots = ScanSession.slotSummaries;
        for (var i = 0; i < slots.length; i++) {
            if (slots[i] && slots[i].face && slots[i].colors && slots[i].colors.length === 9)
                map[slots[i].face] = slots[i].colors;
        }
        return map;
    }

    // Live pose for the progress cube, or null while detection has nothing.
    // Null lets the cube idle-spin as "searching" feedback; a quaternion
    // makes it rotate in lockstep with the physical cube - the pose visibly
    // driving the render is what tells the user tracking actually works.
    readonly property var liveOrientation: (ScanSession.cubeOrientation
        && ScanSession.cubeOrientation.length === 4) ? ScanSession.cubeOrientation : null

    // Check-glyph colour for a capture chip: slot centre colours span
    // near-white (U) to dark blue (B), so one fixed glyph colour would be
    // illegible on one end or the other.
    function chipCheckColor(c) {
        var col = Qt.color(c);
        return (0.299 * col.r + 0.587 * col.g + 0.114 * col.b) > 0.6 ? "#201E1D" : "#FFFFFF";
    }

    // Two recoverable-failure banners sharing one strip. scanRetryMessage:
    // six captures didn't resolve/assemble into a buildable cube
    // (ScanSession.scanNeedsRetry) - the suspect slot is already dropped
    // and scanning simply continues, so the wording is "keep going", not
    // "start over". scanError: SolveController's legacy fallback path
    // rejected a completed scan. Slots carry no identity, so neither can
    // name WHICH face to re-show - the wording stays generic.
    Rectangle {
        id: scanErrorBanner
        visible: AppState.scanError !== "" || AppState.scanRetryMessage !== ""
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: 34
        color: Theme.warning
        z: 10
        Row {
            anchors.centerIn: parent
            spacing: 6
            Icon { name: "alert-triangle"; color: "white"; width: 14; height: 14; strokeWidth: 2.4 }
            Text {
                text: AppState.scanRetryMessage !== "" ? AppState.scanRetryMessage
                    : "Scan didn't look right — keep scanning, show faces again"
                color: "white"; font.pixelSize: 12; font.family: Theme.fontHeading
            }
        }
    }

    // Tracking-state chip: capture is identity-driven now, so the states
    // where commits silently pause deserve honest, low-key feedback - the
    // tracker lost the cube mid-scan (only worth saying once captures
    // exist to protect), it is coasting through a detection gap, or a
    // recovered track is re-associating its captures ("re-orienting").
    // Advisory styling like the quality hints: no warning colour, no alert
    // icon - nothing is wrong, scanning just needs the cube back.
    Rectangle {
        id: trackingChip
        visible: ScanSession.running && (ScanSession.reorienting
            || (ScanSession.facesScanned > 0
                && (ScanSession.trackingState === "lost" || ScanSession.trackingState === "coasting")))
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: scanErrorBanner.visible ? scanErrorBanner.bottom : parent.top
        anchors.topMargin: Theme.space2
        width: trackingChipText.width + 24
        height: 24
        radius: 12
        color: Qt.rgba(0, 0, 0, 0.55)
        z: 9
        Text {
            id: trackingChipText
            anchors.centerIn: parent
            text: ScanSession.reorienting ? "Re-orienting — hold the cube in view"
                : ScanSession.trackingState === "lost" ? "Tracking lost — show the cube again"
                : "Re-acquiring…"
            color: "#F3F2F2"; font.pixelSize: 11; font.family: Theme.fontHeading
        }
    }

    // Capture celebration: a capture used to reset the whole overlay in a
    // single frame - a confusing flash. A deliberate green pulse plus a
    // lingering "captured" chip makes the moment legible. Multiple
    // faceConfirmed signals from one corner capture just restart the same
    // animation and read as a single celebration. faceIndex is a slot
    // index with no face identity (free-form capture), so the chip's
    // wording is deliberately generic.
    property var captureFlashConnections: Connections {
        target: ScanSession
        function onFaceConfirmed(faceIndex) {
            captureCelebration.restart();
        }
    }

    Component {
        id: captureFlash
        Item {
            anchors.fill: parent
            // Green pulse peaking mid-ramp (parabola in celebrationTick),
            // gone once the chip is fully up - a visible "got it" without
            // whiting out the video.
            Rectangle {
                anchors.fill: parent
                color: "#3A9D5D"
                opacity: root.celebrationTick * (1 - root.celebrationTick) * 0.8
            }
            Rectangle {
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.top: parent.top
                anchors.topMargin: Theme.space4
                width: capturedRow.width + 28
                height: 30
                radius: 4
                color: "#3A9D5D"
                opacity: root.celebrationTick
                visible: root.celebrationTick > 0.01
                Row {
                    id: capturedRow
                    anchors.centerIn: parent
                    spacing: 6
                    Icon { name: "check"; color: "white"; width: 14; height: 14; strokeWidth: 2.6 }
                    Text {
                        text: "Face captured"
                        color: "white"; font.pixelSize: 12; font.family: Theme.fontHeading
                    }
                }
            }
        }
    }

    // One shared timeline both layouts' flash overlays read from: ramps to
    // 1 fast, holds, fades - total ~1.4s of unmistakable feedback.
    property real celebrationTick: 0
    property var captureCelebration: SequentialAnimation {
        NumberAnimation { target: root; property: "celebrationTick"; from: 0; to: 1; duration: 180; easing.type: Easing.OutQuad }
        PauseAnimation { duration: 850 }
        NumberAnimation { target: root; property: "celebrationTick"; to: 0; duration: 400; easing.type: Easing.InQuad }
    }

    // Projection of the actual detected geometry, repainted per reading and
    // per frame so quads track the cube. Lives inside the mirrored item, so
    // it draws in unmirrored full-frame pixel coordinates and gets flipped
    // together with the video - no per-point mirroring needed here.
    Component {
        id: arOverlay
        Canvas {
            id: overlayCanvas
            visible: ScanSession.running
            onWidthChanged: requestPaint()
            onHeightChanged: requestPaint()
            Connections {
                target: ScanSession
                function onReadingChanged() { overlayCanvas.requestPaint(); }
                function onFrameUpdated() { overlayCanvas.requestPaint(); }
            }
            onPaint: {
                var ctx = getContext("2d");
                ctx.reset();
                ctx.clearRect(0, 0, width, height);
                var fw = ScanSession.frameWidth;
                var fh = ScanSession.frameHeight;
                if (fw <= 0 || fh <= 0 || width <= 0 || height <= 0)
                    return;

                // Persistence fade: held (post-blip) geometry draws dimmer
                // and dimmer instead of the overlay strobing on and off at
                // the per-frame detection rate.
                var freshness = ScanSession.overlayFreshness;
                if (freshness <= 0)
                    return;
                ctx.globalAlpha = freshness;

                // The video Image uses PreserveAspectCrop, so frame pixels
                // map through the larger of the two axis scales with the
                // overflow cropped symmetrically.
                var s = Math.max(width / fw, height / fh);
                var ox = (width - fw * s) / 2;
                var oy = (height - fh * s) / 2;

                // Bounding box over every quad outline drawn, so the
                // capture-progress arc can circle whatever is being read
                // (one face quad, or all three corner slots).
                var bx0 = 1e9, by0 = 1e9, bx1 = -1e9, by1 = -1e9;
                function quadPath(pts, off) {
                    ctx.beginPath();
                    for (var i = 0; i < 4; i++) {
                        var x = pts[off + i * 2] * s + ox;
                        var y = pts[off + i * 2 + 1] * s + oy;
                        if (i === 0) ctx.moveTo(x, y); else ctx.lineTo(x, y);
                        bx0 = Math.min(bx0, x); by0 = Math.min(by0, y);
                        bx1 = Math.max(bx1, x); by1 = Math.max(by1, y);
                    }
                    ctx.closePath();
                }
                // Corner slots come as one quad each (no per-cell geometry),
                // so the 3x3 cells are recovered by bilinear interpolation
                // over the quad, assuming TL,TR,BR,BL point order.
                function subCellPath(pts, off, r, c) {
                    function at(u, v, k) {
                        var top = (1 - u) * pts[off + k] + u * pts[off + 2 + k];
                        var bot = (1 - u) * pts[off + 6 + k] + u * pts[off + 4 + k];
                        return ((1 - v) * top + v * bot) * s + (k === 0 ? ox : oy);
                    }
                    var u0 = c / 3, u1 = (c + 1) / 3;
                    var v0 = r / 3, v1 = (r + 1) / 3;
                    ctx.beginPath();
                    ctx.moveTo(at(u0, v0, 0), at(u0, v0, 1));
                    ctx.lineTo(at(u1, v0, 0), at(u1, v0, 1));
                    ctx.lineTo(at(u1, v1, 0), at(u1, v1, 1));
                    ctx.lineTo(at(u0, v1, 0), at(u0, v1, 1));
                    ctx.closePath();
                }

                var progress = ScanSession.captureProgress || 0;
                // Sub-second capture cue: outlines thicken and brighten as
                // the capture accumulates, so the read feels live rather
                // than instantaneous-or-stuck.
                var outlineWidth = 2 + progress * 2.5;
                var outlineColor = Qt.rgba(1, 1, 1, 0.8 + progress * 0.2);

                if (ScanSession.cornerDetected && ScanSession.cornerQuadPoints.length === 24) {
                    var cpts = ScanSession.cornerQuadPoints;
                    var ccol = ScanSession.cornerCellColors;
                    for (var slot = 0; slot < 3; slot++) {
                        for (var r = 0; r < 3; r++) {
                            for (var c = 0; c < 3; c++) {
                                var col = ccol[slot * 9 + r * 3 + c];
                                if (!col) continue;
                                subCellPath(cpts, slot * 8, r, c);
                                ctx.globalAlpha = 0.35 * freshness;
                                ctx.fillStyle = col;
                                ctx.fill();
                            }
                        }
                        ctx.globalAlpha = freshness;
                        quadPath(cpts, slot * 8);
                        ctx.lineWidth = outlineWidth;
                        ctx.strokeStyle = outlineColor;
                        ctx.stroke();
                    }
                } else {
                    var cq = ScanSession.cellQuadPoints;
                    if (cq.length === 72) {
                        for (var cell = 0; cell < 9; cell++) {
                            var cellCol = ScanSession.cellColors[cell];
                            var conf = ScanSession.cellConfidence[cell] || 0;
                            quadPath(cq, cell * 8);
                            if (cellCol) {
                                ctx.globalAlpha = (0.25 + conf * 0.4) * freshness;
                                ctx.fillStyle = cellCol;
                                ctx.fill();
                            }
                            ctx.globalAlpha = freshness;
                            ctx.lineWidth = 1;
                            ctx.strokeStyle = Qt.rgba(1, 1, 1, 0.35);
                            ctx.stroke();
                        }
                    }
                    var fq = ScanSession.faceQuadPoints;
                    if (fq.length === 8) {
                        quadPath(fq, 0);
                        ctx.lineWidth = outlineWidth;
                        ctx.strokeStyle = outlineColor;
                        ctx.stroke();
                    }
                }

                // 3D pose wireframe over the cell fills: the eight projected
                // cube vertices (near face 0-3, rear face 4-7 - see
                // ScanSession.cubeVertexPoints). Rear edges drawn first and
                // fainter so the near face visibly sits in front - that depth
                // cue is what makes it read as a tracked object rather than a
                // second flat outline. Display-only; the pose does not feed
                // face identification yet.
                var vp = ScanSession.cubeVertexPoints;
                if (vp.length === 16) {
                    function edge(a, b) {
                        ctx.beginPath();
                        ctx.moveTo(vp[a * 2] * s + ox, vp[a * 2 + 1] * s + oy);
                        ctx.lineTo(vp[b * 2] * s + ox, vp[b * 2 + 1] * s + oy);
                        ctx.stroke();
                    }
                    ctx.lineWidth = 1.5;
                    ctx.strokeStyle = Qt.rgba(1, 1, 1, 0.35);
                    edge(4, 5); edge(5, 6); edge(6, 7); edge(7, 4);
                    ctx.lineWidth = 2.5;
                    ctx.strokeStyle = Qt.rgba(1, 1, 1, 0.9);
                    edge(0, 1); edge(1, 2); edge(2, 3); edge(3, 0);
                    edge(0, 4); edge(1, 5); edge(2, 6); edge(3, 7);
                }

                if (progress > 0.01 && bx1 > bx0) {
                    var cx = (bx0 + bx1) / 2;
                    var cy = (by0 + by1) / 2;
                    var radius = Math.max(bx1 - bx0, by1 - by0) * 0.5 + 16;
                    ctx.beginPath();
                    ctx.arc(cx, cy, radius, -Math.PI / 2,
                            -Math.PI / 2 + progress * 2 * Math.PI, false);
                    ctx.lineWidth = 3;
                    ctx.strokeStyle = Theme.detected;
                    ctx.globalAlpha = 0.9 * freshness;
                    ctx.stroke();
                    ctx.globalAlpha = freshness;
                }
            }
        }
    }

    // Framing guide only - shown when detection has nothing to project
    // (no face, no corner), never alongside the real AR overlay.
    Component {
        id: reticle
        Item {
            property real size: 170
            width: size; height: size
            SequentialAnimation on opacity {
                loops: Animation.Infinite
                NumberAnimation { from: 0.4; to: 0.95; duration: 900; easing.type: Easing.InOutQuad }
                NumberAnimation { from: 0.95; to: 0.4; duration: 900; easing.type: Easing.InOutQuad }
            }
            readonly property real bracket: size * 0.15
            Item { anchors.top: parent.top; anchors.left: parent.left; width: bracket; height: bracket
                Rectangle { width: parent.width; height: 3; color: "#F3F2F2" }
                Rectangle { width: 3; height: parent.height; color: "#F3F2F2" } }
            Item { anchors.top: parent.top; anchors.right: parent.right; width: bracket; height: bracket
                Rectangle { anchors.top: parent.top; width: parent.width; height: 3; color: "#F3F2F2" }
                Rectangle { anchors.right: parent.right; width: 3; height: parent.height; color: "#F3F2F2" } }
            Item { anchors.bottom: parent.bottom; anchors.left: parent.left; width: bracket; height: bracket
                Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 3; color: "#F3F2F2" }
                Rectangle { width: 3; height: parent.height; color: "#F3F2F2" } }
            Item { anchors.bottom: parent.bottom; anchors.right: parent.right; width: bracket; height: bracket
                Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 3; color: "#F3F2F2" }
                Rectangle { anchors.right: parent.right; width: 3; height: parent.height; color: "#F3F2F2" } }
        }
    }

    // Advisory quality hints - scanning is never blocked on these, so no
    // warning colour, no alert icon, no pulse, and the wording stays short.
    Component {
        id: missingHint
        Rectangle {
            width: hintText.width + 24; height: 26
            color: Qt.rgba(0, 0, 0, 0.55)
            Text {
                id: hintText
                anchors.centerIn: parent
                text: !ScanSession.cubeDetected ? "Show the whole cube"
                    : !ScanSession.isSteady ? "Hold steadier"
                    : "Hand in view"
                color: "#F3F2F2"; font.pixelSize: 12; font.family: Theme.fontHeading
            }
        }
    }

    Component {
        id: confirmRetakeRow
        Row {
            spacing: Theme.space2
            AppButton { variant: "secondary"; text: "Retake"; onClicked: ScanSession.retakeFace() }
            AppButton { variant: "primary"; text: "Confirm"; trailingIcon: "chevron-right"; onClicked: ScanSession.confirmFace() }
        }
    }

    // Per-slot capture chip: a captured slot shows its centre colour with a
    // check - slots are keyed by centre colour, so the colour IS the slot's
    // identity to the user ("green's done"), a real upgrade over the old
    // anonymous pips. An uncaptured slot draws as a dashed outline: an
    // explicitly empty socket, not just a dim square.
    Component {
        id: slotChip
        Item {
            property var slotInfo: null // {face, colors} | null
            Rectangle {
                anchors.fill: parent
                visible: slotInfo !== null
                radius: 6
                color: slotInfo ? slotInfo.colors[4] : "transparent"
                Icon {
                    anchors.centerIn: parent
                    name: "check"; width: 14; height: 14; strokeWidth: 2.6
                    color: slotInfo ? root.chipCheckColor(slotInfo.colors[4]) : "#FFFFFF"
                }
            }
            // Canvas, not Rectangle: QML Rectangle borders can't dash, and
            // the dashing is what makes an empty chip read as "awaiting a
            // face" rather than merely inactive.
            Canvas {
                anchors.fill: parent
                visible: slotInfo === null
                onVisibleChanged: requestPaint()
                onWidthChanged: requestPaint()
                onPaint: {
                    var ctx = getContext("2d");
                    ctx.reset();
                    ctx.clearRect(0, 0, width, height);
                    ctx.strokeStyle = "#9B9797";
                    ctx.lineWidth = 1.2;
                    ctx.setLineDash([3, 3]);
                    ctx.beginPath();
                    ctx.roundedRect(1, 1, width - 2, height - 2, 6, 6);
                    ctx.stroke();
                }
            }
        }
    }

    // ---- narrow ----
    Column {
        visible: !root.wide
        anchors.fill: parent

        Row {
            id: headerNarrow
            width: parent.width
            padding: Theme.space3
            Column {
                width: parent.width - 60
                spacing: 2
                Text {
                    text: root.promptPrimary
                    color: "#F3F2F2"; font.family: Theme.fontMono; font.pixelSize: 11
                    width: parent.width; wrapMode: Text.WordWrap
                }
                Text {
                    visible: text !== ""
                    text: root.promptSecondary
                    color: "#BAB6B6"; font.family: Theme.fontMono; font.pixelSize: 10
                    width: parent.width; wrapMode: Text.WordWrap
                }
            }
            Text {
                text: root.sc + " / 6"
                color: "#F3F2F2"; font.family: Theme.fontMono; font.pixelSize: 11; font.weight: Font.DemiBold
            }
        }

        Item {
            width: parent.width
            height: root.height - headerNarrow.height - 130
            clip: true

            // Video and AR overlay flip as one unit so the on-screen cube
            // moves the way the user's hands do; everything textual stays
            // outside this item so it never renders backwards.
            Item {
                id: mirrorNarrow
                anchors.fill: parent
                transform: Scale {
                    origin.x: mirrorNarrow.width / 2
                    xScale: CameraController.previewMirrored ? -1 : 1
                }
                Image {
                    anchors.fill: parent
                    fillMode: Image.PreserveAspectCrop
                    cache: false
                    visible: ScanSession.running
                    source: ScanSession.running ? "image://scanFrame/latest#" + ScanSession.frameSeq : ""
                }
                Loader { anchors.fill: parent; sourceComponent: arOverlay }
            }

            Loader { anchors.fill: parent; sourceComponent: captureFlash }

            Text {
                anchors.centerIn: parent
                visible: ScanSession.starting
                text: "Connecting to camera…"
                color: "#F3F2F2"; font.family: Theme.fontMono; font.pixelSize: 13
            }
            Loader {
                anchors.centerIn: parent
                active: !ScanSession.cubeDetected && !ScanSession.cornerDetected
                sourceComponent: reticle
            }
            // The scan-progress cube is the SAME 26-cubie 3D cube as the
            // launch screen, in skeleton mode: captured faces paint in,
            // missing faces stay translucent sockets. While a pose is
            // detected it rotates in lockstep with the physical cube;
            // otherwise it idle-spins as "searching" feedback.
            Cube3DView {
                width: 104; height: 104
                anchors.top: parent.top; anchors.right: parent.right
                anchors.margins: Theme.space2
                interactive: false
                skeleton: true
                shaderStyle: SettingsState.shaderStyleIndex
                autoRotate: root.liveOrientation === null
                externalRotation: root.liveOrientation
                faceletColors: root.scanFaceletColors
            }
            Loader {
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.bottom: parent.bottom
                anchors.bottomMargin: Theme.space3
                sourceComponent: ScanSession.starting ? null
                    : (ScanSession.readyToConfirm && !ScanSession.autoAdvance) ? confirmRetakeRow
                    : (!ScanSession.cornerDetected
                       && (!ScanSession.cubeDetected || !ScanSession.isSteady || ScanSession.handPresent)) ? missingHint
                    : null
            }
        }

        Rectangle {
            width: parent.width
            height: 130
            color: "#201E1D"

            Column {
                anchors.fill: parent
                anchors.margins: Theme.space3
                spacing: Theme.space3

                // Chips keyed on slotSummaries order: slots fill in capture
                // order, so the row always fills from the left - even when
                // a corner capture lands three slots at once.
                Row {
                    anchors.horizontalCenter: parent.horizontalCenter
                    spacing: Theme.space2
                    Repeater {
                        model: 6
                        delegate: Loader {
                            width: 28; height: 28
                            sourceComponent: slotChip
                            // Binding, not a one-shot onLoaded assignment: slotInfo must
                            // re-evaluate as slotSummaries grows, or chips never fill in.
                            Binding {
                                target: item; property: "slotInfo"
                                value: index < ScanSession.slotSummaries.length
                                    ? ScanSession.slotSummaries[index] : null
                                when: status === Loader.Ready
                            }
                        }
                    }
                }
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: root.sc + " of 6 faces captured"
                    color: "#BAB6B6"; font.family: Theme.fontMono; font.pixelSize: 11
                }
                Row {
                    width: parent.width
                    spacing: 10
                    Text { text: "CONF"; color: "#BAB6B6"; font.family: Theme.fontMono; font.pixelSize: 11; anchors.verticalCenter: parent.verticalCenter }
                    Row {
                        width: parent.width - 90
                        spacing: 4
                        Repeater {
                            model: 3
                            delegate: Rectangle {
                                readonly property real threshold: (index + 1) / 3
                                width: (parent.width - 8) / 3; height: 6
                                color: ScanSession.captureProgress >= threshold ? Theme.warning : "#4B4747"
                            }
                        }
                    }
                    Text {
                        text: !ScanSession.isSteady ? "shaky" : ScanSession.handPresent ? "hand in frame" : ""
                        color: "#C07C12"; font.family: Theme.fontMono; font.pixelSize: 11; anchors.verticalCenter: parent.verticalCenter
                    }
                }
            }
        }
    }

    // ---- wide ----
    Row {
        visible: root.wide
        anchors.fill: parent

        Item {
            width: parent.width - 250
            height: parent.height
            clip: true

            // Same mirror-as-a-unit contract as the narrow layout above.
            Item {
                id: mirrorWide
                anchors.fill: parent
                transform: Scale {
                    origin.x: mirrorWide.width / 2
                    xScale: CameraController.previewMirrored ? -1 : 1
                }
                Image {
                    anchors.fill: parent
                    fillMode: Image.PreserveAspectCrop
                    cache: false
                    visible: ScanSession.running
                    source: ScanSession.running ? "image://scanFrame/latest#" + ScanSession.frameSeq : ""
                }
                Loader { anchors.fill: parent; sourceComponent: arOverlay }
            }

            Loader { anchors.fill: parent; sourceComponent: captureFlash }

            Column {
                anchors.top: parent.top; anchors.left: parent.left; anchors.margins: Theme.space4
                width: parent.width - Theme.space4 * 2
                spacing: 2
                Text {
                    text: root.promptPrimary
                    color: "#F3F2F2"; font.family: Theme.fontMono; font.pixelSize: 13
                    width: parent.width; wrapMode: Text.WordWrap
                }
                Text {
                    visible: text !== ""
                    text: root.promptSecondary
                    color: "#BAB6B6"; font.family: Theme.fontMono; font.pixelSize: 11
                    width: parent.width; wrapMode: Text.WordWrap
                }
            }
            Text {
                anchors.centerIn: parent
                visible: ScanSession.starting
                text: "Connecting to camera…"
                color: "#F3F2F2"; font.family: Theme.fontMono; font.pixelSize: 14
            }
            Loader {
                anchors.centerIn: parent
                active: !ScanSession.cubeDetected && !ScanSession.cornerDetected
                sourceComponent: reticle
                onLoaded: item.size = 250
            }
            Loader {
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.bottom: parent.bottom
                anchors.bottomMargin: Theme.space4
                sourceComponent: ScanSession.starting ? null
                    : (ScanSession.readyToConfirm && !ScanSession.autoAdvance) ? confirmRetakeRow
                    : (!ScanSession.cornerDetected
                       && (!ScanSession.cubeDetected || !ScanSession.isSteady || ScanSession.handPresent)) ? missingHint
                    : null
            }
        }

        Rectangle {
            width: 250
            height: parent.height
            color: "#201E1D"
            Rectangle { width: 2; height: parent.height; color: "#4B4747" }

            Column {
                anchors.fill: parent
                anchors.margins: Theme.space4
                spacing: Theme.space3

                Row {
                    width: parent.width
                    Text { text: "PROGRESS"; color: "#BAB6B6"; font.pixelSize: 11; font.letterSpacing: 1; font.family: Theme.fontHeading }
                    Item { width: parent.width - 120 }
                    Text { text: root.sc + "/6"; color: "#F3F2F2"; font.family: Theme.fontMono; font.pixelSize: 18 }
                }

                // Same skeleton/pose-tracking progress cube as the narrow
                // layout's corner overlay - see the comment there.
                Cube3DView {
                    width: 180; height: 180
                    anchors.horizontalCenter: parent.horizontalCenter
                    interactive: false
                    skeleton: true
                    shaderStyle: SettingsState.shaderStyleIndex
                    autoRotate: root.liveOrientation === null
                    externalRotation: root.liveOrientation
                    faceletColors: root.scanFaceletColors
                }

                // Chips keyed on slotSummaries order: slots fill in capture
                // order, so the grid always fills from the top-left - even
                // when a corner capture lands three slots at once.
                Grid {
                    columns: 3
                    spacing: 10
                    anchors.horizontalCenter: parent.horizontalCenter
                    Repeater {
                        model: 6
                        delegate: Loader {
                            width: 34; height: 34
                            sourceComponent: slotChip
                            // Binding, not a one-shot onLoaded assignment: slotInfo must
                            // re-evaluate as slotSummaries grows, or chips never fill in.
                            Binding {
                                target: item; property: "slotInfo"
                                value: index < ScanSession.slotSummaries.length
                                    ? ScanSession.slotSummaries[index] : null
                                when: status === Loader.Ready
                            }
                        }
                    }
                }
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: root.sc + " of 6 faces captured"
                    color: "#BAB6B6"; font.family: Theme.fontMono; font.pixelSize: 11
                }

                // Clamped: the fixed content above grew (3D cube + chips +
                // label), so on shorter wide windows this spacer collapses
                // instead of going negative and folding the column.
                Item { width: 1; height: Math.max(Theme.space2, parent.height - 470) }

                Text { text: "CONFIDENCE"; color: "#BAB6B6"; font.family: Theme.fontMono; font.pixelSize: 11 }
                Row {
                    width: parent.width
                    spacing: 5
                    Repeater {
                        model: 3
                        delegate: Rectangle {
                            readonly property real threshold: (index + 1) / 3
                            width: (parent.width - 10) / 3; height: 8
                            color: ScanSession.captureProgress >= threshold ? Theme.warning : "#4B4747"
                        }
                    }
                }
                // Advisory only - none of these states block the capture.
                Text {
                    text: ScanSession.cornerDetected ? "Reading three faces at once…"
                        : !ScanSession.cubeDetected ? "Looking for the cube…"
                        : !ScanSession.isSteady ? "Hold steadier for a cleaner read"
                        : ScanSession.handPresent ? "Hand in view"
                        : ScanSession.readyToConfirm && ScanSession.autoAdvance ? "Reading confirmed — advancing…"
                        : ""
                    color: "#C07C12"; font.pixelSize: 12; font.family: Theme.fontHeading
                    width: parent.width; wrapMode: Text.WordWrap
                }
            }
        }
    }
}
