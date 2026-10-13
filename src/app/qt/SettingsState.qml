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
    //
    // Stored as `appearance/theme` (docs/design/screens/settings.md,
    // "Persistence"): the key is named for the design, the QML property keeps
    // the name the existing screens read. Before the design named its keys it
    // was `appearance/themeMode`; that value is carried over once, below.
    property Settings appearanceSettings: Settings {
        id: appearanceSettingsId
        category: "appearance"
        property string theme: "system"
    }
    property alias themeMode: appearanceSettingsId.theme

    // The key this setting had before the design named it. "" when absent; the
    // migration below empties it so that it is only ever carried over once.
    property Settings legacyThemeSettings: Settings {
        id: legacyThemeSettingsId
        category: "appearance"
        property string themeMode: ""
    }
    Component.onCompleted: {
        const old = legacyThemeSettingsId.themeMode;
        if (old === "")
            return;
        if (appearanceSettingsId.theme === "system" && (old === "light" || old === "dark"))
            appearanceSettingsId.theme = old;
        legacyThemeSettingsId.themeMode = "";
    }

    // Which QRhi backends Qt actually supports on this platform (see
    // https://doc.qt.io/qt-6/qsgrendererinterface.html and the Qt 6.0
    // graphics blog post): Direct3D is Windows-only, Metal is macOS/iOS-only,
    // Vulkan/OpenGL are available wherever Qt ships a backend for them - iOS
    // effectively has just the one real choice. Kept as a single source of
    // truth here rather than in StatsOverlay so main.cpp's comment about
    // valid values and this list can't drift apart silently.
    readonly property var availableGraphicsBackends: {
        var opts = [{ key: "auto", label: "Auto" }];
        switch (Qt.platform.os) {
        case "windows":
            opts.push({ key: "d3d11", label: "D3D11" });
            opts.push({ key: "d3d12", label: "D3D12" });
            opts.push({ key: "vulkan", label: "Vulkan" });
            opts.push({ key: "opengl", label: "GL" });
            break;
        case "osx":
            opts.push({ key: "metal", label: "Metal" });
            opts.push({ key: "opengl", label: "GL" });
            break;
        case "linux":
            opts.push({ key: "vulkan", label: "Vulkan" });
            opts.push({ key: "opengl", label: "GL" });
            break;
        case "android":
            opts.push({ key: "vulkan", label: "Vulkan" });
            opts.push({ key: "opengl", label: "GLES" });
            break;
        case "ios":
            opts.push({ key: "metal", label: "Metal" });
            break;
        }
        return opts;
    }

    // ---- Settings the design adds (E2a) -------------------------------------
    // Everything in Settings that the design's Settings screen lists
    // (docs/design/strings.json, settings.*; docs/design/journey.md). Each is
    // persisted the moment it changes and applied live: Theme turns the
    // appearance ones into tokens, and nothing here needs a restart.
    //
    // The value names are the ones the Settings screen and the prototype use,
    // so a stored value means the same to every screen. A stored value that is
    // not one of these (a hand-edited file, a later version's choice) is read
    // as the default by whoever consumes it, never as an error.

    // Language, stored as `language/code`. "" is "same as this device" (the
    // design: absent means the same). Otherwise a language tag: en, de, fr, es,
    // ja, zh-CN, he, ar, yi. E2b turns it into the running translation. A stored
    // "system" from before the key was named is read as "".
    property Settings languageSettings: Settings {
        id: languageSettingsId
        category: "language"
        property string code: ""
    }
    property alias language: languageSettingsId.code

    // Marks on cube colours: a ring, slash, plus, dot or bar on each colour so
    // they can be told apart without hue. On by default.
    property Settings marksSettings: Settings {
        id: marksSettingsId
        category: "appearance"
        property bool marks: true
    }
    property alias marks: marksSettingsId.marks

    // "system" (as the device, multiplier 1) | "large" (1.25) | "larger" (1.5).
    // Multiplied with the operating system's own text size in Theme.textScale.
    property Settings textSizeSettings: Settings {
        id: textSizeSettingsId
        category: "appearance"
        property string textSize: "system"
    }
    property alias textSize: textSizeSettingsId.textSize

    // "system" (as the device) | "reduced" | "full". Theme.reducedMotion.
    property Settings motionSettings: Settings {
        id: motionSettingsId
        category: "appearance"
        property string motion: "system"
    }
    property alias motion: motionSettingsId.motion

    // Solving. How a turn is advanced: "tap" (a tap on the cube or either
    // button, or Space) | "timer" (the paced solve). Pace applies to the timer
    // only: "slow" (about six seconds a turn) | "steady" (four) | "quick"
    // (two and a half).
    property Settings solvingSettings: Settings {
        id: solvingSettingsId
        category: "solving"
        property string advance: "tap"
        property string pace: "steady"
        property bool explain: true
        property bool notation: false
        property bool sound: true
        property bool haptics: true
    }
    property alias advance: solvingSettingsId.advance
    property alias pace: solvingSettingsId.pace
    property alias explainStages: solvingSettingsId.explain
    property alias notation: solvingSettingsId.notation
    property alias sound: solvingSettingsId.sound
    property alias haptics: solvingSettingsId.haptics

    // History, kept on this device only.
    property Settings historySettings: Settings {
        id: historySettingsId
        category: "history"
        property bool keep: true
    }
    property alias keepHistory: historySettingsId.keep

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
