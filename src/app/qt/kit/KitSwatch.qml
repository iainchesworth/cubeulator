import QtQuick
import Cubeulator

// Swatch (docs/design/system.md, "Sticker, swatch, side chip"): a 1 em
// sticker with a 2 px edge, in line with the text that names a colour or a
// side ("Yellow on top"). It is the colour's picture beside its word, never
// instead of it.
KitSticker {
    id: root

    // Defaults to the line of text it sits beside: 1 rem, so it grows with the
    // text size.
    size: Theme.rem
    onPage: true
    edgeWidth: Theme.borderRule

    objectName: "kitSwatch"
}
