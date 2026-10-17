import QtQuick
import QtQuick.Controls
import Cubeulator

// A cube the camera does not read: another size, or a cube typed in. The size is
// chosen here, never guessed from anything, and the cube is given as the scramble
// that mixed it up - the moves a timer or a puzzle site shows - which the app
// applies to a solved cube of that size and then solves.
//
// Sizes 2 to 5, to begin with. 6 and 7 are drawn as well as these (the 3D view
// costs the same at every size) and are added to `sizes` once they have been
// seen on a phone; docs/nxn-app-plan.md, decision 3.
Rectangle {
    id: root
    color: Theme.paper
    readonly property bool wide: width >= Theme.wideBreakpoint

    readonly property var sizes: [2, 3, 4, 5]
    property int size: AppState.entrySize
    // What the last try said was wrong: the controller's answer, or null.
    property var problem: null

    // The same digits as the rest of the app.
    function digits(n) { return Qt.locale(Qt.uiLanguage).toString(n); }

    function solve() {
        var result = AppState.solveScramble(root.size, scramble.text);
        if (result.ok) {
            problem = null;
            return;
        }
        problem = result;
        // Point at the move that is not one.
        scramble.forceActiveFocus();
        if (result.length > 0)
            scramble.select(result.offset, result.offset + result.length);
    }

    readonly property string problemWords: {
        if (problem === null)
            return "";
        switch (problem.error) {
        case "unknownMove": return qsTrId("entry.error.unknownMove").arg(problem.token);
        case "layerOutOfRange": return qsTrId("entry.error.layer").arg(problem.token).arg(digits(size));
        case "alreadySolved": return qsTrId("entry.error.solved");
        }
        return qsTrId("entry.error.empty");
    }

    focus: true
    Keys.onEscapePressed: AppState.closeEntry()
    Keys.onBackPressed: AppState.closeEntry()

    Flickable {
        anchors.fill: parent
        contentWidth: width
        contentHeight: column.height
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        Column {
            id: column
            width: Math.min(root.width, 640)
            x: (parent.width - width) / 2
            spacing: 0

            // Top bar: back, and what this is.
            Item {
                width: parent.width
                // One line of title, as it was; a translation that needs more
                // takes them, and the bar grows to hold them.
                height: Math.max(64, titleText.implicitHeight + 2 * Theme.space3)
                AppButton {
                    id: back
                    objectName: "entry.back"
                    anchors.left: parent.left; anchors.leftMargin: Theme.space3
                    anchors.verticalCenter: parent.verticalCenter
                    variant: "ghost"; leadingIcon: "chevron-left"
                    text: qsTrId("common.back")
                    onClicked: AppState.closeEntry()
                }
                Text {
                    id: titleText
                    objectName: "entry.title"
                    anchors.left: back.right; anchors.leftMargin: Theme.space2
                    anchors.right: parent.right; anchors.rightMargin: Theme.space3
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTrId("entry.title")
                    wrapMode: Text.Wrap
                    font.family: Theme.fontHeading; font.weight: Font.ExtraBold; font.pixelSize: 22
                    color: Theme.ink
                }
                Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 2; color: Theme.divider }
            }

            // ---- the size -----------------------------------------------------
            Column {
                x: Theme.space4
                width: parent.width - 2 * Theme.space4
                topPadding: Theme.space4
                spacing: Theme.space2

                Text {
                    text: qsTrId("entry.size.label")
                    font.capitalization: Font.AllUppercase
                    font.family: Theme.fontHeading; font.pixelSize: 11; font.letterSpacing: 1.4
                    color: Theme.inkMuted(0.6)
                }
                Flow {
                    width: parent.width
                    spacing: Theme.space2
                    Repeater {
                        model: root.sizes
                        delegate: AppButton {
                            required property int modelData
                            objectName: "entry.size." + modelData
                            height: 44
                            variant: modelData === root.size ? "primary" : "secondary"
                            text: qsTrId("entry.size.option").arg(root.digits(modelData))
                            onClicked: {
                                AppState.entrySize = modelData;
                                root.problem = null;
                            }
                        }
                    }
                }
                Text {
                    objectName: "entry.size.note"
                    width: parent.width
                    text: qsTrId("entry.size.camera")
                    wrapMode: Text.WordWrap
                    font.family: Theme.fontHeading; font.pixelSize: 13
                    color: Theme.inkMuted(0.65)
                }
            }

            // ---- the scramble -------------------------------------------------
            Column {
                x: Theme.space4
                width: parent.width - 2 * Theme.space4
                topPadding: Theme.space6
                spacing: Theme.space2

                Text {
                    text: qsTrId("entry.scramble.label")
                    font.capitalization: Font.AllUppercase
                    font.family: Theme.fontHeading; font.pixelSize: 11; font.letterSpacing: 1.4
                    color: Theme.inkMuted(0.6)
                }

                Rectangle {
                    width: parent.width
                    height: Math.max(120, scramble.implicitHeight + 2 * Theme.space2)
                    color: Theme.surface
                    border.width: scramble.activeFocus ? 2 : 1
                    border.color: root.problem !== null ? Theme.accent
                                  : (scramble.activeFocus ? Theme.ink : Theme.divider)

                    TextArea {
                        id: scramble
                        objectName: "entry.scramble"
                        anchors.fill: parent
                        anchors.margins: Theme.space2
                        padding: Theme.space1
                        wrapMode: TextEdit.Wrap
                        placeholderText: qsTrId("entry.scramble.placeholder")
                        placeholderTextColor: Theme.inkMuted(0.45)
                        color: Theme.ink
                        selectionColor: Theme.accent
                        selectedTextColor: Theme.paper
                        font.family: Theme.fontMono; font.pixelSize: 15
                        // A letter is a move, not a word to be corrected.
                        inputMethodHints: Qt.ImhNoPredictiveText | Qt.ImhNoAutoUppercase
                                          | Qt.ImhPreferUppercase
                        background: null
                        onTextChanged: root.problem = null
                        // Return makes a new line, which is whitespace as far as
                        // moves are concerned; Ctrl+Return solves.
                        Keys.onPressed: (event) => {
                            if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter)
                                    && (event.modifiers & Qt.ControlModifier)) {
                                event.accepted = true;
                                root.solve();
                            }
                        }
                    }
                }

                Text {
                    objectName: "entry.error"
                    width: parent.width
                    visible: root.problem !== null
                    text: root.problemWords
                    wrapMode: Text.WordWrap
                    font.family: Theme.fontHeading; font.pixelSize: 13; font.weight: Font.DemiBold
                    color: Theme.accent
                }
                Text {
                    objectName: "entry.scramble.help"
                    width: parent.width
                    visible: root.problem === null
                    text: qsTrId("entry.scramble.help")
                    wrapMode: Text.WordWrap
                    font.family: Theme.fontHeading; font.pixelSize: 12
                    color: Theme.inkMuted(0.6)
                }

                Item { width: 1; height: Theme.space2 }
                AppButton {
                    objectName: "entry.solve"
                    width: parent.width; height: 46
                    variant: "primary"; block: true
                    text: qsTrId("entry.solve"); trailingIcon: "chevron-right"
                    onClicked: root.solve()
                }
                AppButton {
                    objectName: "entry.colours"
                    width: parent.width; height: 44
                    variant: "secondary"; block: true
                    text: qsTrId("common.enterByHand"); trailingIcon: "chevron-right"
                    onClicked: AppState.openColours(root.size)
                }
                AppButton {
                    objectName: "entry.scan"
                    width: parent.width; height: 44
                    variant: "secondary"; block: true
                    text: qsTrId("entry.scan")
                    onClicked: AppState.beginScan(root.size)
                }
                Item { width: 1; height: Theme.space6 }
            }
        }
    }
}
