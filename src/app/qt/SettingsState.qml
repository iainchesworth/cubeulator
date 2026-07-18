pragma Singleton
import QtQuick
import Cubeulator

// Demo/mock settings state, following the same pattern as AppState: the
// camera list below stands in for cube::platform::camera::enumerate_devices()
// (still a stub - see src/app/platform/*/camera_*.cpp) until the vision
// pipeline spec lands and can populate it for real.
QtObject {
    id: settings

    readonly property var cameraDevices: ["System default", "Integrated webcam", "USB camera"]
    property int cameraDeviceIndex: 0

    readonly property var languages: ["English"]
    property int languageIndex: 0

    readonly property var shaderStyles: ["Modernist flat", "Glossy studio", "Toon outline"]
    property int shaderStyleIndex: 0

    property bool cubeAutoRotate: true
    readonly property var rotateSpeeds: ["Slow", "Normal", "Fast"]
    property int rotateSpeedIndex: 1
    readonly property real rotateSpeedValue: [0.15, 0.35, 0.65][rotateSpeedIndex]
}
