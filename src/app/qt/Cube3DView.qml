import QtQuick
import QtQuick3D
import Cubeulator

// GPU-rendered Rubik's cube built from 26 individually-positioned "cubies"
// (the centre piece is never visible, so it's skipped), each carrying its
// own 0-3 coloured stickers. Two things fall out of modelling it this way
// rather than as one 6-face shell:
//  - turnLayer() can animate a real slice turn (reparent the 9 affected
//    cubies under a temporary pivot, animate the pivot, then bake the
//    result back into each cubie) - the foundation the solve-visualisation
//    work needs to show "this section rotates like this".
//  - individual cubies/stickers can be recoloured or highlighted later the
//    way CubeView's progressCube does for the 2D technical sketch.
// Whole-cube dragging still free-spins cubeRoot; releasing eases it back to
// the presented orientation instead of leaving it wherever it was thrown.
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
    // 0 = modernist flat, 1 = glossy studio, 2 = toon outline (custom shader)
    property int shaderStyle: 0

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

    // 26 cubies (skip the fully-hidden centre). Each keeps its stickers on
    // fixed local faces forever; only pos/orientation change as layers turn.
    property var cubieDefs: {
        var defs = [];
        for (var xi = -1; xi <= 1; xi++) {
            for (var yi = -1; yi <= 1; yi++) {
                for (var zi = -1; zi <= 1; zi++) {
                    if (xi === 0 && yi === 0 && zi === 0)
                        continue;
                    var stickers = [];
                    if (yi === 1) stickers.push({ face: "py", color: root.topColor });
                    if (yi === -1) stickers.push({ face: "ny", color: root.bottomColor });
                    if (zi === 1) stickers.push({ face: "pz", color: root.frontColor });
                    if (zi === -1) stickers.push({ face: "nz", color: root.backColor });
                    if (xi === -1) stickers.push({ face: "nx", color: root.leftColor });
                    if (xi === 1) stickers.push({ face: "px", color: root.rightColor });
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
    Component.onCompleted: resetCubieStates()

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

        environment: SceneEnvironment {
            backgroundMode: SceneEnvironment.Transparent
            antialiasingMode: SceneEnvironment.MSAA
            antialiasingQuality: SceneEnvironment.High
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

        Node {
            id: cubeRoot
            eulerRotation: root.homeRotation

            Node { id: pivot }

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

                    Repeater3D {
                        model: cubieNode.modelData.stickers
                        visible: root.shaderStyle !== 2

                        Model {
                            required property var modelData
                            readonly property var orient: root.stickerOrientation[modelData.face]
                            readonly property var axisVec: root.stickerAxis[modelData.face]
                            source: "#Rectangle"
                            scale: Qt.vector3d(root.cubieSize * root.stickerInset / 100, root.cubieSize * root.stickerInset / 100, 1)
                            eulerRotation: Qt.vector3d(orient.rx, orient.ry, 0)
                            readonly property real faceOffset: root.cubieSize / 2 + 0.75
                            position: Qt.vector3d(axisVec[0] * faceOffset, axisVec[1] * faceOffset, axisVec[2] * faceOffset)

                            materials: PrincipledMaterial {
                                baseColor: modelData.color
                                roughness: root.shaderStyle === 1 ? 0.15 : 1.0
                                metalness: root.shaderStyle === 1 ? 0.35 : 0.0
                                specularAmount: root.shaderStyle === 1 ? 1.0 : 0.0
                            }
                        }
                    }

                    Repeater3D {
                        model: cubieNode.modelData.stickers
                        visible: root.shaderStyle === 2

                        Model {
                            required property var modelData
                            readonly property var orient: root.stickerOrientation[modelData.face]
                            readonly property var axisVec: root.stickerAxis[modelData.face]
                            source: "#Rectangle"
                            scale: Qt.vector3d(root.cubieSize * root.stickerInset / 100, root.cubieSize * root.stickerInset / 100, 1)
                            eulerRotation: Qt.vector3d(orient.rx, orient.ry, 0)
                            readonly property real faceOffset: root.cubieSize / 2 + 0.75
                            position: Qt.vector3d(axisVec[0] * faceOffset, axisVec[1] * faceOffset, axisVec[2] * faceOffset)

                            materials: CustomMaterial {
                                shadingMode: CustomMaterial.Shaded
                                fragmentShader: "shaders/toon.frag"
                                property color uBaseColor: modelData.color
                                property color uRimColor: Theme.accent
                                property real uRimPower: 2.2
                            }
                        }
                    }
                }
            }
        }
    }

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
        enabled: root.interactive
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
        running: root.autoRotate && !dragHandler.active && !springAnim.running
        onTriggered: cubeRoot.eulerRotation.y = cubeRoot.eulerRotation.y + root.autoRotateSpeed
    }
}
