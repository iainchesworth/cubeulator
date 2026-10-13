import QtQuick
import Cubeulator

Rectangle {
    id: root
    color: Theme.paper
    readonly property bool wide: width >= Theme.wideBreakpoint

    // ---- narrow (phone) ----
    Column {
        visible: !root.wide
        anchors.fill: parent
        anchors.margins: Theme.space6
        spacing: Theme.space4

        Text {
            text: "SCAN · SOLVE · IN YOUR HANDS"
            color: Theme.accent
            font.family: Theme.fontHeading
            font.pixelSize: 11
            font.letterSpacing: 1.6
        }

        Item {
            width: parent.width
            height: root.height - 360
            Cube3DView {
                width: 190; height: 190
                anchors.centerIn: parent
                shaderStyle: SettingsState.shaderStyleIndex
                autoRotate: SettingsState.cubeAutoRotate
                autoRotateSpeed: SettingsState.rotateSpeedValue
                showStats: SettingsState.statsForNerds && !root.wide
            }
        }

        Column {
            width: parent.width
            spacing: Theme.space2

            AppButton {
                width: parent.width; height: 46
                variant: "primary"; block: true
                text: "Start scan"; trailingIcon: "chevron-right"
                onClicked: AppState.beginScan()
            }
            AppButton {
                width: parent.width; height: 44
                variant: "secondary"; block: true
                text: "Solve history"; trailingIcon: "chevron-right"
            }
            Rectangle { width: parent.width; height: 1; color: Theme.divider }
            Item {
                width: parent.width
                height: 40
                AppButton {
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    variant: "ghost"; text: "English"; leadingIcon: "globe"
                    onClicked: AppState.openSettings()
                }
                AppButton {
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    variant: "ghost"; text: "Settings"; leadingIcon: "menu"
                    onClicked: AppState.openSettings()
                }
            }
        }
    }

    // ---- wide (tablet/desktop) ----
    Row {
        visible: root.wide
        anchors.fill: parent

        // An Item with explicit anchors, not a Column with a hand-computed
        // spacer: the previous fixed "parent.height - 340" spacer assumed a
        // specific window height and pushed the English/Settings footer
        // below the visible area on shorter windows. Anchoring the footer to
        // the bottom and the hero copy to vertical-center keeps every piece
        // on screen at any wide-layout height.
        Item {
            id: leftPane
            width: parent.width / 2
            height: parent.height

            readonly property real pad: Theme.space8 + Theme.space2

            Text {
                anchors.top: parent.top
                anchors.left: parent.left
                anchors.margins: leftPane.pad
                text: "CUBEULATOR"
                color: Theme.accent
                font.family: Theme.fontHeading
                font.pixelSize: 11
                font.letterSpacing: 1.8
            }

            Column {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.leftMargin: leftPane.pad
                anchors.rightMargin: leftPane.pad
                anchors.verticalCenter: parent.verticalCenter
                spacing: Theme.space3
                Text {
                    text: "Scan. Solve.\nIn your hands."
                    color: Theme.ink
                    font.family: Theme.fontHeading
                    font.weight: Font.ExtraBold
                    font.pixelSize: 44
                    lineHeight: 0.98
                    lineHeightMode: Text.ProportionalHeight
                }
                Text {
                    text: "Point the camera, turn the cube through six sides, follow the moves."
                    color: Theme.inkMuted(0.55)
                    font.family: Theme.fontHeading
                    font.pixelSize: 15
                    width: 340
                    wrapMode: Text.WordWrap
                }
                Row {
                    spacing: Theme.space3
                    topPadding: Theme.space3
                    AppButton {
                        height: 46; variant: "primary"
                        text: "Start scan"; trailingIcon: "chevron-right"
                        onClicked: AppState.beginScan()
                    }
                    AppButton { height: 46; variant: "secondary"; text: "Solve history" }
                }
            }

            Row {
                anchors.left: parent.left
                anchors.bottom: parent.bottom
                anchors.margins: leftPane.pad
                spacing: Theme.space6
                AppButton { variant: "ghost"; text: "English"; leadingIcon: "globe"; onClicked: AppState.openSettings() }
                AppButton { variant: "ghost"; text: "Settings"; leadingIcon: "menu"; onClicked: AppState.openSettings() }
            }
        }

        Rectangle {
            width: parent.width / 2
            height: parent.height
            color: Theme.surface

            Rectangle { width: 2; height: parent.height; color: Theme.divider }

            Cube3DView {
                width: 320; height: 320
                anchors.centerIn: parent
                shaderStyle: SettingsState.shaderStyleIndex
                autoRotate: SettingsState.cubeAutoRotate
                autoRotateSpeed: SettingsState.rotateSpeedValue
                showStats: SettingsState.statsForNerds && root.wide
            }
        }
    }
}
