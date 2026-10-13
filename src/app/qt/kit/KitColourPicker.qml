import QtQuick
import QtQuick.Controls.Basic
import Cubeulator

// Colour picker (docs/design/system.md, "Colour picker"): six buttons, each a
// 56 px sticker with its mark and the colour's name beneath. One is chosen:
// a 3 px ink edge and a check. Keys 1 to 6 choose, in the order shown, and
// the arrow keys move the choice; Tab enters and leaves the group once.
//
// The six keep their order and their numbers in a right-to-left layout.
FocusScope {
    id: root

    // The colours' names, in the language, in Theme.cubeColourNames order
    // (white, yellow, orange, red, green, blue).
    property var labels: []
    // The chosen colour's key, or "" for none.
    property string current: ""
    // What the group is called to a screen reader ("Paint with").
    property string title: ""
    property real stickerSize: Theme.targetThumb
    // When on, 1 to 6 choose from anywhere in the window, not only with focus
    // in the group (the review and hand-entry screens want this).
    property bool globalKeys: false
    property bool onOverlay: false

    signal chosen(string colour)

    readonly property var names: Theme.cubeColourNames
    readonly property int currentIndex: names.indexOf(current)

    readonly property color pageColor: onOverlay ? Theme.overlay.worstCase : Theme.paper
    readonly property var contrastPairs: [
        { name: "label", fg: onOverlay ? Theme.overlay.ink : Theme.ink, bg: pageColor, target: 4.5 },
        { name: "chosen edge", fg: onOverlay ? Theme.overlay.ink : Theme.ink, bg: pageColor, target: 3.0 }
    ]

    objectName: "kitColourPicker"
    implicitWidth: flow.implicitWidth
    implicitHeight: flow.implicitHeight

    // Roving: a chosen colour is the one tab stop; with none chosen, the first.
    readonly property int tabStop: currentIndex >= 0 ? currentIndex : 0

    // One Tab stop for the group: the chosen colour, or the first when none is.
    // Assigned rather than bound, and redone when an option loses focus; see
    // KitChoice.syncTabStops for why.
    function syncTabStops() {
        for (let i = 0; i < repeater.count; ++i) {
            const option = repeater.itemAt(i);
            if (option)
                option.activeFocusOnTab = (i === tabStop);
        }
    }
    onTabStopChanged: syncTabStops()

    function choose(index) {
        if (index < 0 || index >= names.length)
            return;
        const colour = names[index];
        const changed = colour !== current;
        current = colour;
        if (changed)
            chosen(colour);
    }
    function moveBy(step) {
        const from = currentIndex >= 0 ? currentIndex : 0;
        const next = Math.max(0, Math.min(names.length - 1, from + step));
        choose(next);
        const item = repeater.itemAt(next);
        if (item)
            item.forceActiveFocus(Qt.TabFocusReason);
    }

    Flow {
        id: flow
        width: root.width
        spacing: Theme.space2

        Repeater {
            id: repeater
            model: root.names
            onItemAdded: root.syncTabStops()

            AbstractButton {
                id: option

                required property int index
                required property string modelData

                readonly property bool isChosen: index === root.currentIndex
                readonly property string label: index < root.labels.length ? root.labels[index] : ""

                objectName: "colour/" + modelData
                text: label
                hoverEnabled: true
                focusPolicy: Qt.StrongFocus
                onActiveFocusChanged: {
                    if (!activeFocus)
                        root.syncTabStops();
                }

                implicitWidth: Math.max(root.stickerSize + 2 * Theme.space2, caption.implicitWidth + Theme.space2)
                implicitHeight: root.stickerSize + Theme.space2 + caption.implicitHeight + 2 * Theme.space2

                background: Rectangle {
                    color: option.down ? Theme.colour("pressWash", root.onOverlay || Theme.effectiveDark)
                        : (option.hovered ? Theme.colour("hoverWash", root.onOverlay || Theme.effectiveDark) : "transparent")
                    radius: Theme.radius
                }

                contentItem: Item {
                    // The 3 px edge of the chosen colour, drawn outside its
                    // sticker so the sticker's own size is the same chosen or
                    // not.
                    Rectangle {
                        visible: option.isChosen
                        width: root.stickerSize + 2 * Theme.borderHeavy
                        height: root.stickerSize + 2 * Theme.borderHeavy
                        anchors.horizontalCenter: parent.horizontalCenter
                        y: Theme.space2 - Theme.borderHeavy
                        color: "transparent"
                        border.width: Theme.borderHeavy
                        border.color: root.onOverlay ? Theme.overlay.ink : Theme.ink
                    }
                    KitSticker {
                        id: sticker
                        colourName: option.modelData
                        size: root.stickerSize
                        onPage: true
                        anchors.horizontalCenter: parent.horizontalCenter
                        y: Theme.space2
                    }
                    // A check in the corner as well: chosen is never colour alone.
                    Rectangle {
                        visible: option.isChosen
                        width: 20
                        height: 20
                        anchors.right: sticker.right
                        anchors.top: sticker.top
                        color: root.onOverlay ? Theme.overlay.ink : Theme.ink
                        KitIcon {
                            anchors.centerIn: parent
                            name: "check"
                            size: 14
                            strokeWidth: 3
                            color: root.onOverlay ? Theme.overlay.worstCase : Theme.paper
                        }
                    }
                    KitText {
                        id: caption
                        objectName: "caption"
                        text: option.label
                        role: "small"
                        onOverlay: root.onOverlay
                        horizontalAlignment: Text.AlignHCenter
                        anchors.top: sticker.bottom
                        anchors.topMargin: Theme.space2
                        anchors.left: parent.left
                        anchors.right: parent.right
                        Accessible.ignored: true
                    }
                }

                KitFocusRing {
                    objectName: "focusRing"
                    shown: option.visualFocus
                    onOverlay: root.onOverlay
                }

                HoverHandler {
                    cursorShape: Qt.PointingHandCursor
                }

                onClicked: {
                    root.choose(index);
                    forceActiveFocus(Qt.MouseFocusReason);
                }

                Keys.onReturnPressed: event => {
                    root.choose(index);
                    event.accepted = true;
                }
                Keys.onEnterPressed: event => {
                    root.choose(index);
                    event.accepted = true;
                }
                // The swatches keep their order whatever the layout, so the
                // arrow keys do not swap here.
                Keys.onLeftPressed: event => {
                    root.moveBy(-1);
                    event.accepted = true;
                }
                Keys.onRightPressed: event => {
                    root.moveBy(1);
                    event.accepted = true;
                }
                Keys.onUpPressed: event => {
                    root.moveBy(-1);
                    event.accepted = true;
                }
                Keys.onDownPressed: event => {
                    root.moveBy(1);
                    event.accepted = true;
                }

                Accessible.role: Accessible.RadioButton
                Accessible.name: label
                Accessible.checkable: true
                Accessible.checked: isChosen
                Accessible.focusable: enabled
                Accessible.onPressAction: root.choose(index)
            }
        }
    }

    // 1 to 6 choose, in the order shown. Within the group by default.
    Keys.onPressed: event => {
        if (event.modifiers === Qt.NoModifier && event.key >= Qt.Key_1 && event.key <= Qt.Key_6) {
            root.choose(event.key - Qt.Key_1);
            event.accepted = true;
        }
    }
    Repeater {
        model: 6
        Item {
            required property int index
            Shortcut {
                sequence: String(index + 1)
                enabled: root.globalKeys
                onActivated: root.choose(index)
            }
        }
    }

    // Left-to-right whatever the language: the colours are a sequence with
    // numbers on their keys.
    LayoutMirroring.enabled: false
    LayoutMirroring.childrenInherit: true

    Accessible.role: Accessible.Grouping
    Accessible.name: title
}
