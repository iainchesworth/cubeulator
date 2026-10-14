# Why is a side near the edge of the square-on gate unread beside a much thinner one?

**Asked:** 2026-10-15. **Closed:** 2026-10-15. **By:** an agent working for the
maintainer, from the band in [the tumble investigation](2026-10-14-scan-tumble.md)
that was "noted, not changed": a drawn tumble with one side at 20 to 30 degrees
and another at 60 to 70, where `read_side` returns nothing and so does
`read_corner`, and the guess that the thin side's stickers beside the near one
broke the lattice fit (`neighbours_of` takes blobs within 3.4 spacings whichever
side they belong to).

## What was tried

Against `develop` at `e1f670b` (which by then had the tumble change and a
second look of its own in `read_side`: the outlines taken again against a
narrower block, for dark stickers beside a bright one, held to a plastic-gap
test). The tumble of that page, drawn again: `cube_tumble_sim <out> "U:YYWYYYWWY
D:YWWWWWWYY L:RRRRRRRRR R:OOOOOOOOO F:BBGGGGGGB B:BBGBBBGGB" 60 30`, 396 frames,
replayed with `cube_scan funnel -v` and joined to `pose.csv`. For each frame
`read_side` said nothing about, a scratch tool that includes `direct_scan.cpp`
whole (its internals are in an anonymous namespace; not committed) dumped every
contour with its shape test, the blobs, each seed's neighbours, how many of
them snapped to the grid, and the fit. The same dump, and a general lattice fit
that does not assume a square grid, over the five recorded sessions the board
scores (`s01`-`s04` and `live-demo-p6-retry`, 3,360 frames, numbers only). And
corner views drawn with `draw_cube_corner`, swept over the near side's column
step (0.80 to 0.95 of the row step), the shear (`tilt` 0 to 6 pixels a column),
the pitch (40 and 56) and the second side's width, over six cubes.

## What it found

