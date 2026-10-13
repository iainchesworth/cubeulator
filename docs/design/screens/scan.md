# Scan

Lane: **scan** (`feature/direct-scan`). This is a proposal for the chrome
and the states of `DirectScanScreen.qml`. How the cube is read is theirs.

The camera picture, what the reader makes of it, and the six sides read so
far. Scanning needs no taps: holding a side still is the input. On a
phone it is done with the phone in one hand and the cube in the other, so
every control is in the bottom thumb zone and 56 high.

The screen's guidance wording as it stands today is the model for the
whole app's voice and is kept. One line changes: the dash in "Reading
this side — hold it there" becomes a full stop.

Entered from: Home, Permission, Complete (**Scan another cube**), the
solve screen's **Scan the cube again**, Review (**Scan instead**).

## Layout

The camera picture fills the screen and is cropped, not letterboxed. It
is never mirrored, and neither are the dots on it: a chip has to match the
cube in the hand. Everything else sits over it on panels of `overlay.panel`.
The screen looks the same in both themes.

**Compact.**

- Top, 16 from the edges and the safe area: the **status panel**, full
  width. A hint panel appears beneath it in `scan.stuck`.
- Bottom, 16 from the edges and 8 more than the safe area: a row of
  buttons special to the state (when there are any); the **six chips** in
  a row, each as wide as fits (at least 48, at most 96); then **Cancel**,
  **Start over** and **More**, equal widths.

**Short.** The status panel takes the leading 60% at the top. The chips
are a 3 x 2 block at the trailing side with the three buttons stacked
beneath it.

**Expanded.** As compact, with the status panel at most 520 wide at the
top leading corner and the chips and buttons centred at the bottom.

## Parts

| part | component | string | objectName |
|---|---|---|---|
| Camera picture | An image filling the screen. Role: graphic. No marks are drawn on it | `scan.a11y.preview` | `scan.picture` |
| Sticker dots | Nine markers on the side in view. A hollow ring while the side is seen; a filled disc in `overlay.ok` while it counts. Each has a dark edge so it shows on any sticker. Ignored by screen readers | - | `scan.dots` |
| Status panel | status panel; a polite live region | `scan.title.*`, `scan.guide.*`, `scan.camera.*`, `scan.solveFailed.title`, `scan.solveFailed.body` | `scan.status` |
| Hint | An overlay panel with a bold line and a sentence | `scan.stuck.title`, `scan.stuck.body` | `scan.hint` |
| Chip 1-6 | side chip. Stickers are the measured colours; each carries the mark of the colour the reader took it for | `scan.a11y.chipEmpty`, `scan.a11y.chipRead`, `scan.a11y.chipInView`, `colourSide.*` | `scan.chip1` ... `scan.chip6` |
| Cancel | button, secondary on a panel, thumb | `common.cancel` | `scan.cancel` |
| Start over | button, secondary on a panel, thumb. Disabled until a side is read | `scan.startOver` | `scan.startOver` |
| More | button, secondary on a panel, thumb, three-dot icon | `common.more` | `scan.more` |
| Check the colours | button, primary on a panel, thumb | `scan.checkColours` | `scan.checkColours` |
| Try again / Look again | button, primary on a panel, thumb, refresh icon | `common.tryAgain`, `common.lookAgain` | `scan.retry` |
| Enter colours by hand | button, secondary on a panel, thumb | `common.enterByHand`, `scan.more.enter` | `scan.enterByHand` |
| Open device settings | button, secondary on a panel, thumb | `common.openDeviceSettings` | `scan.openSettings` |
| Scan again | button, secondary on a panel, thumb | `common.scanAgain` | `scan.scanAgain` |
| More sheet | sheet | `common.more`, `scan.more.enter`, `scan.more.chooseCamera`, `scan.more.switchCamera`, `scan.more.help`, `common.settings`, `common.close` | `scan.moreSheet` |
| Camera list | sheet; one row per camera, the current one checked | `scan.picker.title`, `common.close` | `scan.cameraSheet` |
| Leave dialog | dialog | `scan.leave.title`, `scan.leave.body`, `scan.leave.keep`, `scan.leave.confirm` | `scan.leaveDialog` |
| Start-over dialog | dialog | `scan.restart.title`, `scan.leave.body`, `scan.restart.keep`, `scan.startOver` | `scan.restartDialog` |

