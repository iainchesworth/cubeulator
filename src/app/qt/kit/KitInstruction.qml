import QtQuick
import Cubeulator

// Instruction block (docs/design/system.md, "Instruction block"): the solve
// screen's instruction, in three lines.
//
//   1. a swatch of the side's centre colour and the side's name, in
//      `instruction` type;
//   2. a direction icon in `accentText` and the direction, in `direction` type;
//   3. the amount in `body`, `inkSecondary`; then the notation in a key-name
//      box if it is switched on.
//
// It describes the cube in the person's hand, so it never mirrors in a
// right-to-left layout; the direction words are the language's own. Each line
// wraps inside the width it has: at large text sizes the instruction grows
// taller, it is never cut.
Item {
    id: root

    // "Right", "Top": the side to turn, in the language.
    property string side: ""
    // The colour of that side's centre: white | yellow | orange | red | green | blue.
    property string sideColour: "white"
    // "downwards", "to the left", "clockwise"
    property string direction: ""
    // A name from KitIcons beginning "turn-".
    property string directionIcon: "turn-down"
    // "Quarter turn"
    property string amount: ""
    // "R'". Shown in a key-name box when not empty.
    property string notation: ""
    // Centred on expanded layouts, at the leading edge on compact.
    property bool centred: false
    // What a screen reader is told when it should differ from the three
    // lines read in order.
    property string accessibleName: ""

    readonly property var contrastPairs: [
        { name: "side", fg: Theme.ink, bg: Theme.paper, target: 4.5 },
        { name: "direction", fg: Theme.accentText, bg: Theme.paper, target: 4.5 },
        { name: "amount", fg: Theme.inkSecondary, bg: Theme.paper, target: 4.5 }
    ]

    objectName: "kitInstruction"
    implicitWidth: 320
    implicitHeight: column.implicitHeight

    // The cube and its turns do not follow the layout.
    LayoutMirroring.enabled: false
    LayoutMirroring.childrenInherit: true

    Column {
        id: column
        width: parent.width
        spacing: Theme.space2

        // 1. Side
        Row {
            id: sideLine
            spacing: Theme.space3
            x: root.centred ? Math.max(0, (column.width - width) / 2) : 0

            KitSwatch {
                id: swatch
                objectName: "swatch"
                colourName: root.sideColour
                size: Theme.typeSize("instruction")
                anchors.verticalCenter: parent.verticalCenter
            }
            KitText {
                objectName: "side"
                text: root.side
                role: "instruction"
                width: Math.min(implicitWidth, Math.max(0, column.width - swatch.width - sideLine.spacing))
                anchors.verticalCenter: parent.verticalCenter
                Accessible.ignored: true
            }
        }

        // 2. Direction
        Row {
            id: directionLine
            spacing: Theme.space3
            x: root.centred ? Math.max(0, (column.width - width) / 2) : 0

            KitIcon {
                id: arrow
                objectName: "directionIcon"
                name: root.directionIcon
                size: Theme.typeSize("direction")
                strokeWidth: 3
                color: Theme.accentText
                anchors.verticalCenter: parent.verticalCenter
            }
            KitText {
                objectName: "direction"
                text: root.direction
                role: "direction"
                width: Math.min(implicitWidth, Math.max(0, column.width - arrow.width - directionLine.spacing))
                anchors.verticalCenter: parent.verticalCenter
                Accessible.ignored: true
            }
        }

        // 3. Amount, then notation
        Row {
            id: amountLine
            spacing: Theme.space3
            x: root.centred ? Math.max(0, (column.width - width) / 2) : 0

            KitText {
                objectName: "amount"
                text: root.amount
                role: "body"
                color: Theme.inkSecondary
                width: Math.min(implicitWidth,
                                Math.max(0, column.width - (notationBox.visible ? notationBox.width + amountLine.spacing : 0)))
                anchors.verticalCenter: parent.verticalCenter
                Accessible.ignored: true
            }
            KitKeyName {
                id: notationBox
                objectName: "notation"
                visible: root.notation !== ""
                text: root.notation
                ink: Theme.inkSecondary
                anchors.verticalCenter: parent.verticalCenter
            }
        }
    }

    Accessible.role: Accessible.StaticText
    Accessible.name: accessibleName !== "" ? accessibleName : (side + " " + direction + ". " + amount)
}
