# Settings

Lane: experience. Replaces `SettingsScreen.qml`.

Every setting changes something a person solving a cube can see, applies
at once, and is still set after a restart. Today only two of them
persist, and two do nothing.

Entered from: Home; the scan screen's More sheet; the solve screen's
menu; **Open Settings** on History; the macOS application menu. Back
returns to wherever it was opened from, with a scan or solve as it was.

## Layout

**Compact and short.** A top bar: back and the title. One scrolling
column, 24 margin. Sections in the order below, each under an eyebrow.

**Expanded.** Two panes. Leading: the column, at most 640 wide. Trailing:
`surface`, with the decorative cube, so that theme and colour marks can be
seen changing.

## The settings

| section | setting | control | values (default first) | strings | objectName |
|---|---|---|---|---|---|
| Language | Language | list row showing the current choice; opens the list | Same as this device; then the nine | `settings.section.language`, `settings.language.system` | `settings.language` |
| Appearance | Theme | choice | Same as this device, Light, Dark | `settings.section.appearance`, `settings.theme`, `settings.theme.system`, `settings.theme.light`, `settings.theme.dark` | `settings.theme` |
| | Marks on cube colours | switch, with a note | On | `settings.marks`, `settings.marks.note` | `settings.marks` |
| | Text size | choice | Same as this device, Large (1.25), Larger (1.5). Multiplies the system's own setting | `settings.textSize`, `settings.textSize.system`, `settings.textSize.large`, `settings.textSize.larger` | `settings.textSize` |
| | Motion | choice | Same as this device, Reduced, Full | `settings.motion`, `settings.motion.system`, `settings.motion.reduced`, `settings.motion.full` | `settings.motion` |
| Solving | Move to the next turn | choice | When I tap, On a timer | `settings.section.solving`, `settings.advance`, `settings.advance.tap`, `settings.advance.timer` | `settings.advance` |
| | Pace (only when on a timer) | choice | Steady (4 s); Slow (6 s), Quick (2.5 s) | `settings.pace`, `settings.pace.slow`, `settings.pace.steady`, `settings.pace.quick` | `settings.pace` |
| | Explain each stage | switch, with a note | On | `settings.explain`, `settings.explain.note` | `settings.explain` |
| | Show cube notation | switch, with a note | Off | `settings.notation`, `settings.notation.note` | `settings.notation` |
| | Sound | switch | On. Not shown until the sounds exist (`../platforms.md`) | `settings.sound` | `settings.sound` |
| | Vibration | switch | On. Phones only | `settings.haptics` | `settings.haptics` |
| Camera | see states | | | `settings.section.camera` | `settings.camera` |
| History | Keep a history of solves | switch, with a note | On | `settings.section.history`, `settings.history.keep`, `settings.history.note` | `settings.keepHistory` |
| | Clear history | button, secondary. Disabled when there is none | - | `history.clear` | `settings.clearHistory` |
| About | About Cubeulator | list row | - | `settings.section.about`, `settings.about` | `settings.about` |
| | Help | list row | - | `common.help` | `settings.help` |

Other parts: Back (`common.back`, `settings.back`); the title
(`common.settings`, `settings.title`).

**The mark legend.** Directly under **Marks on cube colours**, a row of
the six swatches, each with its colour's name beneath (`colour.*`). It
shows the marks when the switch is on and plain swatches when it is off,
so the switch's effect is seen where it is flipped. A screen reader reads
each as the colour and its mark: "Yellow, ring" (`mark.*`). It is not
interactive. `objectName`: `settings.markLegend`.

**Gone** from the current screen, with reasons:

- *Detect hands with AI* and *Auto-advance between faces* drove
  `ScanSession`, which no screen starts.
- *Cube style*, *Auto-rotate when idle* and its speed, *Graphics backend*
  and *Stats for nerds* change nothing a person solving a cube needs. The
  home cube's idle turn now follows the Motion setting. The rest are in
  the developer section, in developer builds only.

**Language names** are each written in their own language and are never
translated: English, Deutsch, Français, Español, 日本語, 简体中文, עברית,
العربية, יידיש. They are data, not strings. "Same as this device" follows the
system's choice for this app, where the system has one.

**Every change applies at once**, on every screen, without a restart and
without losing a scan or a solve in progress, including language and
direction. A change made by keyboard is announced with its new value.

**Focus** returns to the row that opened a list or a dialog.

## States

### `settings.main`

The column as above. The Camera section shows whichever of the camera
states below applies; they are named separately so that each is tested.

### `settings.language`

The list of languages: a sheet on compact, a centred panel from medium
up. A radio list; the current one has a check. Hebrew, Arabic and Yiddish
names are set right to left within their rows. Choosing one applies it and closes
the list. **Close**, Esc and back close it without changing anything.

Opened from the first-run screen, it returns there.

### `settings.camera-finding`

Desktop: cameras are being listed. The working indicator and
`settings.camera.finding`. Becomes one of the next two by itself.

### `settings.camera-list`

Desktop: one or more cameras. `settings.camera.choose`, then a choice of
the cameras by the names the system gives them, then **Look again**
(`common.lookAgain`, ghost, refresh icon). The choice is remembered by
device id and applies to the next scan; if that camera is absent at the
next start, the first one found is used and nothing is said.

### `settings.camera-none`

Desktop: none found. A caution notice, `settings.camera.none`, with
**Look again**.

### `settings.camera-allowed`

Android, iOS, macOS: access is allowed. A positive notice,
`settings.camera.allowed`, with **Open device settings**
(`common.openDeviceSettings`), which is where access is taken away.

### `settings.camera-refused`

Android, iOS, macOS: access is refused or has not been asked for. A
caution notice, `settings.camera.refused`, with **Allow camera**
(`permission.allow`), which goes to `permission.ask`.

### `settings.camera-unsupported`

This build cannot use this device's camera. A caution notice,
`settings.camera.unsupported`. No button: there is nothing to do.

### `settings.confirm-clear`

The same dialog as `history.confirm-clear`: `history.clear.title`,
`history.clear.body`, **Keep them** (`history.clear.keep`), **Clear**
(`history.clear.confirm`).

### `settings.developer`

Developer builds only; absent from release builds, not hidden. A last
section under `settings.section.developer`:

- a caution notice, `settings.dev.recording`, when the build records
  failed scans;
- the graphics backend as a choice, headed `settings.dev.backend`;
- the cube style (`settings.dev.cubeStyle`);
- a switch, `settings.dev.stats`.

These strings may stay in English.

### `settings.restart-needed`

Developer builds: a backend other than the running one is chosen. A
caution notice under the choice: `settings.dev.restart`.

## Persistence

Each setting is stored under a stable key and read at start-up before
the first screen is shown, so the app never flashes the wrong theme or
language. The system test "every setting changed and still set after a
restart" walks the table above.

The keys are written in the style ICL Forge and Countdown Solver use,
`group/name` in one `QSettings` store (organisation and application both
`Cubeulator`; `family.md` F2). They are named now because a key renamed
after the first release needs a migration.

| key | setting |
|---|---|
| `language/code` | Language. Absent means "same as this device" |
| `appearance/theme` | Theme |
| `appearance/textSize` | Text size |
| `appearance/motion` | Motion |
| `appearance/marks` | Marks on cube colours |
| `solving/advance`, `solving/pace`, `solving/explain`, `solving/notation`, `solving/sound`, `solving/haptics` | The Solving section |
| `camera/deviceId` | The remembered camera, by the system's device id |
| `history/keep` | Keep a history of solves |
| `firstRun/acknowledgedVersion` | The first-run screen's last version shown; a later version can show it again |
