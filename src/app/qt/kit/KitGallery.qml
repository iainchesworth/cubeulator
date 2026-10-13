import QtQuick
import QtQuick.Controls.Basic
import Cubeulator

// Every kit component in every state, on one scrolling page: the thing the
// tests walk and the thing to look at. Reachable in developer builds only
// (BuildInfo.developerBuild), by `cube_app --gallery` or CUBEULATOR_GALLERY=1.
//
// The controls at the top change what the specimens are shown in, by the same
// settings the app's own Settings screen will: theme, text size, colour marks,
// motion, direction, and labels 40% longer (a stand-in for a translation until
// E2b's pseudo-locale exists).
//
// Every specimen has an objectName beginning "specimen/" followed by the
// component and the state, so a test can find them all without this file
// listing them. This file is developer-only: its words are not translated and
// do not need to be.
KitPage {
    id: root

    // Labels as a translation might be: 40% longer.
    property bool longLabels: false
    maxContentWidth: 100000

    // The page follows the language's direction, as the app does.
    LayoutMirroring.enabled: Theme.rtl
    LayoutMirroring.childrenInherit: true

    function label(text) {
        if (!longLabels || text.length === 0)
            return text;
        return text + " " + text.substring(0, Math.max(2, Math.ceil(text.length * 0.4)));
    }

    // ---- Parts the page is built from ---------------------------------------------

    // A titled group of specimens.
    component Section: Column {
        id: section
        property string title: ""
        default property alias specimens: flow.data
        width: parent ? parent.width : 0
        spacing: Theme.space3

        KitText {
            text: section.title
            role: "headline"
            width: parent.width
        }
        Rectangle {
            width: parent.width
            height: Theme.borderRule
            color: Theme.divider
            Accessible.ignored: true
        }
        Flow {
            id: flow
            width: parent.width
            spacing: Theme.space6
        }
    }

    // One specimen: a caption above, and the component beneath it.
    component Specimen: Column {
        id: specimen
        property string caption: ""
        default property alias content: holder.data
        spacing: Theme.space2

        KitText {
            text: specimen.caption
            role: "small"
            color: Theme.inkSecondary
            width: Math.max(holder.width, 120)
        }
        Item {
            id: holder
            // The focus ring sits outside a control; leave room for it.
            implicitWidth: childrenRect.width + 2 * Theme.space2
            implicitHeight: childrenRect.height + 2 * Theme.space2
            width: implicitWidth
            height: implicitHeight
        }
    }

    // What the camera shows: any picture. The worst case is bright, so that is
    // what the overlay components are shown on.
    component Backdrop: Rectangle {
        default property alias content: inner.data
        implicitWidth: inner.childrenRect.width + 2 * Theme.space4
        implicitHeight: inner.childrenRect.height + 2 * Theme.space4
        color: "#FFFFFF"
        border.width: 1
        border.color: Theme.divider
        Item {
            id: inner
            x: Theme.space4
            y: Theme.space4
            width: childrenRect.width
            height: childrenRect.height
        }
    }

    // ---- The page -----------------------------------------------------------------------
    KitText {
        text: "Cubeulator kit"
        role: "display"
        variant: Theme.compact ? "compact" : ""
        width: parent.width
    }

    // ---- What the specimens are shown in ----
    Section {
        title: "Shown in"

        Specimen {
            caption: "Theme"
            Row {
                spacing: Theme.space2
                Repeater {
                    model: ["system", "light", "dark"]
                    KitButton {
                        objectName: "control/theme/" + modelData
                        text: modelData
                        variant: SettingsState.themeMode === modelData ? "primary" : "secondary"
                        onClicked: SettingsState.themeMode = modelData
                    }
                }
            }
        }
        Specimen {
            caption: "Text size"
            Row {
                spacing: Theme.space2
                Repeater {
                    model: [1.0, 1.5, 2.0]
                    KitButton {
                        objectName: "control/textscale/" + modelData
                        text: modelData.toFixed(1) + "x"
                        variant: Math.abs(Theme.textScale - modelData) < 0.01 ? "primary" : "secondary"
                        onClicked: {
                            SystemTextScale.setOverride(modelData);
                            SettingsState.textSize = "system";
                        }
                    }
                }
            }
        }
        Specimen {
            caption: "Direction"
            Row {
                spacing: Theme.space2
                Repeater {
                    model: [{ key: "en", text: "Left to right" }, { key: "he", text: "Right to left" }]
                    KitButton {
                        objectName: "control/direction/" + modelData.key
                        text: modelData.text
                        variant: (Theme.rtl === (modelData.key === "he")) ? "primary" : "secondary"
                        onClicked: SettingsState.language = modelData.key
                    }
                }
            }
        }
        Specimen {
            caption: "Labels"
            KitButton {
                objectName: "control/longlabels"
                text: root.longLabels ? "40% longer" : "As written"
                variant: root.longLabels ? "primary" : "secondary"
                onClicked: root.longLabels = !root.longLabels
            }
        }
        Specimen {
            caption: "Colour marks"
            KitButton {
                objectName: "control/marks"
                text: SettingsState.marks ? "On" : "Off"
                variant: SettingsState.marks ? "primary" : "secondary"
                onClicked: SettingsState.marks = !SettingsState.marks
            }
        }
        Specimen {
            caption: "Motion"
            KitButton {
                objectName: "control/motion"
                text: SettingsState.motion === "reduced" ? "Reduced" : "Full"
                variant: SettingsState.motion === "reduced" ? "primary" : "secondary"
                onClicked: SettingsState.motion = SettingsState.motion === "reduced" ? "full" : "reduced"
            }
        }
        Specimen {
            caption: "Safe area"
            KitButton {
                objectName: "control/insets"
                text: SystemInsets.overridden ? "A notch" : "None"
                variant: SystemInsets.overridden ? "primary" : "secondary"
                onClicked: {
                    if (SystemInsets.overridden)
                        SystemInsets.clearOverride();
                    else
                        SystemInsets.setOverride(44, 0, 0, 34);
                }
            }
        }
    }

    // ---- Button ----
    Section {
        title: "Button: variant and state"

        Repeater {
            model: {
                const variants = ["primary", "secondary", "ghost"];
                const states = [
                    { key: "rest", preview: "", enabled: true },
                    { key: "hover", preview: "hover", enabled: true },
                    { key: "pressed", preview: "pressed", enabled: true },
                    { key: "focus", preview: "focus", enabled: true },
                    { key: "disabled", preview: "", enabled: false }
                ];
                const list = [];
                for (const v of variants)
                    for (const s of states)
                        list.push({ variant: v, state: s.key, preview: s.preview, enabled: s.enabled });
                return list;
            }
            Specimen {
                caption: modelData.variant + " " + modelData.state
                KitButton {
                    objectName: "specimen/button/" + modelData.variant + "/" + modelData.state
                    text: root.label("Scan a cube")
                    variant: modelData.variant
                    previewState: modelData.preview
                    enabled: modelData.enabled
                }
            }
        }
    }

    Section {
        title: "Button: size, icon, key, block"

        Specimen {
            caption: "regular 48"
            KitButton { objectName: "specimen/button/size/regular"; text: root.label("Regular"); size: "regular" }
        }
        Specimen {
            caption: "thumb 56"
            KitButton { objectName: "specimen/button/size/thumb"; text: root.label("Thumb"); size: "thumb" }
        }
        Specimen {
            caption: "giant 96, key name"
            KitButton {
                objectName: "specimen/button/size/giant"
                text: root.label("Next turn")
                size: "giant"
                keyName: "Space"
            }
        }
        Specimen {
            caption: "leading icon"
            KitButton {
                objectName: "specimen/button/icon/leading"
                text: root.label("Help")
                variant: "ghost"
                leadingIcon: "help"
            }
        }
        Specimen {
            caption: "block with trailing chevron"
            KitButton {
                objectName: "specimen/button/icon/trailing"
                text: root.label("Scan a cube")
                trailingIcon: "chevron-right"
                block: true
                width: 280
            }
        }
        Specimen {
            caption: "wraps at 160"
            KitButton {
                objectName: "specimen/button/wrap"
                text: root.label("Enter the colours by hand")
                variant: "secondary"
                width: 160
            }
        }
        Specimen {
            caption: "over the camera"
            Backdrop {
                Row {
                    spacing: Theme.space3
                    KitButton { objectName: "specimen/button/overlay/primary"; text: root.label("Scan"); onOverlay: true }
                    KitButton { objectName: "specimen/button/overlay/secondary"; text: root.label("Cancel"); variant: "secondary"; onOverlay: true }
                    KitButton { objectName: "specimen/button/overlay/ghost"; text: root.label("More"); variant: "ghost"; onOverlay: true }
                }
            }
        }
    }

    // ---- Icon button ----
    Section {
        title: "Icon button"

        Repeater {
            model: [
                { key: "rest", preview: "", enabled: true },
                { key: "hover", preview: "hover", enabled: true },
                { key: "pressed", preview: "pressed", enabled: true },
                { key: "focus", preview: "focus", enabled: true },
                { key: "disabled", preview: "", enabled: false }
            ]
            Specimen {
                caption: modelData.key
                KitIconButton {
                    objectName: "specimen/iconbutton/" + modelData.key
                    iconName: "chevron-left"
                    text: "Back"
                    previewState: modelData.preview
                    enabled: modelData.enabled
                }
            }
        }
        Specimen {
            caption: "pause"
            KitIconButton { objectName: "specimen/iconbutton/pause"; iconName: "pause"; text: "Pause" }
        }
        Specimen {
            caption: "close"
            KitIconButton { objectName: "specimen/iconbutton/close"; iconName: "cross"; text: "Close" }
        }
        Specimen {
            caption: "over the camera"
            Backdrop {
                KitIconButton { objectName: "specimen/iconbutton/overlay"; iconName: "pause"; text: "Pause"; onOverlay: true }
            }
        }
    }

    // ---- Switch ----
    Section {
        title: "Switch"

        Repeater {
            model: [
                { key: "off", checked: false, preview: "", enabled: true },
                { key: "on", checked: true, preview: "", enabled: true },
                { key: "hover", checked: false, preview: "hover", enabled: true },
                { key: "pressed", checked: true, preview: "pressed", enabled: true },
                { key: "hover-on", checked: true, preview: "hover", enabled: true },
                { key: "pressed-off", checked: false, preview: "pressed", enabled: true },
                { key: "focus", checked: true, preview: "focus", enabled: true },
                { key: "disabled", checked: false, preview: "", enabled: false },
                { key: "disabled-on", checked: true, preview: "", enabled: false }
            ]
            Specimen {
                caption: modelData.key
                KitSwitch {
                    objectName: "specimen/switch/" + modelData.key
                    width: 320
                    text: root.label("Marks on cube colours")
                    note: modelData.enabled ? root.label("A shape on each colour, so they can be told apart.")
                                            : root.label("Not available on this device.")
                    checked: modelData.checked
                    previewState: modelData.preview
                    enabled: modelData.enabled
                }
            }
        }
        Specimen {
            caption: "over the camera"
            Backdrop {
                KitSwitch {
                    objectName: "specimen/switch/overlay"
                    width: 300
                    text: root.label("Sound")
                    onOverlay: true
                    checked: true
                }
            }
        }
    }

    // ---- Choice ----
    Section {
        title: "Choice"

        Specimen {
            caption: "three options, first chosen"
            KitChoice {
                objectName: "specimen/choice/first"
                width: 360
                label: "Theme"
                options: [root.label("Same as device"), root.label("Light"), root.label("Dark")]
                currentIndex: 0
            }
        }
        Specimen {
            caption: "second chosen"
            KitChoice {
                objectName: "specimen/choice/second"
                width: 360
                label: "Theme"
                options: [root.label("Same as device"), root.label("Light"), root.label("Dark")]
                currentIndex: 1
            }
        }
        Specimen {
            caption: "hover on the third"
            KitChoice {
                objectName: "specimen/choice/hover"
                width: 360
                label: "Theme"
                options: [root.label("Same as device"), root.label("Light"), root.label("Dark")]
                currentIndex: 0
                previewIndex: 2
                previewState: "hover"
            }
        }
        Specimen {
            caption: "focus on the chosen"
            KitChoice {
                objectName: "specimen/choice/focus"
                width: 360
                label: "Theme"
                options: [root.label("Same as device"), root.label("Light"), root.label("Dark")]
                currentIndex: 1
                previewIndex: 1
                previewState: "focus"
            }
        }
        Specimen {
            caption: "too narrow for a row: a column"
            KitChoice {
                objectName: "specimen/choice/stacked"
                width: 200
                label: "Text size"
                options: [root.label("Same as device"), root.label("Large"), root.label("Larger")]
                currentIndex: 2
            }
        }
    }

    // ---- List row ----
    Section {
        title: "List row"

        Repeater {
            model: [
                { key: "rest", preview: "", enabled: true, value: "English", chevron: true, highlighted: false },
                { key: "hover", preview: "hover", enabled: true, value: "English", chevron: true, highlighted: false },
                { key: "pressed", preview: "pressed", enabled: true, value: "English", chevron: true, highlighted: false },
                { key: "focus", preview: "focus", enabled: true, value: "English", chevron: true, highlighted: false },
                { key: "highlighted", preview: "", enabled: true, value: "", chevron: false, highlighted: true },
                { key: "no-value", preview: "", enabled: true, value: "", chevron: true, highlighted: false },
                { key: "disabled", preview: "", enabled: false, value: "English", chevron: true, highlighted: false }
            ]
            Specimen {
                caption: modelData.key
                KitListRow {
                    objectName: "specimen/listrow/" + modelData.key
                    width: 360
                    text: root.label("Language")
                    value: modelData.value
                    chevron: modelData.chevron
                    highlighted: modelData.highlighted
                    previewState: modelData.preview
                    enabled: modelData.enabled
                }
            }
        }
    }

    // ---- Top bar ----
    Section {
        title: "Top bar"

        Specimen {
            caption: "back, title, action"
            KitTopBar {
                objectName: "specimen/topbar/full"
                width: 420
                title: root.label("Settings")
                backText: "Back"
                KitIconButton { iconName: "help"; text: "Help" }
            }
        }
        Specimen {
            caption: "no back"
            KitTopBar {
                objectName: "specimen/topbar/root"
                width: 420
                title: root.label("Cubeulator")
                showBack: false
            }
        }
        Specimen {
            caption: "over the camera"
            Backdrop {
                KitTopBar {
                    objectName: "specimen/topbar/overlay"
                    width: 380
                    title: root.label("Scan")
                    backText: "Back"
                    onOverlay: true
                }
            }
        }
    }

    // ---- Dialog and sheet ----
    Section {
        title: "Dialog and sheet"

        Specimen {
            caption: "dialog"
            KitButton {
                objectName: "specimen/dialog/open"
                text: "Open the dialog"
                variant: "secondary"
                onClicked: dialog.open()
            }
        }
        Specimen {
            caption: "sheet"
            KitButton {
                objectName: "specimen/sheet/open"
                text: "Open the sheet"
                variant: "secondary"
                onClicked: sheet.open()
            }
        }

        KitDialog {
            id: dialog
            objectName: "specimen/dialog"
            title: root.label("Leave the scan?")
            body: root.label("The sides you have shown will be forgotten.")
            safeText: root.label("Keep scanning")
            otherText: root.label("Leave")
        }
        KitSheet {
            id: sheet
            objectName: "specimen/sheet"
            title: "More"
            closeText: root.label("Close")
            KitButton { text: root.label("How to hold the cube"); block: true; size: "thumb"; variant: "ghost"; leadingIcon: "help" }
            KitButton { text: root.label("Settings"); block: true; size: "thumb"; variant: "ghost"; leadingIcon: "sliders" }
        }
    }

    // ---- Notice ----
    Section {
        title: "Notice"

        Repeater {
            model: [
                { key: "information", heading: "Nothing is saved", text: "No picture of the cube is kept." },
                { key: "caution", heading: "No camera found", text: "Plug one in, then look again." },
                { key: "positive", heading: "Cube read", text: "All six sides are in." }
            ]
            Specimen {
                caption: modelData.key
                KitNotice {
                    objectName: "specimen/notice/" + modelData.key
                    width: 360
                    kind: modelData.key
                    heading: root.label(modelData.heading)
                    text: root.label(modelData.text)
                    announce: false
                }
            }
        }
        Specimen {
            caption: "with a button"
            KitNotice {
                objectName: "specimen/notice/action"
                width: 360
                kind: "caution"
                heading: root.label("No camera found")
                text: root.label("Plug one in, then look again.")
                announce: false
                KitButton { text: root.label("Look again"); variant: "secondary"; block: true }
            }
        }
    }

    // ---- Card ----
    Section {
        title: "Card"

        Specimen {
            caption: "unfinished solve"
            KitCard {
                objectName: "specimen/card/default"
                width: 360
                heading: root.label("Carry on solving")
                KitText { text: root.label("Turn 12 of 62, white corners."); role: "body"; width: parent.width }
                actions: [
                    KitButton { text: root.label("Carry on"); block: true; size: "thumb" },
                    KitButton { text: root.label("Discard"); block: true; size: "thumb"; variant: "ghost" }
                ]
            }
        }
    }

    // ---- Progress ----
    Section {
        title: "Progress"

        Specimen {
            caption: "solve progress, 40%, five stages"
            KitSolveProgress {
                objectName: "specimen/progress/solve"
                width: 360
                value: 0.4
                boundaries: [0.15, 0.4, 0.6, 0.8]
                text: "Turn 25 of 62"
            }
        }
        Specimen {
            caption: "working"
            KitWorking {
                objectName: "specimen/progress/working"
                width: 280
                text: root.label("Working out the solution")
                announce: false
            }
        }
        Specimen {
            caption: "countdown, paused (full)"
            KitCountdown {
                objectName: "specimen/progress/countdown"
                width: 280
                running: true
                paused: true
                text: "Next turn soon"
            }
        }
        Specimen {
            caption: "capture, over the camera"
            Backdrop {
                KitCaptureBar {
                    objectName: "specimen/progress/capture"
                    width: 140
                    value: 0.6
                    text: "Holding side"
                }
            }
        }
    }

    // ---- Sticker, swatch ----
    Section {
        title: "Sticker and swatch"

        Repeater {
            model: ["white", "yellow", "orange", "red", "green", "blue"]
            Specimen {
                caption: modelData
                KitSticker {
                    objectName: "specimen/sticker/" + modelData
                    colourName: modelData
                    size: 56
                }
            }
        }
        Specimen {
            caption: "12 px, the smallest with a mark"
            Row {
                spacing: Theme.space2
                Repeater {
                    model: ["yellow", "orange", "red", "green", "blue"]
                    KitSticker {
                        objectName: "specimen/sticker/small/" + modelData
                        colourName: modelData
                        size: 12
                    }
                }
            }
        }
        Specimen {
            caption: "11 px: too small for a mark"
            KitSticker { objectName: "specimen/sticker/tiny"; colourName: "red"; size: 11 }
        }
        Specimen {
            caption: "swatch beside its word"
            Row {
                spacing: Theme.space2
                KitSwatch {
                    objectName: "specimen/swatch"
                    colourName: "yellow"
                    anchors.verticalCenter: parent.verticalCenter
                }
                KitText { text: root.label("Yellow on top"); role: "body"; wrapMode: Text.NoWrap; anchors.verticalCenter: parent.verticalCenter }
            }
        }
    }

    // ---- Side chip ----
    Section {
        title: "Side chip, over the camera"

        Repeater {
            model: [
                { key: "empty", progress: 0, focus: "" },
                { key: "filling", progress: 0.55, focus: "" },
                { key: "read", progress: 0, focus: "" },
                { key: "inView", progress: 0, focus: "" },
                { key: "confirmed", progress: 0, focus: "" },
                { key: "focus", progress: 0, focus: "focus" }
            ]
            Specimen {
                caption: modelData.key
                Backdrop {
                    Item {
                        width: 72
                        height: 72 + 12
                        KitSideChip {
                            objectName: "specimen/sidechip/" + modelData.key
                            y: 12
                            size: 72
                            chipState: modelData.key === "focus" ? "read" : modelData.key
                            progress: modelData.progress
                            previewState: modelData.focus
                            colours: ["red", "white", "blue", "green", "orange", "yellow", "red", "green", "blue"]
                            accessibleName: "Side 3: " + modelData.key
                            description: "Red side. Top row: red, white, blue."
                        }
                    }
                }
            }
        }
    }

    // ---- Colour picker ----
    Section {
        title: "Colour picker"

        Specimen {
            caption: "red chosen"
            KitColourPicker {
                objectName: "specimen/colourpicker/red"
                width: 460
                title: "Paint with"
                labels: ["White", "Yellow", "Orange", "Red", "Green", "Blue"]
                current: "red"
            }
        }
        Specimen {
            caption: "none chosen"
            KitColourPicker {
                objectName: "specimen/colourpicker/none"
                width: 460
                title: "Paint with"
                labels: ["White", "Yellow", "Orange", "Red", "Green", "Blue"]
                current: ""
            }
        }
    }

    // ---- Status panel ----
    Section {
        title: "Status panel, over the camera"

        Repeater {
            model: [
                { key: "normal", headline: "Show the next side", body: "Hold it still in the box." },
                { key: "caution", headline: "Camera not found", body: "Plug one in, then look again." },
                { key: "complete", headline: "Cube read", body: "All six sides are in." }
            ]
            Specimen {
                caption: modelData.key
                Backdrop {
                    KitStatusPanel {
                        objectName: "specimen/statuspanel/" + modelData.key
                        width: 360
                        kind: modelData.key
                        headline: root.label(modelData.headline)
                        body: root.label(modelData.body)
                        live: false
                    }
                }
            }
        }
    }

    // ---- Instruction block ----
    Section {
        title: "Instruction block"

        Specimen {
            caption: "at the leading edge"
            KitInstruction {
                objectName: "specimen/instruction/leading"
                width: 300
                side: root.label("Right")
                sideColour: "orange"
                direction: root.label("downwards")
                directionIcon: "turn-down"
                amount: root.label("Quarter turn")
                notation: "R"
            }
        }
        Specimen {
            caption: "centred, anticlockwise"
            KitInstruction {
                objectName: "specimen/instruction/centred"
                width: 300
                centred: true
                side: root.label("Front")
                sideColour: "green"
                direction: root.label("anticlockwise")
                directionIcon: "turn-anticlockwise"
                amount: root.label("Half turn")
            }
        }
    }

    // ---- Stage row ----
    Section {
        title: "Stage row"

        Repeater {
            model: [
                { key: "done", number: 1, name: "White cross", status: "done", explanation: "" },
                { key: "current", number: 2, name: "White corners", status: "current",
                  explanation: "Put the four white corners in place to finish the bottom layer." },
                { key: "todo", number: 3, name: "Middle layer", status: "todo", explanation: "" }
            ]
            Specimen {
                caption: modelData.key
                KitStageRow {
                    objectName: "specimen/stagerow/" + modelData.key
                    width: 360
                    number: modelData.number
                    name: root.label(modelData.name)
                    explanation: root.label(modelData.explanation)
                    status: modelData.status
                    stateText: modelData.status
                }
            }
        }
    }

    // ---- Key name, empty state ----
    Section {
        title: "Key name and empty state"

        Specimen {
            caption: "key name"
            KitKeyName { objectName: "specimen/keyname"; text: "Space" }
        }
        Specimen {
            caption: "empty state"
            KitEmptyState {
                objectName: "specimen/emptystate"
                width: 360
                title: root.label("No solves yet")
                text: root.label("Scan a cube and the solves you finish will be listed here.")
                actionText: root.label("Scan a cube")
            }
        }
    }

    // ---- Icons ----
    Section {
        title: "Icons"

        Repeater {
            model: KitIcons.names
            Specimen {
                caption: modelData
                KitIcon {
                    objectName: "specimen/icon/" + modelData
                    name: modelData
                    size: 32
                }
            }
        }
    }
}
