import QtQuick
import QtQuick.Controls.Basic
import Cubeulator

// Every kit component in every state, on one scrolling page: the thing the
// tests walk and the thing to look at. Reachable in developer builds only
// (BuildInfo.developerBuild), by `cube_app --gallery` or CUBEULATOR_GALLERY=1.
//
// The controls along the top change what the specimens below are shown in,
// by the same settings the app's own Settings screen will: theme, text size,
// colour marks, motion, direction, and labels 40% longer (a stand-in for a
// translation until E2b's pseudo-locale exists).
//
// Every specimen has an objectName beginning "specimen/" followed by the
// component and the state, so a test can find them all without this file
// listing them.
Rectangle {
    id: root

    color: Theme.paper

    // Labels as a translation might be: 40% longer.
    property bool longLabels: false
    // Everything below is laid out in the direction the language reads.
    LayoutMirroring.enabled: Theme.rtl
    LayoutMirroring.childrenInherit: true

    function label(text) {
        if (!longLabels || text.length === 0)
            return text;
        return text + " " + text.substring(0, Math.max(2, Math.ceil(text.length * 0.4)));
    }

    // ---- Parts the page is built from ------------------------------------------------

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
            Accessible.role: Accessible.Heading
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
            spacing: Theme.space4
        }
    }

    // One specimen: the component, with a caption saying what state it is in.
    component Specimen: Column {
        id: specimen
        property string caption: ""
        property alias content: holder.data
        spacing: Theme.space2
        Item {
            id: holder
            implicitWidth: childrenRect.width
            implicitHeight: childrenRect.height
            // The ring sits outside the control; leave room for it.
            width: implicitWidth + 2 * Theme.space2
            height: implicitHeight + 2 * Theme.space2
        }
        KitText {
            text: specimen.caption
            role: "small"
            color: Theme.inkSecondary
            width: Math.max(holder.width, 120)
        }
    }

    // ---- The page -------------------------------------------------------------------------
    Flickable {
        id: flick
        anchors.fill: parent
        contentWidth: width
        contentHeight: page.implicitHeight + 2 * Theme.pageMargin
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar {}

        Column {
            id: page
            x: Theme.pageMargin
            y: Theme.pageMargin
            width: flick.width - 2 * Theme.pageMargin
            spacing: Theme.space8

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
                    content: Row {
                        spacing: Theme.space2
                        Repeater {
                            model: ["system", "light", "dark"]
                            KitButton {
                                objectName: "control/theme/" + modelData
                                text: modelData
                                size: "regular"
                                variant: SettingsState.themeMode === modelData ? "primary" : "secondary"
                                onClicked: SettingsState.themeMode = modelData
                            }
                        }
                    }
                }
                Specimen {
                    caption: "Text size"
                    content: Row {
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
                    content: Row {
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
                    content: KitButton {
                        objectName: "control/longlabels"
                        text: root.longLabels ? "40% longer" : "As written"
                        variant: root.longLabels ? "primary" : "secondary"
                        onClicked: root.longLabels = !root.longLabels
                    }
                }
                Specimen {
                    caption: "Colour marks"
                    content: KitButton {
                        objectName: "control/marks"
                        text: SettingsState.marks ? "On" : "Off"
                        variant: SettingsState.marks ? "primary" : "secondary"
                        onClicked: SettingsState.marks = !SettingsState.marks
                    }
                }
                Specimen {
                    caption: "Motion"
                    content: KitButton {
                        objectName: "control/motion"
                        text: SettingsState.motion === "reduced" ? "Reduced" : "Full"
                        variant: SettingsState.motion === "reduced" ? "primary" : "secondary"
                        onClicked: SettingsState.motion = SettingsState.motion === "reduced" ? "full" : "reduced"
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
                        content: KitButton {
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
                    content: KitButton { objectName: "specimen/button/size/regular"; text: root.label("Regular"); size: "regular" }
                }
                Specimen {
                    caption: "thumb 56"
                    content: KitButton { objectName: "specimen/button/size/thumb"; text: root.label("Thumb"); size: "thumb" }
                }
                Specimen {
                    caption: "giant 96"
                    content: KitButton {
                        objectName: "specimen/button/size/giant"
                        text: root.label("Next turn")
                        size: "giant"
                        keyName: "Space"
                    }
                }
                Specimen {
                    caption: "leading icon"
                    content: KitButton {
                        objectName: "specimen/button/icon/leading"
                        text: root.label("Help")
                        variant: "ghost"
                        leadingIcon: "help"
                    }
                }
                Specimen {
                    caption: "trailing chevron"
                    content: KitButton {
                        objectName: "specimen/button/icon/trailing"
                        text: root.label("Scan a cube")
                        trailingIcon: "chevron-right"
                        block: true
                        width: 280
                    }
                }
                Specimen {
                    caption: "wraps at 160"
                    content: KitButton {
                        objectName: "specimen/button/wrap"
                        text: root.label("Enter the colours by hand")
                        variant: "secondary"
                        width: 160
                    }
                }
            }
        }
    }
}