A primary button on a panel is `#FF5A3D` with a `#1A1918` label in both
themes, because the panel is always dark.

**Chips** are in the order the sides were first shown, leading to
trailing. The nine stickers inside each never mirror. The first empty chip
is where the side being held will land; while it counts, a bar fills along
its bottom edge. A chip arriving scales in over 240 ms; with reduced
motion it appears.

**Nothing on this screen is colour alone.** Seen and counting differ by
ring and disc; a side in view again by a heavier border and a pointer; a
completed scan by a check on every chip; trouble by a triangle and words.

**Focus order:** the state's own buttons, then Cancel, Start over, More.
The chips are not focus stops; a screen reader can move to each and hear
its name. Keys: Esc cancels, R starts over, M opens More, H enters by
hand.

**Sound and haptics** (`../platforms.md`): a tick when a side is read, two
notes when the cube is read.

**The screen stays awake** for as long as it is showing.

## States

The status panel's headline and guidance for each:

| state | headline | guidance |
|---|---|---|
| `scan.starting` | `scan.title.starting` | none |
| `scan.settling` | `scan.title.progress` | `scan.guide.settling` |
| `scan.looking` | `scan.title.progress` (0) | `scan.guide.first` |
| `scan.in-view` | `scan.title.progress` | `scan.guide.inView` |
| `scan.reading` | `scan.title.progress` | `scan.guide.reading` |
| `scan.next-side` | `scan.title.progress` | `scan.guide.next` |
| `scan.seen-before` | `scan.title.progress` | `scan.guide.seenBefore` |
| `scan.conflict` | `scan.title.progress` (6), caution icon | `scan.guide.conflict` |
| `scan.complete` | `scan.title.complete`, check icon | `scan.guide.complete` |
| `scan.resuming` | `scan.title.starting` | `scan.guide.resuming` |
| `scan.solve-failed` | `scan.solveFailed.title`, caution icon | `scan.solveFailed.body` |
| `scan.camera-none` | `scan.camera.none.title`, caution icon | `scan.camera.none.body` |
| `scan.camera-busy` | `scan.camera.busy.title`, caution icon | `scan.camera.busy.body` |
| `scan.camera-blocked` | `scan.camera.blocked.title`, caution icon | `scan.camera.blocked.body` |
| `scan.camera-lost` | `scan.camera.lost.title`, caution icon | `scan.camera.lost.body` |
| `scan.camera-failed` | `scan.camera.failed.title`, caution icon | `scan.camera.failed.body` |

### `scan.starting`

The camera is opening. The picture area is `#141312`. Chips are empty.
**Start over** is disabled. Cancel leaves at once.

### `scan.settling`

The picture is showing. Exposure and colour balance are being fixed and
nothing is read. No dots. Lasts a second or two.

### `scan.looking`

Running, no side read, no cube in view. No dots.

### `scan.in-view`

A side is seen but is not steady. Nine hollow rings on its stickers. A
screen reader hears `scan.a11y.cubeSeen` once per side.

### `scan.reading`

The side is steady and counting. Nine filled discs. The next empty chip
shows the capture bar filling. When it is full the side is read: the chip
fills and scales in, the tick sounds, `scan.a11y.sideRead` is announced,
and the state becomes `scan.next-side`, `scan.conflict` or `scan.complete`.

### `scan.next-side`

One to five sides read; none in view. **Start over** is enabled.

### `scan.seen-before`

The side in view is one already read. Its chip takes a 3 px border and a
pointer above it; its accessible name becomes `scan.a11y.chipInView`.
Rings, not discs: it is not counting. If it reads differently from before,
the reader decides what to do; the chip shows what it now holds.

### `scan.stuck`

No new side has been read for 10 seconds. Not a state of its own so much
as an addition to whichever of `scan.looking`, `scan.next-side`,
`scan.in-view` or `scan.seen-before` is current: the hint panel appears
under the status panel, and two buttons above the chips.

- **Enter the rest by hand** (`scan.more.enter`): to `review.partial`,
  with the sides read so far filled in.
- **Help with scanning** (`scan.more.help`): to `help.scanning`; back
  returns here with the scan as it was.
- It clears itself when a side is read. It is advice, not a time limit:
  the scan carries on behind it.

### `scan.conflict`

Six sides are read and they do not make a valid cube. The scan keeps
going: showing a side again can settle it.

- **Check the colours**, primary, above the chips: to `review.miscount` or
  `review.impossible`, with the six sides as read.
