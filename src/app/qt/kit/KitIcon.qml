import QtQuick
import QtQuick.Shapes
import Cubeulator

// One icon from KitIcons, drawn as a Shape so it stays crisp at any size and
// takes a token colour. An icon is decoration: the control it sits in carries
// the name, so it is ignored by screen readers (docs/design/accessibility.md).
Item {
    id: root

    property string name: ""
    property color color: Theme.ink
    // Side of the square it is drawn in, in pixels. The artwork is a 24 grid,
    // scaled to fit; the stroke scales with it.
    property real size: 24
    property real strokeWidth: 2

    // Flip horizontally when the layout is mirrored: the back chevron, the
    // chevron on a row. Turn directions never flip; they describe the cube.
    readonly property bool mirrored: !KitIcons.isTurn(name)
        && KitIcons.mirrors(name, Theme.script)
        && (parent ? parent.LayoutMirroring.enabled : false)

    implicitWidth: size
    implicitHeight: size

    // The icon decides its own mirroring above, from its parent; its own
    // direction is never flipped by an ancestor.
    LayoutMirroring.enabled: false
    LayoutMirroring.childrenInherit: true

    Accessible.ignored: true

    Item {
        width: 24
        height: 24
        scale: root.size / 24
        transformOrigin: Item.TopLeft
        transform: Scale {
            xScale: root.mirrored ? -1 : 1
            origin.x: 12
        }

        Shape {
            anchors.fill: parent
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                strokeColor: root.color
                strokeWidth: root.strokeWidth
                fillColor: "transparent"
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                PathSvg {
                    path: KitIcons.path(root.name)
                }
            }
        }
    }
}
