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
        // Small-capital labels are written in sentence case in the catalogue
        // and capitalised here, which is locale-aware (content.md).
        font.capitalization: Font.AllUppercase
        color: Theme.accent
    }

    // The theme choices by their key, so that each is a plain qsTrId() call
    // the extractor can see; SettingsState owns the keys, not the words.
    readonly property var themeLabels: ({
        system: qsTrId("settings.theme.system"),
        light: qsTrId("settings.theme.light"),
        dark: qsTrId("settings.theme.dark")
    })

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
                objectName: chipRoot.objectName !== "" ? chipRoot.objectName + "." + index : ""
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
            SectionLabel { text: qsTrId("settings.section.camera") }

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
                        ? qsTrId("settings.camera.finding")
                        : CameraController.devices.length > 0
                            ? qsTrId("settings.camera.choose")
                            : qsTrId("settings.camera.none")
                    font.family: Theme.fontHeading; font.pixelSize: 12
                    color: Theme.inkMuted(0.6)
                    width: parent.width; wrapMode: Text.WordWrap
                }
                ChipRow {
                    visible: CameraController.devices.length > 0
                    objectName: "settings.camera"
                    options: CameraController.devices.map(function (d) { return d.label; })
                    currentIndex: CameraController.devices.findIndex(function (d) { return d.id === CameraController.selectedDeviceId; })
                    onSelected: (index) => CameraController.selectDevice(CameraController.devices[index].id)
                }
                AppButton {
                    variant: "ghost"; objectName: "settings.refreshCameras"; leadingIcon: "refresh"; text: qsTrId("common.lookAgain")
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
                        text: AppState.cameraGranted ? qsTrId("settings.camera.allowed") : qsTrId("settings.camera.refused")
                        font.family: Theme.fontHeading; font.pixelSize: 13; color: Theme.ink
                    }
                }
                AppButton {
                    visible: !AppState.cameraGranted
                    objectName: "settings.grantCamera"; variant: "secondary"; text: qsTrId("permission.allow")
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
                objectName: "settings.handDetection"
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
                objectName: "settings.autoAdvance"
                checked: ScanSession.autoAdvance
                label: "Auto-advance between faces"
                onToggled: ScanSession.autoAdvance = !ScanSession.autoAdvance
            }
        }

        Column {
            width: parent.width
            spacing: Theme.space2
            SectionLabel { text: qsTrId("settings.section.appearance") }
            ChipRow {
                objectName: "settings.theme"
                options: SettingsState.themeModes.map(function (m) { return root.themeLabels[m.key]; })
                currentIndex: SettingsState.themeModes.findIndex(function (m) { return m.key === SettingsState.themeMode; })
                onSelected: (index) => SettingsState.themeMode = SettingsState.themeModes[index].key
            }
        }

        Column {
            width: parent.width
            spacing: Theme.space2
            SectionLabel { text: qsTrId("settings.section.language") }
            // The first choice follows the device; the rest are the nine
            // languages, each written in itself and never translated. Choosing
            // one applies at once - strings, direction, numbers - and is kept
            // under language/code (LanguageManager).
            ChipRow {
                objectName: "settings.language"
                readonly property var languages: LanguageManager.availableLanguages()
                options: [qsTrId("settings.language.system").arg(LanguageManager.deviceLanguage().name)]
                    .concat(languages.map(function (l) { return l.name; }))
                currentIndex: {
                    if (LanguageManager.selectedLanguage === "")
                        return 0;
                    for (var i = 0; i < languages.length; ++i) {
                        if (languages[i].code === LanguageManager.selectedLanguage)
                            return i + 1;
                    }
                    return 0;
                }
                onSelected: (index) => {
                    if (index === 0)
                        LanguageManager.useSystemLanguage();
                    else
                        LanguageManager.setLanguage(languages[index - 1].code);
                }
            }
        }

        Column {
            width: parent.width
            spacing: Theme.space2
            SectionLabel { text: qsTrId("settings.dev.cubeStyle") }
            Text {
                text: "Shader style for the 3D cube shown around the app."
                font.family: Theme.fontHeading; font.pixelSize: 12
                color: Theme.inkMuted(0.6)
                width: parent.width; wrapMode: Text.WordWrap
            }
            ChipRow {
                objectName: "settings.cubeStyle"
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
                objectName: "settings.cubeAutoRotate"
                checked: SettingsState.cubeAutoRotate
                label: "Auto-rotate when idle"
                onToggled: SettingsState.cubeAutoRotate = !SettingsState.cubeAutoRotate
            }
            ChipRow {
                objectName: "settings.rotateSpeed"
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
                text: qsTrId("settings.dev.backend").arg(parent.currentBackendName)
                font.family: Theme.fontHeading; font.pixelSize: 12
                color: Theme.inkMuted(0.6)
                width: parent.width; wrapMode: Text.WordWrap
            }
            ChipRow {
                objectName: "settings.graphicsBackend"
                options: SettingsState.availableGraphicsBackends.map(function (b) { return b.label; })
                currentIndex: SettingsState.availableGraphicsBackends.findIndex(function (b) { return b.key === SettingsState.graphicsBackend; })
                onSelected: (index) => SettingsState.graphicsBackend = SettingsState.availableGraphicsBackends[index].key
            }
            Text {
                visible: parent.pendingRestart
                text: qsTrId("settings.dev.restart")
                font.family: Theme.fontHeading; font.pixelSize: 12
                color: Theme.warning
                width: parent.width; wrapMode: Text.WordWrap
            }
        }

        // E4a: the way to About on Windows and Linux, which have no menu bar
        // (platforms.md). A placeholder row; E3a/E3b replace it with the real
        // About row and screen.
        Column {
            width: parent.width
            // Reads AppInfo, which only the desktop shell registers.
            visible: !AppState.isMobile
            AppButton {
                objectName: "aboutRow"
                variant: "ghost"
                text: qsTrId("settings.about")
                onClicked: AppInfo.showAbout()
            }
        }

        Column {
            width: parent.width
            spacing: Theme.space3
            SectionLabel { text: qsTrId("settings.section.developer") }
            Toggle {
                objectName: "settings.statsForNerds"
                checked: SettingsState.statsForNerds
                label: qsTrId("settings.dev.stats")
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
            AppButton { variant: "ghost"; objectName: "settings.back"; leadingIcon: "chevron-left"; text: qsTrId("common.back"); onClicked: AppState.closeSettings() }
        }
        Text { text: qsTrId("common.settings"); font.family: Theme.fontHeading; font.weight: Font.ExtraBold; font.pixelSize: 28; color: Theme.ink }

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

            AppButton { variant: "ghost"; objectName: "settings.back"; leadingIcon: "chevron-left"; text: qsTrId("common.back"); onClicked: AppState.closeSettings() }
            Text { text: qsTrId("common.settings"); font.family: Theme.fontHeading; font.weight: Font.ExtraBold; font.pixelSize: 36; color: Theme.ink }

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

            Rectangle { anchors.left: parent.left; width: 2; height: parent.height; color: Theme.divider }

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
                    LayoutMirroring.enabled: false
                    LayoutMirroring.childrenInherit: true
                }

                AppButton {
                    anchors.horizontalCenter: parent.horizontalCenter
                    variant: "secondary"
                    objectName: "settings.testTurn"
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
