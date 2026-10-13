import QtQuick
import QtQuick.Controls.Basic
import Cubeulator

// Icon button (docs/design/system.md, "Icon button"): a 26 px icon in a
// 56 x 56 target. States as the ghost button. Used for back, pause and close
// only; an icon never stands alone anywhere else.
//
// `text` is what it is called: the accessible name, and the tooltip on
// desktop after 600 ms of hover or at once on keyboard focus. It is not drawn
// on the button.
AbstractButton {
    id: root

    // A name from KitIcons. Not `icon`: that is AbstractButton's own, and final.
    property string iconName: ""
    property bool onOverlay: false
    property string accessibleName: ""
    // For the gallery and tests: "" | "hover" | "pressed" | "focus".
    property string previewState: ""
    // Tooltips are for a pointer; a phone has none.
    property bool tooltips: Qt.platform.os !== "android" && Qt.platform.os !== "ios"

    readonly property bool dark: onOverlay || Theme.effectiveDark
    readonly property bool showHover: enabled && (previewState === "hover" || hovered)
    readonly property bool showPressed: enabled && (previewState === "pressed" || down)
    readonly property bool showFocus: enabled && (previewState === "focus" || visualFocus)

    readonly property color fillColor: showPressed ? Theme.colour("pressWash", dark)
        : (showHover ? Theme.colour("hoverWash", dark) : "transparent")
    // The icon is the ghost button's label colour.
    readonly property color iconColor: !enabled ? Theme.colour("inkDisabled", dark)
        : (onOverlay ? Theme.overlay.ink : Theme.colour("ink", dark))
    readonly property color pageColor: onOverlay ? Theme.overlay.worstCase : Theme.paper
    readonly property var contrastPairs: [
        { name: "icon", fg: iconColor, bg: fillColor.a > 0 ? Theme.blend(fillColor, pageColor) : pageColor,
          target: enabled ? 3.0 : 3.0 }
    ]

    objectName: "kitIconButton"
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    implicitWidth: Theme.targetThumb
    implicitHeight: Theme.targetThumb

    background: Rectangle {
        color: root.fillColor
        radius: Theme.radius
        Behavior on color {
            ColorAnimation {
                duration: Theme.motionQuick
            }
        }
    }

    contentItem: Item {
        KitIcon {
            objectName: "icon"
            anchors.centerIn: parent
            name: root.iconName
            size: 26
            color: root.iconColor
        }
    }

    KitFocusRing {
        objectName: "focusRing"
        shown: root.showFocus
        onOverlay: root.onOverlay
    }

    HoverHandler {
        enabled: root.enabled
        cursorShape: Qt.PointingHandCursor
    }

    // ---- Tooltip -------------------------------------------------------------
    property bool tipDue: false
    Timer {
        id: hoverDelay
        interval: 600
        running: root.tooltips && root.hovered && !root.visualFocus
        onTriggered: root.tipDue = true
    }
    onHoveredChanged: if (!hovered) tipDue = false

    Popup {
        id: tip
        parent: root
        visible: root.tooltips && root.text !== "" && (root.visualFocus || root.tipDue)
        x: (root.width - width) / 2
        y: root.height + Theme.space1
        padding: Theme.space2
        closePolicy: Popup.NoAutoClose
        focus: false
        modal: false
        background: Rectangle {
            color: Theme.surface
            border.width: Theme.borderHair
            border.color: Theme.outline
        }
        contentItem: KitText {
            text: root.text
            role: "small"
            wrapMode: Text.NoWrap
            Accessible.ignored: true
        }
        Accessible.ignored: true
    }

    Keys.onReturnPressed: event => {
        if (root.enabled) {
            root.click();
            event.accepted = true;
        }
    }
    Keys.onEnterPressed: event => {
        if (root.enabled) {
            root.click();
            event.accepted = true;
        }
    }

    Accessible.role: Accessible.Button
    Accessible.name: accessibleName !== "" ? accessibleName : text
    Accessible.focusable: enabled
    Accessible.onPressAction: root.click()
}
