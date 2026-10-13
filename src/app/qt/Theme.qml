pragma Singleton
import QtQuick
import Cubeulator

// The design tokens (docs/design/tokens.json, specified in
// docs/design/system.md) and the system settings that scale them.
//
// tokens.json is the source of truth. This file is a hand port, and
// tests/kit/theme_tokens_test.cpp fails when any value here differs from the
// JSON, in either theme, so the two cannot drift. When a token changes, change
// tokens.json first, run docs/design/tools/check_tokens.py, then this file.
//
// A screen uses a token's NAME and never its value; a colour or a pixel size
// written into a screen is a defect (docs/app-experience-plan.md, E3 gate).
//
// The names from before the design system (accent600, inkMuted(), faceU and
// so on) are kept at the foot of this file as legacy aliases because the scan
// and solve screens still use them. New code does not.
QtObject {
    id: theme

    // ---- Which theme --------------------------------------------------------
    // "dark" | "light" resolves directly; "system" follows the OS via
    // SystemTheme (a live-reactive wrapper over QStyleHints::colorSchemeChanged,
    // since a binding on Qt.styleHints.colorScheme does not update in QML).
    readonly property bool effectiveDark: SettingsState.themeMode === "dark"
        || (SettingsState.themeMode === "system" && SystemTheme.dark)

    // ---- Colour: the semantic tokens, light and dark ----------------------------
    // One table, the shape of tokens.json's "color" object, so the test that
    // keeps the two in step is a straight comparison. The named properties
    // below read from it; `colour(name, dark)` reads a token for a given theme
    // regardless of the current one, which is how a control drawn over the
    // camera (always against a dark panel) takes the dark palette in a light
    // theme.
    readonly property var tokens: ({
        paper:           { light: "#F3F2F2", dark: "#1A1918" },
        surface:         { light: "#EAE9E9", dark: "#242220" },
        ink:             { light: "#201E1D", dark: "#F3F2F2" },
        inkSecondary:    { light: "#5E5B5A", dark: "#B0ACAB" },
        inkDisabled:     { light: "#858281", dark: "#7A7776" },
        outline:         { light: "#858281", dark: "#7A7776" },
        divider:         { light: "#C9C7C6", dark: "#3A3836" },
        accent:          { light: "#EC3013", dark: "#EC3013" },
        accentText:      { light: "#B82009", dark: "#FF7359" },
        accentFill:      { light: "#B82009", dark: "#FF5A3D" },
        accentFillHover: { light: "#A51903", dark: "#FF6B50" },
        accentFillPress: { light: "#9E1702", dark: "#FF7D65" },
        onAccent:        { light: "#FFFFFF", dark: "#1A1918" },
        accentTint:      { light: "#FFF2EF", dark: "#3A1A14" },
        ok:              { light: "#286F6D", dark: "#5ABDBA" },
        caution:         { light: "#8A5800", dark: "#E0A030" },
        hoverWash:       { light: "#E4E2E2", dark: "#2E2C2A" },
        pressWash:       { light: "#DCDAD9", dark: "#343230" },
        cubeBody:        { light: "#1B1A1A", dark: "#0E0D0D" },
        cubeEdge:        { light: "#201E1D", dark: "#8C8988" }
    })
    function colour(name, dark) { return dark ? tokens[name].dark : tokens[name].light; }

    readonly property color paper:           colour("paper", effectiveDark)
    readonly property color surface:         colour("surface", effectiveDark)
    readonly property color ink:             colour("ink", effectiveDark)
    readonly property color inkSecondary:    colour("inkSecondary", effectiveDark)
    readonly property color inkDisabled:     colour("inkDisabled", effectiveDark)
    readonly property color outline:         colour("outline", effectiveDark)
    readonly property color divider:         colour("divider", effectiveDark)
    // The brand red. Shapes only (progress fill, the underline on "Solved",
    // the icon); never text, where accentText is used.
    readonly property color accent:          colour("accent", effectiveDark)
    readonly property color accentText:      colour("accentText", effectiveDark)
    readonly property color accentFill:      colour("accentFill", effectiveDark)
    readonly property color accentFillHover: colour("accentFillHover", effectiveDark)
    readonly property color accentFillPress: colour("accentFillPress", effectiveDark)
    // tokens.json calls this `onAccent`. A QML property whose name begins "on"
    // and a capital reads as a signal handler and cannot be read (checked on
    // 6.8.3, tests/kit/theme_tokens_test.cpp), so the property is `accentOn`;
    // the token table above and tokens.json keep the design's name.
    readonly property color accentOn:        colour("onAccent", effectiveDark)
    readonly property color accentTint:      colour("accentTint", effectiveDark)
    // Teal, so a positive state is never mistaken for the cube's green.
    readonly property color ok:              colour("ok", effectiveDark)
    readonly property color caution:         colour("caution", effectiveDark)
    readonly property color hoverWash:       colour("hoverWash", effectiveDark)
    readonly property color pressWash:       colour("pressWash", effectiveDark)
    readonly property color cubeBody:        colour("cubeBody", effectiveDark)
    readonly property color cubeEdge:        colour("cubeEdge", effectiveDark)

    // ---- Over the camera ------------------------------------------------------
    // The scan screen draws on a live picture no theme controls, so these are
    // the same in both themes. Contrast is measured against a pure white
    // picture, the worst case (check_tokens.py).
    readonly property QtObject overlay: QtObject {
        // Panel and scrim are a colour with an alpha, kept apart so a test can
        // compare each with tokens.json and a screen can blend them itself.
        readonly property color panelColor:   "#141312"
        readonly property real  panelAlpha:   0.86
        readonly property color panel:        Qt.rgba(0x14 / 255, 0x13 / 255, 0x12 / 255, 0.86)
        readonly property color ink:          "#F3F2F2"
        readonly property color inkSecondary: "#D9D6D4"
        readonly property color ok:           "#5FE07A"
        readonly property color caution:      "#FFB347"
        readonly property color outline:      "#9C9998"
        readonly property color scrimColor:   "#141312"
        readonly property real  scrimAlpha:   0.6
        readonly property color scrim:        Qt.rgba(0x14 / 255, 0x13 / 255, 0x12 / 255, 0.6)
    }
    // The dialog and sheet scrim is the same on every screen.
    readonly property color scrim: overlay.scrim

    // ---- The six cube colours and their marks ---------------------------------
    // Tuned so the closest pair under protanopia or deuteranopia is 11.7 apart
    // instead of 5.4. Hue is never the only signal: each colour carries a mark
    // (ring, slash, plus, dot, bar; white is plain), on by default.
    readonly property QtObject cube: QtObject {
        readonly property color white:  "#F2F0EC"
        readonly property color yellow: "#F6CA3F"
        readonly property color orange: "#F87E0F"
        readonly property color red:    "#CD3B37"
        readonly property color green:  "#4EB584"
        readonly property color blue:   "#0D5792"
    }
    // The order the colour picker shows them in; keys 1 to 6 choose.
    readonly property var cubeColourNames: ["white", "yellow", "orange", "red", "green", "blue"]
    readonly property var cubeMarks: ({
        white: "none", yellow: "ring", orange: "slash", red: "plus", green: "dot", blue: "bar"
    })
    readonly property var cubeMarkInks: ({
        white: "#201E1D", yellow: "#201E1D", orange: "#201E1D", red: "#FFFFFF", green: "#201E1D", blue: "#FFFFFF"
    })
    function cubeColour(name) { return cube[name]; }
    function cubeMark(name) { return cubeMarks[name] || "none"; }
    function cubeMarkInk(name) { return cubeMarkInks[name] || "#201E1D"; }
    // Settings > Appearance > Marks on cube colours.
    readonly property bool marksOn: SettingsState.marks

    // ---- Spacing, rules, targets ---------------------------------------------
    // Spacing is these eight values and nothing else.
    readonly property real space1: 4
    readonly property real space2: 8
    readonly property real space3: 12
    readonly property real space4: 16
    readonly property real space6: 24
    readonly property real space8: 32
    readonly property real space12: 48
    readonly property real space16: 64

    readonly property real borderHair: 1    // inside a component
    readonly property real borderRule: 2    // between regions and round a control
    readonly property real borderHeavy: 3   // the one emphasised thing
    readonly property real radius: 0        // every corner is square

    // Minimum sizes of anything the person can press.
    readonly property real targetMin: 48        // every interactive item
    readonly property real targetThumb: 56      // scan screen, sheets, dialogs
    readonly property real targetHandsFull: 96  // the solve screen's two controls

    // ---- Language, script, direction ------------------------------------------
    // "system" follows the device; otherwise a language tag such as "de".
    readonly property string languageTag: SettingsState.language === "system"
        ? Qt.locale().name : SettingsState.language
    readonly property var uiLocale: Qt.locale(languageTag)
    readonly property string languageCode: languageTag.split(/[-_]/)[0].toLowerCase()
    // "latin" | "hebrew" | "arabic" | "ja" | "zh"
    readonly property string script: {
        switch (languageCode) {
        case "he": case "iw": return "hebrew";
        case "ar": return "arabic";
        case "ja": return "ja";
        case "zh": return "zh";
        default: return "latin";
        }
    }
    // The layout mirrors. The cube, the camera picture, a side's nine
    // stickers and the direction of a turn never do (docs/design/accessibility.md).
    readonly property bool rtl: uiLocale.textDirection === Qt.RightToLeft

    // ---- Type -------------------------------------------------------------------
    // Archivo for Latin, IBM Plex Sans Hebrew and Arabic for those scripts, the
    // system's own for Japanese and Chinese, chosen by language so the shapes
    // are right (the two share code points, not forms). One family for the
    // whole UI in that language; every one of them draws Latin, so numbers and
    // the app's name still show. QML's `font` has no fallback list in 6.8, so
    // the choice is made here rather than left to glyph fallback.
    readonly property string fontHeading: "Archivo"
    readonly property string fontMono: "IBM Plex Mono"
    readonly property var installedFamilies: Qt.fontFamilies()
    function firstInstalled(candidates) {
        for (let i = 0; i < candidates.length; ++i) {
            if (installedFamilies.indexOf(candidates[i]) >= 0)
                return candidates[i];
        }
        return "";
    }
    function systemCjkFamilies(which) {
        switch (Qt.platform.os) {
        case "windows":
            return which === "ja" ? ["Yu Gothic UI", "Meiryo UI", "MS UI Gothic"]
                                  : ["Microsoft YaHei UI", "Microsoft YaHei"];
        case "osx":
        case "ios":
            return which === "ja" ? ["Hiragino Sans", "Hiragino Kaku Gothic ProN"]
                                  : ["PingFang SC", "Heiti SC"];
        default:
            return which === "ja" ? ["Noto Sans CJK JP", "Noto Sans JP", "Source Han Sans JP"]
                                  : ["Noto Sans CJK SC", "Noto Sans SC", "Source Han Sans SC", "WenQuanYi Micro Hei"];
        }
    }
    readonly property string headingFamily: {
        switch (script) {
        case "hebrew": return "IBM Plex Sans Hebrew";
        case "arabic": return "IBM Plex Sans Arabic";
        case "ja": return firstInstalled(systemCjkFamilies("ja")) || fontHeading;
        case "zh": return firstInstalled(systemCjkFamilies("zh")) || fontHeading;
        default: return fontHeading;
        }
    }
    // The scripts that have no extra-bold: their headings use 700.
    readonly property int maxWeight: (script === "hebrew" || script === "arabic") ? 700 : 800
    // Scripts without capitals drop the eyebrow's tracking and capitalisation.
    readonly property bool hasCapitals: script === "latin"

    // The scale. Sizes are in rem; one rem is 16 px x textScale. "remCompact" and
    // "remGiant" are the two role sizes that depend on where the role is used.
    readonly property var typeRoles: ({
        display:       { rem: 2.75,   remCompact: 2.25, weight: 800, line: 1.0,  family: "heading" },
        title:         { rem: 1.75,   weight: 800, line: 1.1,  family: "heading" },
        headline:      { rem: 1.375,  weight: 700, line: 1.2,  family: "heading" },
        instruction:   { rem: 2.0,    weight: 800, line: 1.1,  family: "heading" },
        direction:     { rem: 1.5,    weight: 700, line: 1.15, family: "heading" },
        body:          { rem: 1.0,    weight: 400, line: 1.45, family: "heading" },
        bodyStrong:    { rem: 1.0,    weight: 700, line: 1.45, family: "heading" },
        small:         { rem: 0.875,  weight: 400, line: 1.45, family: "heading" },
        label:         { rem: 1.0,    remGiant: 1.25, weight: 800, line: 1.2, family: "heading" },
        eyebrow:       { rem: 0.8125, weight: 700, line: 1.2,  family: "heading", tracking: 0.1, caps: true },
        numeral:       { rem: 1.0,    weight: 500, line: 1.2,  family: "mono" },
        numeralLarge:  { rem: 2.0,    weight: 500, line: 1.1,  family: "mono" }
    })

    // ---- Text size ----------------------------------------------------------------
    // The system's text size times the app's own (Settings > Appearance > Text
    // size: same as device, large 1.25, larger 1.5), clamped to 0.85 - 2.0.
    // Every layout must hold at 2.0 (WCAG 1.4.4).
    readonly property real appTextScale: {
        switch (SettingsState.textSize) {
        case "large": return 1.25;
        case "larger": return 1.5;
        default: return 1.0;
        }
    }
    readonly property real textScale: Math.max(0.85, Math.min(2.0, SystemTextScale.scale * appTextScale))
    readonly property real rem: 16 * textScale

    // ---- Motion -----------------------------------------------------------------------
    // On when the system asks for it, or when Settings > Appearance > Motion is
    // Reduced. "Full" is the person's own choice to override the system.
    readonly property bool reducedMotion: SettingsState.motion === "reduced"
        || (SettingsState.motion === "system" && SystemMotion.reduced)
    readonly property int motionInstant: 0
    readonly property int motionQuick: reducedMotion ? 0 : 120   // hover, press, a chip arriving
    readonly property int motionBase: reducedMotion ? 0 : 200    // a sheet or dialog opening
    readonly property int motionScreen: reducedMotion ? 120 : 280 // changing screen; reduced is opacity only
    readonly property int motionTurn: reducedMotion ? 0 : 380    // one layer turn (solve lane)

    // ---- Size classes ---------------------------------------------------------------------
    // Published by the root window (KitWindowBindings), in device-independent
    // pixels. The defaults are a desktop window until something publishes.
    property real windowWidth: 900
    property real windowHeight: 640
    // compact < 600, medium 600 - 839, expanded >= 840.
    readonly property string sizeClass: windowWidth >= 840 ? "expanded" : (windowWidth >= 600 ? "medium" : "compact")
    // short < 480 high, at any width; takes priority over the width class.
    readonly property string heightClass: windowHeight < 480 ? "short" : "regular"
    readonly property bool compact: sizeClass === "compact"
    readonly property bool expanded: sizeClass === "expanded"
    // 24 compact (16 under 360 wide), 32 medium, 40 expanded.
    readonly property real pageMargin: sizeClass === "expanded" ? 40
        : (sizeClass === "medium" ? 32 : (windowWidth < 360 ? 16 : 24))
    // What the expanded screens call a column of running text, 34 rem.
    readonly property real measure: 34 * rem

    // ---- Safe areas ----------------------------------------------------------------------------
    // The system's furniture over the window, as leading and trailing so it
    // mirrors. Added to the page margins on all four sides.
    readonly property QtObject insets: QtObject {
        readonly property real top: SystemInsets.top
        readonly property real bottom: SystemInsets.bottom
        readonly property real leading: theme.rtl ? SystemInsets.right : SystemInsets.left
        readonly property real trailing: theme.rtl ? SystemInsets.left : SystemInsets.right
    }

    // ---- Helpers --------------------------------------------------------------------------------
    function typeRole(name) { return typeRoles[name] || typeRoles.body; }
    // The role's size in pixels at the current text scale. `variant` picks the
    // alternative size a role has: "compact" for display, "giant" for label.
    function typeSize(name, variant) {
        const role = typeRole(name);
        let size = role.rem;
        if (variant === "compact" && role.remCompact !== undefined)
            size = role.remCompact;
        else if (variant === "giant" && role.remGiant !== undefined)
            size = role.remGiant;
        return size * rem;
    }
    function inkMuted(opacity) { return Qt.rgba(ink.r, ink.g, ink.b, opacity); }

    // ---- Legacy names ------------------------------------------------------------------------------
    // From before the design system. Still used by the scan and solve screens
    // (which this work must not edit) and removed with the last of them. Where
    // a legacy name has a token that means the same, it is an alias of it; where
    // not, it keeps its old value so those screens look as they did.
    //
    // CHANGED: faceU..faceR now return the retuned cube colours with their
    // colour-vision-safe spacing, so the solve lane's cube and the scan lane's
    // chips shift slightly in hue. Nothing else about those screens changes.
    readonly property color accent100: accentTint
    readonly property color accent600: "#DD2B0F"
    readonly property color accent700: "#AE1800"

    readonly property color detected: "#2F7D7B"
    readonly property color warning: "#C07C12"

    readonly property color neutral200: effectiveDark ? "#2A2827" : "#EAE7E7"
    readonly property color neutral400: effectiveDark ? "#8C8888" : "#BAB6B6"
    readonly property color neutral500: "#9B9797"
    readonly property color neutral600: effectiveDark ? "#BAB6B6" : "#7D7979"
    readonly property color neutral900: effectiveDark ? "#EAE7E7" : "#2D2B2B"

    readonly property color faceU: cube.white
    readonly property color faceD: cube.yellow
    readonly property color faceF: cube.green
    readonly property color faceB: cube.blue
    readonly property color faceL: cube.orange
    readonly property color faceR: cube.red

    readonly property int wideBreakpoint: 720
}
