import QtQuick
import Cubeulator

// History (docs/design/screens/history.md): the cubes solved on this device,
// newest first, and a way back into an unfinished one.
//
// One column at every size, at most 640 wide, centred from medium up. A row is
// two targets side by side: the row itself, and a 56 x 56 delete button at its
// trailing end.
//
// What a row opens:
//   - the newest unfinished solve carries on, from the turn reached
//     (AppState.resumeSolve);
//   - any other - a finished one, or an unfinished one that another scan has
//     since left behind, which cannot be carried on - opens its solution for
//     review (solution.overview, AppState.openSolution), worked out again from
//     the cube as it was scanned.
Rectangle {
    id: root
    color: Theme.paper
    readonly property bool wide: width >= Theme.wideBreakpoint

    // The states of journey.md: history.empty, history.list, history.off (and
    // the two dialogs, below). Switched off with solves still kept shows the
    // list with the message above it as a notice.
    readonly property bool hasEntries: SolveHistory.count > 0
    readonly property bool keeping: SolveHistory.keep

    // The entry a delete dialog is asking about (0: none) and whether the
    // clear dialog is up.
    property int deleteTarget: 0
    property bool confirmingClear: false
    // What a screen reader is told after a delete or a clear.
    property string announcement: ""

    function goBack() {
        AppState.closeHistory();
    }

    // Theme's sticker palette is keyed by colour, under names left over from a
    // white-on-top scheme: faceU is white, faceD yellow, faceL orange, faceR
    // red (the solve screen reads it the same way).
    function colorHex(name) {
        switch (name) {
        case "White": return Theme.faceU;
        case "Yellow": return Theme.faceD;
        case "Green": return Theme.faceF;
        case "Blue": return Theme.faceB;
        case "Orange": return Theme.faceL;
        case "Red": return Theme.faceR;
        }
        return Theme.paper;
    }

    function digits(n) {
        return Qt.locale(Qt.uiLanguage).toString(n);
    }
    // The date and the time, in the order and the digits of the app's language.
    function dateOf(entry) {
        return Qt.locale(Qt.uiLanguage).toString(new Date(entry.started), Locale.ShortFormat);
    }
    function summaryOf(entry) {
        var summary = entry.finished
            ? qsTrId("history.item.done", entry.turns).arg(AppState.durationLabel(entry.seconds))
            : qsTrId("history.item.unfinished").arg(digits(entry.turn)).arg(digits(entry.turns));
        // A 3x3 is the cube nothing needs to be said of.
        return entry.size === 3 ? summary : qsTrId("history.item.size").arg(digits(entry.size)).arg(summary);
    }

    // Hands are on the keys, or on a phone's back gesture: back leaves, and
    // with a dialog up it is the dialog's (ConfirmDialog takes the focus).
    focus: true
    Component.onCompleted: forceActiveFocus()
    Keys.onEscapePressed: root.goBack()
    Keys.onBackPressed: root.goBack()

    // ---- parts ----------------------------------------------------------------

    // The stickers of the scanned cube's front side - nine for a 3x3, N x N for a
    // cube of size N, in the same 44 pixels: a way to recognise a scramble at a
    // glance, not information, so it is not read out. The cube is the object in
    // the hand, and does not mirror with the layout.
    component SideChip: Grid {
        property var colours: []
        readonly property int stickersAcross: colours.length > 0 ? Math.round(Math.sqrt(colours.length)) : 3
        width: 44; height: 44
        columns: stickersAcross
        spacing: 1
        LayoutMirroring.enabled: false
        LayoutMirroring.childrenInherit: true
        Accessible.ignored: true
        Repeater {
            model: parent.stickersAcross * parent.stickersAcross
            Rectangle {
                width: (44 - (parent.stickersAcross - 1)) / parent.stickersAcross; height: width
                color: root.colorHex(index < parent.colours.length ? parent.colours[index] : "")
                // White on the light theme's paper would be a hole.
                border.width: 1
                border.color: Theme.inkMuted(0.35)
            }
        }
    }

    // What is said when history is off: a heading, a line, and the way to
    // Settings. An empty state when nothing is kept, a notice above the list
    // when something is.
    component OffState: Column {
        spacing: Theme.space3
        Text {
            width: parent.width
            text: qsTrId("history.off.title")
            wrapMode: Text.WordWrap
            font.family: Theme.fontHeading; font.weight: Font.ExtraBold; font.pixelSize: 20
            color: Theme.ink
        }
        Text {
            width: parent.width
            text: qsTrId("history.off.body")
            wrapMode: Text.WordWrap
            font.family: Theme.fontHeading; font.pixelSize: 14
            color: Theme.inkMuted(0.7)
        }
        AppButton {
            objectName: "history.off.open"
            variant: "secondary"
            maxWidth: parent.width
            text: qsTrId("history.off.open")
            onClicked: AppState.openSettings()
        }
    }

    // ---- the column -------------------------------------------------------------
    Item {
        id: column
        width: Math.min(root.width, 640)
        height: parent.height
        anchors.horizontalCenter: parent.horizontalCenter

        // Top bar: back, the title, and Clear history at the trailing end when
        // there is anything to clear. The title wraps onto as many lines as it
        // needs and the bar grows to hold them (as the entry screen's does). When the
        // title in one line and the button do not both fit beside Back - "Historique"
        // and "Effacer l'historique" on a phone - the button takes a line of its
        // own under them, at the same end, instead of cutting the title off.
        Item {
            id: bar
            width: parent.width
            height: barTop.height + (clearBelow ? clearButton.height + Theme.space3 : 0)

            readonly property bool clearBelow: clearButton.visible && !clearBeside
            readonly property bool clearBeside: back.width + titleSize.advanceWidth + clearButton.implicitWidth
                + Theme.space3 * 3 + Theme.space2 <= width

            // The title as it is set on one line.
            TextMetrics {
                id: titleSize
                font: titleText.font
                text: titleText.text
            }

            Item {
                id: barTop
                width: parent.width
                height: Math.max(64, titleText.implicitHeight + 2 * Theme.space3)

                AppButton {
                    id: back
                    objectName: "history.back"
                    anchors.left: parent.left; anchors.leftMargin: Theme.space3
                    anchors.verticalCenter: parent.verticalCenter
                    variant: "ghost"; leadingIcon: "chevron-left"
                    text: qsTrId("common.back")
                    onClicked: root.goBack()
                }
                Text {
                    id: titleText
                    objectName: "history.title"
                    anchors.left: back.right; anchors.leftMargin: Theme.space2
                    anchors.right: parent.right
                    // Short of the button when it is beside the title.
                    anchors.rightMargin: Theme.space3
                        + (bar.clearBeside && clearButton.visible ? clearButton.width + Theme.space3 : 0)
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTrId("history.title")
                    wrapMode: Text.Wrap
                    font.family: Theme.fontHeading; font.weight: Font.ExtraBold; font.pixelSize: 22
                    color: Theme.ink
                    Accessible.role: Accessible.Heading
                    Accessible.name: text
                }
            }
            AppButton {
                id: clearButton
                objectName: "history.clear"
                visible: root.hasEntries
                anchors.right: parent.right; anchors.rightMargin: Theme.space3
                y: bar.clearBeside ? (barTop.height - height) / 2 : barTop.height
                variant: "ghost"
                maxWidth: bar.width - Theme.space3 * 2
                text: qsTrId("history.clear")
                onClicked: root.confirmingClear = true
            }
            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 2; color: Theme.divider }
        }

        // history.off with solves still kept: the message sits above the list.
        Rectangle {
            id: offNotice
            objectName: "history.off"
            anchors.top: bar.bottom
            width: parent.width
            visible: !root.keeping && root.hasEntries
            height: visible ? offNoticeContent.implicitHeight + Theme.space4 * 2 : 0
            color: Theme.surface
            OffState {
                id: offNoticeContent
                x: Theme.space4; y: Theme.space4
                width: parent.width - Theme.space4 * 2
            }
            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 2; color: Theme.divider }
        }

        // history.list. The list itself is not named: the test harness keys
        // every control inside a *named* ListView as one "<list>.row", and a
        // row and its delete button are two controls the spec names apart
        // (history.row1, history.delete1). The name is on the item around it.
        Item {
            objectName: "history.list"
            anchors.top: offNotice.bottom; anchors.bottom: parent.bottom
            width: parent.width
            visible: root.hasEntries

            ListView {
                id: list
                anchors.fill: parent
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                model: SolveHistory.entries

                delegate: Item {
                    id: row
                    required property var modelData
                    required property int index

                    width: ListView.view.width
                    // 64, and taller for a date and summary that need more lines.
                    height: Math.max(64, rowText.implicitHeight + Theme.space3 * 2)
                    objectName: "history.row" + (index + 1)

                    readonly property string dateText: root.dateOf(modelData)
                    readonly property string summaryText: root.summaryOf(modelData)

                    Accessible.role: Accessible.Button
                    Accessible.name: qsTrId("history.a11y.row").arg(dateText).arg(summaryText)

                    function open() {
                        if (modelData.carryOn)
                            AppState.resumeSolve(modelData.id);
                        else
                            AppState.openSolution(SolveHistory.reviewOf(modelData.id), dateText);
                    }

                    Rectangle {
                        anchors.fill: parent
                        visible: rowArea.containsMouse
                        color: Theme.inkMuted(0.07)
                    }
                    Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.divider }

                    // The row itself: everything but the delete button.
                    MouseArea {
                        id: rowArea
                        anchors.left: parent.left; anchors.right: deleteButton.left
                        anchors.top: parent.top; anchors.bottom: parent.bottom
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: row.open()
                    }

                    // The chip does not mirror (the cube is the object in the
                    // hand), and that includes its own anchors: it sits unanchored
                    // in a slot that does, and the text is anchored to the slot.
                    Item {
                        id: chip
                        anchors.left: parent.left; anchors.leftMargin: Theme.space4
                        anchors.verticalCenter: parent.verticalCenter
                        width: 44; height: 44
                        SideChip { colours: row.modelData.stickers }
                    }
                    Column {
                        id: rowText
                        anchors.left: chip.right; anchors.leftMargin: Theme.space3
                        anchors.right: chevron.left
                        anchors.rightMargin: Theme.space2
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 2
                        Text {
                            width: parent.width
                            text: row.dateText
                            wrapMode: Text.Wrap
                            font.family: Theme.fontHeading; font.weight: Font.ExtraBold; font.pixelSize: 15
                            color: Theme.ink
                            Accessible.ignored: true
                        }
                        Text {
                            width: parent.width
                            text: row.summaryText
                            wrapMode: Text.Wrap
                            font.family: Theme.fontHeading; font.pixelSize: 12
                            color: Theme.inkMuted(0.6)
                            Accessible.ignored: true
                        }
                    }
                    Icon {
                        id: chevron
                        anchors.right: deleteButton.left; anchors.rightMargin: Theme.space2
                        anchors.verticalCenter: parent.verticalCenter
                        name: "chevron-right"
                        width: 18; height: 18
                        color: Theme.ink
                    }

                    // 56 x 56, the cross. "Delete" and the date, so that a list of
                    // them can be told apart by a screen reader.
                    Item {
                        id: deleteButton
                        objectName: "history.delete" + (row.index + 1)
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        width: 56; height: 56
                        Accessible.role: Accessible.Button
                        Accessible.name: qsTrId("history.a11y.delete").arg(row.dateText)
                        Rectangle {
                            anchors.fill: parent
                            color: Theme.inkMuted(0.07)
                            visible: deleteArea.containsMouse
                        }
                        Icon {
                            anchors.centerIn: parent
                            name: "cross"
                            width: 18; height: 18
                            color: Theme.ink
                        }
                        MouseArea {
                            id: deleteArea
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: root.deleteTarget = row.modelData.id
                        }
                    }
                }
            }
        }

        // history.empty: nothing has been solved. The same Scan a cube as Home.
        Column {
            id: emptyState
            objectName: "history.empty"
            visible: !root.hasEntries && root.keeping
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.verticalCenter: parent.verticalCenter
            anchors.verticalCenterOffset: bar.height / 2
            width: Math.min(parent.width - Theme.space6 * 2, 420)
            spacing: Theme.space3
            Text {
                width: parent.width
                text: qsTrId("history.empty.title")
                wrapMode: Text.WordWrap
                font.family: Theme.fontHeading; font.weight: Font.ExtraBold; font.pixelSize: 20
                color: Theme.ink
                Accessible.role: Accessible.Heading
                Accessible.name: text
            }
            Text {
                width: parent.width
                text: qsTrId("history.empty.body")
                wrapMode: Text.WordWrap
                font.family: Theme.fontHeading; font.pixelSize: 14
                color: Theme.inkMuted(0.7)
            }
            AppButton {
                objectName: "history.empty.scan"
                variant: "primary"
                maxWidth: parent.width
                text: qsTrId("home.scan")
                trailingIcon: "chevron-right"
                onClicked: AppState.beginScan()
            }
        }

        // history.off with nothing kept: the empty state says so instead.
        Item {
            id: offState
            objectName: "history.off"
            visible: !root.hasEntries && !root.keeping
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.verticalCenter: parent.verticalCenter
            anchors.verticalCenterOffset: bar.height / 2
            width: Math.min(parent.width - Theme.space6 * 2, 420)
            height: offStateContent.implicitHeight
            OffState { id: offStateContent; width: parent.width }
        }
    }

    // What a screen reader is told when a row or the whole list has gone. The
    // list changes under it, so there is no control left to carry the news.
    Item {
        objectName: "history.announcement"
        width: 1; height: 1
        opacity: 0
        Accessible.role: Accessible.AlertMessage
        Accessible.name: root.announcement
    }

    // ---- dialogs (last, so they are over everything) ----------------------------
    ConfirmDialog {
        objectName: "history.deleteDialog"
        open: root.deleteTarget !== 0
        restoreFocusTo: root
        title: qsTrId("history.delete.title")
        keepText: qsTrId("history.delete.keep")
        confirmText: qsTrId("history.delete")
        onKept: root.deleteTarget = 0
        onConfirmed: {
            SolveHistory.remove(root.deleteTarget);
            root.deleteTarget = 0;
            root.announcement = qsTrId("history.a11y.deleted");
        }
    }
    ConfirmDialog {
        objectName: "history.clearDialog"
        open: root.confirmingClear
        restoreFocusTo: root
        title: qsTrId("history.clear.title")
        body: qsTrId("history.clear.body", SolveHistory.count)
        keepText: qsTrId("history.clear.keep")
        confirmText: qsTrId("history.clear.confirm")
        onKept: root.confirmingClear = false
        onConfirmed: {
            SolveHistory.clear();
            root.confirmingClear = false;
            root.announcement = qsTrId("history.a11y.cleared");
        }
    }
}
