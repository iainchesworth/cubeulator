# Review and hand entry

> **Status in 0.1.0-beta.1:** Not built (recovery plan R3, still to do). There
> is no screen to check or correct the colours or to enter a cube by hand:
> Home has no **Enter colours by hand**, the menu's item for it is disabled,
> and a scan that does not make a valid cube stays on the scan screen and
> says so.

Lane: **scan** (`feature/direct-scan`; recovery plan R3). This is a
proposal for the chrome and the states. Checking that colours make a cube,
and assembling one, are theirs.

One screen does two jobs: **checking** a cube the camera read, and
**entering** one by hand. The second is also the only way in on a device
whose camera the build cannot use, which is every phone until mobile
capture exists.

Entered from: Home (**Enter colours by hand**: entering); `scan.conflict`,
`scan.solve-failed` and `solve.hold` (**Check the colours**: checking);
`scan.stuck`, `scan.more` and the camera-failure states (entering, with
any sides already read filled in); Permission.

## How it works

**Paint, do not pick.** Choose a colour once, then tap every sticker of
that colour. A cube has eight stickers a side to set; choosing a colour
per sticker would double the taps.

**One side at a time, large.** On a phone the unfolded cube is 12
stickers wide, which is 27 px each: too small to hit with a cube in the
other hand. So the side being edited is shown alone, with stickers 60 to
110 px across, and six buttons choose which side.

**The centre is fixed.** It never moves on a real cube, and it names the
side. Tapping it explains that and changes nothing.

**Any way up.** A side can be entered whichever way up it is held, as it
can be scanned. This relies on the scan lane's assembly trying each side
in its four rotations (it already does this for scanned sides). If hand
entry cannot use that, the screen must instead say how to hold each side,
and `review.intro.enter` and `help.entering.body` change. **This is the
main thing for the scan lane to confirm.**

## Layout

**Compact.** A top bar: back, the title, and Help at the trailing end.
Then one column, 16 margin, sized to fit the window without scrolling:

1. The instruction, in `small`.
2. The **side picker**: six buttons in a row, each 56 high.
3. The **side being edited**: a 3 x 3 grid on `cubeBody`, square, taking
   the height that is left (at most 340, at least 168).
4. The **palette**: six swatches in a row, 56 high. Beneath it, the chosen
   colour's name in bold at the leading side and **Clear this side** at the
   trailing side.
5. The **status**: a one-line notice.
6. A row: **Scan instead** (ghost) then **Solve** (primary, taking the rest
   of the width). In the thumb zone.

**Short.** Two columns. Leading: the side picker over the side being
edited. Trailing: the palette, the status, the actions. No instruction.

**Expanded.** Two columns, 40 margin. Leading: instruction, side picker,
the side being edited, the palette. Trailing: the **whole cube unfolded**
as an overview (not shown on smaller sizes), then the status, then the
actions.

## Parts

| part | component | string | objectName |
|---|---|---|---|
| Back | icon button | `common.back` | `review.back` |
| Title | top bar title, heading level 1 | `review.title.check`, `review.title.enter` | `review.title` |
| Help | icon button | `common.help` | `review.help` |
| Instruction | `small`, `inkSecondary` | `review.intro.check`, `review.intro.enter` | `review.intro` |
| Side picker | A group of six toggle buttons, one pressed. Each shows a sticker of that side's centre colour with its mark, and a check in the corner when all nine are set. Group name `review.sidePicker` | `review.sidePicker`, `review.a11y.sideDone`, `review.a11y.sideLeft`, `colourSide.*` | `review.side.white` ... `review.side.blue` |
| Side being edited | A group of nine sticker buttons, named for the side. An unset sticker is `paper` with a dashed `outline` inset | `review.a11y.sticker`, `colour.*`, `colour.unset` | `review.sticker1` ... `review.sticker9` |
| Palette | colour picker. Group name `review.palette` | `review.palette`, `colour.*` | `review.colour.white` ... `review.colour.blue` |
| Chosen colour | `bodyStrong` | `colour.*` | `review.chosenColour` |
| Clear this side | button, ghost | `review.clearSide` | `review.clearSide` |
| Unfolded cube | 54 small stickers in the shape of a cross. A picture, not a control; hidden from screen readers, who have the side picker. The side being edited is outlined | - | `review.net` |
| Status | notice; a polite live region | `review.status.*`, `review.centre` | `review.status` |
| Solve | button, primary, thumb, trailing chevron | `review.solve` | `review.solve` |
| Scan instead | button, ghost, thumb | `common.scanInstead` | `review.scanInstead` |
| Discard dialog | dialog | `review.discard.title`, `review.discard.body`, `review.discard.keep`, `review.discard.confirm` | `review.discardDialog` |

