import QtQuick
import Cubeulator

// The three ways to solve a cube - Beginner, Intermediate, Advanced - as a row
// of buttons, and under them a line saying what the one chosen is. It only
// reports a choice: the solve screen turns it into a re-solve from the cube as
// it stands, the settings screen into the method the next solve starts with.
// Both do that the same way, by setting SettingsState.solveMethod.
Column {
    id: root

    // The method chosen: a SolveController.Method value.
    property int current: SolveController.Beginner
    // Whether to say what the chosen method is. The solve screen's narrow
    // layout has no room for it and leaves it out.
    property bool showNote: true
    // False while a solve is being worked out, so a second choice does not
    // pile on a first.
    property bool choosable: true
    // Whether the cube is one that is reduced to a 3x3 first, so that the methods
    // are for the 3x3 it becomes ("Finish with").
    property bool core: false
    signal selected(int method)

    width: parent ? parent.width : 0
    spacing: Theme.space1

    // Each a plain qsTrId() with a literal id, so the extractor can see it;
    // the order is SolveController.Method's.
    readonly property var names: [
        qsTrId("solve.method.beginner"),
        qsTrId("solve.method.intermediate"),
        qsTrId("solve.method.advanced")
    ]
    readonly property var notes: [
        qsTrId("solve.method.beginner.note"),
        qsTrId("solve.method.intermediate.note"),
        qsTrId("solve.method.advanced.note")
    ]

    Text {
        visible: root.core
        objectName: root.objectName !== "" ? root.objectName + ".core" : ""
        width: parent.width
        text: qsTrId("solve.method.finishWith")
        wrapMode: Text.WordWrap
        font.family: Theme.fontHeading; font.pixelSize: 12
        color: Theme.inkMuted(0.6)
    }

    Flow {
        width: parent.width
        spacing: Theme.space2

        Repeater {
            model: root.names
            delegate: AppButton {
                objectName: root.objectName !== "" ? root.objectName + "." + index : ""
                height: 34
                text: modelData
                enabled: root.choosable
                variant: index === root.current ? "primary" : "secondary"
                onClicked: root.selected(index)
            }
        }
    }

    Text {
        visible: root.showNote
        objectName: root.objectName !== "" ? root.objectName + ".note" : ""
        width: parent.width
        text: root.notes[root.current] !== undefined ? root.notes[root.current] : ""
        wrapMode: Text.WordWrap
        font.family: Theme.fontHeading; font.pixelSize: 12
        color: Theme.inkMuted(0.6)
    }
}
