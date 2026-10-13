import QtQuick
import QtQuick.Controls.Basic
import Cubeulator

// Button (docs/design/system.md, "Button"): a label with an optional leading
// icon, trailing chevron or key name. Three variants, three sizes.
//
// Built on AbstractButton, so it takes keyboard focus, answers Space, reports
// hover and press, and gives `visualFocus`, which is true for keyboard focus
// only: that is what the focus ring is for. The Basic style is imported so
// that customising it never trips the native styles' "does not support
// customization" warning.
AbstractButton {
    id: root

    // primary | secondary | ghost. One primary button a screen.
    property string variant: "primary"
    // regular (48 high) | thumb (56) | giant (96, the solve screen only)
    property string size: "regular"
    // Fills the width; the label sits at the leading edge and the chevron at
    // the trailing edge.
    property bool block: false
    property string leadingIcon: ""
    property string trailingIcon: ""
    // A key's name shown at the trailing side ("Space"), desktop reminder.
    property string keyName: ""
    // Drawn over the camera picture: takes the dark palette and the overlay's
    // edge and ink, whichever theme is in use.
    property bool onOverlay: false
    // What a screen reader says when it should differ from the visible label.
    property string accessibleName: ""
    property string accessibleDescription: ""
    // For the gallery and tests: "" | "hover" | "pressed" | "focus". Shows a
    // state without the pointer or the keyboard being there.
    property string previewState: ""

    readonly property real minHeight: size === "giant" ? Theme.targetHandsFull
        : (size === "thumb" ? Theme.targetThumb : Theme.targetMin)

    // The state the picture is drawn in.
    readonly property bool dark: onOverlay || Theme.effectiveDark
    readonly property bool showHover: enabled && (previewState === "hover" || hovered)
    readonly property bool showPressed: enabled && (previewState === "pressed" || down)
    readonly property bool showFocus: enabled && (previewState === "focus" || visualFocus)

    // What it draws, exposed so a test can hold the colours to the contrast in
    // tokens.json without guessing at the pixels.
    readonly property color fillColor: {
        if (!enabled)
            return variant === "primary" ? Theme.colour("surface", dark) : "transparent";
        if (variant === "primary") {
            return Theme.colour(showPressed ? "accentFillPress" : (showHover ? "accentFillHover" : "accentFill"), dark);
        }
        if (showPressed)
            return Theme.colour("pressWash", dark);
        if (showHover)
            return Theme.colour("hoverWash", dark);
        return "transparent";
    }
    readonly property color edgeColor: {
        if (!enabled)
            return variant === "ghost" ? "transparent" : Theme.colour("divider", dark);
        if (variant === "secondary")
            return onOverlay ? Theme.overlay.outline : Theme.colour("outline", dark);
        return "transparent";
    }
    readonly property color labelColor: {
        if (!enabled)
            return Theme.colour("inkDisabled", dark);
        if (variant === "primary")
            return Theme.colour("onAccent", dark);
        if (variant === "secondary")
            return onOverlay ? Theme.overlay.ink : Theme.colour("ink", dark);
        return Theme.colour("accentText", dark);
    }

    // What sits behind the button: the page, or over the camera the worst
    // picture a panel can be on. And every pairing the button draws, which the
    // tests hold to tokens.json's targets in both themes without having to
    // know what a button is.
    readonly property color pageColor: onOverlay ? Theme.overlay.worstCase : Theme.paper
    readonly property color behindLabel: fillColor.a > 0 ? Theme.blend(fillColor, pageColor) : pageColor
    readonly property var contrastPairs: {
        const pairs = [{ name: "label", fg: labelColor, bg: behindLabel, target: enabled ? 4.5 : 3.0 }];
        // The fill of a primary button is itself the boundary against the page.
        if (variant === "primary")
            pairs.push({ name: "fill", fg: behindLabel, bg: pageColor, target: enabled ? 3.0 : 1.0 });
        if (variant === "secondary" && enabled)
            pairs.push({ name: "edge", fg: edgeColor, bg: pageColor, target: 3.0 });
        return pairs;
    }

    objectName: "kitButton"
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    // The label and icons are inset from the button's edge, the same on both
    // sides so that a right-to-left layout needs nothing from it.
    horizontalPadding: Theme.space4
    verticalPadding: Theme.space3

    // As wide as its words want, but never wider than the screen's content
    // column: a label longer than that wraps (the label's own width follows).
    implicitWidth: Math.min(Math.max(minHeight, content.implicitWidth + 2 * Theme.space4),
                            Math.max(minHeight, Theme.windowWidth - 2 * Theme.pageMargin))
    // A block button fills its parent unless it is given a width of its own.
    width: block && parent ? parent.width : implicitWidth
    implicitHeight: Math.max(minHeight, content.implicitHeight + 2 * Theme.space3)

    background: Rectangle {
        objectName: "background"
        color: root.fillColor
        border.width: Theme.borderRule
        border.color: root.edgeColor
        radius: Theme.radius
        Behavior on color {
            ColorAnimation {
                duration: Theme.motionQuick
            }
        }
    }

    contentItem: Item {
        id: content

        readonly property real iconSize: 20
        readonly property bool hasLeading: root.leadingIcon !== ""
        readonly property bool hasTrailing: root.trailingIcon !== ""
        readonly property bool hasKey: root.keyName !== ""
        readonly property real trailingWidth: (hasTrailing ? iconSize + Theme.space2 : 0)
            + (hasKey ? keyBox.implicitWidth + Theme.space2 : 0)

        implicitWidth: (hasLeading ? iconSize + Theme.space2 : 0) + label.implicitWidth + trailingWidth
        implicitHeight: Math.max(label.implicitHeight, iconSize)

        KitIcon {
            id: leading
            visible: content.hasLeading
            name: root.leadingIcon
            size: content.iconSize
            color: root.labelColor
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
        }

        KitText {
            id: label
            objectName: "label"
            text: root.text
            role: "label"
            variant: root.size === "giant" ? "giant" : ""
            color: root.labelColor
            horizontalAlignment: root.block ? Text.AlignLeft : Text.AlignHCenter
            anchors.left: content.hasLeading ? leading.right : parent.left
            anchors.leftMargin: content.hasLeading ? Theme.space2 : 0
            anchors.right: parent.right
            anchors.rightMargin: content.trailingWidth
            anchors.verticalCenter: parent.verticalCenter
            // The button names itself; its parts are not separate stops.
            Accessible.ignored: true
        }

        KitKeyName {
            id: keyBox
            visible: content.hasKey
            text: root.keyName
            ink: root.labelColor
            anchors.right: parent.right
            anchors.rightMargin: content.hasTrailing ? content.iconSize + Theme.space2 : 0
            anchors.verticalCenter: parent.verticalCenter
        }

        KitIcon {
            id: trailing
            visible: content.hasTrailing
            name: root.trailingIcon
            size: content.iconSize
            color: root.labelColor
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
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

    // Space is Controls' own. Enter and Return activate too: a person with a
    // keyboard expects both, and so does a switch or a pedal that sends one.
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
    Accessible.description: accessibleDescription
    Accessible.focusable: enabled
    Accessible.onPressAction: root.click()
}
