# The solution

Lane: experience, drawing the solve lane's cube. This is what the
**Review the solution** button opens (`SolutionScreen.qml`; what is built
and what waits for another lane is under [Built](#built)).

Every turn of a finished solve, grouped by stage, with the explanations in
full. Hands are free here, so it is where someone learning the method
reads what each stage did and steps through it at their own speed.

Entered from: **Review the solution** on Complete; a finished entry in
History.

## Layout

**Compact.** A top bar: back and the title. Then:

1. Two lines: how many turns in how many stages (bold), and the method in
   `small`.
2. The cube, 210 high.
3. A caption under it, centred, in the mono face.
4. Three buttons in a row, 56 high: **Previous**, **Play** (primary), **Next**.
   A 2 px rule beneath.
5. The **list**, scrolling in what height is left.

**Short.** Two columns: the summary, cube, caption and buttons leading;
the list trailing.

**Expanded.** Two panes. Leading: summary, the cube in all the height that
is left, caption, buttons. Trailing: the list.

## Parts

| part | component | string | objectName |
|---|---|---|---|
| Back | icon button | `common.back` | `solution.back` |
| Title | top bar title, heading level 1 | `solution.title` | `solution.title` |
| Summary | `bodyStrong`; the number of stages is the method's: five, four or two | `solution.summary` | `solution.summary` |
| Method | `small`, `inkSecondary` | `solution.method.beginner`, `solution.method.intermediate`, `solution.method.advanced` | `solution.method` |
| Cube | The solve lane's cube view, showing the cube before the selected turn. Role: graphic; its name is the caption | - | `solution.cube` |
| Caption | `numeral`, `inkSecondary` | `solution.atStart`, `solve.progress`, `solution.atEnd` | `solution.caption` |
| Previous | button, secondary, thumb | `solution.previous` | `solution.previous` |
| Play / Pause / Start again | button, primary, thumb | `solution.play`, `solution.pause`, `solution.startAgain` | `solution.play` |
| Next | button, secondary, thumb | `solution.next` | `solution.next` |
| Stage heading | A numbered box, the stage's name, how many turns, then its what and why in `small` | `stage.*`, `solution.stageTurns` | `solution.stage1` ... `solution.stage5` |
| Turn row | A list row, 48 high: the turn's number in the mono face, a swatch of the side's centre, the three parts of the turn, the direction arrow at the trailing end | `side.*`, `dir.*`, `amount.*`, `solution.a11y.turn`, `turn.*` | `solution.turn1` ... |

- A turn row shows side, direction and amount on one line separated by
  dots ("Right · downwards · Quarter turn"). Its accessible name is the
  whole sentence (`solution.a11y.turn`).
- On a cube bigger than a 3x3 a turn of a layer inside it, or of several
  together, says which: "Right side · layer 2 · downwards · Quarter turn",
  "Top · layers 1 to 2 · ...", counted from the side the move is named for
  (`solution.layer`, `solution.layers`). A turn of the outer layer says
  nothing more, so a 3x3's rows are as they were. The accessible name is the
  parts joined by commas, where a side's own turn has a sentence of its own.
- The selected row has `accentTint`, bold text and a 6 px `accent` bar at
  its leading edge. It is kept in view as the selection moves.
- With **Show cube notation** on, each stage heading also carries the
  stage's turns as one line of notation in the mono face, left to right.
  That is how cubers read an algorithm.
- The stage explanations are always shown here, whatever **Explain each
  stage** is set to.
- The cube, the swatches and the arrows never mirror.

Keys: forward and back arrows for next and previous; Space plays and
pauses; Home and End go to the first turn and to the solved cube.

Opened from History, the title is followed by the solve's date in the
top bar's second line.

## States

### `solution.overview`

Just opened. The cube is as it was scanned; the caption is
`solution.atStart`; the first turn is selected; the list is at the top.
**Previous** is disabled.

### `solution.stepping`

A turn after the first is selected, by **Next**, **Previous** or a tap on
a row. The cube shows the state before that turn, and the caption says
"Turn 10 of 62". **Next** and **Previous** play one turn on the cube
(380 ms; a snap with reduced motion). A tap on a row jumps without
animating.

### `solution.playing`

**Play** was pressed. Turns play one after another, about 700 ms apart
(900 ms and snapping with reduced motion). The main button reads
**Pause**. Selecting a row, **Next** or **Previous** stops the play.
Reaching the end goes to `solution.end`.

Nothing here runs for more than a few seconds without a visible control
that stops it.

### `solution.end`

After the last turn. The cube is solved; the caption is
`solution.atEnd`; **Next** is disabled; the main button reads **Start
again**, which goes back to `solution.overview`.

## Needs from the solve lane

The same cube view as the solve screen, driven by a second, independent
position: stepping here must not disturb a solve in progress. It needs
the list of turns (`SolveController.moves`), the stage ranges, and a way
to show the cube at any turn: `SolveController.stickersAt(turn)`, which is
`stickers` for any position and moves nothing.

## Built

`SolutionScreen.qml`, opened by **Review the solution** on Complete
(`AppState.openSolution()`; back is `AppState.closeSolution()`, to the
screen it was opened from). What is the same as above is not repeated;
what is not:

- **What it reviews** is `AppState.solutionSource`, which is
  `SolveController` for the solve just finished. Anything with its shape
  can be put there, which is how a finished entry in History opens this
  screen: an object with `moves`, `stageRanges`, `method`,
  `stickersAt(turn)` and a `solutionChanged` signal, and the date as the
  second argument of `openSolution(source, subtitle)`. The screen's header
  says the same.
- **A cube of another size** is reviewed the same way: the cube is the view
  that draws any size (`CubeNView`), the stages listed are the ones the cube
  needed (a reduction with no parity to set up has no such stage), and each
  side's swatch is the colour the side is named by (`sideColours` on the
  source; a 3x3 reads it from its centre sticker). A source with no `size`
  is a 3x3.
- **Layout classes.** The app has two (`Theme.wideBreakpoint`, 720), not
  the three of `system.md`, so: one column under 720 wide, two panes from
  720 or when the window is under 480 high. Short and expanded are the
  same two panes, the cube taking what the summary, caption and buttons
  leave.
- **The cube is looked at, not handled**: it cannot be dragged, and it
  does not nudge the layer of the selected turn, because a nudge never
  stops. It is seen from behind for a turn of the back side, as on the
  solve screen. It is shown as held at the start of the solve (yellow on
  top, green facing), which is the hold every turn's words are written for.
- **Reduced motion and Show cube notation** are properties of the screen
  (`reducedMotion`, `showNotation`), false until the settings rebuild has
  the two settings to bind them to: the app has neither today.
- **Escape and the back key** do what the back button does. The rest of
  the navigation stack is E2a's.
- **A solve whose method was changed part-way** is reviewed from where the
  change was made: the controller keeps the solution it is on, not the
  turns made before it, which is also what Complete counts.
