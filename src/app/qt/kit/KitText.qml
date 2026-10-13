import QtQuick
import Cubeulator

// Text in one of the design's type roles (docs/design/system.md, "Type").
// Size follows the text scale, family follows the language's script, and the
// text wraps: it is never elided on a control, because truncation in the
// pseudo-locale is a failed test.
//
// Eyebrows are the only capitals. They are set by capitalisation, never typed
// in capitals, so a translator sees ordinary text.
Text {
    id: root

    // display | title | headline | instruction | direction | body | bodyStrong |
    // small | label | eyebrow | numeral | numeralLarge
    property string role: "body"
    // The role's alternative size: "compact" (display) or "giant" (label).
    property string variant: ""
    // Drawn over the camera picture: the overlay's ink instead of the theme's.
    property bool onOverlay: false

    readonly property var spec: Theme.typeRole(role)
    readonly property bool isHeading: role === "display" || role === "title" || role === "headline"

    color: onOverlay ? Theme.overlay.ink : Theme.ink
    textFormat: Text.PlainText
    wrapMode: Text.Wrap
    elide: Text.ElideNone

    // The size in pixels, worked out once: letter spacing is a multiple of it,
    // and reading `font.pixelSize` back inside `font.letterSpacing` is a loop.
    readonly property real pixelSize: Math.round(Theme.typeSize(role, variant))

    font.family: spec.family === "mono" ? Theme.fontMono : Theme.headingFamily
    font.pixelSize: pixelSize
    font.weight: spec.family === "mono" ? spec.weight : Math.min(spec.weight, Theme.maxWeight)
    font.capitalization: (spec.caps === true && Theme.hasCapitals) ? Font.AllUppercase : Font.MixedCase
    font.letterSpacing: (spec.tracking !== undefined && Theme.hasCapitals) ? spec.tracking * pixelSize : 0
    lineHeight: spec.line
    lineHeightMode: Text.ProportionalHeight

    Accessible.role: isHeading ? Accessible.Heading : Accessible.StaticText
    Accessible.name: text
}