- **Start over**: to `scan.confirm-restart`.

### `scan.complete`

Six sides make one valid cube. Held for at least 1.4 seconds while the
solution is worked out, so that the last side is seen arriving.

- Every chip takes a 3 px `overlay.ok` border and a check in its corner,
  one after another along the row (all at once with reduced motion).
- Only **Cancel** remains.
- Then, by itself: to `solve.hold`; or to `solve.already-solved` if the
  cube is solved; or to `scan.solve-failed`.
- Announced: `scan.title.complete`.

### `scan.solve-failed`

The cube was read but no solution was found. Rare, and said plainly.

- **Check the colours**, primary: to `review.valid` with the cube as read.
- **Scan again**: forgets the sides; to `scan.looking`.

### `scan.resuming`

The app came back to the front, or the lid was opened, mid-scan. The
camera was released while the app was away and is starting again. The
chips keep the sides already read. Becomes whichever state fits.

### `scan.camera-none`

No camera. Picture area dark; chips keep anything already read.

- **Look again**, primary: to `scan.starting`.
- **Enter colours by hand**: to `review.empty`.
- Back and Cancel: to Home, without asking.

### `scan.camera-busy`

Another program has the camera.

- **Try again**, primary. **Enter colours by hand**. Back.

### `scan.camera-blocked`

The operating system refuses: Windows privacy settings, a Linux portal,
macOS after a refusal.

- **Try again**, primary. **Open device settings**. **Enter colours by
  hand**. Back.

### `scan.camera-lost`

The camera stopped or was unplugged mid-scan. The sides read are kept.

- **Try again**, primary: to `scan.resuming`.
- **Enter colours by hand**: to `review.partial` with those sides.
- **More > Choose a camera**: to `scan.camera-picker`.

### `scan.camera-failed`

Any other camera error. The guidance ends with a short code
(`scan.camera.failed.body`, `%1`), in the mono face, that can be quoted in
a bug report.

- **Try again**, primary. **Enter colours by hand**. Back.

A camera failure is announced assertively: it stops the person.

### `scan.more`

A sheet over the current state. The scan carries on behind it.

1. **Enter the rest by hand**: to `review.partial` or `review.empty`.
2. **Choose a camera** (desktop, when there is more than one): to
   `scan.camera-picker`. **Switch camera** (phones): swaps front and back
   and closes the sheet.
3. **Help with scanning**: to `help.scanning`.
4. **Settings**: to `settings.main`.
5. **Close**.

### `scan.camera-picker`

A sheet listing the cameras by the names the system gives them; the
current one has a check. Choosing one closes the sheet and goes to
`scan.starting` with the sides read so far kept. Desktop only.

### `scan.confirm-leave`

Dialog. Shown when Cancel or back is used with at least one side read.

- **Keep scanning** (`scan.leave.keep`): the safe choice; back to the scan.
- **Stop** (`scan.leave.confirm`): to Home.

With no side read, Cancel and back leave at once.

### `scan.confirm-restart`

Dialog. Shown when **Start over** is used with at least one side read.

- **Keep these** (`scan.restart.keep`): the safe choice.
- **Start over** (`scan.startOver`): forgets every side; to `scan.looking`.

## Needs from the scan lane

1. **Codes, not sentences.** `DirectScan.cameraError` is text built in
   C++ today. The lane contract is that wording lives in QML: expose a
   code (none, busy, blocked, lost, other) and, for "other", the short
   code shown to the person.
2. **Is the side in view one already read?** Today that is `inView` on a
   side; the guidance needs it as a state so that it can say so.
3. **What each sticker was taken for**, even provisionally, so that a chip
   sticker can carry its mark. If the naming is only settled when the cube
   is valid, say so and the marks appear then.
4. **Keeping sides across a camera restart**: after the app was in the
   background, after a camera was unplugged, and after choosing another
   camera.
5. **Handing the sides read so far to the review screen**, and taking a
   cube back from it.
6. **A solver failure signal** that is distinct from "not valid yet".
7. **Events** the shell can hang sound, haptics and announcements on: side
   read, cube read.
8. **Releasing the camera** when the app is not in front.
9. **Developer builds that record failed scans** show
   `settings.dev.recording` on this screen. Release builds record nothing
   (brief, D7).

Not needed from the lane: the 10-second hint timer, the dialogs, the
sheet, the keep-awake request. Those are the shell's.
