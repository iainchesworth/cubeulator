# Does the scan catch a cube turned slowly about one axis and then another?

**Asked:** 2026-10-14. **Closed:** 2026-10-14. **By:** an agent working for the
maintainer, from a report that the browser build said "the six sides fit
together in more than one way" however slowly the cube was tumbled.

## What was tried

The report: a real, mixed-up cube, six sides read correctly with their colours,
and the scan stuck on `ambiguous_scan` while the cube was turned to show two
sides at once, about one axis and then another. The question was whether the
reader loses the tumble, and where.

Data, none of it showing anyone's face in the repository:

- **Two real recordings** (kept outside the repository, `CUBE_SCAN_RECORD_DIR`):
  a patterned cube held tilted in one hand under an evening lamp, 480 frames,
  six sides that make 20 cubes; and a red/orange cube, 1,449 frames, six sides
  that make 32. Neither is a tumble: the first never shows the sides that tell
  its 20 cubes apart, the second has no frame with two sides in view.
- **Synthetic tumbles** with a pose known to the degree: `cube_tumble_sim`
  (new, `tools/scan`) draws a cube turned 360 degrees about the vertical and
  then 360 about the horizontal, at a speed and frame rate asked for, with
  optional blur and exposure drift, and writes it as a recorded session with
  `index.csv`, `layout.txt` (the cube it is) and `pose.csv` (each side's angle
  to the camera, per frame). The two real cubes above were drawn at 30, 60 and
  120 degrees a second, at 30 and 10 frames a second, with and without a +-15%
  exposure swing over seven seconds: 24 sessions. 63 more cubes whose six flat
  sides make 2 to 112 cubes (the 18 cubes one turn from solved, 20 two-turn
  cubes, 25 random 25-move scrambles) were tumbled at 60 degrees a second,
  30 frames a second.
- `cube_scan funnel <session>` (new) replays a session as the app's own loop
  does and says how far each frame with no square-on side got in the corner
  reader, which gate stopped each side of a read, and what the scan decided.

The synthetic tumbles are a drawing: one lamp, flat colours, no glare, no hand.
They say what the reader does with geometry, never with a room.

## What it found

The shipped scan read **no corner view at all in any of the 24 tumbles**, and
reached `ambiguous_scan` or no cube in all 24 (the six sides were captured in
16 of them; in the other eight, all at 10 frames a second, at most one side
was, see 6). Of the 63 cubes, it settled 7, each exactly, where a lopsided view
happened to be read, and left 56 ambiguous. It was not one fault but six, in a
row.

### 1. The reader took a window across the edge of two sides (the main cause)

Two sides turned to much the same angle - the middle of every quarter-turn of a
tumble - look like one continuous grid: the columns step by about the same
amount on either side of their shared edge. A 3x3 window across the edge holds
as many blobs as either side and fits as well (rms 0.044 of a step, where the
gate is 0.14). `find_lattices` took the window with the most blobs, then the
tightest, set its blobs aside and searched what was left: the window across the
edge went first and left six blobs and three, which make no lattice. A frame
with 18 sticker blobs and two sides in view read as one lattice and no corner.

A cube tumbled at 60 degrees a second, 30 frames a second, with two sides in
view (`cube_scan funnel`, with `pose.csv` for the angles); the shipped reader
read none of these:

| the more oblique side, degrees from square | frames | read square-on | corner read | neither |
| --- | ---: | ---: | ---: | ---: |
| 50 to 54 | 20 | 0 | 20 | 0 |
| 55 to 59 | 40 | 0 | 40 | 0 |
| 60 to 64 | 34 | 0 | 17 | 17 |
| 65 to 69 | 38 | 2 | 0 | 36 |
| 70 to 74 | 40 | 21 | 0 | 19 |
| 75 or more | 56 | 56 | 0 | 0 |

Square-on reads are taken to about 30 degrees (axis ratio 0.85); the corner
reader loses a side at about 63 (a sticker foreshortened to 2.4:1 is no longer
a blob). Between those, where neither side is within 30 degrees, is a third of
every quarter-turn, and that is where the pair views are: about ten frames a
pair at 60 degrees a second and 30 frames a second. Beside it is a band, with
the more oblique side at 60 to 70 degrees and the other at 20 to 30, in which
neither reader reads anything (36 of 38 frames at 65 to 69): the near side is
within the square-on gate, but with the far side's thin stickers beside it the
lattice `read_side` wants is not found. A side is read square-on a little
later in the turn for it. It costs reads, not the cube; it is noted, not
changed.

