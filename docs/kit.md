# The component kit

Workstream E2a of the [app experience plan](app-experience-plan.md). The
design is in [`design/system.md`](design/system.md) and
[`design/tokens.json`](design/tokens.json); this page is about the code that
builds it: where it is, how to use it, what holds it to the design, what was
found that the design did not know, and the one change that needs agreeing
before it is made (the split of `AppState.qml`, at the foot).

Everything here is in `src/app/qt/kit/` and the `Cubeulator` QML module, plus
`Theme.qml` and `SettingsState.qml`. No screen was rebuilt: that is E3.

## Using it

```qml
import Cubeulator

KitPage {                       // page fill, margins, safe area, scrolls
    KitTopBar { title: qsTr("Settings"); backText: qsTr("Back"); onBackClicked: Navigator.back() }
    KitSwitch { text: qsTr("Sound"); checked: SettingsState.sound
                onToggled: SettingsState.sound = checked }
    KitButton { text: qsTr("Scan a cube"); block: true; size: "thumb" }
}
```

- **Tokens.** `Theme.paper`, `Theme.ink`, `Theme.accentText`, ... exactly the
  names in `tokens.json`, in the current theme. Sizes in `Theme.space4`,
  `Theme.targetThumb`, type roles through `KitText { role: "body" }`. A colour or
  a pixel size written into a screen is a defect.
- **No words in the kit.** Every component takes its text as a property
  (`text`, `title`, `backText`, `closeText`...). Nothing user-visible is
  defined inside it; translation (E2b) happens at the screen.
- **State is never colour alone**, by construction: a chosen option inverts and
  carries a check, an on switch has its thumb at the other end and a check, a
  stage has a number, a fill or a check.
- **Every interactive component** has an `objectName`, an accessible role and
  name, takes keyboard focus, shows the focus ring for keyboard focus only,
  and has a `previewState` ("", "hover", "pressed", "focus") so the gallery and
  tests can show a state without a pointer.

### Components

