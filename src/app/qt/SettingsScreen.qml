import QtQuick
import Cubeulator

Rectangle {
    id: root
    color: Theme.paper
    readonly property bool wide: width >= Theme.wideBreakpoint

    Component {
        id: toggleRow
        Row {
            id: toggleRoot
            property string label: ""
            property string description: ""
            property bool checked: false
            property bool enabled: true
            signal toggled()

            width: parent ? parent.width : 0
            spacing: Theme.space3
            opacity: enabled ? 1.0 : 0.45

            Rectangle {
                width: 20; height: 20
                anchors.verticalCenter: labelCol.verticalCenter
                border.width: 1.5; border.color: Theme.divider
                color: toggleRoot.checked ? Theme.accent100 : "transparent"
                Icon {
                    anchors.centerIn: parent
                    visible: toggleRoot.checked
                    name: "check"; color: Theme.accent; width: 14; height: 14; strokeWidth: 3
                }
                MouseArea {
                    anchors.fill: parent
                    enabled: toggleRoot.enabled
                    cursorShape: Qt.PointingHandCursor
                    onClicked: toggleRoot.toggled()
                }
            }
            Column {
                id: labelCol
                width: parent.width - 32
                spacing: 2
                Text { text: toggleRoot.label; font.family: Theme.fontHeading; font.weight: Font.ExtraBold; font.pixelSize: 14; color: Theme.ink }
                Text {
                    visible: text !== ""
                    text: toggleRoot.description
                    font.family: Theme.fontHeading; font.pixelSize: 12
                    color: Qt.rgba(0.125, 0.117, 0.113, 0.6)
                    width: parent.width; wrapMode: Text.WordWrap
                }
            }
        }
    }

    Flickable {
        anchors.fill: parent
        anchors.margins: root.wide ? Theme.space8 : Theme.space4
        contentWidth: width
        contentHeight: content.implicitHeight
        clip: true

        Column {
            id: content
            width: root.wide ? Math.min(parent.width, 480) : parent.width
            spacing: Theme.space6

            Row {
                spacing: Theme.space2
                AppButton { variant: "ghost"; leadingIcon: "chevron-left"; text: "Back"; onClicked: AppState.goLaunch() }
            }

            Text { text: "Settings"; font.family: Theme.fontHeading; font.weight: Font.ExtraBold; font.pixelSize: 26; color: Theme.ink }

            Column {
                width: parent.width
                spacing: Theme.space4

                Text { text: "SCANNING"; color: Theme.accent; font.family: Theme.fontHeading; font.pixelSize: 11; font.letterSpacing: 1.4 }

                Loader {
                    width: parent.width
                    sourceComponent: toggleRow
                    onLoaded: {
                        item.label = "Detect hands with AI";
                        item.description = SettingsModel.handDetectionAvailable
                            ? "Uses an on-device model to avoid reading colours while a hand blocks the cube."
                            : SettingsModel.handDetectionUnavailableReason;
                        item.checked = Qt.binding(function() { return ScanSession.handDetectionEnabled; });
                        item.enabled = SettingsModel.handDetectionAvailable;
                        item.toggled.connect(function() {
                            ScanSession.handDetectionEnabled = !ScanSession.handDetectionEnabled;
                        });
                    }
                }

                Loader {
                    width: parent.width
                    sourceComponent: toggleRow
                    onLoaded: {
                        item.label = "Auto-advance between faces";
                        item.description = "Confirm each face automatically once the reading is steady and confident.";
                        item.checked = Qt.binding(function() { return ScanSession.autoAdvance; });
                        item.toggled.connect(function() {
                            ScanSession.autoAdvance = !ScanSession.autoAdvance;
                        });
                    }
                }
            }

            Rectangle { width: parent.width; height: 1; color: Theme.divider }

            Column {
                width: parent.width
                spacing: Theme.space3

                Item {
                    width: parent.width
                    height: 32
                    Text {
                        anchors.left: parent.left
                        anchors.verticalCenter: parent.verticalCenter
                        text: "CAMERA"; color: Theme.accent; font.family: Theme.fontHeading; font.pixelSize: 11; font.letterSpacing: 1.4
                    }
                    AppButton {
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        height: 32; variant: "ghost"; leadingIcon: "refresh"; text: "Refresh"
                        onClicked: CameraController.refreshDevices()
                    }
                }

                Text {
                    visible: CameraController.devices.length === 0
                    text: CameraController.busy ? "Looking for cameras…" : "No cameras found."
                    font.family: Theme.fontHeading; font.pixelSize: 13
                    color: Qt.rgba(0.125, 0.117, 0.113, 0.6)
                }

                Repeater {
                    model: CameraController.devices
                    delegate: Item {
                        width: content.width
                        height: deviceRowInner.implicitHeight

                        Row {
                            id: deviceRowInner
                            spacing: Theme.space3

                            Icon {
                                anchors.verticalCenter: parent.verticalCenter
                                visible: modelData.id === CameraController.selectedDeviceId
                                name: "check"; color: Theme.detected; width: 16; height: 16; strokeWidth: 2.4
                            }
                            Item {
                                visible: !(modelData.id === CameraController.selectedDeviceId)
                                width: 16; height: 16
                            }
                            Text {
                                text: modelData.label
                                font.family: Theme.fontHeading; font.pixelSize: 13; color: Theme.ink
                                anchors.verticalCenter: parent.verticalCenter
                            }
                        }
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: CameraController.selectDevice(modelData.id)
                        }
                    }
                }
            }
        }
    }
}
