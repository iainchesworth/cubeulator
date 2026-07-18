import QtQuick
import Cubeulator

Rectangle {
    id: root
    color: Theme.paper
    readonly property bool wide: width >= Theme.wideBreakpoint

    component SectionLabel: Text {
        font.family: Theme.fontHeading
        font.pixelSize: 11
        font.letterSpacing: 1.4
        color: Theme.accent
    }

    component ChipRow: Flow {
        id: chipRoot
        property var options: []
        property int currentIndex: 0
        signal selected(int index)
        width: parent ? parent.width : 0
        spacing: Theme.space2

        Repeater {
            model: chipRoot.options
            delegate: AppButton {
                text: modelData
                variant: index === chipRoot.currentIndex ? "primary" : "secondary"
                onClicked: chipRoot.selected(index)
            }
        }
    }

    component Toggle: Row {
        property bool checked: false
        property string label: ""
        signal toggled()
        spacing: Theme.space2
        Rectangle {
            width: 20; height: 20; anchors.verticalCenter: parent.verticalCenter
            border.width: 1.5; border.color: Theme.divider
            Icon { anchors.centerIn: parent; visible: parent.parent.checked; name: "check"; color: Theme.accent; width: 14; height: 14; strokeWidth: 3 }
            MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: parent.parent.toggled() }
        }
        Text {
            text: parent.label
            anchors.verticalCenter: parent.verticalCenter
            font.family: Theme.fontHeading; font.pixelSize: 13; color: Theme.ink
        }
    }

    component SettingsBody: Column {
        spacing: Theme.space6
        width: parent ? parent.width : 0

        Column {
            width: parent.width
            spacing: Theme.space2
            SectionLabel { text: "CAMERA" }
            Text {
                text: "Choose which device Cubeulator reads the cube's faces from."
                font.family: Theme.fontHeading; font.pixelSize: 12
                color: Qt.rgba(0.125, 0.117, 0.113, 0.6)
                width: parent.width; wrapMode: Text.WordWrap
            }
            ChipRow {
                options: SettingsState.cameraDevices
                currentIndex: SettingsState.cameraDeviceIndex
                onSelected: (index) => SettingsState.cameraDeviceIndex = index
            }
        }

        Column {
            width: parent.width
            spacing: Theme.space2
            SectionLabel { text: "LANGUAGE" }
            ChipRow {
                options: SettingsState.languages
                currentIndex: SettingsState.languageIndex
                onSelected: (index) => SettingsState.languageIndex = index
            }
        }

        Column {
            width: parent.width
            spacing: Theme.space2
            SectionLabel { text: "CUBE STYLE" }
            Text {
                text: "Shader style for the 3D cube shown around the app."
                font.family: Theme.fontHeading; font.pixelSize: 12
                color: Qt.rgba(0.125, 0.117, 0.113, 0.6)
                width: parent.width; wrapMode: Text.WordWrap
            }
            ChipRow {
                options: SettingsState.shaderStyles
                currentIndex: SettingsState.shaderStyleIndex
                onSelected: (index) => SettingsState.shaderStyleIndex = index
            }
        }

        Column {
            width: parent.width
            spacing: Theme.space3
            SectionLabel { text: "CUBE MOTION" }
            Toggle {
                checked: SettingsState.cubeAutoRotate
                label: "Auto-rotate when idle"
                onToggled: SettingsState.cubeAutoRotate = !SettingsState.cubeAutoRotate
            }
            ChipRow {
                options: SettingsState.rotateSpeeds
                currentIndex: SettingsState.rotateSpeedIndex
                onSelected: (index) => SettingsState.rotateSpeedIndex = index
                opacity: SettingsState.cubeAutoRotate ? 1.0 : 0.4
                enabled: SettingsState.cubeAutoRotate
            }
        }
    }

    // ---- narrow ----
    Column {
        visible: !root.wide
        anchors.fill: parent
        anchors.margins: Theme.space6
        spacing: Theme.space4

        Row {
            width: parent.width
            spacing: Theme.space2
            AppButton { variant: "ghost"; leadingIcon: "chevron-left"; text: "Back"; onClicked: AppState.closeSettings() }
        }
        Text { text: "Settings"; font.family: Theme.fontHeading; font.weight: Font.ExtraBold; font.pixelSize: 28; color: Theme.ink }

        Flickable {
            width: parent.width
            height: root.height - 160
            contentHeight: body.implicitHeight
            clip: true
            SettingsBody { id: body; width: parent.width }
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
            spacing: Theme.space4

            AppButton { variant: "ghost"; leadingIcon: "chevron-left"; text: "Back"; onClicked: AppState.closeSettings() }
            Text { text: "Settings"; font.family: Theme.fontHeading; font.weight: Font.ExtraBold; font.pixelSize: 36; color: Theme.ink }

            Flickable {
                width: parent.width - (Theme.space8 + Theme.space2) * 2
                height: parent.height - 200
                contentHeight: wideBody.implicitHeight
                clip: true
                SettingsBody { id: wideBody; width: parent.width }
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

                Cube3DView {
                    id: previewCube
                    width: 280; height: 280
                    shaderStyle: SettingsState.shaderStyleIndex
                    autoRotate: SettingsState.cubeAutoRotate
                    autoRotateSpeed: SettingsState.rotateSpeedValue
                }

                AppButton {
                    anchors.horizontalCenter: parent.horizontalCenter
                    variant: "secondary"
                    text: "Test turn"
                    onClicked: {
                        var axes = ["x", "y", "z"];
                        var layers = [-1, 0, 1];
                        previewCube.turnLayer(axes[Math.floor(Math.random() * 3)], layers[Math.floor(Math.random() * 3)], true);
                    }
                }
            }
        }
    }
}
