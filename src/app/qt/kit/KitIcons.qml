pragma Singleton
import QtQuick

// The icon set: line icons on a 24 x 24 grid, 2 px stroke, round caps and
// joins, no fill (docs/design/system.md, "Icons"). The shapes are redrawn
// from the Lucide set the app already used (ISC licence) as SVG path data, so
// KitIcon can draw them as Shape items that scale cleanly and take a colour
// token, instead of painting them on a Canvas.
//
// A name beginning "turn-" is a direction of a turn of the cube. Those are
// never mirrored; they describe the object in the hand, not the layout.
QtObject {
    readonly property var paths: ({
        // Navigation: these mirror in right-to-left.
        "chevron-left": "M15 6 L9 12 L15 18",
        "chevron-right": "M9 6 L15 12 L9 18",
        "chevron-down": "M6 9 L12 15 L18 9",
        "chevron-up": "M6 15 L12 9 L18 15",

        // State.
        "check": "M20 6 L9 17 L4 12",
        "cross": "M18 6 L6 18 M6 6 L18 18",
        "alert": "M12 3 L2 20 L22 20 Z M12 10 L12 14 M12 17 L12 17.01",
        "info": "M21 12 A9 9 0 1 1 3 12 A9 9 0 1 1 21 12 M12 11 L12 17 M12 7.5 L12 7.51",
        "help": "M21 12 A9 9 0 1 1 3 12 A9 9 0 1 1 21 12 M9.5 9 C9.5 7 12 6.3 13 8 C13.7 9.2 13 9.8 12 10.7 C11.3 11.3 11 11.7 11 12.7 M12 17 L12 17.01",

        // Objects.
        "camera": "M14.5 4 L9.5 4 L7 7 L4 7 C2.9 7 2 7.9 2 9 L2 18 C2 19.1 2.9 20 4 20 L20 20 C21.1 20 22 19.1 22 18 L22 9 C22 7.9 21.1 7 20 7 L17 7 Z M15.2 13 A3.2 3.2 0 1 1 8.8 13 A3.2 3.2 0 1 1 15.2 13",
        "clock": "M21 12 A9 9 0 1 1 3 12 A9 9 0 1 1 21 12 M12 7 L12 12 L15.5 14",
        "sliders": "M21 4 L14 4 M10 4 L3 4 M21 12 L12 12 M8 12 L3 12 M21 20 L16 20 M12 20 L3 20 M14 2 L14 6 M8 10 L8 14 M16 18 L16 22",
        "grid": "M3 3 L21 3 L21 21 L3 21 Z M3 9 L21 9 M3 15 L21 15 M9 3 L9 21 M15 3 L15 21",
        "pause": "M8 5 L8 19 M16 5 L16 19",
        "refresh": "M12 3 A9 9 0 1 1 3.98 7.91 M21 4 L21 9 L16 9",
        "globe": "M21 12 A9 9 0 1 1 3 12 A9 9 0 1 1 21 12 M3 12 L21 12 M12 3 C14.5 5.4 15.8 8.6 15.8 12 C15.8 15.4 14.5 18.6 12 21 C9.5 18.6 8.2 15.4 8.2 12 C8.2 8.6 9.5 5.4 12 3 Z",
        "menu": "M4 7 L20 7 M4 12 L20 12 M4 17 L20 17",
        "more": "M5 12 L5 12.01 M12 12 L12 12.01 M19 12 L19 12.01",
        "sun": "M16 12 A4 4 0 1 1 8 12 A4 4 0 1 1 16 12 M12 2 L12 5 M12 19 L12 22 M2 12 L5 12 M19 12 L22 12 M5 5 L7 7 M17 17 L19 19 M19 5 L17 7 M7 17 L5 19",

        // The direction of a turn: describe the cube, so never mirrored.
        "turn-left": "M19 12 L5 12 M12 19 L5 12 L12 5",
        "turn-right": "M5 12 L19 12 M12 5 L19 12 L12 19",
        "turn-up": "M12 19 L12 5 M5 12 L12 5 L19 12",
        "turn-down": "M12 5 L12 19 M19 12 L12 19 L5 12",
        "turn-clockwise": "M21 12 A9 9 0 1 1 12 3 C14.52 3 16.93 4 18.74 5.74 L21 8 M21 3 L21 8 L16 8",
        "turn-anticlockwise": "M3 12 A9 9 0 1 0 12 3 C9.48 3 7.07 4 5.26 5.74 L3 8 M3 3 L3 8 L8 8",
        "turn-either": "M8 3 L4 7 L8 11 M4 7 L20 7 M16 21 L20 17 L16 13 M20 17 L4 17"
    })

    readonly property var names: Object.keys(paths)

    function path(name) {
        return paths[name] || "";
    }
    function isTurn(name) {
        return name.indexOf("turn-") === 0;
    }
    // Does this icon flip when the layout is mirrored? The back and forward
    // chevrons and the chevron on a row do. The question mark does in Arabic
    // only, where the mark itself is written the other way round.
    function mirrors(name, script) {
        if (name.indexOf("chevron-left") === 0 || name.indexOf("chevron-right") === 0)
            return true;
        if (name === "help")
            return script === "arabic";
        return false;
    }
}
