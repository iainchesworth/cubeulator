import QtQuick
import QtQuick3D
import CubePrototype

// The comparison: a cube built the way Cube3DView.qml builds a 3x3, only larger.
// One object for every cubie that shows anything (a body and a sticker for each
// face that shows), each sticker with a material of its own bound to the colour
// of its facelet, and a layer lifted by hanging its cubies off a pivot. Nothing
// here is slow by mistake: it is what scaling the 3x3's view up would be.
Item {
    id: root

    required property Player player
    required property FrameStats stats
    property int turnMs: 160

    property int objects: 0
    readonly property int quads: 0
    readonly property int builds: 0

    property bool turning: false
    property var move: ({ axis: 0, layer: 0, degrees: 0 })
    property real angle: 0
    signal finished()

    readonly property real reach: (player.size - 1) / 2
    readonly property var cubieList: player.visibleCubies()

    property var held: []

    function coordinate(cubie, axis) {
        return axis === 0 ? cubie.x : (axis === 1 ? cubie.y : cubie.z)
    }

    function begin() {
        var next = player.pendingMove()
        if (next.degrees === undefined)
            return false
        var t0 = stats.nowMicros()
        move = next
        var list = []
        for (var i = 0; i < cubies.count; i++) {
            var item = cubies.objectAt(i)
            if (root.coordinate(root.cubieList[i], next.axis) === next.layer) {
                item.parent = pivot
                list.push(item)
            }
        }
        held = list
        pivot.visible = true
        angle = 0
        stats.tally("pickUp", stats.nowMicros() - t0)
        turn.from = 0
        turn.to = next.degrees
        turn.duration = root.turnMs * (Math.abs(next.degrees) > 90 ? 1.5 : 1)
        turn.start()
        return true
    }

    function end() {
        var t0 = stats.nowMicros()
        for (var i = 0; i < held.length; i++)
            held[i].parent = hold
        held = []
        angle = 0
        // Every cubie is where it started, repainted as the cube now stands.
        player.commit()
        stats.tally("setDown", stats.nowMicros() - t0)
        root.finished()
    }

    NumberAnimation {
        id: turn
        target: root
        property: "angle"
        onFinished: root.end()
    }

    View3D {
        anchors.fill: parent
        camera: camera
        environment: SceneEnvironment {
            clearColor: "#1d2127"
            backgroundMode: SceneEnvironment.Color
            antialiasingMode: SceneEnvironment.MSAA
            antialiasingQuality: SceneEnvironment.Medium
        }

        PerspectiveCamera {
            id: camera
            position: Qt.vector3d(0, 0, root.player.size * 2.3 + 5)
            clipNear: 1
            clipFar: 200
        }
        DirectionalLight {
            eulerRotation: Qt.vector3d(-32, -28, 0)
            brightness: 1.6
            ambientColor: "#303030"
        }
        DirectionalLight {
            eulerRotation: Qt.vector3d(28, 150, 0)
            brightness: 0.6
        }

        PrincipledMaterial {
            id: bodyMaterial
            baseColor: "#101010"
            metalness: 0
            roughness: 0.5
        }

        Node {
            id: hold
            eulerRotation: Qt.vector3d(-24, -32, 0)

            Node {
                id: pivot
                visible: false
                rotation: Quaternion.fromAxisAndAngle(
                    Qt.vector3d(root.move.axis === 0 ? 1 : 0, root.move.axis === 1 ? 1 : 0,
                                root.move.axis === 2 ? 1 : 0), root.angle)
            }

            Repeater3D {
                id: cubies
                model: root.cubieList.length
                delegate: Node {
                    id: cubie
                    required property int index
                    readonly property var def: root.cubieList[index]
                    position: Qt.vector3d(def.x - root.reach, def.y - root.reach, def.z - root.reach)
                    Component.onCompleted: root.objects += 1 + def.stickers.length

                    Model {
                        source: "#Cube"
                        scale: Qt.vector3d(0.0098, 0.0098, 0.0098)
                        materials: bodyMaterial
                    }
                    Repeater3D {
                        model: cubie.def.stickers
                        delegate: Model {
                            id: sticker
                            required property var modelData
                            readonly property int d: modelData.direction
                            source: "#Rectangle"
                            scale: Qt.vector3d(0.0086, 0.0086, 1)
                            // A rectangle faces +z; stand it on the face it is for.
                            eulerRotation: d === 5 ? Qt.vector3d(0, 180, 0)
                                         : d === 0 ? Qt.vector3d(0, 90, 0)
                                         : d === 1 ? Qt.vector3d(0, -90, 0)
                                         : d === 2 ? Qt.vector3d(-90, 0, 0)
                                         : d === 3 ? Qt.vector3d(90, 0, 0)
                                         : Qt.vector3d(0, 0, 0)
                            position: Qt.vector3d(d === 0 ? 0.51 : (d === 1 ? -0.51 : 0),
                                                  d === 2 ? 0.51 : (d === 3 ? -0.51 : 0),
                                                  d === 4 ? 0.51 : (d === 5 ? -0.51 : 0))
                            materials: PrincipledMaterial {
                                baseColor: { root.player.revision; return root.player.colourAt(sticker.modelData.facelet) }
                                metalness: 0
                                roughness: 0.38
                            }
                        }
                    }
                }
            }
        }
    }
}
