import QtQuick
import Cubeulator

// Stage row (docs/design/system.md, "Stage row"): in the expanded solve
// screen's side panel. A 32 px numbered box, the stage's name, and for the
// current stage its explanation.
//
//   to come   2 px `outline` box, the number
//   current   `accentFill` box, the number in `onAccent`; `accentTint` row;
//             explanation shown
//   done      2 px `ok` edge, a check
//
// The three differ by a number, a fill and a check, not by colour alone.
Rectangle {
    id: root

    property int number: 1
    property string name: ""
    property string explanation: ""
    // todo | current | done. Not `state`: that is Item's own.
    property string status: "todo"
    // What a screen reader adds after the name ("Done", "Current stage"), in
    // the language.
    property string stateText: ""

    readonly property bool isCurrent: status === "current"
    readonly property bool isDone: status === "done"

    readonly property color boxFill: isCurrent ? Theme.accentFill : "transparent"
    readonly property color boxEdge: isDone ? Theme.ok : (isCurrent ? Theme.accentFill : Theme.outline)
    readonly property color boxInk: isCurrent ? Theme.accentOn : (isDone ? Theme.ok : Theme.ink)
    readonly property color rowFill: isCurrent ? Theme.accentTint : "transparent"
    readonly property color rowBehind: isCurrent ? Theme.accentTint : Theme.paper

    readonly property var contrastPairs: [
        { name: "name", fg: Theme.ink, bg: rowBehind, target: 4.5 },
        { name: "explanation", fg: Theme.inkSecondary, bg: rowBehind, target: 4.5 },
        { name: "number", fg: boxInk, bg: isCurrent ? Theme.accentFill : rowBehind, target: 4.5 },
        { name: "box edge", fg: boxEdge, bg: rowBehind, target: 3.0 }
    ]

    objectName: "kitStageRow"
    implicitWidth: 320
    implicitHeight: content.implicitHeight + 2 * Theme.space3
    color: rowFill
    radius: Theme.radius

    Row {
        id: content
        x: Theme.space3
        y: Theme.space3
        width: root.width - 2 * Theme.space3
        spacing: Theme.space3

        Rectangle {
            id: box
            objectName: "box"
            width: 32
            height: 32
            color: root.boxFill
            border.width: root.isCurrent ? 0 : Theme.borderRule
            border.color: root.boxEdge
            radius: Theme.radius
            Accessible.ignored: true

            KitText {
                visible: !root.isDone
                anchors.centerIn: parent
                text: String(root.number)
                role: "numeral"
                color: root.boxInk
                wrapMode: Text.NoWrap
                Accessible.ignored: true
            }
            KitIcon {
                visible: root.isDone
                anchors.centerIn: parent
                name: "check"
                size: 20
                strokeWidth: 3
                color: root.boxInk
            }
        }

        Column {
            width: parent.width - box.width - parent.spacing
            spacing: Theme.space1

            KitText {
                objectName: "name"
                width: parent.width
                text: root.name
                role: "bodyStrong"
                Accessible.ignored: true
            }
            KitText {
                objectName: "explanation"
                width: parent.width
                visible: root.isCurrent && root.explanation !== ""
                text: root.explanation
                role: "small"
                color: Theme.inkSecondary
                Accessible.ignored: true
            }
        }
    }

    Accessible.role: Accessible.ListItem
    Accessible.name: stateText !== "" ? (number + ". " + name + ". " + stateText) : (number + ". " + name)
    Accessible.description: isCurrent ? explanation : ""
}
