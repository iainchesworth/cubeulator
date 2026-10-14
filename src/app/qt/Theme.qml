pragma Singleton
import QtQuick
import Cubeulator

QtObject {
    // "dark" | "light" resolves directly; "system" follows the OS via
    // SystemTheme (a live-reactive wrapper - see its own comment for why
    // that can't just be a binding on Qt.styleHints.colorScheme).
    readonly property bool effectiveDark: SettingsState.themeMode === "dark"
        || (SettingsState.themeMode === "system" && SystemTheme.dark)

    readonly property color ink: effectiveDark ? "#F3F2F2" : "#201E1D"
    readonly property color paper: effectiveDark ? "#1A1918" : "#F3F2F2"
    readonly property color surface: effectiveDark ? "#242220" : "#EAE9E9"
    readonly property color divider: effectiveDark ? Qt.rgba(1, 1, 1, 0.16) : Qt.rgba(0.125, 0.117, 0.113, 0.4)

    // Secondary/muted text was previously a hardcoded near-black rgba tuple
    // throughout the screens, which read fine on the light paper background
    // but would be near-illegible on the dark one - this derives it from
    // the current ink colour instead, so it inverts correctly in both modes.
    function inkMuted(opacity) { return Qt.rgba(ink.r, ink.g, ink.b, opacity); }

    readonly property color accent: "#EC3013"
    readonly property color accent100: effectiveDark ? "#3A1A14" : "#FFF2EF"
    readonly property color accent600: "#DD2B0F"
    readonly property color accent700: "#AE1800"

    readonly property color detected: "#2F7D7B"
    readonly property color warning: "#C07C12"

    readonly property color neutral200: effectiveDark ? "#2A2827" : "#EAE7E7"
    readonly property color neutral400: effectiveDark ? "#8C8888" : "#BAB6B6"
    readonly property color neutral500: "#9B9797"
    readonly property color neutral600: effectiveDark ? "#BAB6B6" : "#7D7979"
    readonly property color neutral900: effectiveDark ? "#EAE7E7" : "#2D2B2B"

    // Cube sticker colours represent a real, physical cube - they stay the
    // same regardless of the app's own light/dark chrome.
    readonly property color faceU: "#F2F0EC"
    readonly property color faceD: "#F0C94A"
    readonly property color faceF: "#3A9D5D"
    readonly property color faceB: "#2F6FAE"
    readonly property color faceL: "#DF7A2B"
    readonly property color faceR: "#D1372A"

    // The face. Archivo is the Latin one. Japanese and Chinese share code
    // points and not shapes, so for them the family is chosen by the app's
    // language and not left to glyph fallback, which draws Chinese in
    // Japanese forms (or the reverse) and sets one line in two faces at two
    // weights. QML's font has no fallback list, so the first of the
    // language's own families that is installed is named here. Which families
    // those are is the platform's to say (PlatformFacts, from
    // interfaces/platform_info.hpp: the system faces on a desktop or phone,
    // the bundled Noto Sans JP and SC in the browser). Qt.uiLanguage is the
    // language the manager installed: "ja_JP", "zh_CN".
    // docs/design/platforms.md, "Fonts".
    //
    // The first installed family for `code` ("ja" or "zh"), or "" when the
    // machine has none.
    function systemCjkFamily(code) {
        const wanted = code === "ja" ? PlatformFacts.japaneseFaces : PlatformFacts.chineseFaces;
        const installed = Qt.fontFamilies();
        for (let i = 0; i < wanted.length; ++i) {
            if (installed.indexOf(wanted[i]) >= 0)
                return wanted[i];
        }
        return "";
    }
    readonly property string fontHeading: {
        const code = Qt.uiLanguage.split(/[-_]/)[0].toLowerCase();
        if (code === "ja" || code === "zh")
            return systemCjkFamily(code) || "Archivo";
        return "Archivo";
    }
    readonly property string fontMono: "IBM Plex Mono"

    readonly property real space1: 4
    readonly property real space2: 8
    readonly property real space3: 12
    readonly property real space4: 16
    readonly property real space6: 24
    readonly property real space8: 32

    readonly property int wideBreakpoint: 720
}
