# Complete

Lane: **solve** reaches it; the chrome is experience. Replaces
`CompleteScreen.qml`.

The end of a solve. It says so in one word, shows what it took, and
offers what people do next. Today nothing navigates here and one of its
two buttons does nothing.

Entered from: the last turn of a solve, by itself.

## Layout

**Compact.** One column, 24 margin:

1. The cube, solved, in the height that is left (at least 120). Still:
   the bobbing of the current screen is removed for everyone.
2. The eyebrow, the word, and a 5 px `accent` underline 120 wide that
   sweeps in once over 900 ms (drawn at once with reduced motion).
3. Two figures side by side in a 2 px ruled box: turns, and time.
4. **Scan another cube** (primary, full width), **Review the solution**
   (secondary, full width), **Done** (ghost).
5. **My cube is not solved** (ghost), on a line of its own.

**Short.** Two columns: the cube leading, the rest trailing with smaller
figures.

**Expanded.** Two panes, 5:7. Leading: `surface`, the cube centred, a rule
on its trailing edge. Trailing: the column, centred vertically, at most
620 wide, with the word at 4 rem and the three actions in a row.

## Parts

| part | component | string | objectName |
|---|---|---|---|
| Cube | The solve lane's cube view, solved, yellow on top as it was held. Decorative | - | `complete.cube` |
| Eyebrow | eyebrow | `complete.eyebrow` | `complete.eyebrow` |
| The word | `display`, heading level 1 | `complete.title` | `complete.title` |
| Turns | A label in eyebrow style (`inkSecondary`) over a `numeralLarge` figure | `complete.turns` | `complete.turns` |
| Time | The same. Minutes and seconds, formatted by the locale; always left to right | `complete.time` | `complete.time` |
| Scan another cube | button, primary, thumb, block, trailing chevron | `complete.scanAnother` | `complete.scanAnother` |
| Review the solution | button, secondary, block, trailing chevron | `complete.review` | `complete.review` |
| Done | button, ghost | `common.done` | `complete.done` |
| My cube is not solved | button, ghost | `complete.notSolved` | `complete.notSolved` |
| Not-solved sheet | sheet | `complete.notSolved.title`, `complete.notSolved.body`, `solve.lost.rescan`, `common.close` | `complete.notSolvedSheet` |

"Method: Beginner" is dropped from here. The method is named once, in
**Review the solution**, for the people who want to know.

**Time** is from the first turn to the last, not counting time spent
paused, in the background or in the menu. It is how long the person spent
turning, which is the figure they would guess.

A screen reader hears, on entry: the word, then "62 turns", then the time
as a duration ("12 minutes 40 seconds").

Sound and haptics: three rising notes; a success pattern.

The solve is added to History, marked finished, when this screen opens.

## States

### `complete.default`

- **Scan another cube**: to `permission.ask` or `scan.starting`.
- **Review the solution**: to `solution.overview`.
- **Done**, and back: to Home. Back never returns into the finished solve.
- **My cube is not solved**: to `complete.not-solved`.

The app cannot see the real cube, so it cannot know the cube is solved;
it knows only that every turn was advanced. "Solved" describes the
picture. The last button is there for when the cube in the hand
disagrees.

### `complete.not-solved`

A sheet: `complete.notSolved.title`, `complete.notSolved.body`.

- **Scan the cube again** (primary): to the scan. The new solve starts
  from wherever the cube now is.
- **Close**: back to `complete.default`.

It blames a turn, not the person, and it does not ask them to work out
which.