> **Corrected 2026-10-15** ([the dead-band
> investigation](2026-10-15-square-on-dead-band.md)). The far side's stickers
> are not in the fit at all: past 1.45 to one they are not blobs. And in most of
> that band the near side is not inside the gate: the two angles of a pair sum
> to 103 to 104 degrees, not 90, so a far side at 65 to 69 has its near side at
> 33 to 39 and a ratio of 0.79 to 0.85. What was lost inside the gate (15 of
> the 72 frames) was the square grid `read_side` lays first, which cannot hold
> a side narrower one way past about 0.87; it is read now, by placing the blobs through their own fit.

On the tilted-hand recording the same cause, less often: of 345 frames that went
to the corner reader, 35 were read (all two-sided), 161 had fewer than 12
blobs, 115 one lattice, 34 two lattices that did not share an edge.

### 2. The newest 48 corner reads are not the views of a tumble

`SideScan` kept the newest 48 corner reads and the scan chose the cube that most
of them fit, if one did more than any other. A tumble at 30 degrees a second
gave 156 reads of 12 views: the true cube fitted 151, its nearest rival 78;
with only the newest 48 both fitted all 48 (the last views are of sides they
agree on), a tie, ambiguous for ever. A slower tumble is worse, not better.
Reads are now kept one per view (the same sides seen again, from the
neighbouring angle, replace the first if they are better), bounded at 64.

### 3. Sixteen candidates

`assemble_all_scanned_faces` was asked for its default sixteen. The two real
cubes make 20 and 32; with the true one at random among them it is outside the
sixteen one time in five and one time in two, and then no corner read can fit
it. Of the 63 test cubes, two make 112. The cap is 256, and a scan with more
than that is not chosen among.

### 4. A side seen at an angle is a side of its own

A side seen at an angle is lit differently from the same side square-on: in one
recording the same orange read at lightness 14, 22 and 31; in a drawn tumble a
second group began for each of three sides (12 groups for six sides). Such a
group reaches `kMinReads` and fills a slot, and a scan with four real sides and
two strays is "six sides" that make no cube: the screen says "These six sides
do not make a valid cube yet", and the verdict costs up to 150 ms of native
time (15 name exchanges of 4,096 assemblies) on every frame - several times
that in WebAssembly, which was not measured. A corner read that fits no side,
but is one of them lit differently under the loose measure (`colour_distance`,
mean over nine stickers within 12), is no longer counted, and starts no side.

### 5. A winner on three views

With the reader fixed, the tilted-hand recording was settled at frame 270 by a
candidate that fitted 3 of 7 views and its nearest rival 2: a single noisy view
decided, and the cube was not that one (it fitted 8 of the recording's 67
reads, where six others fitted 46 to 50).
The scan now chooses a candidate only if it misses at most one edge's worth of
the views that fit any cube and the next misses at least two more. (A read that
fits no cube is a blurred frame, and votes for no one; a three-sided read weighs
its three edges.)

### 6. "Held still" is a distance per frame

The gate on a counted read is that the centre moved less than 0.12 of a sticker
spacing since the frame before. A side turned about the cube's middle has its
centre 1.5 spacings from the axis, so that is 4.6 degrees a frame: 137 degrees a
second at 30 frames a second, 100 at 22 (the two real recordings ran at 22, with
a 64 to 66 ms 90th percentile and gaps of 240 to 330 ms), and 46 at 10. A
tumble of 60 degrees a second at 10 frames a second captured one side of six.
The blur that the gate is for is the same at 10 frames a second as at 30; the
allowance now grows with the time since the previous frame (to four times),
from the timestamp the controller has.

### What did not matter, and what is not known

- **The shared edge as a constraint on the pair's relative roll** shrinks the
  candidate set no further than `fit_corner` does: it tests exactly that (the
  turn a side's colours imply must carry the shared edge to the neighbour's
  side), per candidate cube, on the cube's own 3D sticker positions. With every
  read kept, ranking was unambiguous (61 against 36; 151 against 78). A
  candidate-free chaining of the rolls would be the same information stored
  differently; it was not built.
