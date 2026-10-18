# 2026-10-17: can a cube of N x N be settled by two sides at once?

## Question

Six flat sides of a cube say what is on each side but not which way up each was held. For a 3x3
that leaves two cubes about one scramble in eighty, and the corner views of the scan
(`read_corner`, `fit_corner`) settle it. Above a 3x3 nothing did, and the cubes it matters for are
the ones most people try first: a cube a few turns from solved. A 2x2's flat sides make more than
one cube one time in four; the 4x4s and 5x5s recorded the same evening ended "can be put together
in more than one way" five times in twelve (the cubes had had a handful of turns). The plan's
answer was the review screen. The owner asked for a corner reader, so that a cube is scanned
without that.

## What was built

`read_corner(frame, size)` and `fit_corner_each(read, cubes, colours)` for any size from 2 to 6
(`kMaxCornerSize`); the 3x3's code is the case `size == 3` and its results are byte for byte what
they were (the recorded 3x3 sessions, 5 of 5 exact, and all 38 corner tests).

- **Lattices of N x N.** A grid laid from a blob and two neighbours drifts from the stickers as it
  goes out (a side at an angle changes its pitch across it): at 4x4 and above the grid is placed a
  ring at a time, each ring refit by least squares. A hypothesis that finds fewer than five of the
  nine around its origin is dropped, and so is one whose two axes differ in length by more than 4
  or turn the wrong way (the same grid transposed). A 3x3's search is unchanged.
- **Choosing the sides of a corner.** Two sides at much the same angle make one grid of 2N x N, and
  every window of it with two thirds of the cells is a lattice: a few hundred for a 6x6, the two
  true ones 36th and 300th by blobs. The cap of 24 windows is 512 above a 3x3, and a partition is
  weighed only if it has a pair that meets and a third that meets one of them (a third that meets
  neither changes nothing).
- **Fitting a cube.** A side is matched to a face of the candidate by its N x N colours (an even
  size has no centre to name it by), in each quarter-turn within 1.5 of the best; every shared
  edge must then be where the cube puts the two faces' common edge, with the stickers paired as
  the cube pairs them, on positions from the faces' own directions. A face's distance is found
  once for each distinct face (the cubes six sides make share most of theirs).
- **Counting.** `SideScan::add_corner` takes a corner view at any size: a side of it counts as a
  read of its side by the same rules (most of its stickers found as blobs, not a strip, held
  still), the share of the stickers rather than 7 of 9.

## Drawn tumbles

`cube_tumble_sim` now draws a scrambled cube of any size (`random --size N --moves M --seed S`),
turned about the vertical and then the horizontal at 60 degrees a second and 30 frames a second, in
perspective, with shading and noise; it writes the cube (`cube_n.txt`) so that `cube_scan board`
scores what the scan chose against the cube itself, up to the way it is held. Cubes four turns
from solved, 16 for each size:

| size | exact | wrong | no cube (sides not all captured, or still ambiguous) |
| --- | ---: | ---: | ---: |
| 4x4 | 15 | 0 | 1 |
| 5x5 | 16 | 0 | 0 |
| 6x6 | 9 | 0 | 7 (6 sides, and the corner views do not separate the last candidates) |
| 2x2 | 8 | 0 | 8 (fewer than six sides: a near-solved 2x2's sides are solid, and shading splits one) |

Nothing is wrong. What is missing is a cube, never a different cube: a choice among candidates is
made only when the best misses at most one edge and the next at least two more. A cube chosen
this way took 8 corner views at 4x4 and 5x5, the 8 corners of one tumble.

What a frame costs (a desktop, one thread, `cube_scan cost`): a corner read is 2 to 5 ms at 4x4,
6 to 12 at 5x5 and 12 to 35 at 6x6, paid only on frames with no side square to the camera. That
is why `kMaxCornerSize` is 6.

## Real cubes

The 4x4 and 5x5 recorded by lamplight (the cube held square, turned from side to side) hold one to
three corner views in a session, not the eight a tumble makes; of the six ambiguous sessions one
had corner reads at all (two), and both fit all four candidates: they showed no sticker the
candidates differ on. The reader
itself is shown on drawings only; whether it reads a 4x4 held corner-on by hand under a lamp is
not measured. The 3x3's own corner reads were marginal in the same light (docs/vision-pipeline.md).
The scan asks, when it is ambiguous, "Turn the cube slowly so that two sides show at once"; it
does not name a corner (`settling_corner` is a 3x3's).

## Finding the size from the cube

The scan screen is of a size the person chooses, or, by default, of the size the cube is found to be
(`DirectScan::detectedSize`). The reader of N x N reads a grid of fewer inside a bigger side and never a
bigger grid than there is: drawn sides of 2x2 to 6x6, 72 frames each, read at every size up to their own
(a 5x5 at 2, 3, 4 and 5, 72 of 72 each, and at 6 or 7 never). So the cube's size is the largest size
(2 to 5) a frame reads at, found by going up from the current size while it reads or down until it
does; the scan takes the largest that at least five of the last eight frames that read anything
read at, and stops looking once a side is captured. On the recorded sessions the largest size read at
is the session's for most frames (4x4: 137 of 153 frames that read at all in one, 5x5: 310 of 330 in
another; the rest read at a smaller size, a hand over some stickers), `cube_scan detect-size <session>`.
Choosing a size stands until Auto is chosen again. What is not covered: a cube held with its stickers
covered enough that it reads one size smaller on most frames of the first second reads as that size and is
then kept, which is what the buttons are for.

## Where the code is

`src/lib/src/vision/corner_view.cpp` (lattices), `corner_fit_n.cpp` (fit), `direct_scan.cpp`
(`SideScan::add_corner`, `assemble_n`); `tests/unit/corner_view_nxn_test.cpp`,
`tests/unit/direct_scan_controller_test.cpp`; `cube_tumble_sim <dir> random 60 30 --size 5
--moves 4 --seed 3` and `cube_scan board <dir>`.