| type | what | notes |
|---|---|---|
| `KitText` | text in a type role | wraps, never elides; eyebrow capitalisation is a font property |
| `KitIcon` | one of `KitIcons.names`, drawn as a `Shape` | `turn-*` icons never mirror; chevrons do |
| `KitFocusRing`, `KitKeyName` | parts | |
| `KitButton` | primary, secondary, ghost; regular, thumb, giant | `block` fills the width; `onOverlay` for the camera |
| `KitIconButton` | 26 px icon in a 56 target | `text` is the name and the tooltip; not `icon` (Qt's) but `iconName` |
| `KitSwitch` | row with label, note, switch | whole row is the target |
| `KitChoice` | 2 to 4 options, one chosen | one Tab stop; arrows choose; wraps to a column |
| `KitListRow` | label, value, chevron | |
| `KitTopBar` | back, title (a heading), trailing slot | |
| `KitDialog`, `KitSheet` | `Popup`s | register with `Navigator`; safe choice first and focused |
| `KitNotice`, `KitCard` | in-place notice; the one boxed object | notice announces itself |
| `KitSolveProgress`, `KitWorking`, `KitCountdown`, `KitCaptureBar` | the four progress parts | |
| `KitSticker`, `KitSwatch`, `KitSideChip`, `KitColourPicker` | the cube's parts | never mirror; marks on by default |
| `KitStatusPanel`, `KitInstruction`, `KitStageRow`, `KitEmptyState` | scan and solve parts | status panel is a live region |
| `KitPage` | layout primitive | size-class margins + insets; scrolls the whole screen |
| `KitWindowBindings` | window plumbing | publishes size, watches insets, mirrors, binds back |
| `KitGallery` | developer gallery | below |

### Settings and system helpers

| property | source | live | override |
|---|---|---|---|
| `Theme.effectiveDark` | `SystemTheme` (existing) and `SettingsState.themeMode` | yes | the setting |
| `Theme.textScale` | `SystemTextScale` x Settings > Text size, clamped 0.85 - 2.0 | yes | the setting; `SystemTextScale.setOverride` |
| `Theme.reducedMotion` | `SystemMotion`, or Settings > Motion (system, reduced, full) | yes | the setting; `SystemMotion.setOverride` |
| `Theme.sizeClass`, `heightClass` | the window, via `KitWindowBindings` | yes | set `Theme.windowWidth` |
| `Theme.insets` (top, bottom, leading, trailing) | `SystemInsets` | on rotate / screen change | `SystemInsets.setOverride` |
| `Theme.rtl`, `Theme.script`, `Theme.headingFamily` | `SettingsState.language` | yes | the setting |

Persisted in `SettingsState` (`QSettings`, applied the moment they change):
`themeMode` (existing), `language`, `marks`, `textSize`, `motion`, `advance`,
`pace`, `explainStages`, `notation`, `sound`, `haptics`, `keepHistory`.
Values are the ones in `strings.json` (`settings.*`): `system | large |
larger`, `system | reduced | full`, `tap | timer`, `slow | steady | quick`. A
stored value that is not one of these reads as the default.

Where each platform's setting is read (`platforms.md`, "Text size, motion,
theme, language"):

| | text size | reduced motion | status here |
|---|---|---|---|
| Windows | `HKCU\Software\Microsoft\Accessibility\TextScaleFactor` | `SPI_GETCLIENTAREAANIMATION` | **built and read on this machine**; refreshed on `WM_SETTINGCHANGE` and on activation |
| macOS | none system-wide | `NSWorkspace.accessibilityDisplayShouldReduceMotion` | written, **not compiled** (no Mac here) |
| iOS | Dynamic Type, `UIFontMetrics` | `UIAccessibilityIsReduceMotionEnabled` | written, **not compiled** |
| Android | `Configuration.fontScale` (JNI) | `animator_duration_scale == 0` (JNI) | written; **syntax-checked** against the NDK and Qt's Android headers, never run |
| Linux | GSettings `text-scaling-factor` | GSettings `enable-animations` | written; **not run** (no desktop session here) |

## Navigation and back

`Navigator` (singleton) holds the stack and the one `back()` every platform's
binding calls. The rules are `journey.md`'s: back goes where the person came
from; from `complete` and `solve` it goes `home`; from `scan` after a camera
failure it goes `home` (call `Navigator.setBackTarget("home")`); an open dialog
or sheet takes back first and chooses its safe option; then a screen's guard
(`Navigator.addGuard(fn)`, for "leave the scan?"); at the root it reports
`backAtRoot()` and does nothing.

`KitWindowBindings { window: root }` (first child of the window) binds it:

| | back |
|---|---|
| desktop | Esc; Alt+Left (Alt+Right in right-to-left); Cmd+[ on macOS; the mouse's back button |
| Android | the `Back` key and gesture (Shortcut on `Qt::Key_Back`) and, failing that, the window's close request; the app closes from `home` |
| iOS | a drag from the leading edge, and the on-screen control |

Also: it publishes the window's size to `Theme`, tells `SystemInsets` which
screen to read, and mirrors the content by the language.

## The gallery

A developer build (`CUBEULATOR_DEVELOPER_BUILD`, on unless the build type is
Release or MinSizeRel) opens it with `cube_app --gallery` or
`CUBEULATOR_GALLERY=1`. A release build ignores both (`BuildInfo.galleryRequested`
is false). It shows every component in every state, with controls for theme,
text size, direction, labels 40% longer, colour marks, motion and a simulated
notch. Its words are English and not translated.

## What holds it to the design

`cube_kit_tests` (Catch2, a `QGuiApplication` on the offscreen platform, one
QML engine). Run `ctest -L kit` or the binary directly.

| test file | holds |
|---|---|
| `theme_tokens_test.cpp` | every colour (both themes), overlay, cube colour and mark, space, target, border, type role, motion token and size-class edge equals `tokens.json`; every pair in `tokens.json` holds its ratio, computed from the running `Theme`; the old names still answer |
| `gallery_test.cpp` | over the whole gallery: every component's declared colour pairs hold their ratio in both themes and in right-to-left; every interactive item has a name, a role and a 48 px target; Tab reaches every enabled control; at text scale 2.0 and labels 40% longer no text is cut off or overflows; the cube is never mirrored |
| `components_test.cpp`, `button_test.cpp`, `text_test.cpp` | each component's own rules: keys, focus, what a screen reader is told, what mirrors |
| `navigator_test.cpp` | the back rules and every platform's binding |
| `settings_test.cpp` | defaults; every setting survives a second engine; applies live |
| `system_helpers_test.cpp` | helper overrides; an announcement reaches the accessibility layer |

Every component declares the pairings it draws as a `contrastPairs` array
(foreground, background, target). That is how the gallery test checks all of
them without knowing what any of them is: a new component only has to declare
its own.

## Found, and what was done

These are things the design could not be built as written, or that the
Qt version did, on 6.8.3. Each is in the code where it bites and has a test.

1. **`onAccent` cannot be a QML property.** A property named `on` and a
   capital reads as a signal handler and comes back undefined. `Theme.accentOn`
   is the colour; the token table keeps `onAccent`. If a later Qt allows it,
   `theme_tokens_test.cpp` says so.
2. **`outline` is under 3:1 on the hover and press washes** (2.96 and 2.74 in
   the light theme, 3.13 and 2.87 in the dark). The switch's track edge sits
   inside a washed row, so it is `ink` while hovered or pressed; nothing else
   in the kit puts `outline` on a wash. `tokens.json`'s pair table has no such
   pairing, so `check_tokens.py` does not see it. **For E1:** either add the
   pairs and darken `outline` (about `#7E7B7A` in light), or accept the kit's
   workaround.
3. **`font.families` does not exist** in 6.8's QML `font`, so there is no
   fallback list. One family per language: Archivo; IBM Plex Sans Hebrew;
   IBM Plex Sans Arabic; the system's CJK font, picked from
   `Qt.fontFamilies()`. Every one of them draws Latin, so numbers and the app's
   name still show. (`platforms.md` assumed per-glyph fallback.)
4. **Safe areas.** Qt 6.8.3 has no public safe-area API
   (`QWindow::safeAreaMargins` is 6.9; the 6.8 accessor is QPA-private). On
   Android and iOS the insets are the difference between the screen's geometry
   and available geometry, which gets system bars and not necessarily a
   cut-out. Desktop is zero. Not tried on a device.
5. **`Accessible.announce` exists in 6.8** (`QAccessibleAnnouncementEvent`).
   Qt's Windows plugin imports `UiaRaiseNotificationEvent`, which is what
   Narrator speaks. The kit routes every announcement through `Announcer`.
   Whether each reader actually speaks it is E6.
6. **Names the design uses that Qt already owns**: `icon` and `state` are an
   `AbstractButton`'s and an `Item`'s, so the icon button's is `iconName` and a
   stage row's is `status`.
7. **Controls take back `activeFocusOnTab = false` on the item that has focus**,
   so a roving tab stop (choice, colour picker) is re-synced when an option
   loses focus, not bound.
8. **A `Popup` is not an item**: `Keys` and the accessible role go on its
   content item; its handlers must not use `parent` (that is the overlay).
9. **`Qt.labs.settings` is deprecated** in 6.8 and logs at start-up. It was
   already used; E2b will meet it.
10. **`StaticText` is reported focusable to a reader** by Qt Quick, so text can
    be reached; the keyboard's Tab never lands on it (tested).

## Not verified

- **A screen reader's speech.** Checked: Windows UI Automation shows each
  control as the right type, with its name, `Invoke` or `Toggle`, keyboard
  focusable (or not when disabled), and real OS Tab moves focus between them
  in reading order. Narrator's *speech* was not listened to. Android
  (TalkBack), iOS (VoiceOver), macOS (VoiceOver) and Linux (Orca) were not run.
- **Back on Android and iOS**: the `Back` key as a shortcut, the close-request
  fallback, predictive back, the iOS edge drag. Written from the Qt docs.
- **macOS and iOS code** (`system_query_apple.mm`) is not compiled.
- **Linux, macOS, Android builds** of the kit. Windows built and tested.
- **Archivo in the app** until the font files are added (below).

## Fonts

`src/app/qt/fonts/*.ttf` are compiled in under `:/fonts` and registered by
`kit::installBundledFonts()` before any QML loads. Wanted: Archivo 400, 700,
800; IBM Plex Mono 500; IBM Plex Sans Hebrew and Arabic 400, 700; each with its
OFL text beside it. The generated notices (E0b, `feature/ax-licences`) must
list them, so that session needs to know the list above.

## Proposed split of `AppState.qml`

**Not done.** This edit is to a file the scan lane owns, so it waits for
agreement. Nothing above needs it: `Navigator` works and is tested beside
`AppState`, and the screens still route by `AppState.screen`.

Today `AppState` is routing *and* the scan flow. Proposed:

| today in `AppState` | goes to | how |
|---|---|---|
| `screen`, `previousScreen`, `openSettings()`, `closeSettings()`, `goLaunch()`, `restart()` | **Navigator** (this lane) | `Navigator.current`; `push("settings")` / `back()`; `reset("home")`. `AppState.screen` stays as a read-only alias (`launch` for `home`) so nothing that reads it breaks |
| `requestPermission()`, the route part of `beginScan()`, `proceedToScan()`, `scanAnother()` | **Navigator** calls, from the screens | `Navigator.push("permission" or "scan")` |
| `isMobile`, `cameraGranted` | stay (scan lane) | policy the route decision reads |
| `onScreenChanged` (start and stop `DirectScan`, clear `scanComplete`) | stays (scan lane) | triggered from `Connections { target: Navigator; function onNavigated(id, params, kind) }` |
| `scanComplete`, `completeBeat`, `showSolutionWhenReady()`, `solveControllerConnections`, `directScanConnections`, `scanError`, `scanRetryMessage`, `scanRetryTimer`, `scanSessionConnections`, `scanned` | stay (scan lane) | where they now assign `screen = "solve"` they call `Navigator.replace("solve")` (three places) |
| `moveCount`, `elapsedSeconds`, `clockTimer`, `elapsedLabel()` | stay for now | shared by Solve and Complete; E3d decides |
| `Component.onCompleted: CameraController.refreshDevices()` | stays | |

And in `Main.qml`: the `Loader` chooses by `Navigator.current` through an id to
component table instead of the `switch` on `AppState.screen`; new screens
(history, review, solution, help...) are rows in that table.

What the scan lane gets from it: a scan screen that registers
`Navigator.addGuard(...)` to ask "leave the scan?" before back leaves, and
back from a camera failure going home. What it gives up: nothing that is
reading cube data; only the six lines that name a screen.

The edit would be one commit, six places in `AppState.qml` and the `Loader` in
`Main.qml`, with the existing tests unchanged. Needed first: Iain's agreement
and the scan lane's.
