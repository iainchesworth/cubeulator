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

            // Desktop: pick from whatever's actually plugged in.
            // CameraController (not CameraDeviceModel) is the source of
            // truth here: it's the same controller ScanSession.start() reads
            // selectedDeviceId from, and its refreshDevices() runs off the
            // GUI thread (see camera_controller.hpp) - CameraDeviceModel's
            // refresh() enumerated synchronously and would freeze the window
            // for the several seconds real hardware enumeration takes.
            Column {
                visible: !AppState.isMobile
                width: parent.width
                spacing: Theme.space2
                Text {
                    text: CameraController.busy
                        ? "Looking for cameras…"
                        : CameraController.devices.length > 0
                            ? "Choose which device Cubeulator reads the cube's faces from."
                            : "No camera was detected. Plug one in, then refresh."
                    font.family: Theme.fontHeading; font.pixelSize: 12
                    color: Theme.inkMuted(0.6)
                    width: parent.width; wrapMode: Text.WordWrap
                }
                ChipRow {
                    visible: CameraController.devices.length > 0
                    options: CameraController.devices.map(function (d) { return d.label; })
                    currentIndex: CameraController.devices.findIndex(function (d) { return d.id === CameraController.selectedDeviceId; })
                    onSelected: (index) => CameraController.selectDevice(CameraController.devices[index].id)
                }
                AppButton {
                    variant: "ghost"; leadingIcon: "refresh"; text: "Refresh"
                    onClicked: CameraController.refreshDevices()
                }
            }

            // Mobile: one camera, so this is about permission, not choice.
            Column {
                visible: AppState.isMobile
                width: parent.width
                spacing: Theme.space2
                Row {
                    spacing: Theme.space2
                    Icon {
                        anchors.verticalCenter: parent.verticalCenter
                        name: AppState.cameraGranted ? "check" : "alert-triangle"
                        color: AppState.cameraGranted ? Theme.detected : Theme.warning
                        width: 16; height: 16; strokeWidth: 2.4
                    }
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: AppState.cameraGranted ? "Camera access granted" : "Camera access not granted"
                        font.family: Theme.fontHeading; font.pixelSize: 13; color: Theme.ink
                    }
                }
                AppButton {
                    visible: !AppState.cameraGranted
                    variant: "secondary"; text: "Grant camera access"
                    onClicked: AppState.requestPermission()
                }
            }
        }

        Column {
            width: parent.width
            spacing: Theme.space3
            SectionLabel { text: "SCANNING" }
            Toggle {
                width: parent.width
                checked: ScanSession.handDetectionEnabled
                label: "Detect hands with AI"
                enabled: SettingsModel.handDetectionAvailable
                opacity: SettingsModel.handDetectionAvailable ? 1.0 : 0.45
                onToggled: ScanSession.handDetectionEnabled = !ScanSession.handDetectionEnabled
            }
            Text {
                visible: !SettingsModel.handDetectionAvailable
                text: SettingsModel.handDetectionUnavailableReason
                font.family: Theme.fontHeading; font.pixelSize: 12
                color: Theme.inkMuted(0.6)
                width: parent.width; wrapMode: Text.WordWrap
            }
            Toggle {
                width: parent.width
                checked: ScanSession.autoAdvance
                label: "Auto-advance between faces"
                onToggled: ScanSession.autoAdvance = !ScanSession.autoAdvance
            }
        }

        Column {
            width: parent.width
            spacing: Theme.space2
            SectionLabel { text: "APPEARANCE" }
            ChipRow {
                options: SettingsState.themeModes.map(function (m) { return m.label; })
                currentIndex: SettingsState.themeModes.findIndex(function (m) { return m.key === SettingsState.themeMode; })
                onSelected: (index) => SettingsState.themeMode = SettingsState.themeModes[index].key
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
                color: Theme.inkMuted(0.6)
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

        Column {
            width: parent.width
            spacing: Theme.space2
            SectionLabel { text: "RENDERING" }
            readonly property string currentBackendName: SettingsState.graphicsApiNames[GraphicsInfo.api] || ("API " + GraphicsInfo.api)
            readonly property bool pendingRestart: SettingsState.graphicsBackend !== "auto"
                && SettingsState.graphicsBackendKeyNames[SettingsState.graphicsBackend] !== currentBackendName
            Text {
                text: "Graphics backend. Currently running " + parent.currentBackendName + "."
                font.family: Theme.fontHeading; font.pixelSize: 12
                color: Theme.inkMuted(0.6)
                width: parent.width; wrapMode: Text.WordWrap
            }
            ChipRow {
                options: SettingsState.availableGraphicsBackends.map(function (b) { return b.label; })
                currentIndex: SettingsState.availableGraphicsBackends.findIndex(function (b) { return b.key === SettingsState.graphicsBackend; })
                onSelected: (index) => SettingsState.graphicsBackend = SettingsState.availableGraphicsBackends[index].key
            }
            Text {
                visible: parent.pendingRestart
                text: "Restart Cubeulator to switch to this backend."
                font.family: Theme.fontHeading; font.pixelSize: 12
                color: Theme.warning
                width: parent.width; wrapMode: Text.WordWrap
            }
        }

        Column {
            width: parent.width
            spacing: Theme.space3
            SectionLabel { text: "DEVELOPER" }
            Toggle {
                checked: SettingsState.statsForNerds
                label: "Stats for nerds (FPS, render backend)"
                onToggled: SettingsState.statsForNerds = !SettingsState.statsForNerds
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
                    showStats: SettingsState.statsForNerds
                }

                AppButton {
                    anchors.horizontalCenter: parent.horizontalCenter
                    variant: "secondary"
                    text: "Test turn"
                    onClicked: {
                        var axes = ["x", "y", "z"];
                        var layers = [-1, 0, 1];
                        var axis = axes[Math.floor(Math.random() * 3)];
                        var layer = layers[Math.floor(Math.random() * 3)];
                        // Demonstrates Instructional Glow's active-layer hook
                        // using the same layer being turned - a stand-in for
                        // "this is the layer you're about to move" until the
                        // solve-visualisation session wires up real data.
                        previewCube.highlightedLayer = { axis: axis, layer: layer };
                        previewCube.turnLayer(axis, layer, true);
                    }
                }
            }
        }
    }
}