The side being edited, the palette and the unfolded cube **never mirror**.
The palette's order is fixed: white, yellow, orange, red, green, blue.

Sides are named by centre colour ("Red side"), in this order: yellow,
green, orange, blue, red, white. Choosing a side with stickers unset is
the default on entry.

**Keyboard.** 1-6 choose the colour. Arrow keys move a cursor over the
nine stickers; Space or Enter paints; Backspace unsets. Page Down and Page
Up change side. Ctrl+Enter (Cmd+Enter) solves when the cube is valid.
Tab order: side picker (one stop; arrows within), the stickers (one stop;
arrows within), palette (one stop), Clear, Scan instead, Solve.

**Screen reader.** A sticker reads as `review.a11y.sticker`: "Red side,
row 2, column 3: Blue". The status is announced when validity changes,
not on every sticker.

## States

The status notice for each:

| state | kind | text |
|---|---|---|
| `review.empty` | information | `review.status.empty` |
| `review.partial` | information | `review.status.partial` |
| `review.miscount` | caution | `review.status.miscount`, then `review.status.miscountOver` and `review.status.miscountUnder`, each built from `review.status.countItem` |
| `review.impossible` | caution | `review.status.impossible`, `review.status.impossibleBody` |
| `review.valid` | positive | `review.status.valid` |

**Solve** is disabled in every state but `review.valid`. The status
notice directly above it is the reason, and is its accessible description.

### `review.empty`

Entering by hand; only the six centres are set. Title
`review.title.enter`; instruction `review.intro.enter`. The first side
(yellow) is showing and white is the chosen colour.

### `review.partial`

Some stickers are unset. The status counts them down. A side whose nine
are all set has a check on its button. The side picker moves by itself to
the next unfinished side when the last sticker of a side is set.

Also where `scan.stuck`, `scan.more` and `scan.camera-lost` land, with
the sides the camera read already filled in and checked.

### `review.miscount`

Every sticker is set but some colour does not appear nine times. The
status names them: "Too many: Red (10). Too few: Orange (8)."

Arriving from `scan.conflict`, the title is `review.title.check` and the
instruction `review.intro.check`.

### `review.impossible`

Nine of each, but the pieces cannot fit together: a corner or an edge
appears twice, or a piece is twisted. The app cannot say which sticker is
wrong, and does not guess. It asks for each side to be compared again.

### `review.valid`

A valid cube. **Solve** is enabled and takes the focus.

Arriving from `solve.hold` or `scan.solve-failed`, the cube is the one
that was read, already valid: this is where someone who doubts the camera
looks it over.

### `review.working`

**Solve** was pressed. The screen shows the working indicator and
`review.working`; nothing can be pressed. By itself: to `solve.hold`, to
`solve.already-solved`, or to `review.solve-failed`. Normally under a
second.

### `review.solve-failed`

No solution was found for a cube that passed as valid. The status is a
caution: `review.failed.title`, `review.failed.body`, with **Try again**
(`common.tryAgain`) and **Scan instead**.

### `review.confirm-discard`

Dialog. Shown when back or **Scan instead** is used after any sticker was
set or changed here.

- **Keep editing** (`review.discard.keep`): the safe choice.
- **Discard** (`review.discard.confirm`): to where the screen was opened
  from, or to the scan.

With nothing changed, back and **Scan instead** leave at once.

## Needs from the scan lane

1. **Whether a hand-entered side can be in any rotation** (above).
2. **A validity result with a reason**: incomplete; which colours are over
   and under; counts right but not a real cube; valid. Codes, not
   sentences.
3. **A cube in, a cube out**: taking the sides read so far (measured
   colours and what each was taken for), and handing a valid cube to the
   solver, the way `SolveController.solveCube` is fed today.
4. **Two cubes that both fit.** The recovery plan notes that a cube one
   turn from solved can be assembled two ways from square-on sides. If
   that can happen here, the screen needs a state that shows both and
   asks which; it is not designed yet.
