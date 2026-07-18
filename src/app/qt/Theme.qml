pragma Singleton
import QtQuick

QtObject {
    readonly property color ink: "#201E1D"
    readonly property color paper: "#F3F2F2"
    readonly property color surface: "#EAE9E9"
    readonly property color divider: Qt.rgba(0.125, 0.117, 0.113, 0.4)

    readonly property color accent: "#EC3013"
    readonly property color accent100: "#FFF2EF"
    readonly property color accent600: "#DD2B0F"
    readonly property color accent700: "#AE1800"

    readonly property color detected: "#2F7D7B"
    readonly property color warning: "#C07C12"

    readonly property color neutral200: "#EAE7E7"
    readonly property color neutral400: "#BAB6B6"
    readonly property color neutral500: "#9B9797"
    readonly property color neutral600: "#7D7979"
    readonly property color neutral900: "#2D2B2B"

    readonly property color faceU: "#F2F0EC"
    readonly property color faceD: "#F0C94A"
    readonly property color faceF: "#3A9D5D"
    readonly property color faceB: "#2F6FAE"
    readonly property color faceL: "#DF7A2B"
    readonly property color faceR: "#D1372A"

    readonly property string fontHeading: "Archivo"
    readonly property string fontMono: "IBM Plex Mono"

    readonly property real space1: 4
    readonly property real space2: 8
    readonly property real space3: 12
    readonly property real space4: 16
    readonly property real space6: 24
    readonly property real space8: 32

    readonly property int wideBreakpoint: 720
}
