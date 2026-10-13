pragma Singleton
import QtQuick
import Qt.labs.settings
import Cubeulator

// Demo/mock settings state, following the same pattern as AppState. Camera
// device selection lives on CameraController (selectedDeviceId/
// selectDevice()), not here - it's the same controller ScanSession.start()
// reads from, so there's a single source of truth for which device is live.
QtObject {
    id: settings

    readonly property var languages: ["English"]
    property int languageIndex: 0

    readonly property var shaderStyles: ["Physically-Based Realism", "Instructional Glow", "Borderlands Cel-Shading"]
    property int shaderStyleIndex: 0

    property bool cubeAutoRotate: true
    readonly property var rotateSpeeds: ["Slow", "Normal", "Fast"]
    property int rotateSpeedIndex: 1
    readonly property real rotateSpeedValue: [0.15, 0.35, 0.65][rotateSpeedIndex]

    property bool statsForNerds: false

    // Persisted (survives restart) because it can only take effect on the
    // next launch - QQuickWindow's graphics API is fixed for the process's
    // lifetime, see main.cpp. "auto" leaves Qt's own per-platform default.
    // QtObject has no default property, so this needs an explicit property
    // assignment (not just a bare child) - the alias below then reaches
    // through its id, since aliases can't target a property name directly.
    property Settings persistedSettings: Settings {
        id: persistedSettingsId
        category: "rendering"
        property string graphicsBackend: "auto"
    }
    property alias graphicsBackend: persistedSettingsId.graphicsBackend

    // "system" | "light" | "dark" - unlike graphicsBackend this applies
    // live (Theme.qml's colors are just reactive QML properties, no process
    // restart needed), but it's still persisted since it's a genuine
    // standing preference.
    readonly property var themeModes: [
        { key: "system", label: "System" },
        { key: "light", label: "Light" },
        { key: "dark", label: "Dark" }
    ]
    property Settings appearanceSettings: Settings {
        id: appearanceSettingsId
        category: "appearance"
        property string themeMode: "system"
    }
    property alias themeMode: appearanceSettingsId.themeMode

    // Which QRhi backends Qt actually supports on this platform (see
    // https://doc.qt.io/qt-6/qsgrendererinterface.html): "auto" always, then
    // whatever the platform's own implementation lists (PlatformFacts, from
    // interfaces/platform_info.hpp). Kept as a single source of truth here
    // rather than in StatsOverlay so main.cpp's comment about valid values
    // and this list can't drift apart silently.
    readonly property var availableGraphicsBackends:
        [{ key: "auto", label: "Auto" }].concat(PlatformFacts.graphicsBackends)

    // GraphicsInfo.api (a running window's actual backend) -> display name.
    readonly property var graphicsApiNames: ({
        0: "Unknown", 1: "Software", 2: "OpenVG", 3: "OpenGL",
        4: "Direct3D11", 5: "Vulkan", 6: "Metal", 7: "Null", 8: "Direct3D12"
    })
    // graphicsBackend's persisted key -> the display name graphicsApiNames
    // would produce for it, so a "restart to apply" hint can tell whether
    // the chosen backend matches what's actually running.
    readonly property var graphicsBackendKeyNames: ({
        d3d11: "Direct3D11", d3d12: "Direct3D12", vulkan: "Vulkan", opengl: "OpenGL", metal: "Metal"
    })
}
