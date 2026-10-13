import QtQuick
import QtQuick.Controls.Basic
import Cubeulator

// Switch (docs/design/system.md, "Switch"): for a setting that is on or off.
// A row, label and note at the leading side, the switch at the trailing side;
// the whole row is the target, at least 56 high.
//
// On and off differ by position and by the check, not by colour: the thumb
// sits at the leading end when off and the trailing end when on, and carries
// a check when on.
AbstractButton {
    id: root

    // The label is `text`. The note under it says what it does, or, when the
    // switch is disabled, why.
    property string note: ""
    property bool onOverlay: false
    property string accessibleName: ""
    // For the gallery and tests: "" | "hover" | "pressed" | "focus".
    property string previewState: ""

    readonly property bool dark: onOverlay || Theme.effectiveDark
    readonly property bool showHover: enabled && (previewState === "hover" || hovered)
    readonly property bool showPressed: enabled && (previewState === "pressed" || down)
    readonly property bool showFocus: enabled && (previewState === "focus" || visualFocus)

    readonly property color rowFill: showPressed ? Theme.colour("pressWash", dark)
        : (showHover ? Theme.colour("hoverWash", dark) : "transparent")
    readonly property color labelColor: enabled ? (onOverlay ? Theme.overlay.ink : Theme.colour("ink", dark))
                                               : Theme.colour("inkDisabled", dark)
    readonly property color noteColor: enabled ? (onOverlay ? Theme.overlay.inkSecondary : Theme.colour("inkSecondary", dark))
                                               : Theme.colour("inkDisabled", dark)
    // The track's edge when off, its fill when on.
    //
    // `outline` is 3:1 on the page and on a pane but not on the hover and press
    // washes the whole row takes (2.96 and 2.74 in the light theme, 3.13 and
    // 2.87 in the dark: docs/design/tokens.json has no such pairing, so
    // check_tokens.py does not see it). While the row is hovered or pressed the
    // off switch's edge and thumb are `ink` instead, which holds 3:1 on both.
    readonly property bool onWash: showHover || showPressed
    readonly property color restingEdge: onOverlay ? Theme.overlay.outline : Theme.colour("outline", dark)
    readonly property color offEdge: onWash && !onOverlay ? Theme.colour("ink", dark) : restingEdge
    readonly property color trackFill: checked && enabled ? Theme.colour("accentFill", dark) : "transparent"
    readonly property color trackEdge: !enabled ? Theme.colour("divider", dark)
        : (checked ? Theme.colour("accentFill", dark) : offEdge)
    readonly property color thumbFill: !enabled ? Theme.colour("divider", dark)
        : (checked ? Theme.colour("onAccent", dark) : offEdge)
    readonly property color pageColor: onOverlay ? Theme.overlay.worstCase : Theme.paper
    readonly property color rowBehind: rowFill.a > 0 ? Theme.blend(rowFill, pageColor) : pageColor

    readonly property var contrastPairs: [
        { name: "label", fg: labelColor, bg: rowBehind, target: enabled ? 4.5 : 3.0 },
        { name: "note", fg: noteColor, bg: rowBehind, target: enabled ? 4.5 : 3.0 },
        { name: "track edge", fg: trackEdge, bg: rowBehind, target: enabled ? 3.0 : 1.0 },
        { name: "thumb", fg: thumbFill, bg: trackFill.a > 0 ? trackFill : rowBehind, target: enabled ? 3.0 : 1.0 }
    ]

    objectName: "kitSwitch"
    checkable: true
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    implicitWidth: 320
    implicitHeight: Math.max(Theme.targetThumb, textColumn.implicitHeight + 2 * Theme.space3)

    background: Rectangle {
        color: root.rowFill
        radius: Theme.radius
        Behavior on color {
            ColorAnimation {
                duration: Theme.motionQuick
            }
        }
    }

    contentItem: Item {
        Column {
            id: textColumn
            anchors.left: parent.left
            anchors.right: track.left
            anchors.rightMargin: Theme.space4
            anchors.verticalCenter: parent.verticalCenter
            spacing: Theme.space1

            KitText {
                objectName: "label"
                width: parent.width
                text: root.text
                role: "bodyStrong"
                color: root.labelColor
                Accessible.ignored: true
            }
            KitText {
                objectName: "note"
                width: parent.width
                visible: root.note !== ""
                text: root.note
                role: "small"
                color: root.noteColor
                Accessible.ignored: true
            }
        }

        // 52 x 32 track; 24 x 24 square thumb with a 4 px margin.
        Rectangle {
            id: track
            objectName: "track"
            width: 52
            height: 32
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            color: root.trackFill
            border.width: Theme.borderRule
            border.color: root.trackEdge
            radius: Theme.radius
            Behavior on color {
                ColorAnimation {
                    duration: Theme.motionQuick
                }
            }

            Rectangle {
                id: thumb
                objectName: "thumb"
                width: 24
                height: 24
                y: 4
                // Leading end when off, trailing when on; the layout's
                // direction decides which side of the track that is.
                x: (root.checked !== track.LayoutMirroring.enabled) ? track.width - width - 4 : 4
                color: root.thumbFill
                radius: Theme.radius
                Behavior on x {
                    NumberAnimation {
                        duration: Theme.motionQuick
                        easing.type: Easing.OutCubic
                    }
                }

                KitIcon {
                    anchors.centerIn: parent
                    visible: root.checked
                    name: "check"
                    size: 16
                    strokeWidth: 3
                    color: root.trackFill.a > 0 ? root.trackFill : Theme.colour("accentFill", root.dark)
                }
            }
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

    Accessible.role: Accessible.CheckBox
    Accessible.name: accessibleName !== "" ? accessibleName : text
    Accessible.description: note
    Accessible.checkable: true
    Accessible.checked: checked
    Accessible.focusable: enabled
    Accessible.onToggleAction: root.toggle()
    Accessible.onPressAction: root.toggle()
}
