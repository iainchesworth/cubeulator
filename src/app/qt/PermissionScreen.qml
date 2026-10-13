import QtQuick
import Cubeulator

Rectangle {
    id: root
    color: Theme.paper
    readonly property bool wide: width >= Theme.wideBreakpoint

    // Qt::PermissionStatus::Denied - see CameraPermission.hpp. Once denied,
    // the OS may or may not show the prompt again on request() (Android
    // stops after "don't ask again"; Qt's API doesn't expose which), so
    // both a re-ask and the OS-settings escape hatch are offered.
    readonly property bool denied: CameraPermission.status === 1

    Connections {
        target: CameraPermission
        function onStatusChanged() {
            if (CameraPermission.status === 2)
                AppState.proceedToScan();
        }
    }

    readonly property var reassurances: [
        qsTrId("permission.point1"),
        qsTrId("permission.point2"),
        qsTrId("permission.point3")
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
        Text {
            text: root.denied ? qsTrId("permission.refused.title") : qsTrId("permission.title")
            font.family: Theme.fontHeading; font.weight: Font.ExtraBold; font.pixelSize: 25; color: Theme.ink
            width: parent.width; wrapMode: Text.WordWrap
        }
        Text {
            text: root.denied ? qsTrId("permission.refused.body") : qsTrId("permission.body")
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
                text: root.denied ? qsTrId("common.tryAgain") : qsTrId("permission.allow")
                onClicked: CameraPermission.request()
            }
            AppButton {
                visible: root.denied
                width: parent.width; height: 40; variant: "ghost"
                text: qsTrId("common.openDeviceSettings")
                onClicked: CameraPermission.openSettings()
            }
            AppButton {
                width: parent.width; height: 40; variant: "ghost"
                text: qsTrId("permission.notNow")
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
            Text {
                text: root.denied ? qsTrId("permission.refused.title") : qsTrId("permission.title")
                font.family: Theme.fontHeading; font.weight: Font.ExtraBold; font.pixelSize: 32; color: Theme.ink
                width: parent.width - parent.padding * 2; wrapMode: Text.WordWrap
            }
            Text {
                text: root.denied ? qsTrId("permission.refused.body") : qsTrId("permission.body")
                font.family: Theme.fontHeading; font.pixelSize: 15; color: Theme.ink
                width: 380; wrapMode: Text.WordWrap
            }
            Row {
                spacing: Theme.space3
                topPadding: Theme.space3
                AppButton {
                    height: 46; variant: "primary"
                    text: root.denied ? qsTrId("common.tryAgain") : qsTrId("permission.allow")
                    onClicked: CameraPermission.request()
                }
                AppButton { visible: root.denied; height: 46; variant: "ghost"; text: qsTrId("common.openDeviceSettings"); onClicked: CameraPermission.openSettings() }
                AppButton { height: 46; variant: "secondary"; text: qsTrId("permission.notNow"); onClicked: AppState.goLaunch() }
            }
        }

        Rectangle {
            width: parent.width / 2
            height: parent.height
            color: Theme.surface

            Rectangle { anchors.left: parent.left; width: 2; height: parent.height; color: Theme.divider }

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
