import QtQuick
import QtQuick.Controls.Basic
import Cubeulator

// List row (docs/design/system.md, "List row"): a row that goes somewhere or
// shows a value. Label, an optional value in the secondary ink, a trailing
// chevron. At least 56 high, full width, a hairline beneath. States as the
// ghost button with an `ink` label. Used in Settings, Help, History,
// Licences and sheets.
AbstractButton {
    id: root

    // The label is `text`. `value` is what the row shows ("English").
    property string value: ""
    // A row that shows a value and goes nowhere has no chevron.
    property bool chevron: true
    // The current row, or the chosen one: filled with the accent's tint.
    property bool highlighted: false
    property bool onOverlay: false
    property string accessibleName: ""
    // For the gallery and tests: "" | "hover" | "pressed" | "focus".
    property string previewState: ""

    readonly property bool dark: onOverlay || Theme.effectiveDark
    readonly property bool showHover: enabled && (previewState === "hover" || hovered)
    readonly property bool showPressed: enabled && (previewState === "pressed" || down)
    readonly property bool showFocus: enabled && (previewState === "focus" || visualFocus)

    readonly property color fillColor: showPressed ? Theme.colour("pressWash", dark)
        : (showHover ? Theme.colour("hoverWash", dark)
           : (highlighted ? Theme.colour("accentTint", dark) : "transparent"))
    readonly property color labelColor: enabled ? (onOverlay ? Theme.overlay.ink : Theme.colour("ink", dark))
                                               : Theme.colour("inkDisabled", dark)
    readonly property color valueColor: enabled ? (onOverlay ? Theme.overlay.inkSecondary : Theme.colour("inkSecondary", dark))
                                               : Theme.colour("inkDisabled", dark)
    readonly property color pageColor: onOverlay ? Theme.overlay.worstCase : Theme.paper
    readonly property color behindColor: fillColor.a > 0 ? Theme.blend(fillColor, pageColor) : pageColor

    readonly property var contrastPairs: [
        { name: "label", fg: labelColor, bg: behindColor, target: enabled ? 4.5 : 3.0 },
        { name: "value", fg: valueColor, bg: behindColor, target: enabled ? 4.5 : 3.0 }
    ]

    objectName: "kitListRow"
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    implicitWidth: 320
    implicitHeight: Math.max(Theme.targetThumb, content.implicitHeight + 2 * Theme.space3 + Theme.borderHair)

    background: Item {
        Rectangle {
            anchors.fill: parent
            color: root.fillColor
            Behavior on color {
                ColorAnimation {
                    duration: Theme.motionQuick
                }
            }
        }
        // The hairline beneath.
        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: Theme.borderHair
            color: root.onOverlay ? Theme.overlay.outline : Theme.colour("divider", root.dark)
        }
    }

    contentItem: Item {
        id: content
        implicitHeight: Math.max(labelText.implicitHeight, valueText.visible ? valueText.implicitHeight : 0, 24)

        KitIcon {
            id: arrow
            visible: root.chevron
            name: "chevron-right"
            size: 20
            color: root.valueColor
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
        }
        KitText {
            id: valueText
            objectName: "value"
            visible: root.value !== ""
            text: root.value
            role: "body"
            color: root.valueColor
            horizontalAlignment: Text.AlignRight
            width: Math.min(implicitWidth, content.width * 0.45)
            anchors.right: root.chevron ? arrow.left : parent.right
            anchors.rightMargin: root.chevron ? Theme.space2 : 0
            anchors.verticalCenter: parent.verticalCenter
            Accessible.ignored: true
        }
        KitText {
            id: labelText
            objectName: "label"
            text: root.text
            role: "body"
            color: root.labelColor
            anchors.left: parent.left
            anchors.right: valueText.visible ? valueText.left : (root.chevron ? arrow.left : parent.right)
            anchors.rightMargin: Theme.space3
            anchors.verticalCenter: parent.verticalCenter
            Accessible.ignored: true
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
    Accessible.name: accessibleName !== "" ? accessibleName : (value !== "" ? text + ", " + value : text)
    Accessible.focusable: enabled
    Accessible.onPressAction: root.click()
}
