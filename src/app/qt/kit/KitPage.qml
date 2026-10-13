import QtQuick
import QtQuick.Controls.Basic
import Cubeulator

// Page: the layout primitive every screen is built in (docs/design/system.md,
// "Size classes"). It fills its parent, paints the page, adds the page margin
// for the size class to the system's safe-area insets on all four edges, and
// scrolls the whole screen when its content is taller than the window.
//
// Nothing is sized by subtracting a constant from the window height: content
// takes the height it needs, and the page scrolls when that is more than
// there is. Screens that must shrink their picture first (scan, solve) set
// `scrolls: false` and manage their own height.
//
//   KitPage {
//       KitText { text: "Settings"; role: "title" }
//       ...
//   }
Item {
    id: root

    default property alias content: column.data
    // Scroll when the content is taller than the window.
    property bool scrolls: true
    // The page's own fill: "paper" or "surface".
    property string background: "paper"
    // Medium and wider screens centre a column this wide (560 at medium).
    property real maxContentWidth: Theme.sizeClass === "medium" ? 560 : (Theme.sizeClass === "expanded" ? 720 : 100000)

    // The margins, as the screens that lay things out beside the column need
    // them: the system's furniture plus the page margin, per edge.
    readonly property real marginTop: Theme.insets.top + Theme.pageMargin
    readonly property real marginBottom: Theme.insets.bottom + Theme.pageMargin
    readonly property real marginLeading: Theme.insets.leading + Theme.pageMargin
    readonly property real marginTrailing: Theme.insets.trailing + Theme.pageMargin
    readonly property real contentWidth: Math.max(0, Math.min(maxContentWidth, width - marginLeading - marginTrailing))

    objectName: "kitPage"
    anchors.fill: parent

    Rectangle {
        anchors.fill: parent
        color: root.background === "surface" ? Theme.surface : Theme.paper
        Accessible.ignored: true
    }

    Flickable {
        id: flick
        anchors.fill: parent
        contentWidth: width
        contentHeight: Math.max(height, column.implicitHeight + root.marginTop + root.marginBottom)
        interactive: root.scrolls && contentHeight > height
        boundsBehavior: Flickable.StopAtBounds
        clip: true

        ScrollBar.vertical: ScrollBar {
            visible: root.scrolls
        }

        Column {
            id: column
            // Centred when a maximum width applies; at the leading margin
            // otherwise.
            x: root.marginLeading + Math.max(0, (root.width - root.marginLeading - root.marginTrailing - root.contentWidth) / 2)
            y: root.marginTop
            width: root.contentWidth
            spacing: Theme.space4
        }
    }
}
