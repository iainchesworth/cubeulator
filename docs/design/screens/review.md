# Review and hand entry

> **Status:** The colours screen (the section "Entering the colours of a cube of
> any size", below) is built for sizes 2 to 5 and is the screen this page's 3x3
> sketch became: one side at a time, paint rather than pick, the count and the
> cube check beside Solve. It also puts right a cube the camera misread (the
> section "Putting a misread scan right"), reached from the scan screen once six
> sides are in, and from the solve screen for a cube read as valid. Not built: the sketch's
> `review.working` and `review.solve-failed` states (Solve goes straight to the
> solve screen), the keyboard map below, and the unfolded overview.

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

## Putting a misread scan right

The same screen as "Entering the colours of a cube of any size" below, started from
what the camera read instead of from nothing (`AppState.reviewScan`). It differs in
five ways, and everything else is that screen:

- **It starts full.** Every sticker the camera named is painted. If the six sides
  made a cube the cube is shown as it is held; if they made none (a misread, or two
  cubes that fit) the six sides are placed by their middles **as the camera had them**,
  and the screen says so (`colours.notUpright`) because each may need turning.
- **Sides are named by their middle colour** ("Red side") on a cube with a middle
  sticker, and the sides round the grid likewise, as the sketch above asks: a person
  holding the cube thinks of it that way. The order is yellow, green, orange, blue,
  red, white. An even size has no middle and keeps top, front and so on.
- **The camera's doubts are shown.** A sticker the colour naming placed with little
  to spare over another colour carries a "?" drawn on it (a mark, so that it does not
  depend on telling the colours apart), the note counts them, and the screen reader
  reads it. Painting a sticker settles the doubt. The threshold (`kDoubtBelow`) is a
  first guess, not measured against real misreads.
- **Every change can be taken back.** *Undo*, *Redo* (also Ctrl+Z, Ctrl+Y), and
  *Back to the scan*, which puts every sticker as the camera read it. A drag over
  several stickers is one step. Leaving asks first only when something differs from
  the scan (`review.discard.*`); *Scan instead* reads *Scan again* and, like Back,
  starts a new scan.
- **Two ways beside tapping a sticker.** *Turn this side* turns the side shown a
  quarter turn clockwise as seen from outside, for a side that came the wrong way up.
  *Describe this side* opens a box to type the whole side as letters in reading
  order (W Y G B O R, `?` for a sticker you cannot tell; rows may be separated with
  `/`, spaces or lines, and an N by N side is N x N letters), prefilled with the side as
  it stands. The letters are fixed, like the letters of a move; the word for each colour
  is in the help text, in the reader's language. A description of the wrong length or with
  a letter that is no colour is refused, says which and where, and changes nothing.

What the person is told does not change: counts, then whether the pieces fit
together (`review.status.*`, `review.fault.*`), with the sticker of the piece at
fault outlined. **A cube that cannot be one never reaches the solver**: *Solve* is
on only for a valid cube. There is no "solve anyway".

| part | component | string | objectName |
|---|---|---|---|
| Title | heading | `review.title.check` | `colours.title` |
| Instruction | `small` | `review.intro.check` | `colours.intro` |
| Sides could be the wrong way up | caution line | `review.notUpright` | `colours.notUpright` |
| How many stickers the camera doubted | `small` | `review.doubt.note` | `colours.doubtNote` |
| A doubted sticker | a "?" on the sticker; its name adds that the camera was unsure | `review.a11y.stickerUnsure` | `colours.doubt.<n>` |
| Undo, Redo | buttons, ghost | `review.undo`, `review.redo` | `colours.undo`, `colours.redo` |
| Back to the scan | button, ghost, only here | `review.revert` | `colours.revert` |
| Turn this side | button, ghost | `review.turnSide` | `colours.turnSide` |
| Describe this side | button, ghost, opens the box | `review.describe.open` | `colours.describe` |
| The box | one line in the mono face, always left to right | `review.describe.help`, `review.describe.field` | `colours.describeText` |
| Use these, Cancel | buttons | `review.describe.apply`, `common.cancel` | `colours.describeApply`, `colours.describeCancel` |
| What is wrong with the description | `accent` line | `review.describe.tooFew`, `review.describe.tooMany`, `review.describe.unknownLetter` | `colours.describeError` |

**From the solve screen.** A cube the camera read as valid and read wrong is only
found out at the solve, so the solve screen has the same button (`solve.checkColours`,
`scan.checkColours`, ghost) for a 3x3 that came from a scan: `AppState.solveCamera` and
`DirectScan.complete`, so not for a typed cube, a scramble, a solve carried on from the
history, a cube that has been corrected and solved (it is a typed cube from then on) or a
scan since started over. It is a row of its own over the stepping buttons in both
layouts, and *Out of step? Rescan* narrow moves up over it: beside that button it did not
fit in every language. **Back from there is the solve screen**, as it was (the solution is
untouched, the camera comes back on it); only *Scan again* starts a new scan. Leaving with
changes asks first, as always. *Solve* on the corrected colours is a new solve from the
first turn (`solveColours`); the solve it replaces stays in the history as unfinished.

**Not built, and not verified.** Any size but 3x3 from a scan (the scan reads 3x3 only). The "?"
threshold on real recordings. A cube that fits two ways is shown as six sides to
compare, not as the two candidates (the sketch's "Two cubes that both fit").

## A cube of another size

Not part of the 3x3's journey, so it has no state of its own in
`../journey.md`: the screen is reached from Home by **Another size of cube**
(`home.other`), and it is how a cube that is not a 3x3 gets into the app
until the camera reads one (`../../nxn-app-plan.md`, P2 and P3). One column,
at most 640 wide, scrolling when the window is short:

| part | component | string | objectName |
|---|---|---|---|
| Back | button, ghost, chevron | `common.back` | `entry.back` |
| Title | heading | `entry.title` | `entry.title` |
| Size | a row of buttons that wraps, the chosen one primary: 2×2, 3×3, 4×4, 5×5 | `entry.size.label`, `entry.size.option` | `entry.size.<n>` |
| What the camera does at this size | one line under the sizes | `entry.size.camera`, `entry.size.noCamera` | `entry.size.note` |
| The scramble | a multi-line box in the mono face, on `surface`, the border `accent` while it is wrong | `entry.scramble.label`, `entry.scramble.placeholder` | `entry.scramble` |
| What a scramble looks like | one paragraph under the box, replaced by the fault while there is one | `entry.scramble.help` | `entry.scramble.help` |
| What is wrong | the fault, in `accent`, and the move at fault selected in the box | `entry.error.unknownMove`, `entry.error.layer`, `entry.error.empty`, `entry.error.solved` | `entry.error` |
| Solve | button, primary, block, trailing chevron. Control+Return in the box does the same | `entry.solve` | `entry.solve` |
| Scan instead | button, secondary, block; only at 3×3 | `entry.scan` | `entry.scan` |
| Enter the colours | button, secondary, block, trailing chevron. Opens the screen below at the size chosen | `common.enterByHand` | `entry.colours` |

**The size is chosen, never read off the scramble or the camera.** Sizes 2 to 5
are offered; 6 and 7 follow once the 3D cube has been seen on a phone.

**A scramble is notation** (`../../nxn-solver.md`, "Notation"): R, U', F2,
2R, Rw, 3Rw, 2-3R, and x, y, z for the whole cube. The letters are not translated
and a language does not change what is typed. A fault names the move at
fault, which is selected in the box so that it can be typed over; nothing is
started until the scramble is whole. A scramble that leaves the cube as it
was is refused, because there is nothing to solve.

**Solving** goes to the solve screen with no camera, as the demo does.

#### Entering the colours of a cube of any size

The hand entry above, built for any size, and the way a cube that is not a
3x3 is entered when there is no scramble to hand. It differs from the 3x3
sketch in how a side is named and held, because an even size has no centre
sticker to name it by and a 5x5 has 150 stickers to set:

- **The cube is held any way up and its sides are named for where they are**:
  the side facing up is the top, the side facing the person the front. No colour
  is given beforehand, not even a centre. Whichever six colours are there are the
  cube's, and the library says whether they can be a cube's (`review.status.*`,
  `review.fault.*`).
- **One side at a time, as it looks from outside**, with the sides against its
  four edges named round the grid, so which way up the grid goes is read off the
  screen and not guessed. Wide, the cube is drawn beside it and turned to show the
  side being painted, with the unpainted stickers grey.
- **Paint, do not pick**: choose a colour once, then tap or drag over the
  stickers that have it. A seventh swatch takes a colour away. **Fill this side**
  paints the whole side, which is how a solved side is entered in one touch.
- **What is wrong is said and pointed at**: too many or too few of a colour; or
  every colour N × N times and still not a cube, with what is wrong with it and the
  sticker of the piece at fault outlined. Solve is on only for a cube that is one.
- **Leaving with colours entered asks first** (`review.discard.*`); **Scan
  instead** is there at 3×3.

| part | component | string | objectName |
|---|---|---|---|
| Back | button, ghost, chevron | `common.back` | `colours.back` |
| Title | heading | `review.title.enter` | `colours.title` |
| Clear all | button, ghost, at the trailing end of the bar | `review.clearAll` | `colours.clearAll` |
| The cube | wide only; the unpainted stickers grey | - | `colours.cube` |
| Instruction | `small`, `inkSecondary` | `review.intro.enterN` | `colours.intro` |
| Side picker | six buttons in the order top, front, right, back, left, bottom; a tick when the side is full | `side.top`, `side.front`, `side.right`, `side.back`, `side.left`, `side.bottom`, `review.sidePicker` | `colours.side.<letter>` |
| Edge names | the side against each edge of the grid, small, round it | `side.*` | `colours.edge.top`, `.right`, `.bottom`, `.left` |
| The side | N × N stickers on `cubeBody`, tapped or dragged over. Each is a button named by `review.a11y.sticker` | `review.a11y.sticker`, `colour.unset` | `colours.grid`, `colours.sticker.<n>` |
| Palette | six swatches and one that takes a colour away | `colour.white`, `colour.yellow`, `colour.green`, `colour.blue`, `colour.orange`, `colour.red`, `review.palette` | `colours.colour.<n>` |
| Fill this side | button, ghost | `review.fillSide` | `colours.fillSide` |
| Status | a headline and, when there is one, what is wrong | `review.status.emptyN`, `review.status.partial`, `review.status.miscount`, `review.status.miscountOver`, `review.status.miscountUnder`, `review.status.countItem`, `review.status.impossible`, `review.status.impossibleBody`, `review.status.valid`, `review.fault.noScheme`, `review.fault.centreColours`, `review.fault.unknownPiece`, `review.fault.repeatedPiece`, `review.fault.twistedCorner`, `review.fault.flippedEdge`, `review.fault.swappedPieces` | `colours.status` |
| Solve | button, primary, block; on only for a cube that is one | `review.solve` | `colours.solve` |
| Scan instead | button, secondary; only at 3×3 | `entry.scan` | `colours.scan` |
| Discard dialog | dialog | `review.discard.title`, `review.discard.body`, `review.discard.keep`, `review.discard.confirm` | `colours.discard` |

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