- **Exposure drift.** A +-15% swing over seven seconds, which an unlocked
  browser camera can make, breaks naming on the scan: 3 of the 12 drifting
  tumbles give no cube (two at 120 degrees a second, 10 frames a second, where
  the reads are too few whatever the light, and one "not physically valid", at
  120 degrees a second and 30 frames a second). Whether a real browser camera
  drifts by that much during a tumble is **not measured**: the browser's OpenCV
  has no JPEG encoder, so the page records no frames, and the diagnostics
  channel is a file in the page's memory. Measure it before blaming it. A way
  to lock it from the page is
  `MediaStreamTrack.applyConstraints` with `exposureMode` and
  `whiteBalanceMode` set to `manual`, where the camera supports them; that is
  the web platform's, and was not done here. It is now (see
  [Differences from the desktop apps](../web.md#differences-from-the-desktop-apps)),
  and the drift itself is still unmeasured: `tools/web/camera-photometry-probe.html`
  is how ([Testing](../testing.md#the-cameras-exposure-and-white-balance)).
- **Three-sided views.** None was seen in any real recording; the blob step
  drops dark stickers beside a bright side (see `docs/vision-pipeline.md`), and
  the reader's own preparation is still the next step. The synthetic tumbles are
  two-sided because a cube turned about one axis shows two.

## Decision

Done, in the same change as this page: the six changes above, with a test for
each (`tests/unit/corner_view_test.cpp`; each was removed in turn and its test
failed), and `cube_scan funnel`, `cube_tumble_sim` and `cube_scan` reading
`index.csv` and `layout.txt` so that the finding can be repeated.

On the 24 tumbles: 19 are read exactly, none wrongly, 5 give no cube (four at
120 degrees a second and 10 frames a second, which is under 8 steady reads of
any side; one with drift). On the 63 cubes: 63 of 63 exactly at 60 degrees a
second. The scoreboard is as it was (s01-s04 4/4 exact, the live demo MATCH),
the tilted-hand recording is still ambiguous - correctly, the views that tell
its 20 cubes apart were never shown - and still asks for the corner where the
yellow, green and red sides meet.

**In the browser build**, which is where it was reported: the same drawn
tumbles played to the page's camera at their frame times
(`tests/web/specs/tumble.spec.mjs`, the diagnostics build in Chromium on a
software renderer, 28 frames a second reached the scan). The build from
`develop` ends the 60 degrees a second tumble of either cube with six sides
read, `ambiguous` and `settlingCorner` yellow, green, red - the screen the
report describes. The changed build completes it at 10.7 s, and also the 30 and
120 degrees a second tumbles (20 s, 6 s) and the +-15% drifting one (11 s); the
120 degrees a second tumble at 10 frames a second does not, as natively.

Open, not decided here:

- **A real tumble.** The finding rests on drawings; no real recording is one.
  What is missing, for each of a daylight take and a lamp take: a cube turned
  slowly (30 to 60 degrees a second) about one axis and then another with two
  sides in view throughout; both opposite corners of the patterned cube held
  for a second each (a three-sided view, which has never been recorded); and a
  take in the browser build with the diagnostics channel on (`Ctrl+Shift+D`),
  to put numbers on the exposure drift.
- **The cost of a verdict of "six sides, no cube"** (up to 150 ms, every frame)
  was still there for a scan that really has a misread side. Closed afterwards,
  in a change of its own: a failed assembly is kept and made again only when
  the names given to the stickers change (or, for sides that make several
  cubes, the corner reads or the colours they are fitted in); see
  [the vision pipeline](../vision-pipeline.md). A guard on how far the colours
  had moved was tried first and changed two verdicts, which names did not.

## Where the code is

`bugfix/scan-tumble`, merged by the pull request that carries this page. The
tools, to repeat it:

```sh
cube_tumble_sim <out-dir> "<layout>" <deg/s> <fps> [--yaw --pitch --tilt --blur --drift --shade --seed]
cube_scan funnel <out-dir> [-v]                 # what the scan did, and where each frame stopped
cube_scan replay <out-dir>                      # the cube, if it was known
TUMBLE_SESSION=<out-dir> npx playwright test specs/tumble.spec.mjs   # in tests/web, against a diagnostics build
```

A layout is a line of `cube_scan replay` output for a cube; the two real
cubes' are `U:YYWYYYWWY D:YWWWWWWYY L:RRRRRRRRR R:OOOOOOOOO F:BBGGGGGGB
B:BBGBBBGGB` and `U:WWYWYYWYY D:YWYWWYWYW L:OORORRORR R:RROROOROO F:GBGBGBBGB
B:GGGGBGBBB`.
