import QtQuick
import QtQuick.Controls.Basic
import Cubeulator

// Choice (docs/design/system.md, "Choice"): two to four options, one chosen.
// A row of joined secondary buttons that wraps to a column when the labels do
// not fit. The chosen option is filled with ink and carries a check, so it is
// not told apart by colour alone.
//
// Arrow keys move the choice (and choose it); Tab enters the group once and
// leaves it once. In a right-to-left layout the horizontal arrows swap with
// the layout.
FocusScope {
    id: root

    // The options' labels, in order.
    property var options: []
    property int currentIndex: 0
    // What the group is called to a screen reader ("Theme").
    property string label: ""
    property bool onOverlay: false
    // For the gallery: show this option as hovered, pressed or focused.
    property int previewIndex: -1
    property string previewState: ""

    signal activated(int index)

    readonly property int count: options.length
    readonly property bool dark: onOverlay || Theme.effectiveDark
    readonly property real overlap: Theme.borderRule

    // Each option's natural width: its label on one line, a check, padding.
    // Reported by the options as they are made and as their labels change.
    property var naturals: []
    readonly property real totalNatural: {
        let sum = 0;
        for (let i = 0; i < naturals.length; ++i)
            sum += naturals[i] || 0;
        return sum;
    }
    function noteNatural(index, width) {
        if (naturals[index] === width)
            return;
        const copy = naturals.slice();
        copy[index] = width;
        naturals = copy;
    }
    // A row if the options fit side by side, sharing their edges; otherwise a
    // column. In a row the room left over is shared out equally.
    readonly property bool stacked: count > 0 && totalNatural - (count - 1) * overlap > width
    readonly property real spare: stacked || count === 0 ? 0
        : Math.max(0, (width - (totalNatural - (count - 1) * overlap)) / count)
    readonly property var contrastPairs: {
        const pairs = [];
        const page = onOverlay ? Theme.overlay.worstCase : Theme.paper;
        for (let i = 0; i < count; ++i) {
            const chosen = i === currentIndex;
            pairs.push({ name: "option " + i + (chosen ? " (chosen)" : ""),
                         fg: chosen ? Theme.colour("paper", dark) : (onOverlay ? Theme.overlay.ink : Theme.colour("ink", dark)),
                         bg: chosen ? Theme.colour("ink", dark) : page, target: 4.5 });
            pairs.push({ name: "option " + i + " edge",
                         fg: onOverlay ? Theme.overlay.outline : Theme.colour("outline", dark),
                         bg: page, target: 3.0 });
        }
        return pairs;
    }

    objectName: "kitChoice"
    implicitWidth: Math.max(totalNatural - Math.max(0, count - 1) * overlap, 1)
    implicitHeight: grid.implicitHeight

    // Roving: the group is one Tab stop, at the chosen option. The others are
    // reached by the arrow keys or a click.
    //
    // Assigned, not bound, and re-done when an option loses focus: on Qt 6.8.3
    // a Control that has active focus takes back a `false` written to its
    // activeFocusOnTab (so a binding on it, or on focusPolicy, leaves the old
    // option a Tab stop after the choice has moved). The option that held
    // focus when the choice changed is fixed up when focus leaves it, which is
    // always just after.
    function syncTabStops() {
        for (let i = 0; i < repeater.count; ++i) {
            const option = repeater.itemAt(i);
            if (option)
                option.activeFocusOnTab = (i === currentIndex);
        }
    }
    onCurrentIndexChanged: syncTabStops()

    function select(index) {
        if (index < 0 || index >= count || index === currentIndex)
            return;
        currentIndex = index;
        activated(index);
    }

    function moveBy(step) {
        const next = Math.max(0, Math.min(count - 1, currentIndex + step));
        select(next);
        const item = repeater.itemAt(next);
        if (item)
            item.forceActiveFocus(Qt.TabFocusReason);
    }

    Grid {
        id: grid
        width: root.width
        columns: root.stacked ? 1 : Math.max(1, root.count)
        rowSpacing: -root.overlap
        columnSpacing: -root.overlap

        Repeater {
            id: repeater
            model: root.options
            onItemAdded: root.syncTabStops()

            AbstractButton {
                id: option

                required property int index
                required property string modelData

                readonly property bool chosen: index === root.currentIndex
                readonly property bool preview: root.previewIndex === index
                readonly property bool showHover: enabled && ((preview && root.previewState === "hover") || hovered)
                readonly property bool showPressed: enabled && ((preview && root.previewState === "pressed") || down)
                readonly property bool showFocus: enabled && ((preview && root.previewState === "focus") || visualFocus)

                objectName: "option" + index
                text: modelData
                checkable: false
                hoverEnabled: true
                horizontalPadding: Theme.space4
                verticalPadding: Theme.space3
                focusPolicy: Qt.StrongFocus
                onActiveFocusChanged: {
                    if (!activeFocus)
                        root.syncTabStops();
                }

                width: root.stacked ? grid.width : implicitWidth + root.spare
                implicitWidth: label.implicitWidth + checkSlot + 2 * Theme.space4
                implicitHeight: Math.max(Theme.targetMin, label.implicitHeight + 2 * Theme.space3)

                readonly property real checkSlot: 20 + Theme.space2

                Component.onCompleted: root.noteNatural(index, implicitWidth)
                onImplicitWidthChanged: root.noteNatural(index, implicitWidth)

                background: Rectangle {
                    objectName: "background"
                    color: option.chosen ? Theme.colour("ink", root.dark)
                        : (option.showPressed ? Theme.colour("pressWash", root.dark)
                           : (option.showHover ? Theme.colour("hoverWash", root.dark) : "transparent"))
                    border.width: Theme.borderRule
                    border.color: !option.enabled ? Theme.colour("divider", root.dark)
                        : (root.onOverlay ? Theme.overlay.outline : Theme.colour("outline", root.dark))
                    radius: Theme.radius
                    Behavior on color {
                        ColorAnimation {
                            duration: Theme.motionQuick
                        }
                    }
                }

                contentItem: Item {
                    KitIcon {
                        id: check
                        visible: option.chosen
                        name: "check"
                        size: 20
                        color: Theme.colour("paper", root.dark)
                        anchors.left: parent.left
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    KitText {
                        id: label
                        objectName: "label"
                        text: option.text
                        role: "label"
                        horizontalAlignment: Text.AlignHCenter
                        color: !option.enabled ? Theme.colour("inkDisabled", root.dark)
                            : (option.chosen ? Theme.colour("paper", root.dark)
                               : (root.onOverlay ? Theme.overlay.ink : Theme.colour("ink", root.dark)))
                        anchors.left: parent.left
                        anchors.leftMargin: option.checkSlot
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        Accessible.ignored: true
                    }
                }

                KitFocusRing {
                    objectName: "focusRing"
                    shown: option.showFocus
                    onOverlay: root.onOverlay
                }

                HoverHandler {
                    enabled: option.enabled
                    cursorShape: Qt.PointingHandCursor
                }

                onClicked: {
                    root.select(index);
                    forceActiveFocus(Qt.MouseFocusReason);
                }

                // Space or Enter on the focused option chooses it, though the
                // arrow keys have already.
                Keys.onReturnPressed: event => {
                    root.select(index);
                    event.accepted = true;
                }
                Keys.onEnterPressed: event => {
                    root.select(index);
                    event.accepted = true;
                }
                Keys.onLeftPressed: event => {
                    root.moveBy(option.LayoutMirroring.enabled ? 1 : -1);
                    event.accepted = true;
                }
                Keys.onRightPressed: event => {
                    root.moveBy(option.LayoutMirroring.enabled ? -1 : 1);
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
                Accessible.name: text
                Accessible.checkable: true
                Accessible.checked: chosen
                Accessible.focusable: enabled
                Accessible.onPressAction: root.select(index)
            }
        }
    }

    Accessible.role: Accessible.Grouping
    Accessible.name: label
}