**The thin side's stickers were never in the fit.** In every frame of the band
the blob set was the near side's nine stickers or fewer and nothing else. The far
side's stickers, at 65 to 73 degrees, are 3.3 to 4.1 to one, and
`sticker_blobs` drops everything past 1.45 to one before a lattice is looked for.
(Two perpendicular sides seen along one line have angles that sum to at least
90, and that cut is 46 degrees, so a near side inside the gate, 32 degrees or
less, has a far side past 58. Perspective moves each by the angle between the
lines of sight to the two centres, a few degrees for a cube at arm's length.)

**The near side is not inside the gate in most of the band.** `pose.csv` is the
angle to the line of sight to each side's centre, and the two angles of a pair
sum to 103 to 104 degrees here, not 90: a far side at 65 to 69 has its near side
at 33 to 39. A general fit gives those frames an axis ratio of 0.79 to 0.85
(cosine 0.04 to 0.10, rms 0.05 of a step). The gate is 0.85, by design, and
refuses them. A near side at 20 to 30 degrees goes with a far side past 71.

**What did go wrong is the first try at placing the blobs, in the frames where
the near side is inside the gate.** 15 of the 72 frames neither reader read had
nine blobs, a general fit of ratio 0.856 to 0.887, cosine under 0.09 and rms
under 0.05: everything the gate asks for. `grid_frame` takes the median
nearest-neighbour distance as the pitch of a *square* grid and `snap_to_grid`
lets a cell be 0.3 off. A side narrower one way has its third column
2/ratio spacings from the first, which is 0.3 off at a ratio of 0.87; five or six
of nine snapped and the seed was dropped ("not 3x3"). A shear does the same from
the other axis (a cosine of 0.15 is inside the gate and a third of a cell off by
the third column; in drawings the shear cases at ratio 0.90 to 0.96 failed too).
So the reader took sides to about 0.87 to 0.89 in a perspective view, not the
0.85 it documents, and erratically between 0.82 and 0.89: six frames of this
tumble were read at a true ratio of 0.82 to 0.84 because a partial snap of six
points happened to fit a ratio over 0.85. Those are not changed.

**On the recordings it is 1.2% of frames** (40 of 3,360 had no read from the
reader the tumble change left, while a general lattice passed the gates): the
same mechanism at a ratio of 0.85 to 0.89, and lattices of six or seven blobs,
which this change does not take.

**Contamination by a second side is real and narrow.** In drawings, a second
side whose stickers are within the blob band (a column step past 0.8 of the
row's) beside a near side at the gate's edge stopped the read: the nearest-neighbour
pitch is of both, and the cells that did snap spanned more than three, and the
seed was dropped. A near side clearly inside the gate (0.9 and over)
read in six of six cubes either way; one cube of six failed at 0.9 beside 0.8.

**A tempting fix reads a lattice that is not there.** Taking the best lattice
that passes the gate, from windows placed through their own fit, read a drawn
side at a column step of 0.8, past the gate, as ratio 0.852: two columns of the
near side and the merged column of the thin side beside it (its three narrow
columns are one square blob a row, at 78 degrees and over). The near side, at
0.80, fitted tighter and was refused. A candidate that shares a blob with a
better-fitting one that was refused is not looked at.

## Decision

Done, in the change that carries this page: `read_blobs` is unchanged when its
first try finds a side held square, and byte for byte, so every read that was
made is made. When it does not, the blobs are placed again
(`fit_lattice_through_fit`): every 3x3 window of the lattice that the blobs
already on the square grid make, from each seed's own neighbours, with at least
eight blobs (six or seven fit anything through six parameters); candidates are
weighed as before, the most blobs and then the tightest, and one that shares a
blob with a better one that fails the gate is not looked at. The gate itself is
the same. It is in `read_blobs`, so both of `read_side`'s looks have it, and a
frame it reads in the first is not taken to the second.

| | develop | after |
| --- | ---: | ---: |
| tumble: far side 70 to 75, read square-on | 21 of 40 | 36 of 40 |
| tumble: far side 65 to 69, read square-on | 2 of 38 | 2 of 38 |
| tumble: frames neither reader reads | 72 | 57 |
| tumble: frames that pass the gate and are not read | 15 | 0 |
| recorded reads: `live-demo-p6-retry`, `s01` to `s04` | 626, 791, 233, 244, 224 | +1, +3, +5, +1, +1 |
| recorded sessions exact, cube known at the same frame | 5 of 5 | 5 of 5 |
| reads lost, groups started | - | 0, 0 |
| drawn poses inside the gate that read (of 128) | 97 | 126 |
| `read_side`, a side held square | 393 to 783 µs | 402 to 781 µs |
| `read_side`, nine blobs and no side (both looks) | 1,104 µs | 1,160 µs |
| `read_side`, a side it now reads | 1,213 µs, nothing | 2,563 µs |

(Times are medians of alternating runs at high priority on a machine other
sessions were building on: a read of a side held square comes in two modes,
about 400 and 780 µs, in both builds.) The 11 reads gained on the recordings
are of nine stickers (one of eight), 3 to 10
Lab units from a side the scan held; two, at 19 and 18.5, are the second and
third frames of a session under the camera's own exposure. A first version
without the eight-sticker floor also took six and seven, and two of those three
were 21 and 34 units from every side.

**Not done.** The 57 frames still read by neither reader (near side 33 to 42
degrees, ratio 0.76 to 0.85, far side 63 to 73) are outside the gate by design,
and a general fit gives each of them a lattice that is otherwise clean: cosine
0.10 at most, rms 0.06 at most, seven to nine blobs. A wider gate would read
them. The sampled patch is 0.6 of a spacing (`kPatchHalfWidth`), "well inside
the sticker for a side up to 15% off square", and a side at 40 degrees is lit
differently from the same side square-on (the stray sides of the tumble
investigation, 4); neither has been measured on a real tumble, and which gate is
right is the maintainer's to say.

## Where the code is

`bugfix/square-on-gate-edge`, in the pull request that carries this page.
`src/lib/src/vision/direct_scan.cpp`: `windows_through_fit`,
`fit_lattice_through_fit`, `is_square_on`; tests in
`tests/unit/corner_view_test.cpp` (each was removed in turn and a test failed:
the pass itself, the placement through the fit, the 3x3 window, the overlap
rule, the gate inside the pass, and the eight-blob floor) and a metric each for
a side just past the gate and a side at its edge in the performance tier.
