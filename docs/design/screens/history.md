# History

> **Status in 0.1.0-beta.1:** Built, as `HistoryScreen.qml` and
> `SolveHistory`: the **History** button on Home opens the list, and Settings
> has **Keep a history of solves** and **Clear history**. What is not yet is
> under **Status: built** below.

Lane: experience. New. In the first release by the maintainer's decision of
2026-10-13; the "Solve history" button on Home did nothing before.

**Status: built** (`HistoryScreen.qml`, `SolveHistory`). What is not yet:
the **thumbnail has no marks** (the marks are the kit's, and the app has no
kit yet), and **`solve.hold-resume`** is not in the app, so carrying on goes
straight to `solve.move` on the turn that was reached, with the camera off
(nothing was scanned this session to match it against).

The cubes solved on this device, newest first, and a way back into an
unfinished one.

Entered from: **History** on Home.

## What is kept

For each solve: when it started, the cube as scanned (54 colours), the
turn reached, the number of turns, and the time taken. The solution is
not stored; it is worked out again from the cube, which gives the same
answer. So that it does, the **method** it was solved with is kept too, and
when the method was changed part-way (which solves on from the cube in
hand), the cube that solution starts from; neither is more than a number
and 54 colours.

- **No picture is ever kept.** That is why the privacy statement can say
  so.
- Kept in the app's own data folder on this device (`solve-history.json`;
  `cube::platform::paths`, with `CUBEULATOR_DATA_DIR` as the tests' way of
  keeping out of a real one). Never sent anywhere. A file that cannot be
  read is set aside (`.unreadable`) and the history starts empty. Where a
  platform has not said where its data folder is (Android and iOS today) Qt's
  own application-data location is used; in the browser build the folder is
  the page's in-memory file system, so the history lasts for the session.
- The newest 100 are kept; older ones drop off the end.
- A solve is added when its first turn is advanced, marked unfinished,
  and marked finished when Complete opens. Only the newest unfinished
  solve can be carried on; starting another scan leaves the older one
  unfinished and viewable.
- **Keep a history of solves** in Settings turns this off. Turning it off
  stops adding; it does not delete. **Clear history** deletes.

### The file, and cubes of other sizes

The cube is any size from 2x2 to 9x9 (`docs/nxn-app-plan.md`): its colours are
written as 6 N^2 letters, face by face, and each is read back through the same
check a scan's assembly uses (`cube::nxn::validate`), so a cube that turns
could not have made is dropped with its entry, not handed to the solver.

- The file is **version 1** for as long as every entry is a 3x3 with no kept
  solution, which is byte for byte what the first release wrote, so an app that
  goes back to it still reads it. An entry of another size carries a `size`, and
  one with a kept solution a `moves` (the turns of single layers, in cubing
  notation, `R 2R' U ...`); either makes the file **version 2**. A version 1
  file is read as 3x3s. A file of a later version is set aside, as before.
- A 3x3's solution is worked out again (it is the same for the same method). A
  bigger cube's is **kept**, because the solver's answer for it improves from
  one release to the next and a solve being carried on must not change under the
  person doing it. Carrying the solve on, and reviewing it, put the kept
  solution back as it was - there when the screen opens, nothing to wait for -
  and the stages the screens say ("Centres", "Edges", ...) are kept with it as
  `stages`, a list of a stage number and how many of the turns it is, in turn.
  A kept solution that does not solve its cube, or whose stages do not add up
  to its turns, is discarded and the entry stays; one kept with no stages is
  worked out again, as a 3x3's.
- A solution is at most 2000 turns (the biggest cube the library solves takes
  about 740 on average, 766 the most seen); more is a damaged file. The first
  release's limit was 400.

## Layout

One column at every size, at most 640 wide, centred from medium up.

A top bar: back, the title, and **Clear history** (ghost) at the trailing
end when there is anything to clear.

The list is rows 64 high. Each row is two targets side by side: the row
itself, and a 56 x 56 delete button at its trailing end.

## Parts

| part | component | string | objectName |
|---|---|---|---|
| Back | icon button | `common.back` | `history.back` |
| Title | top bar title, heading level 1 | `history.title` | `history.title` |
| Clear history | button, ghost | `history.clear` | `history.clear` |
| Row | list row. Leading: a 44 px side chip of the scanned cube's front side, as a thumbnail (decorative). Then the date and time, formatted by the locale; beneath, in `small`, the summary. Trailing chevron | `history.item.done`, `history.item.unfinished`, `history.item.size` | `history.row1` ... |
| Delete | icon button, cross icon | `history.delete` | `history.delete1` ... |
| Empty | empty state | `history.empty.title`, `history.empty.body`, `home.scan` | `history.empty` |
| Off | empty state | `history.off.title`, `history.off.body`, `history.off.open` | `history.off` |
| Delete dialog | dialog | `history.delete.title`, `history.delete.keep`, `history.delete` | `history.deleteDialog` |
| Clear dialog | dialog | `history.clear.title`, `history.clear.body`, `history.clear.keep`, `history.clear.confirm` | `history.clearDialog` |

A row's accessible name is the date followed by the summary
(`history.a11y.row`). The delete button's is "Delete" followed by the date
(`history.a11y.delete`), so that a list of them can be told apart. Deleting
one is announced (`history.a11y.deleted`), and so is clearing
(`history.a11y.cleared`).

The thumbnail is nine stickers with marks. It is a way to recognise a
scramble at a glance, not information.

## States

### `history.empty`

Nothing has been solved. The empty state: `history.empty.title`,
`history.empty.body`, and **Scan a cube** (primary), which does what it
does on Home.

### `history.list`

One or more solves.

- A finished row reads "62 turns · 12:40" (`history.item.done`). Tapping
  it: to `solution.overview` for that solve.
- A cube that is not a 3x3 starts its row with its size: "5×5 · 310 turns ·
  41:05" (`history.item.size`). Its chip is that many stickers across in the
  same 44 px, so a 5x5's front side is five by five.
- An unfinished row reads "Unfinished · turn 24 of 62"
  (`history.item.unfinished`). Tapping it: to `solve.hold-resume`.
- **Delete** on a row: to `history.confirm-delete`.
- **Clear history**: to `history.confirm-clear`.
- Only the newest unfinished row can be carried on. An older unfinished row
  is listed, can be deleted, and opens `solution.overview` as a finished one
  does: the solution to the cube as scanned, worked out again with the method
  it was followed with, under the date of the solve. Back returns here.

### `history.off`

Keeping history is switched off. The empty state: `history.off.title`,
`history.off.body`, and **Open Settings**, which goes to `settings.main`.
Solves kept before it was switched off are still listed beneath, if there
are any; the message then sits above the list as a notice instead.

### `history.confirm-delete`

Dialog: `history.delete.title`.

- **Keep it**: the safe choice.
- **Delete**: removes the row. A screen reader hears that it was deleted,
  and focus moves to the next row, or to the empty state.

### `history.confirm-clear`

Dialog: `history.clear.title`, `history.clear.body` ("4 solves will be
deleted from this device.").

- **Keep them**: the safe choice.
- **Clear**: to `history.empty`. An unfinished solve goes too, and with
  it the card on Home.
