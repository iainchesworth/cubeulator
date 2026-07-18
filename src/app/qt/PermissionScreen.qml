import QtQuick
import Cubeulator

Rectangle {
    id: root
    color: Theme.paper
    readonly property bool wide: width >= Theme.wideBreakpoint

    readonly property var reassurances: [
        "Frames are processed on-device, in real time.",
        "Nothing is stored, uploaded, or shared.",
        "You can revoke access any time in Settings."
    ]

    Component {
        id: reassuranceRow
        Row {
            property string label: ""
            spacing: 10
            Icon { name: "check"; color: Theme.detected; width: 18; height: 18; strokeWidth: 2.4 }
            Text { text: label; font.family: Theme.fontHeading; font.pixelSize: 13; width: root.wide ? 280 : 220; wrapMode: Text.WordWrap; color: Theme.ink }
        }
    }

    // ---- narrow ----
    Column {
        visible: !root.wide
        anchors.fill: parent
        anchors.margins: Theme.space6
        spacing: Theme.space4

        Rectangle {
            width: 56; height: 56
            color: "transparent"; border.width: 2; border.color: Theme.ink
            Icon { anchors.centerIn: parent; name: "camera"; width: 30; height: 30 }
        }
        Text { text: "Camera access"; font.family: Theme.fontHeading; font.weight: Font.ExtraBold; font.pixelSize: 25; color: Theme.ink }
        Text {
            text: "Cubeulator reads your cube's colours through the camera to build the solve."
            font.family: Theme.fontHeading; font.pixelSize: 14; color: Theme.ink
            width: parent.width; wrapMode: Text.WordWrap
        }
        Column {
            width: parent.width
            spacing: Theme.space3
            topPadding: Theme.space2
            Repeater {
                model: root.reassurances
                delegate: Loader {
                    sourceComponent: reassuranceRow
                    onLoaded: item.label = modelData
                }
            }
        }
        Item { width: 1; height: root.height - 430 }
        Column {
            width: parent.width
            spacing: Theme.space2
            AppButton {
                width: parent.width; height: 46; variant: "primary"; block: true
                text: "Enable camera"
                onClicked: AppState.grantCamera()
            }
            AppButton {
                width: parent.width; height: 40; variant: "ghost"
                text: "Not now"
                onClicked: AppState.goLaunch()
            }
        }
    }

    // ---- wide ----
    Row {
        visible: root.wide
        anchors.fill: parent

        Column {
            width: parent.width / 2
            height: parent.height
            padding: Theme.space8 + Theme.space2
            spacing: Theme.space3

            Rectangle {
                width: 56; height: 56
                color: "transparent"; border.width: 2; border.color: Theme.ink
                Icon { anchors.centerIn: parent; name: "camera"; width: 30; height: 30 }
            }
            Text { text: "Camera access"; font.family: Theme.fontHeading; font.weight: Font.ExtraBold; font.pixelSize: 32; color: Theme.ink }
            Text {
                text: "Cubeulator reads your cube's colours through the camera to build the solve. Grant access to begin scanning."
                font.family: Theme.fontHeading; font.pixelSize: 15; color: Theme.ink
                width: 380; wrapMode: Text.WordWrap
            }
            Row {
                spacing: Theme.space3
                topPadding: Theme.space3
                AppButton { height: 46; variant: "primary"; text: "Enable camera"; onClicked: AppState.grantCamera() }
                AppButton { height: 46; variant: "secondary"; text: "Not now"; onClicked: AppState.goLaunch() }
            }
        }

        Rectangle {
            width: parent.width / 2
            height: parent.height
            color: Theme.surface

            Rectangle { width: 2; height: parent.height; color: Theme.divider }

            Column {
                anchors.centerIn: parent
                spacing: Theme.space4
                width: 300
                Repeater {
                    model: root.reassurances
                    delegate: Loader {
                        sourceComponent: reassuranceRow
                        onLoaded: item.label = modelData
                    }
                }
            }
        }
    }
}
