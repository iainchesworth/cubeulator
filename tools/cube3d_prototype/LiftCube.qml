import QtQuick
import QtQuick3D
import CubePrototype

// The layer-lift renderer. The cube at rest is one mesh. While a layer turns it
// is two: the layer, hung on a pivot, and everything else, with solid black where
// they part. Nothing is built per cubie and nothing is bound per sticker, so
// a move costs the same however many stickers the cube has.
Item {
    id: root

    required property Player player
    required property FrameStats stats
    property int turnMs: 160

    // How many objects the scene holds, how big its meshes are, and how many times
    // they were built (for the report).
    readonly property int objects: 3
    property int quads: whole.quads
    property int builds: whole.builds + rest.builds + layer.builds

    property bool turning: false
    property var move: ({ axis: 0, layer: 0, degrees: 0 })
    property real angle: 0
    signal finished()

    // Starts the next move. False once there are none.
    function begin() {
        var next = player.pendingMove()
        if (next.degrees === undefined)
            return false
        var t0 = stats.nowMicros()
        move = next
        turning = true
        stats.tally("pickUp", stats.nowMicros() - t0)
        angle = 0
        turn.from = 0
        turn.to = next.degrees
        turn.duration = root.turnMs * (Math.abs(next.degrees) > 90 ? 1.5 : 1)
        turn.start()
        return true
    }

    function end() {
        var t0 = stats.nowMicros()
        player.commit()
        whole.revision = player.revision
        turning = false
        angle = 0
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
            id: stickers
            vertexColorsEnabled: true
            baseColor: "white"
            metalness: 0
            roughness: 0.38
        }

        Node {
            eulerRotation: Qt.vector3d(-24, -32, 0)

            Model {
                visible: !root.turning
                materials: stickers
                geometry: CubeGeometry {
                    id: whole
                    player: root.player
                    part: 0
                    active: !root.turning
                }
            }
            Model {
                visible: root.turning
                materials: stickers
                geometry: CubeGeometry {
                    id: rest
                    player: root.player
                    part: 1
                    axis: root.move.axis
                    layer: root.move.layer
                    active: root.turning
                }
            }
            Node {
                visible: root.turning
                rotation: Quaternion.fromAxisAndAngle(
                    Qt.vector3d(root.move.axis === 0 ? 1 : 0, root.move.axis === 1 ? 1 : 0,
                                root.move.axis === 2 ? 1 : 0), root.angle)
                Model {
                    materials: stickers
                    geometry: CubeGeometry {
                        id: layer
                        player: root.player
                        part: 2
                        axis: root.move.axis
                        layer: root.move.layer
                        active: root.turning
                    }
                }
            }
        }
    }
}
