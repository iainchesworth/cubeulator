# History

> **Status in 0.1.0-beta.1:** Not built. Nothing is kept between solves, and
> the **History** button on Home still does nothing.

Lane: experience. New. In the first release by Iain's decision of
2026-10-13; the "Solve history" button on Home did nothing before.

The cubes solved on this device, newest first, and a way back into an
unfinished one.

Entered from: **History** on Home.

## What is kept

For each solve: when it started, the cube as scanned (54 colours), the
turn reached, the number of turns, and the time taken. The solution is
not stored; it is worked out again from the cube, which gives the same
answer.

- **No picture is ever kept.** That is why the privacy statement can say
  so.
- Kept in the app's own data folder on this device. Never sent anywhere.
- The newest 100 are kept; older ones drop off the end.
- A solve is added when its first turn is advanced, marked unfinished,
  and marked finished when Complete opens. Only the newest unfinished
  solve can be carried on; starting another scan leaves the older one
  unfinished and viewable.
- **Keep a history of solves** in Settings turns this off. Turning it off
  stops adding; it does not delete. **Clear history** deletes.

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
| Row | list row. Leading: a 44 px side chip of the scanned cube's front side, as a thumbnail (decorative). Then the date and time, formatted by the locale; beneath, in `small`, the summary. Trailing chevron | `history.item.done`, `history.item.unfinished` | `history.row1` ... |
| Delete | icon button, cross icon | `history.delete` | `history.delete1` ... |
| Empty | empty state | `history.empty.title`, `history.empty.body`, `home.scan` | `history.empty` |
| Off | empty state | `history.off.title`, `history.off.body`, `history.off.open` | `history.off` |
| Delete dialog | dialog | `history.delete.title`, `history.delete.keep`, `history.delete` | `history.deleteDialog` |
| Clear dialog | dialog | `history.clear.title`, `history.clear.body`, `history.clear.keep`, `history.clear.confirm` | `history.clearDialog` |

A row's accessible name is the date followed by the summary. The delete
button's is "Delete" followed by the date, so that a list of them can be
told apart.

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
- An unfinished row reads "Unfinished · turn 24 of 62"
  (`history.item.unfinished`). Tapping it: to `solve.hold-resume`.
- **Delete** on a row: to `history.confirm-delete`.
- **Clear history**: to `history.confirm-clear`.

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
