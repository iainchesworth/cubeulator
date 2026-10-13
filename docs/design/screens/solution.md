# The solution

Lane: experience, drawing the solve lane's cube. New. This is what the
**Review the solution** button was meant to open.

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
| Summary | `bodyStrong` | `solution.summary` | `solution.summary` |
| Method | `small`, `inkSecondary` | `solution.method` | `solution.method` |
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
to show the cube at any turn.
