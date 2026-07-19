import QtQuick
import QtQuick3D
import QtQuick3D.Helpers
import Cubeulator

// GPU-rendered Rubik's cube built from 26 individually-positioned "cubies"
// (the centre piece is never visible, so it's skipped), each carrying its
// own 0-3 coloured stickers. Two things fall out of modelling it this way
// rather than as one 6-face shell:
//  - turnLayer() can animate a real slice turn (reparent the 9 affected
//    cubies under a temporary pivot, animate the pivot, then bake the
//    result back into each cubie) - the foundation the solve-visualisation
//    work needs to show "this section rotates like this".
//  - individual stickers can be recoloured per-facelet (faceletColors) -
//    the scan screen paints captured faces onto a skeleton cube this way.
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
    // must never rebuild per frame for the same reason cubieDefs must not
    // (see that comment); per-frame pose updates merely rotate the shared
    // parent node.
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

    // The drag / idle-spin paths write cubeRoot.eulerRotation imperatively,
    // so the pose hand-off is imperative too: zero the presentation tilt
    // while a live pose drives the cube (otherwise homeRotation would
    // compose with the pose), restore it when the pose goes away.
    onExternallyDrivenChanged: {
        springAnim.stop();
        cubeRoot.eulerRotation = externallyDriven ? Qt.vector3d(0, 0, 0) : root.homeRotation;
    }

    // Instructional Glow's active-layer highlight: which layer to call out,
    // or null for none. This is a placeholder hook - it's checked against
    // each cubie's *home* position, not its live position after turns, which
    // is enough for a stationary demo highlight. The solve-visualisation
    // session owns wiring this to the real "which layer moves next" solver
    // output.
    property var highlightedLayer: null // { axis: "x"|"y"|"z", layer: -1|0|1 } | null

    readonly property bool dragging: dragHandler.active
    readonly property bool turning: turnState.active

    readonly property real cubieSize: 34
    readonly property real cubieGap: 2
    readonly property real step: cubieSize + cubieGap
    readonly property real stickerInset: 0.86

    readonly property vector3d homeRotation: Qt.vector3d(-24, -32, 0)

    // Turns a face layer: axis is "x"|"y"|"z", layerIndex is -1|0|1, positive
    // is a right-hand-rule turn looking down the positive axis. Ignored while
    // another turn or a drag is in progress.
    function turnLayer(axis, layerIndex, clockwise) {
        if (turnState.active || dragHandler.active)
            return;
        var affected = [];
        for (var i = 0; i < cubieStates.length; i++) {
            if (cubieStates[i].pos[axisIndex(axis)] === layerIndex)
                affected.push(i);
        }
        if (affected.length === 0)
            return;

        pivot.eulerRotation = Qt.vector3d(0, 0, 0);
        for (var a = 0; a < affected.length; a++)
            cubieRepeater.objectAt(affected[a]).parent = pivot;

        turnState.axis = axis;
        turnState.angle = clockwise ? -90 : 90;
        turnState.affected = affected;
        turnState.active = true;
        pivotAnim.property = "eulerRotation." + axis;
        pivotAnim.from = 0;
        pivotAnim.to = turnState.angle;
        pivotAnim.start();
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

    // Standard single-axis rotation matrix, built from the same angle used
    // to drive the pivot's eulerRotation animation - so the baked result
    // always matches wherever the animation visually left off, regardless
    // of which rotation-direction convention Quick3D uses internally.
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

    function finishTurn() {
        var m = turnMatrix(turnState.axis, turnState.angle);
        for (var a = 0; a < turnState.affected.length; a++) {
            var idx = turnState.affected[a];
            var st = cubieStates[idx];
            st.pos = matVec(m, st.pos);
            st.orientation = matMul(m, st.orientation);

            var item = cubieRepeater.objectAt(idx);
            item.parent = cubeRoot;
            item.position = Qt.vector3d(st.pos[0] * root.step, st.pos[1] * root.step, st.pos[2] * root.step);
            item.rotation = root.matToQuaternion(st.orientation);
        }
        pivot.eulerRotation = Qt.vector3d(0, 0, 0);
        turnState.active = false;
    }

    // Facelet indexing convention (row-major 0-8 per face): each face is
    // read looking at it from OUTSIDE the cube, rows top to bottom, columns
    // left to right, with screen-up = +y on the four side faces (F/B/L/R),
    // screen-up = -z (the B edge) on U, and screen-up = +z (the F edge) on
    // D; columns follow the right-handed cross product of view direction
    // and screen-up. The scan assembler corrects per-face rotations
    // mathematically, so the load-bearing contract here is only that this
    // mapping is FIXED and shared by every caller, not that it matches any
    // printed cube notation.
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
    // Each sticker resolves its colour per-facelet from faceletColors when
    // an entry exists, falling back to the uniform face colour; `painted`
    // records which case so skeleton mode knows which stickers to dim.
    // Referencing root.faceletColors here means a new map re-evaluates the
    // whole binding and the Repeater3D rebuilds - fine at scan cadence (a
    // handful of captures), not something to drive per-frame.
    property var cubieDefs: {
        var defs = [];
        for (var xi = -1; xi <= 1; xi++) {
            for (var yi = -1; yi <= 1; yi++) {
                for (var zi = -1; zi <= 1; zi++) {
                    if (xi === 0 && yi === 0 && zi === 0)
                        continue;
                    var faces = [];
                    if (yi === 1) faces.push({ key: "py", letter: "U", uniform: root.topColor });
                    if (yi === -1) faces.push({ key: "ny", letter: "D", uniform: root.bottomColor });
                    if (zi === 1) faces.push({ key: "pz", letter: "F", uniform: root.frontColor });
                    if (zi === -1) faces.push({ key: "nz", letter: "B", uniform: root.backColor });
                    if (xi === -1) faces.push({ key: "nx", letter: "L", uniform: root.leftColor });
                    if (xi === 1) faces.push({ key: "px", letter: "R", uniform: root.rightColor });
                    var stickers = [];
                    for (var f = 0; f < faces.length; f++) {
                        var arr = root.faceletColors ? root.faceletColors[faces[f].letter] : undefined;
                        var over = (arr && arr.length === 9)
                            ? arr[root.faceletIndex(faces[f].letter, xi, yi, zi)] : undefined;
                        stickers.push({
                            face: faces[f].key,
                            color: over ? over : faces[f].uniform,
                            painted: !!over
                        });
                    }
                    defs.push({ home: [xi, yi, zi], stickers: stickers });
                }
            }
        }
        return defs;
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
            Behavior on rotation {
                QuaternionAnimation { duration: 120; type: QuaternionAnimation.Slerp }
            }

            Node {
                id: cubeRoot
                eulerRotation: root.homeRotation

                Node { id: pivot }

                // Capture fill rings (see faceRings above). Parented to
                // cubeRoot so they turn with the cube under both the pose
                // feed and drag/idle spin; each ring is four thin bars
                // rather than a textured quad because Quick3D primitives
                // have no stroked-rectangle, and four boxes cost nothing.
                // NoLighting keeps the state colour legible at every
                // orientation - a ring is a status lamp, not a surface.
                Repeater3D {
                    id: faceRingRepeater
                    model: root.faceRingItems

                    Node {
                        id: ringNode
                        required property var modelData
                        readonly property var orient: root.stickerOrientation[modelData.face]
                        readonly property var axisVec: root.stickerAxis[modelData.face]
                        // Just proud of the sticker plane (cubie surface +
                        // sticker offset ~53.75) so the ring never
                        // z-fights the stickers it frames.
                        readonly property real ringDistance: 1.5 * root.step - root.cubieGap / 2 + 1.5
                        // Frame the full 3x3 face with a little air: the
                        // ring reads as "this whole face", not as part of
                        // any one sticker.
                        readonly property real ringSide: 3 * root.step - root.cubieGap + 4
                        readonly property real ringHalf: ringSide / 2
                        readonly property real ringThickness: 2.4
                        readonly property color ringColor: modelData.state === "conflict"
                            ? Theme.warning : "#3A9D5D" // the capture-celebration green

                        position: Qt.vector3d(axisVec[0] * ringDistance,
                                              axisVec[1] * ringDistance,
                                              axisVec[2] * ringDistance)
                        eulerRotation: Qt.vector3d(orient.rx, orient.ry, 0)

                        // Local +z now points along the face normal (same
                        // frame the stickers are laid out in), so the four
                        // bars live in the local XY plane. Horizontal bars
                        // overhang by one thickness to close the corners.
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

                        position: Qt.vector3d(modelData.home[0] * root.step, modelData.home[1] * root.step, modelData.home[2] * root.step)

                        Model {
                            source: "#Cube"
                            scale: Qt.vector3d(root.cubieSize / 100, root.cubieSize / 100, root.cubieSize / 100)
                            materials: PrincipledMaterial { baseColor: "#1B1A1A"; roughness: 1.0; metalness: 0.0 }
                        }

                        // Style 2's inverted-hull ink outline. A cube's faces
                        // are all axis-aligned, so a uniformly-scaled duplicate
                        // mesh rendered front-face-culled (only its backfaces,
                        // poking out past the real mesh, are visible) gives an
                        // exact outline with no custom vertex shader needed.
                        // +1 total (0.5 per side) stays well clear of
                        // root.cubieGap (2, i.e. a 1-unit half-gap to the next
                        // cubie) so neighbouring cubies' outlines don't fuse
                        // into one blob.
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

                        // Style 0: Physically-Based Realism (default). Roughness/
                        // metalness/specular/clearcoat tuned for real textured
                        // ABS plastic with a light lacquer coat - not the bare
                        // flat PrincipledMaterial default, and not the old
                        // "glossy studio" chrome-like look either (that used
                        // metalness 0.35, too metallic for a plastic sticker).
                        // Environment reflections come from the shared
                        // lightProbe set on `environment` below.
                        Repeater3D {
                            model: cubieNode.modelData.stickers
                            visible: root.shaderStyle === 0

                            Model {
                                required property var modelData
                                // Skeleton mode hands unpainted stickers to the
                                // shared placeholder repeater below.
                                visible: !root.skeleton || modelData.painted
                                readonly property var orient: root.stickerOrientation[modelData.face]
                                readonly property var axisVec: root.stickerAxis[modelData.face]
                                source: "#Rectangle"
                                scale: Qt.vector3d(root.cubieSize * root.stickerInset / 100, root.cubieSize * root.stickerInset / 100, 1)
                                eulerRotation: Qt.vector3d(orient.rx, orient.ry, 0)
                                readonly property real faceOffset: root.cubieSize / 2 + 0.75
                                position: Qt.vector3d(axisVec[0] * faceOffset, axisVec[1] * faceOffset, axisVec[2] * faceOffset)

                                materials: PrincipledMaterial {
                                    baseColor: modelData.color
                                    roughness: 0.35
                                    metalness: 0.0
                                    specularAmount: 0.6
                                    specularTint: 0.1
                                    clearcoatAmount: 0.35
                                    clearcoatRoughnessAmount: 0.12
                                }
                            }
                        }

                        // Style 1: Instructional Glow. Quantized-band diffuse +
                        // fresnel rim (toon.frag) plus an emissive boost on
                        // stickers belonging to root.highlightedLayer.
                        Repeater3D {
                            model: cubieNode.modelData.stickers
                            visible: root.shaderStyle === 1

                            Model {
                                required property var modelData
                                visible: !root.skeleton || modelData.painted
                                readonly property var orient: root.stickerOrientation[modelData.face]
                                readonly property var axisVec: root.stickerAxis[modelData.face]
                                source: "#Rectangle"
                                scale: Qt.vector3d(root.cubieSize * root.stickerInset / 100, root.cubieSize * root.stickerInset / 100, 1)
                                eulerRotation: Qt.vector3d(orient.rx, orient.ry, 0)
                                readonly property real faceOffset: root.cubieSize / 2 + 0.75
                                position: Qt.vector3d(axisVec[0] * faceOffset, axisVec[1] * faceOffset, axisVec[2] * faceOffset)

                                readonly property bool inHighlightedLayer: root.highlightedLayer !== null
                                    && cubieNode.modelData.home[root.axisIndex(root.highlightedLayer.axis)] === root.highlightedLayer.layer

                                materials: CustomMaterial {
                                    shadingMode: CustomMaterial.Shaded
                                    fragmentShader: "shaders/toon.frag"
                                    property color uBaseColor: modelData.color
                                    property color uRimColor: Theme.accent
                                    property real uRimPower: 2.2
                                    property real uGlowStrength: inHighlightedLayer ? 1.0 : 0.0
                                }
                            }
                        }

                        // Style 2: Borderlands Cel-Shading. Hard 2-step banding
                        // (cel.frag, no colour rim - the outline mesh above
                        // provides the silhouette line instead); the
                        // halftone/grunge/vignette look is a screen-space post
                        // pass on `environment`, not per-sticker.
                        Repeater3D {
                            model: cubieNode.modelData.stickers
                            visible: root.shaderStyle === 2

                            Model {
                                required property var modelData
                                visible: !root.skeleton || modelData.painted
                                readonly property var orient: root.stickerOrientation[modelData.face]
                                readonly property var axisVec: root.stickerAxis[modelData.face]
                                source: "#Rectangle"
                                scale: Qt.vector3d(root.cubieSize * root.stickerInset / 100, root.cubieSize * root.stickerInset / 100, 1)
                                eulerRotation: Qt.vector3d(orient.rx, orient.ry, 0)
                                readonly property real faceOffset: root.cubieSize / 2 + 0.75
                                position: Qt.vector3d(axisVec[0] * faceOffset, axisVec[1] * faceOffset, axisVec[2] * faceOffset)

                                materials: CustomMaterial {
                                    shadingMode: CustomMaterial.Shaded
                                    fragmentShader: "shaders/cel.frag"
                                    property color uBaseColor: modelData.color
                                }
                            }
                        }

                        // Skeleton placeholders: unpainted stickers render as
                        // this ONE style-independent dim translucent panel
                        // (the styled repeaters above skip them) rather than
                        // three per-style variants - the custom toon/cel
                        // shaders have no alpha path, and an "empty socket
                        // awaiting a face" should look identical under every
                        // shader style anyway. NoLighting keeps the dimness
                        // constant as the cube rotates under the pose feed.
                        Repeater3D {
                            model: cubieNode.modelData.stickers
                            visible: root.skeleton

                            Model {
                                required property var modelData
                                visible: !modelData.painted
                                readonly property var orient: root.stickerOrientation[modelData.face]
                                readonly property var axisVec: root.stickerAxis[modelData.face]
                                source: "#Rectangle"
                                scale: Qt.vector3d(root.cubieSize * root.stickerInset / 100, root.cubieSize * root.stickerInset / 100, 1)
                                eulerRotation: Qt.vector3d(orient.rx, orient.ry, 0)
                                readonly property real faceOffset: root.cubieSize / 2 + 0.75
                                position: Qt.vector3d(axisVec[0] * faceOffset, axisVec[1] * faceOffset, axisVec[2] * faceOffset)

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

    StatsOverlay { active: root.showStats }

    QtObject {
        id: turnState
        property bool active: false
        property string axis: "y"
        property real angle: 90
        property var affected: []
    }

    NumberAnimation {
        id: pivotAnim
        target: pivot
        duration: 380
        easing.type: Easing.InOutQuad
        onFinished: root.finishTurn()
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
