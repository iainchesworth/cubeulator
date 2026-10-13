import QtQuick
import Cubeulator

// The focus ring: 2 px of ink, 2 px outside the control, with the page
// showing between (docs/design/system.md, "Components"). Over the camera it
// is the overlay's ink. It is shown for keyboard focus only, never for a tap
// or a click; the control decides that and sets `shown`.
//
// Put it inside the control it surrounds; it grows outward from its parent.
Rectangle {
    id: root

    property bool shown: false
    property bool onOverlay: false

    readonly property real gap: Theme.borderRule

    anchors.fill: parent
    anchors.margins: -(Theme.borderRule + gap)
    color: "transparent"
    border.width: Theme.borderRule
    border.color: onOverlay ? Theme.overlay.ink : Theme.ink
    radius: Theme.radius
    visible: shown

    Accessible.ignored: true
}
