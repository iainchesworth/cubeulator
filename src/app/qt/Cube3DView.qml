import QtQuick
import QtQuick3D
import QtQuick3D.Helpers
import Cubeulator

// GPU-rendered Rubik's cube built from 26 individually-positioned "cubies"
// (the centre piece is never visible, so it's skipped), each carrying its
// own 0-3 coloured stickers. Two things fall out of modelling it this way
// rather than as one 6-face shell:
//  - a layer can really turn: its nine cubies hang off a pivot while it
//    moves and are set down where it leaves them. The solve screen uses
//    this twice over - guideFace nudges the layer the next move turns, a
//    little way and back, and playMove() turns it all the way.
//  - individual stickers can be recoloured per-facelet (faceletColors) -
//    the solve screen paints the cube as it stands this way.
// Whole-cube dragging still free-spins cubeRoot; releasing eases it back to
// the presented orientation instead of leaving it wherever it was thrown.
// When externalRotation is set (scan-screen pose tracking) drag and idle
// spin are suspended and the cube follows the supplied quaternion instead.
Item {
    id: root

    property color topColor: Theme.faceU
    property color bottomColor: Theme.faceD
    property color frontColor: Theme.faceF
    property color backColor: Theme.faceB
    property color leftColor: Theme.faceL
    property color rightColor: Theme.faceR

    property bool interactive: true
    property bool autoRotate: true
    property real autoRotateSpeed: 0.35 // degrees per animation tick while idle
    // 0 = Physically-Based Realism (default), 1 = Instructional Glow,
    // 2 = Borderlands Cel-Shading
    property int shaderStyle: 0
    property bool showStats: false

    // Per-facelet sticker painting: null keeps the uniform face colours
    // above; otherwise a JS object mapping face letter -> 9 CSS colours,
    // row-major in the faceletIndex() convention documented below, e.g.
    // { "U": [...9...], "F": [...9...] }. Faces absent from the map (or
    // null entries within an array) count as unpainted.
    property var faceletColors: null

    // A new painting is a new cube: every layer goes back where it started,
    // in the same pass that recolours the stickers. After playMove() has
    // turned a layer, painting the cube as it now stands therefore changes
    // nothing on screen - provided the turn shown was the turn made, which
    // is what cube_view_test.cpp holds this file to.
    onFaceletColorsChanged: resetLayers()

    // ---- Move guidance ----------------------------------------------------
    // The side whose layer the next move turns: "U", "D", "L", "R", "F",
    // "B", or "" for none. While set, that layer nudges a little way in the
    // direction it has to go and back, over and over. guideTurns is quarter
    // turns clockwise as seen from outside that side - 1, 2 or 3, as in
    // cube::solver::Move. A half turn nudges twice before it returns.
    property string guideFace: ""
    property int guideTurns: 1
    onGuideFaceChanged: scheduleGuide()
    onGuideTurnsChanged: scheduleGuide()

    // How far one nudge swings the layer, in degrees.
    property real nudgeAngle: 30
    // How long a quarter turn takes when playMove() turns a layer for real.
    property int turnDuration: 380
    // How much the stickers outside the layer in hand darken, 0..1, so the
    // layer that moves stands out from the rest.
    property real guideDim: 0

    // True while playMove() is turning a layer.
    property bool playing: false
    readonly property bool nudging: nudgeOnce.running || nudgeTwice.running
    // playMove()'s turn has finished and the layer is set down.
    signal movePlayed()

    // ---- How the cube is shown --------------------------------------------
    // The cube's own orientation: identity is U up, F towards the viewer and
    // R to the viewer's right. The solve screen sets this to how the cube is
    // being held.
    property quaternion holdRotation: Qt.quaternion(1, 0, 0, 0)
    // Where the cube is looked at from, in degrees: viewYaw turns it about
    // the vertical like a turntable, then viewPitch tips the top towards the
    // viewer. Applied outside homeRotation, which a screen using these
    // leaves at zero.
    property real viewPitch: 0
    property real viewYaw: 0
    property int viewDuration: 450
    property real shownPitch: viewPitch
    property real shownYaw: viewYaw
    Behavior on shownPitch { NumberAnimation { duration: root.viewDuration; easing.type: Easing.InOutCubic } }
    Behavior on shownYaw { NumberAnimation { duration: root.viewDuration; easing.type: Easing.InOutCubic } }

    // Skeleton mode: unpainted stickers render as dim translucent
    // placeholder panels instead of their uniform face colour, so a cube
    // with a partial faceletColors map reads as "filling in" one captured
    // face at a time. The dark cubie bodies stay visible either way.
    property bool skeleton: false

    // Per-face capture fill rings: null, or a JS object mapping face
    // letter -> "ok" | "conflict". Each named face gets a thin frame drawn
    // just outside its sticker plane - green for a cleanly captured face,
    // warning-coloured while the face has an outstanding verification
    // conflict (ScanSession.faceCaptureStates) - so the progress cube says
    // not just WHAT was captured (the painted stickers) but how much of
    // the cube is banked and which face needs re-showing, at a glance and
    // on the rotating cube itself. The backing model only re-evaluates
    // when this map object is REPLACED, which the scan screen does on
    // capture events alone (facesScannedChanged) - the ring Repeater3D
    // must never rebuild per frame; per-frame pose updates merely rotate
    // the shared parent node.
    property var faceRings: null

    // faceRings flattened for the Repeater3D, in fixed face order so an
    // unchanged map yields an identical model. Letters map onto the same
    // local-face keys the stickers use (stickerOrientation/stickerAxis).
    readonly property var faceRingItems: {
        var items = [];
        if (!faceRings)
            return items;
        var faceKeys = { U: "py", D: "ny", F: "pz", B: "nz", L: "nx", R: "px" };
        var letters = ["U", "D", "F", "B", "L", "R"];
        for (var i = 0; i < letters.length; i++) {
            if (faceRings[letters[i]])
                items.push({ face: faceKeys[letters[i]], state: faceRings[letters[i]] });
        }
        return items;
    }

    // Live pose driving: a scalar-first [w, x, y, z] quaternion, or null.
    // While set, drag and auto-rotate are suspended and the cube slerps
    // after the supplied orientation - the scan screen feeds it the
    // detected physical-cube pose so the render visibly tracks the real
    // cube. Null restores the presented/drag behaviour exactly.
    property var externalRotation: null
    readonly property bool externallyDriven: externalRotation !== null
        && externalRotation.length === 4

    // Live pose translation: an [x, y, z] scene-unit offset, or null.
    // Fed alongside externalRotation so the model moves through the
    // viewport with the physical cube instead of pivoting in place;
    // null eases the cube back to centre.
    property var externalPosition: null
    readonly property bool externallyPositioned: externalPosition !== null
        && externalPosition.length === 3

    // Raw-pose debug mode (CUBE_VISION_RAW_POSE=1): the view's own
    // easing animations switch off so each pose sample lands exactly
    // as delivered - smoothing anywhere would defeat the mode's point.
    property bool externalSnap: false

    // The drag / idle-spin paths write cubeRoot.eulerRotation imperatively,
    // so the pose hand-off is imperative too: zero the presentation tilt
    // while a live pose drives the cube (otherwise homeRotation would
    // compose with the pose), restore it when the pose goes away.
    onExternallyDrivenChanged: {
        springAnim.stop();
        cubeRoot.eulerRotation = externallyDriven ? Qt.vector3d(0, 0, 0) : root.homeRotation;
    }

    // Instructional Glow's active-layer highlight: which layer to call out,
    // or null for none, checked against each cubie's *home* position. The
    // layer in hand (being nudged or turned) glows as well, wherever its
    // cubies started.
    property var highlightedLayer: null // { axis: "x"|"y"|"z", layer: -1|0|1 } | null

    readonly property bool dragging: dragHandler.active
    readonly property bool turning: playing

    readonly property real cubieSize: 34
    readonly property real cubieGap: 2
    readonly property real step: cubieSize + cubieGap
    readonly property real stickerInset: 0.86

    property vector3d homeRotation: Qt.vector3d(-24, -32, 0)

    // Turns a layer a quarter turn: axis is "x"|"y"|"z", layerIndex is
    // -1|0|1, and `clockwise` is clockwise seen from the positive end of the
    // axis. Ignored while another turn or a drag is in progress.
    function turnLayer(axis, layerIndex, clockwise) {
        if (playing || dragHandler.active)
            return;
        playTurn({ axis: axis, layer: layerIndex, degrees: clockwise ? -90 : 90 });
    }

    readonly property var sideLayers: ({
        U: { axis: "y", layer: 1 }, D: { axis: "y", layer: -1 },
        R: { axis: "x", layer: 1 }, L: { axis: "x", layer: -1 },
        F: { axis: "z", layer: 1 }, B: { axis: "z", layer: -1 }
    })

    // The turn a move makes: which layer, about which axis, and how many
    // degrees about the positive direction of that axis (right-handed, so
    // positive is anticlockwise seen from the positive end). A move is
    // quarter turns clockwise seen from outside its side, and D, L and B are
    // looked at from the negative end of their axis - so for them clockwise
    // is positive. Null for anything that is not a move.
    function moveTurn(face, turns) {
        var side = sideLayers[face];
        if (!side || turns < 1 || turns > 3)
            return null;
        var quarters = turns === 3 ? -1 : turns; // three clockwise is one anticlockwise
        return { axis: side.axis, layer: side.layer, degrees: -90 * quarters * side.layer };
    }

    // Turns the layer of `face` the whole way - `turns` quarter turns
    // clockwise seen from outside it - then sets it down and emits
    // movePlayed. A turn already under way is finished on the spot first.
    function playMove(face, turns) {
        var turn = moveTurn(face, turns);
        if (!turn)
            return false;
        playTurn(turn);
        return true;
    }

    function playTurn(turn) {
        finishPlaying();
        stopNudge();
        if (!heldLayer || heldLayer.axis !== turn.axis || heldLayer.layer !== turn.layer) {
            setDown(0);
            pickUp(turn);
        }
        if (!heldLayer)
            return;
        playTarget = turn.degrees;
        playing = true;
        playAnim.from = turnAngle;
        playAnim.to = playTarget;
        playAnim.duration = turnDuration * (Math.abs(playTarget) > 90 ? 1.5 : 1);
        playAnim.start();
    }

    // Jumps to the end of the turn under way, if there is one.
    function finishPlaying() {
        if (!playing)
            return;
        playAnim.stop();
        endPlay();
    }

    function endPlay() {
        if (!playing)
            return;
        setDown(playTarget);
        playing = false;
        // Whoever is listening normally repaints the cube as it now stands,
        // which restarts the guide; if nobody does, it restarts from here.
        scheduleGuide();
        movePlayed();
    }

    // The layer hanging off the pivot, or null:
    // { axis, layer, cubies: indices into cubieStates }.
    property var heldLayer: null
    // The same cubies as a lookup, for the stickers' own bindings.
    property var heldCubies: ({})
    // The pivot's angle about heldLayer.axis, in degrees.
    property real turnAngle: 0
    property real playTarget: 0
    property real nudgeSign: 1

    function pickUp(turn) {
        var along = axisIndex(turn.axis);
        var cubies = [];
        var lookup = {};
        for (var i = 0; i < cubieStates.length; i++) {
            if (cubieStates[i].pos[along] === turn.layer && cubieRepeater.objectAt(i)) {
                cubies.push(i);
                lookup[i] = true;
            }
        }
        if (cubies.length === 0)
            return;
        turnAngle = 0;
        pivot.axis = Qt.vector3d(along === 0 ? 1 : 0, along === 1 ? 1 : 0, along === 2 ? 1 : 0);
        for (var c = 0; c < cubies.length; c++)
            cubieRepeater.objectAt(cubies[c]).parent = pivot;
        heldLayer = { axis: turn.axis, layer: turn.layer, cubies: cubies };
        heldCubies = lookup;
    }

    // Puts the layer in hand back on the cube, `degrees` (a multiple of 90)
    // round from where it was picked up.
    function setDown(degrees) {
        if (!heldLayer)
            return;
        var m = turnMatrix(heldLayer.axis, degrees);
        for (var c = 0; c < heldLayer.cubies.length; c++) {
            var idx = heldLayer.cubies[c];
            var st = cubieStates[idx];
            st.pos = matVec(m, st.pos);
            st.orientation = matMul(m, st.orientation);
            placeCubie(idx);
        }
        turnAngle = 0;
        heldLayer = null;
        heldCubies = ({});
    }

    function placeCubie(idx) {
        var item = cubieRepeater.objectAt(idx);
        if (!item)
            return;
        var st = cubieStates[idx];
        item.parent = holdNode;
        item.position = Qt.vector3d(st.pos[0] * root.step, st.pos[1] * root.step, st.pos[2] * root.step);
        item.rotation = root.matToQuaternion(st.orientation);
    }

    // Every cubie back where it started, nothing turning.
    function resetLayers() {
        playAnim.stop();
        playing = false;
        stopNudge();
        turnAngle = 0;
        heldLayer = null;
        heldCubies = ({});
        for (var i = 0; i < cubieStates.length; i++) {
            cubieStates[i].pos = cubieDefs[i].home.slice();
            cubieStates[i].orientation = matIdentity();
            placeCubie(i);
        }
        scheduleGuide();
    }

    function stopNudge() {
        nudgeOnce.stop();
        nudgeTwice.stop();
    }

    // The guide restarts once things have settled: a step of the solve
    // changes the painting, the side and the turn count together, and the
    // layer should be picked up once, from the cube as repainted.
    function scheduleGuide() { Qt.callLater(root.applyGuide); }

    function applyGuide() {
        if (playing)
            return;
        stopNudge();
        setDown(0);
        var turn = moveTurn(guideFace, guideTurns);
        if (!turn || cubieStates.length === 0)
            return;
        pickUp(turn);
        if (!heldLayer)
            return;
        nudgeSign = turn.degrees > 0 ? 1 : -1;
        if (Math.abs(turn.degrees) > 90)
            nudgeTwice.restart();
        else
            nudgeOnce.restart();
    }

    // Every sticker as it sits in the scene right now, measured from the
    // nodes themselves rather than from the bookkeeping above, in the cube's
    // own frame and in cubie spacings:
    //   { color: "#rrggbb", cubie: [x, y, z], facing: [x, y, z] }
    // - which cubie position the sticker is on (fractional while a layer is
    // part-way round) and which way it faces. x runs L to R, y D to U, z B
    // to F. This is what the tests compare with cube::solver::Cube.
    function stickerPlacements() {
        var out = [];
        for (var i = 0; i < cubieDefs.length; i++) {
            var cubie = cubieRepeater.objectAt(i);
            for (var j = 0; j < cubieDefs[i].stickers.length; j++) {
                var sticker = cubie.stickerAt(j);
                var at = holdNode.mapPositionFromScene(sticker.scenePosition);
                var facing = holdNode.mapDirectionFromScene(
                    sticker.mapDirectionToScene(Qt.vector3d(0, 0, 1)));
                var lift = sticker.faceOffset;
                out.push({
                    color: "" + sticker.stickerColor,
                    cubie: [(at.x - facing.x * lift) / step, (at.y - facing.y * lift) / step,
                            (at.z - facing.z * lift) / step],
                    facing: [facing.x, facing.y, facing.z]
                });
            }
        }
        return out;
    }

    // Which way each side faces in the scene, where the viewer looks along
    // -z with +y up and +x to the right: { U: [x, y, z], ... }. A side is
    // in view when its z is positive.
    function sideDirections() {
        var out = {};
        var keys = { U: "py", D: "ny", F: "pz", B: "nz", L: "nx", R: "px" };
        for (var letter in keys) {
            var axis = stickerAxis[keys[letter]];
            var d = holdNode.mapDirectionToScene(Qt.vector3d(axis[0], axis[1], axis[2]));
            out[letter] = [d.x, d.y, d.z];
        }
        return out;
    }

    function axisIndex(axis) { return axis === "x" ? 0 : (axis === "y" ? 1 : 2); }

    function matIdentity() { return [1, 0, 0, 0, 1, 0, 0, 0, 1]; }

    function matMul(a, b) {
        var r = new Array(9);
        for (var i = 0; i < 3; i++)
            for (var j = 0; j < 3; j++) {
                var s = 0;
                for (var k = 0; k < 3; k++)
                    s += a[i * 3 + k] * b[k * 3 + j];
                r[i * 3 + j] = s;
            }
        return r;
    }

    function matVec(m, v) {
        return [
            m[0] * v[0] + m[1] * v[1] + m[2] * v[2],
            m[3] * v[0] + m[4] * v[1] + m[5] * v[2],
            m[6] * v[0] + m[7] * v[1] + m[8] * v[2]
        ];
    }

    // Standard right-handed single-axis rotation matrix for a multiple of
    // 90 degrees - the same rotation the pivot shows at that angle, so a
    // layer set down sits exactly where its turn left it.
    function turnMatrix(axis, angleDeg) {
        var rad = angleDeg * Math.PI / 180;
        var c = Math.round(Math.cos(rad));
        var s = Math.round(Math.sin(rad));
        if (axis === "x") return [1, 0, 0, 0, c, -s, 0, s, c];
        if (axis === "y") return [c, 0, s, 0, 1, 0, -s, 0, c];
        return [c, -s, 0, s, c, 0, 0, 0, 1];
    }

    // Shepperd's method: standard, numerically-safe rotation-matrix ->
    // quaternion conversion (avoids the QML "quaternion" type's undocumented
    // multiplication support - this needs no runtime quaternion composition
    // at all, only this one conversion of an explicitly-composed matrix).
    function matToQuaternion(m) {
        var m00 = m[0], m01 = m[1], m02 = m[2];
        var m10 = m[3], m11 = m[4], m12 = m[5];
        var m20 = m[6], m21 = m[7], m22 = m[8];
        var trace = m00 + m11 + m22;
        var qw, qx, qy, qz, s;
        if (trace > 0) {
            s = Math.sqrt(trace + 1.0) * 2;
            qw = 0.25 * s; qx = (m21 - m12) / s; qy = (m02 - m20) / s; qz = (m10 - m01) / s;
        } else if (m00 > m11 && m00 > m22) {
            s = Math.sqrt(1.0 + m00 - m11 - m22) * 2;
            qw = (m21 - m12) / s; qx = 0.25 * s; qy = (m01 + m10) / s; qz = (m02 + m20) / s;
        } else if (m11 > m22) {
            s = Math.sqrt(1.0 + m11 - m00 - m22) * 2;
            qw = (m02 - m20) / s; qx = (m01 + m10) / s; qy = 0.25 * s; qz = (m12 + m21) / s;
        } else {
            s = Math.sqrt(1.0 + m22 - m00 - m11) * 2;
            qw = (m10 - m01) / s; qx = (m02 + m20) / s; qy = (m12 + m21) / s; qz = 0.25 * s;
        }
        return Qt.quaternion(qw, qx, qy, qz);
    }

    // Facelet indexing convention (row-major 0-8 per face): each face is
    // read looking at it from OUTSIDE the cube, rows top to bottom, columns
    // left to right, with screen-up = +y on the four side faces (F/B/L/R),
    // screen-up = -z (the B edge) on U, and screen-up = +z (the F edge) on
    // D; columns follow the right-handed cross product of view direction
    // and screen-up. cube::solver::outside_view() produces exactly this
    // order from a solver Cube.
    function faceletIndex(letter, xi, yi, zi) {
        if (letter === "U") return (zi + 1) * 3 + (xi + 1);
        if (letter === "D") return (1 - zi) * 3 + (xi + 1);
        if (letter === "F") return (1 - yi) * 3 + (xi + 1);
        if (letter === "B") return (1 - yi) * 3 + (1 - xi);
        if (letter === "L") return (1 - yi) * 3 + (zi + 1);
        return (1 - yi) * 3 + (1 - zi); // R
    }

    // 26 cubies (skip the fully-hidden centre). Each keeps its stickers on
    // fixed local faces forever; only pos/orientation change as layers turn.
    // Nothing here depends on how the cube is painted: each sticker looks
    // its own colour up (see stickerNode), so repainting recolours 54
    // materials and rebuilds nothing.
    readonly property var cubieDefs: {
        var defs = [];
        for (var xi = -1; xi <= 1; xi++) {
            for (var yi = -1; yi <= 1; yi++) {
                for (var zi = -1; zi <= 1; zi++) {
                    if (xi === 0 && yi === 0 && zi === 0)
                        continue;
                    var faces = [];
                    if (yi === 1) faces.push({ key: "py", letter: "U" });
                    if (yi === -1) faces.push({ key: "ny", letter: "D" });
                    if (zi === 1) faces.push({ key: "pz", letter: "F" });
                    if (zi === -1) faces.push({ key: "nz", letter: "B" });
                    if (xi === -1) faces.push({ key: "nx", letter: "L" });
                    if (xi === 1) faces.push({ key: "px", letter: "R" });
                    var stickers = [];
                    for (var f = 0; f < faces.length; f++) {
                        stickers.push({
                            face: faces[f].key,
                            letter: faces[f].letter,
                            index: faceletIndex(faces[f].letter, xi, yi, zi)
                        });
                    }
                    defs.push({ home: [xi, yi, zi], stickers: stickers });
                }
            }
        }
        return defs;
    }

    function uniformColor(letter) {
        switch (letter) {
        case "U": return root.topColor;
        case "D": return root.bottomColor;
        case "F": return root.frontColor;
        case "B": return root.backColor;
        case "L": return root.leftColor;
        }
        return root.rightColor;
    }

    readonly property color bodyColor: "#1B1A1A"
    function towardsBody(c, amount) {
        return Qt.rgba(c.r + (bodyColor.r - c.r) * amount, c.g + (bodyColor.g - c.g) * amount,
                       c.b + (bodyColor.b - c.b) * amount, 1);
    }

    // Runtime pos/orientation per cubie (index-aligned with cubieDefs /
    // cubieRepeater), separate from cubieDefs so turns can mutate it without
    // re-triggering the Repeater's model binding.
    property var cubieStates: []
    function resetCubieStates() {
        var states = [];
        for (var i = 0; i < cubieDefs.length; i++)
            states.push({ pos: cubieDefs[i].home.slice(), orientation: matIdentity() });
        cubieStates = states;
    }
    Component.onCompleted: {
        resetCubieStates();
        // A view created with a live pose already bound never sees an
        // externallyDrivenChanged edge, so clear the presentation tilt
        // here too.
        if (externallyDriven)
            cubeRoot.eulerRotation = Qt.vector3d(0, 0, 0);
        scheduleGuide();
    }

    readonly property var stickerOrientation: ({
        py: { rx: -90, ry: 0 },
        ny: { rx: 90, ry: 0 },
        pz: { rx: 0, ry: 0 },
        nz: { rx: 0, ry: 180 },
        nx: { rx: 0, ry: -90 },
        px: { rx: 0, ry: 90 }
    })
    readonly property var stickerAxis: ({
        py: [0, 1, 0], ny: [0, -1, 0], pz: [0, 0, 1], nz: [0, 0, -1], nx: [-1, 0, 0], px: [1, 0, 0]
    })

    View3D {
        id: view3d
        anchors.fill: parent
        camera: camera

        environment: ExtendedSceneEnvironment {
            backgroundMode: SceneEnvironment.Transparent
            antialiasingMode: SceneEnvironment.MSAA
            antialiasingQuality: SceneEnvironment.High

            // Physically-Based Realism's environment reflections: a small
            // procedural sky used purely as a light probe (backgroundMode
            // stays Transparent, so it's never actually painted as a visible
            // sky) - a real photographed HDRI would look better but there
            // isn't one in the repo's assets yet.
            lightProbe: Texture {
                textureData: ProceduralSkyTextureData {}
            }

            // Borderlands Cel-Shading's vignette + halftone/grunge post
            // pass. Both come from ExtendedSceneEnvironment/Effect, which is
            // why this is that type rather than a plain SceneEnvironment
            // even though the other two styles don't use either property.
            vignetteEnabled: root.shaderStyle === 2
            vignetteStrength: 10
            effects: root.shaderStyle === 2 ? [celShadePost] : []
        }

        Effect {
            id: celShadePost
            passes: Pass {
                shaders: Shader {
                    stage: Shader.Fragment
                    shader: "shaders/celshade_post.frag"
                }
            }
        }

        PerspectiveCamera {
            id: camera
            position: Qt.vector3d(0, 0, 430)
            fieldOfView: 26
        }

        DirectionalLight {
            eulerRotation: Qt.vector3d(-35, -35, 0)
            brightness: 1.1
        }
        DirectionalLight {
            eulerRotation: Qt.vector3d(40, 150, 0)
            brightness: 0.35
        }
        // A tight, bright third light only for Physically-Based Realism -
        // the clearcoat/specular/reflection tuning alone still reads flat
        // without a sharp catch-light to actually bounce off it, which is
        // what a real studio product-shot key light gives you.
        DirectionalLight {
            visible: root.shaderStyle === 0
            eulerRotation: Qt.vector3d(-60, 20, 0)
            brightness: 1.6
        }

        // Pose-follow stage: externalRotation lands on this dedicated
        // parent, NOT on cubeRoot, because the drag / idle-spin / spring
        // paths all write cubeRoot.eulerRotation imperatively - keeping the
        // quaternion on its own node means neither side ever clobbers the
        // other. The Behavior slerps between successive detections so
        // camera-rate pose updates read as smooth tracking, not stepping.
        Node {
            id: poseNode
            rotation: root.externallyDriven
                ? Qt.quaternion(root.externalRotation[0], root.externalRotation[1],
                                root.externalRotation[2], root.externalRotation[3])
                : Qt.quaternion(1, 0, 0, 0)
            position: root.externallyPositioned
                ? Qt.vector3d(root.externalPosition[0], root.externalPosition[1],
                              root.externalPosition[2])
                : Qt.vector3d(0, 0, 0)
            Behavior on rotation {
                enabled: !root.externalSnap
                QuaternionAnimation { duration: 120; type: QuaternionAnimation.Slerp }
            }
            Behavior on position {
                enabled: !root.externalSnap
                Vector3dAnimation { duration: 120 }
            }

            // Where the cube is looked at from: the tip towards the viewer
            // outside the turntable spin, so the cube's uprights stay
            // upright on screen however far round it is turned.
            Node {
                id: tiltNode
                eulerRotation: Qt.vector3d(root.shownPitch, 0, 0)

                Node {
                    id: spinNode
                    eulerRotation: Qt.vector3d(0, root.shownYaw, 0)

                    Node {
                        id: cubeRoot
                        eulerRotation: root.homeRotation

                        // How the cube itself is turned: everything that
                        // is part of the cube hangs off this.
                        Node {
                            id: holdNode
                            rotation: root.holdRotation
                            Behavior on rotation {
                                QuaternionAnimation { duration: root.viewDuration; type: QuaternionAnimation.Slerp }
                            }

                            // The layer being turned hangs off this while
                            // it moves.
                            Node {
                                id: pivot
                                property vector3d axis: Qt.vector3d(0, 1, 0)
                                rotation: Quaternion.fromAxisAndAngle(axis, root.turnAngle)
                            }

                            // Capture fill rings (see faceRings above). Each
                            // ring is four thin bars rather than a textured
                            // quad because Quick3D primitives have no
                            // stroked-rectangle, and four boxes cost
                            // nothing. NoLighting keeps the state colour
                            // legible at every orientation - a ring is a
                            // status lamp, not a surface.
                            Repeater3D {
                                id: faceRingRepeater
                                model: root.faceRingItems

                                Node {
                                    id: ringNode
                                    required property var modelData
                                    readonly property var orient: root.stickerOrientation[modelData.face]
                                    readonly property var axisVec: root.stickerAxis[modelData.face]
                                    // Just proud of the sticker plane (cubie
                                    // surface + sticker offset ~53.75) so the
                                    // ring never z-fights the stickers it
                                    // frames.
                                    readonly property real ringDistance: 1.5 * root.step - root.cubieGap / 2 + 1.5
                                    // Frame the full 3x3 face with a little
                                    // air: the ring reads as "this whole
                                    // face", not as part of any one sticker.
                                    readonly property real ringSide: 3 * root.step - root.cubieGap + 4
                                    readonly property real ringHalf: ringSide / 2
                                    readonly property real ringThickness: 2.4
                                    readonly property color ringColor: modelData.state === "conflict"
                                        ? Theme.warning : "#3A9D5D" // the capture-celebration green

                                    position: Qt.vector3d(axisVec[0] * ringDistance,
                                                          axisVec[1] * ringDistance,
                                                          axisVec[2] * ringDistance)
                                    eulerRotation: Qt.vector3d(orient.rx, orient.ry, 0)

                                    // Local +z now points along the face
                                    // normal (same frame the stickers are
                                    // laid out in), so the four bars live in
                                    // the local XY plane. Horizontal bars
                                    // overhang by one thickness to close the
                                    // corners.
                                    Model {
                                        source: "#Cube"
                                        position: Qt.vector3d(0, ringNode.ringHalf, 0)
                                        scale: Qt.vector3d((ringNode.ringSide + ringNode.ringThickness) / 100,
                                                           ringNode.ringThickness / 100, ringNode.ringThickness / 100)
                                        materials: PrincipledMaterial { baseColor: ringNode.ringColor; lighting: PrincipledMaterial.NoLighting }
                                    }
                                    Model {
                                        source: "#Cube"
                                        position: Qt.vector3d(0, -ringNode.ringHalf, 0)
                                        scale: Qt.vector3d((ringNode.ringSide + ringNode.ringThickness) / 100,
                                                           ringNode.ringThickness / 100, ringNode.ringThickness / 100)
                                        materials: PrincipledMaterial { baseColor: ringNode.ringColor; lighting: PrincipledMaterial.NoLighting }
                                    }
                                    Model {
                                        source: "#Cube"
                                        position: Qt.vector3d(-ringNode.ringHalf, 0, 0)
                                        scale: Qt.vector3d(ringNode.ringThickness / 100,
                                                           ringNode.ringSide / 100, ringNode.ringThickness / 100)
                                        materials: PrincipledMaterial { baseColor: ringNode.ringColor; lighting: PrincipledMaterial.NoLighting }
                                    }
                                    Model {
                                        source: "#Cube"
                                        position: Qt.vector3d(ringNode.ringHalf, 0, 0)
                                        scale: Qt.vector3d(ringNode.ringThickness / 100,
                                                           ringNode.ringSide / 100, ringNode.ringThickness / 100)
                                        materials: PrincipledMaterial { baseColor: ringNode.ringColor; lighting: PrincipledMaterial.NoLighting }
                                    }
                                }
                            }

                            Repeater3D {
                                id: cubieRepeater
                                model: root.cubieDefs

                                Node {
                                    id: cubieNode
                                    required property var modelData
                                    required property int index

                                    readonly property bool inHand: root.heldCubies[index] === true
                                    function stickerAt(j) { return stickerRepeater.objectAt(j); }

                                    position: Qt.vector3d(modelData.home[0] * root.step, modelData.home[1] * root.step, modelData.home[2] * root.step)

                                    Model {
                                        source: "#Cube"
                                        scale: Qt.vector3d(root.cubieSize / 100, root.cubieSize / 100, root.cubieSize / 100)
                                        materials: PrincipledMaterial { baseColor: root.bodyColor; roughness: 1.0; metalness: 0.0 }
                                    }

                                    // Style 2's inverted-hull ink outline. A
                                    // cube's faces are all axis-aligned, so a
                                    // uniformly-scaled duplicate mesh rendered
                                    // front-face-culled (only its backfaces,
                                    // poking out past the real mesh, are
                                    // visible) gives an exact outline with no
                                    // custom vertex shader needed. +1 total
                                    // (0.5 per side) stays well clear of
                                    // root.cubieGap (2, i.e. a 1-unit half-gap
                                    // to the next cubie) so neighbouring
                                    // cubies' outlines don't fuse into one
                                    // blob.
                                    Model {
                                        visible: root.shaderStyle === 2
                                        source: "#Cube"
                                        scale: Qt.vector3d((root.cubieSize + 1) / 100, (root.cubieSize + 1) / 100, (root.cubieSize + 1) / 100)
                                        materials: PrincipledMaterial {
                                            baseColor: "black"
                                            lighting: PrincipledMaterial.NoLighting
                                            cullMode: Material.FrontFaceCulling
                                        }
                                    }

                                    Repeater3D {
                                        id: stickerRepeater
                                        model: cubieNode.modelData.stickers

                                        // One sticker: where it sits on its
                                        // cubie, what colour it is now, and
                                        // one panel per shader style, of
                                        // which only the style in use draws.
                                        Node {
                                            id: stickerNode
                                            required property var modelData
                                            readonly property var orient: root.stickerOrientation[modelData.face]
                                            readonly property var axisVec: root.stickerAxis[modelData.face]
                                            readonly property real faceOffset: root.cubieSize / 2 + 0.75

                                            // This sticker's entry in the
                                            // painting, if it has one.
                                            readonly property var paint: {
                                                var side = root.faceletColors ? root.faceletColors[modelData.letter] : undefined;
                                                return (side && side.length === 9) ? side[modelData.index] : undefined;
                                            }
                                            readonly property bool painted: !!paint
                                            readonly property color stickerColor: painted ? paint : root.uniformColor(modelData.letter)
                                            // Stickers outside the layer in
                                            // hand sit back a little.
                                            readonly property color shownColor: root.guideDim > 0 && root.heldLayer !== null && !cubieNode.inHand
                                                ? root.towardsBody(stickerColor, root.guideDim) : stickerColor
                                            // Skeleton mode hands unpainted
                                            // stickers to the placeholder
                                            // panel below.
                                            readonly property bool solid: !root.skeleton || painted
                                            // Instructional Glow lights up
                                            // the layer in hand, and any
                                            // layer highlightedLayer names.
                                            readonly property bool calledOut: cubieNode.inHand
                                                || (root.highlightedLayer !== null
                                                    && cubieNode.modelData.home[root.axisIndex(root.highlightedLayer.axis)] === root.highlightedLayer.layer)

                                            position: Qt.vector3d(axisVec[0] * faceOffset, axisVec[1] * faceOffset, axisVec[2] * faceOffset)
                                            eulerRotation: Qt.vector3d(orient.rx, orient.ry, 0)

                                            // Style 0: Physically-Based
                                            // Realism (default). Roughness/
                                            // metalness/specular/clearcoat
                                            // tuned for real textured ABS
                                            // plastic with a light lacquer
                                            // coat. Environment reflections
                                            // come from the shared lightProbe
                                            // set on `environment`.
                                            Model {
                                                visible: root.shaderStyle === 0 && stickerNode.solid
                                                source: "#Rectangle"
                                                scale: Qt.vector3d(root.cubieSize * root.stickerInset / 100, root.cubieSize * root.stickerInset / 100, 1)
                                                materials: PrincipledMaterial {
                                                    baseColor: stickerNode.shownColor
                                                    roughness: 0.35
                                                    metalness: 0.0
                                                    specularAmount: 0.6
                                                    specularTint: 0.1
                                                    clearcoatAmount: 0.35
                                                    clearcoatRoughnessAmount: 0.12
                                                }
                                            }

                                            // Style 1: Instructional Glow.
                                            // Quantized-band diffuse + fresnel
                                            // rim (toon.frag) plus an emissive
                                            // boost on the layer called out.
                                            Model {
                                                visible: root.shaderStyle === 1 && stickerNode.solid
                                                source: "#Rectangle"
                                                scale: Qt.vector3d(root.cubieSize * root.stickerInset / 100, root.cubieSize * root.stickerInset / 100, 1)

                                                materials: CustomMaterial {
                                                    shadingMode: CustomMaterial.Shaded
                                                    fragmentShader: "shaders/toon.frag"
                                                    property color uBaseColor: stickerNode.shownColor
                                                    property color uRimColor: Theme.accent
                                                    property real uRimPower: 2.2
                                                    property real uGlowStrength: stickerNode.calledOut ? 1.0 : 0.0
                                                }
                                            }

                                            // Style 2: Borderlands
                                            // Cel-Shading. Hard 2-step banding
                                            // (cel.frag, no colour rim - the
                                            // outline mesh above provides the
                                            // silhouette line instead); the
                                            // halftone/grunge/vignette look is
                                            // a screen-space post pass on
                                            // `environment`, not per-sticker.
                                            Model {
                                                visible: root.shaderStyle === 2 && stickerNode.solid
                                                source: "#Rectangle"
                                                scale: Qt.vector3d(root.cubieSize * root.stickerInset / 100, root.cubieSize * root.stickerInset / 100, 1)
                                                materials: CustomMaterial {
                                                    shadingMode: CustomMaterial.Shaded
                                                    fragmentShader: "shaders/cel.frag"
                                                    property color uBaseColor: stickerNode.shownColor
                                                }
                                            }

                                            // Skeleton placeholder: an
                                            // unpainted sticker renders as
                                            // this ONE style-independent dim
                                            // translucent panel rather than
                                            // three per-style variants - the
                                            // custom toon/cel shaders have no
                                            // alpha path, and an "empty socket
                                            // awaiting a face" should look
                                            // identical under every shader
                                            // style anyway. NoLighting keeps
                                            // the dimness constant as the cube
                                            // rotates.
                                            Model {
                                                visible: root.skeleton && !stickerNode.painted
                                                source: "#Rectangle"
                                                scale: Qt.vector3d(root.cubieSize * root.stickerInset / 100, root.cubieSize * root.stickerInset / 100, 1)
                                                materials: PrincipledMaterial {
                                                    baseColor: "#C9C5C5"
                                                    lighting: PrincipledMaterial.NoLighting
                                                    opacity: 0.16
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    StatsOverlay { active: root.showStats }

    NumberAnimation {
        id: playAnim
        target: root
        property: "turnAngle"
        easing.type: Easing.InOutQuad
        onFinished: root.endPlay()
    }

    // The nudge for a quarter turn: out with a little kick, a beat held
    // there, back, and a rest before the next.
    SequentialAnimation {
        id: nudgeOnce
        loops: Animation.Infinite
        NumberAnimation {
            target: root; property: "turnAngle"
            to: root.nudgeSign * root.nudgeAngle
            duration: 170; easing.type: Easing.OutBack; easing.overshoot: 1.6
        }
        PauseAnimation { duration: 190 }
        NumberAnimation { target: root; property: "turnAngle"; to: 0; duration: 260; easing.type: Easing.InOutQuad }
        PauseAnimation { duration: 460 }
    }

    // The nudge for a half turn: two kicks, one after the other, then back.
    SequentialAnimation {
        id: nudgeTwice
        loops: Animation.Infinite
        NumberAnimation {
            target: root; property: "turnAngle"
            to: root.nudgeSign * root.nudgeAngle
            duration: 160; easing.type: Easing.OutBack; easing.overshoot: 1.6
        }
        PauseAnimation { duration: 120 }
        NumberAnimation {
            target: root; property: "turnAngle"
            to: root.nudgeSign * root.nudgeAngle * 2
            duration: 160; easing.type: Easing.OutBack; easing.overshoot: 1.6
        }
        PauseAnimation { duration: 220 }
        NumberAnimation { target: root; property: "turnAngle"; to: 0; duration: 340; easing.type: Easing.InOutQuad }
        PauseAnimation { duration: 460 }
    }

    DragHandler {
        id: dragHandler
        target: null
        enabled: root.interactive && !root.externallyDriven
        property real lastX: 0
        property real lastY: 0
        onActiveChanged: {
            if (active) {
                springAnim.stop();
                lastX = centroid.position.x;
                lastY = centroid.position.y;
            } else {
                springAnim.start();
            }
        }
        onCentroidChanged: {
            if (!active)
                return;
            var dx = centroid.position.x - lastX;
            var dy = centroid.position.y - lastY;
            cubeRoot.eulerRotation.y = cubeRoot.eulerRotation.y + dx * 0.4;
            cubeRoot.eulerRotation.x = Math.max(-85, Math.min(85, cubeRoot.eulerRotation.x - dy * 0.4));
            lastX = centroid.position.x;
            lastY = centroid.position.y;
        }
    }

    // Eases back to the presented orientation once the user lets go, rather
    // than leaving the cube wherever it was thrown.
    ParallelAnimation {
        id: springAnim
        NumberAnimation { target: cubeRoot; property: "eulerRotation.x"; to: root.homeRotation.x; duration: 550; easing.type: Easing.OutBack }
        NumberAnimation { target: cubeRoot; property: "eulerRotation.y"; to: root.homeRotation.y; duration: 550; easing.type: Easing.OutBack }
    }

    Timer {
        interval: 16
        repeat: true
        running: root.autoRotate && !root.externallyDriven && !dragHandler.active && !springAnim.running
        onTriggered: cubeRoot.eulerRotation.y = cubeRoot.eulerRotation.y + root.autoRotateSpeed
    }
}
