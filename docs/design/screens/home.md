# Home

> **Status in 0.1.0-beta.1:** Partly built, still as the legacy
> `LaunchScreen.qml`: the eyebrow, the 3D cube, the headline and lede (wide
> layout only), **Scan a cube**, **History** (the list of solves) and a
> language button and **Settings** that both open Settings. Not built: Enter
> colours by hand, Help, the unfinished-solve card, the camera notices, the
> short layout and the size classes; its `objectName`s are `launch.*`, not the
> `home.*` below, and unlike the spec it has a language button.

Lane: experience. Replaces `LaunchScreen.qml`.

Where every journey starts and ends. It says what the app does in one
line and offers the two ways in: scan, or enter by hand.

Entered from: app start (after the first), back from anywhere, **Done** on
Complete, **Not now** on Permission, leaving a solve.

## Layout

**Compact.** One column, 24 margin, three rows:

1. The eyebrow.
2. The cube, in whatever height is left (at least 110).
3. The copy block, at the bottom: headline, lede, the card if there is one,
   the notice if there is one, the two actions full width, then a 2 px
   rule and the three small actions in a row, spread across the width.

The primary action is the lowest full-width control, in the thumb zone.

**Short.** Two columns under the eyebrow: the cube leading, the copy
trailing. The lede is dropped and the headline is 1.5 rem.

**Expanded.** Two equal panes. Leading: the eyebrow at the top (40
margin); the copy block centred vertically, at most 560 wide, with the
two actions side by side and the three small actions in a row beneath a
rule. Trailing: `surface`, a 2 px rule on its leading edge, the cube
centred.

When the content is taller than the window (large text), the whole screen
scrolls; the cube shrinks to its minimum first.

## Parts

| part | component | string | objectName |
|---|---|---|---|
| Eyebrow | eyebrow | `app.name` | `home.eyebrow` |
| Cube | The solve lane's cube view: solved, white on top, not interactive. Decorative; ignored by screen readers | - | `home.cube` |
| Headline | `display`, heading level 1 | `app.tagline` | `home.headline` |
| Lede | `body`, `inkSecondary` | `home.lede` | `home.lede` |
| Scan | button, primary, thumb, block, trailing chevron | `home.scan` | `home.scan` |
| Enter by hand | button, secondary, block, trailing chevron | `common.enterByHand` | `home.enterByHand` |
| Another size | button, secondary, block, trailing chevron. Opens [the screen for another size or a typed scramble](review.md#a-cube-of-another-size) | `home.other` | `launch.other` |
| History | button, ghost, clock icon | `home.history` | `home.history` |
| Help | button, ghost, help icon | `common.help` | `home.help` |
| Settings | button, ghost, sliders icon | `common.settings` | `home.settings` |
| Unfinished-solve card | card | `home.resume.title`, `home.resume.body` | `home.resumeCard` |
| Carry on | button, primary | `common.carryOn` | `home.carryOn` |
| Discard | button, ghost | `home.resume.discard` | `home.discard` |
| Camera notice | notice, caution | see states | `home.notice` |
| Look again | button, secondary, refresh icon | `common.lookAgain` | `home.lookAgain` |

Focus order: the card's buttons if present, then Scan, Enter by hand,
History, Help, Settings. Focus starts on the primary action.

The cube turns slowly while idle. With reduced motion it is still. It can
be dragged round with a pointer; nothing depends on that.

There is no language button here. Language is the first row in Settings
and a button on the first-run screen.

## States

### `home.default`

A camera is available and nothing is unfinished. **Scan a cube** is the
primary action.

- **Scan a cube**: to `permission.ask` where the system asks per app and
  access is not yet allowed; otherwise to `scan.starting`.
- **Enter colours by hand**: to `review.empty`.
- **History**, **Help**, **Settings**: to those screens.

### `home.finding-camera`

Desktop, at start-up, while cameras are being listed. As `home.default`,
plus a status line above the actions: the working indicator and
`home.findingCamera`. Scan stays enabled: if it is pressed before a camera
is found, the scan screen shows `scan.starting` and then its own result.
Becomes `home.default` or `home.no-camera` by itself. The status line is
announced politely once.

### `home.no-camera`

Desktop; no camera was found.

- A caution notice above the actions: `home.noCamera.title`,
  `home.noCamera.body`, and **Look again** (to `home.finding-camera`).
- **Enter colours by hand** becomes the primary action and moves above
  Scan.
- **Scan a cube** is shown disabled. Its accessible description is the
  notice's text, so the reason is read with it.
- The notice is announced when it appears.

### `home.camera-unsupported`

This build cannot use this device's camera: every phone until mobile
capture exists (plan decision 8).

- A caution notice: `home.unsupported.title`, `home.unsupported.body`.
- **Enter colours by hand** is the primary action.
- **Scan a cube** is not shown at all. Nothing the person can do would
  enable it, so a disabled button would be a dead control.

### `home.resume`

An unfinished solve is stored. Combines with any of the states above: the
card sits between the lede and the actions.

- The card: `home.resume.title` in bold, `home.resume.body` beneath in the
  mono face ("Turn 24 of 62"), **Carry on** and **Discard**.
- **Carry on** is primary. While the card shows, **Scan a cube** is drawn
  as a secondary button so that there is still one primary action.
- **Carry on**: to `solve.hold-resume`.
- **Discard**: to `home.confirm-discard`.

### `home.confirm-discard`

A dialog over `home.resume`: `home.discard.title`, `home.discard.body`.

- **Keep it** (`home.discard.keep`): the safe choice; focused when the
  dialog opens; what Esc and back choose. Back to `home.resume`.
- **Discard** (`home.resume.discard`): removes the stored solve and its
  history entry's "unfinished" mark; back to the state beneath, without
  the card.

## Back

Android: leaves the app, as the system does by default. Elsewhere: nothing.
Esc never closes the window.
