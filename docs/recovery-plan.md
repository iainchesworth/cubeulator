# Recovery plan

> **Status in 0.1.0-beta.1:** The recovery is built as far as scanning and
> solving: the side reader and assembly (`src/lib/include/cube/vision/direct_scan.hpp`),
> the scan screen (`DirectScanScreen.qml`) and the solve screen that follows
> the cube by camera are in the app, and the old tracking pipeline is gone
> (R6). Not built: the review and hand-entry screen (R3, so a cube cannot be
> entered without a camera that reads it), the live gate on fresh scrambles
> (R4, not run) and most of free handling (R5; only two sides in one view,
> which settles the cubes six flat sides cannot). The measurements below are
> from recordings that are not in the repository.

Status: **accepted 2026-10-13.** Supersedes the tracking-first plan
(`pipeline-plan.md`, removed from the tree in R6; it and its phase gates are
at the tag `archive/tracking-first`).

## Verdict

The camera input was never the problem. On the one real recording where all
six faces were shown (`live-demo-p6-retry`, 2026-08-07), a 250-line
throwaway script with no tracker and no learned model reads the complete
cube state. The current pipeline, on the same frames, cannot.

The project has been solving a much harder problem than the product needs —
continuous 6-DoF pose tracking of a rotationally symmetric object — as a
precondition for reading any sticker. The recovery is to read faces
directly, identify each by its own content, and put the tracker out of the
read path.

## What was measured

All on the real recording `D:\cubeulator-corpus\live-demo-p6-retry`
(799 frames, 640x360, warm indoor light). No true state was recorded for
it, so "correct" below means: exactly one physically valid cube survives
the 4,096 per-face roll combinations (a single wrong sticker breaks that),
and the reads match the frames by eye.

| reader | result |
|---|---|
| P6 tip, offline runner (`09133c5`) | det 100%, pose 100%, locked 97.6%, **17 epochs, 5/6 faces, cannot assemble** |
| spike (`tools/spikes/cubescan/cubescan.py`) | 533 steady reads, exactly 6 face groups, within-face spread 0.6-1.4 Lab, **1 valid cube of 4,096** |

The spike result is not balanced on a threshold: it returns the same cube
with its grouping threshold anywhere from 4 to 26, on every 5th frame, and
at 320x180 (`sweep.py`). About 15 ms per frame in Python.

On the earlier session (`live-demo-p6`, backlit by a window) the spike
reports *incomplete*, which is the right answer — the white face was never
shown square-on. That session exposed two real hazards that the plan below
has to handle:

- **Colour drift.** The red centre's b* moved from 23.6 to -1.9 during the
  session, further than the red/orange separation (~13). One face split
  into three groups.
- **A junk lattice.** For 10 frames (131-140) a 3x3 was fitted across two
  faces seen edge-on.

No frames from these sessions are in the repository: they show the person
holding the cube. `cube_scan board` reruns the measurement on a local copy.

## Progress

**2026-10-14.** R0, R2, R6 done; R1 started; R3 half done; R4 not run; R5(b) started.

- **R0.** Previous tip tagged `archive/tracking-first`. Webcam frames
  showing the user (`tests/data/captures`, `docs/evidence`) are removed from
  every unpushed commit; commits already on GitHub are untouched, so
  `develop` still fast-forwards. Commit hashes quoted in the phase gates and
  `pipeline-plan.md` (now only at `archive/tracking-first`) predate that
  rewrite. Originals are in the bundles
  under `D:\cubeulator-backup`, and the removed files are in
  `D:\cubeulator-corpus\legacy-capture-dumps`. The July recording corpus,
  recorded as lost, was still in the local LFS store: 6,035 frames in 14
  clips, now at `D:\cubeulator-corpus\july-corpus`. The corner-detector
  weights (AGPL-derived) were left out of the published history as well.
  Pushed: `develop`, `feature/direct-scan`, `feature/pipeline-p6-endtoend`,
  `feature/m3t-vcpkg-port` and the tag.
- **R1.** Four live sessions recorded in backlit daylight
  (`D:\cubeulator-corpus\direct-scan\20261013`). No cube was solved by
  hand to label them; instead three scans of the unchanged cube agree, and a
  fourth taken after one turn differs from them by exactly one quarter turn
  of the middle slice. Each session's cube is in its `truth.txt`. More
  lighting conditions and a deliberately scrambled cube are still to record.
  The July corpus scores 0 of 14, with almost no lattice reads: it is a
  different cube, light-bodied with flush tiles and thin pale gaps, and the
  reader looks for darker plastic between stickers. Many of those clips hold
  a side square-on, so it is the cube and not the handling.
- **R2.** `cube::vision::read_side` and `SideScan`
  (`src/lib/.../vision/direct_scan.*`) port the spike and feed the existing
  `resolve_colors`, `assemble_scanned_faces` and `solve`. `cube_scan board`
  (`tools/scan`) is the scoreboard:

  | session | light | result |
  |---|---|---|
  | `live-demo-p6-retry` (Aug) | tungsten | exact, cube known at frame 307 |
  | `20261013/s01`..`s04` | backlit daylight | exact, 4 of 4 |
  | `live-demo-p6` (Aug) | backlit | no cube - white never shown square-on |

  For all five readable sessions the solver's moves, applied to an
  independently built model of the cube, end solved.
- **Found on the way.**
  - Colour naming alone swaps red and orange by tungsten light (three
    degrees of hue apart). A cube with two colours exchanged is never valid,
    so `SideScan::cube` lets validity pick the naming.
  - The photometry lock cleared auto white balance and left the camera on a
    stored 4000 K preset, 12% bluer than the automatic's choice in daylight.
    Fixed: the lock now writes back the temperature the automatic chose and
    checks the picture did not move (0.2% on the workstation webcam).
  - The solve screen read its move total and stage list once, before the
    solve had finished, and never again. Fixed.
  - A cube one turn from solved can be assembled two ways from its six sides
    (the turned layer could be either of two opposite ones). Square-on reads
    cannot tell them apart; it needs two sides in one view, which is R5.
- **R3.** `DirectScan` and `DirectScanScreen.qml` are the app's scan screen:
  sticker dots, one chip per side read in its measured colours, no
  mirroring, cancel and start-over. A side being held fills the next chip
  as it counts; a finished scan stays on screen, confirmed, while the solve
  runs, then fades to the solution. Scans run through the app window by the
  user reach the solve screen ("the scanning is great"). Still to do: the
  review screen and hand entry, the solve screen's own UX, and R4's live
  gate has not been run.

**2026-10-13, the solve screen** (branch `feature/solve-guidance`, on top of
`feature/direct-scan`).

- **Moves are shown, not spelt.** The solve screen draws the whole cube in
  3D as it stands. The layer the next move turns nudges a little way in the
  direction it has to go and back - twice for a half turn - and stepping
  turns it all the way before the solution moves on. A move on the back
  swings the view round behind the cube first and says that it is the view
  that has turned; the left and bottom layers are shown from the usual
  viewpoint, where two of their three rows of stickers are in view.
- **The cube on screen is the cube in the hand.** The camera stays on after
  the scan. A side held square to it is matched against the cube as it
  should stand, which gives the hold - one of 24 - and the 3D cube turns to
  it; until a side has been seen, the stated hold stands (yellow up, green
  towards you). This is `read_side` again, not tracking: nothing is
  estimated between reads and nothing is read at an angle (that is R5).
- **The camera follows the solve.** The same read steps the solution on,
  with no button pressed, when the side in view fits the cube after the
  move asked for and clearly not the cube before it. When it fits the cube
  after a turn of some other side instead - or of the right side the wrong
  way - the screen names the side that was turned. The move on the side
  facing the holder is the one the camera never sees, and the buttons and
  keys still step whenever it does not.
- **Measured.** `cube_scan holds` replays a recorded scan against its own
  cube. On the five sessions that read, a steady read sits a median of 1.3
  to 1.7 from the hold it shows and nine in ten within 3.5; the nearest
  hold that would look different is never less than 4.9 further off. Red
  against orange is the narrowest pair: 7.8 apart by tungsten light, 14.5
  to 17.9 by daylight.
- **Fingers.** `read_side` needs six of the nine stickers and samples the
  other three where they must be - and a cube being solved has fingers on
  it, which read much like an orange sticker. Following the first live
  solve, where a correct move was now and then called wrong, the solve
  screen's matching counts only the stickers actually found
  (`SideRead::found`). On the same five recordings that took steady reads
  fitting no hold from 62 of 1,716 to 20. A mistake is now only called on a
  side seen whole, with at least two stickers plainly the wrong colour, held
  for most of a second; `CUBE_HELD_LOG` records what the camera made of each
  change, for the next time it gets one wrong. The scan itself still counts
  all nine.
- **Proved on what is drawn.** `cube_qml_tests` loads the real
  `Cube3DView.qml` and `SolveScreen.qml` and reads every sticker's place
  back out of the 3D scene: all 18 moves land where `Cube::apply` puts
  them, the nudge leans towards the move and not away, a whole solution
  steps to solved and back, and under all 24 holds the layer about to move
  is in view. Reversing a turn or dropping a side's flip fails it.
- **Not yet done.** Nobody has followed it through a real solve: the nudge's
  feel, the back view and the camera match are untried on a real cube. The
  matcher has only ever seen an unmoved cube in the recordings; none of
  them has a move made in view.
- **Found on the way.** The solver can ask for the same side twice in a row
  across a stage boundary (`F2 F2`), which undoes itself. Grabbing a window
  with the 3D cube in it a second time brings the process down (Qt 6.8.3,
  Direct3D); only the hidden gallery test grabs at all.

- **R6, first half.** Brought forward ahead of R4: nothing in the app had
  started the previous pipelines since the direct scan landed, and their
  untested code was what kept the coverage gate shut. Removed: the
  tracking pipeline (corner detector, pose from corners, the detect-track
  loop, pose refiner, face projection, track fusion), the vendored M3T
  tracker and its glew/glfw dependencies, the offline runner, and the
  previous scan controller and screen with the two settings that drove
  them.
- **R6, second half.** Removed: the classical cascade those sat on (grid
  detection, face, corner and edge observation, pose estimation, the
  orientation lattice and tracker, capture routing and gates, the idle gate,
  stability, auto-advance, the `vision.hpp` facade and the temporal fusion
  aggregator), the hand detector with onnxruntime, its palm model, the
  settings model and the inference-environment seam that existed for it, the
  legacy scan model (`ScanProgress`, colour scheme, `cube_from_scan`), the
  synthetic-corpus and labelling tools, the analysis scripts, and the old
  pipeline's plan, gates, metrics, trace and ground-truth documents. Their
  data-dependent tests went with them: each ran only against a local corpus
  and fed nothing back to the product. `cube_scan board` is the scoreboard.
  `docs/vision-pipeline.md` and `docs/recording-format.md` describe what is
  left.
- **R5(b), started.** About one scrambled cube in eighty (12 of 1000) is two
  cubes to six flat square-on sides, however well read. `read_corner` finds
  two or three lattices that touch in frames with no square-on side;
  `fit_corner` asks whether a candidate cube explains them (each side matches
  under a quarter-turn and the shared edges meet where the cube puts them);
  `SideScan` takes the candidate most corner reads fit. `cube_scan corners`
  scores it against truth: on the six two-side frames in the recorded
  sessions, 4 fit the true cube and rule out 17-18 of the 18 cubes one turn
  away; the 2 that did not were narrow strips. An ambiguous cube drawn with
  its corner views settles to the true cube through `SideScan` and through
  the app's controller, which says so (`ambiguous`) rather than "show each
  side again". Not done: a recording of a person turning a real ambiguous
  cube, so the end-to-end rate is unmeasured; reading three sides at once as
  a faster way to scan; R5(a), reading a side up to about 35 degrees off
  square-on.

- **Logo on a centre sticker.** A Rubik's cube's white centre carries a
  wordmark in four colours across its middle, where a sticker was sampled
  (median of a patch 0.44 of a spacing across). On recorded reads of such a
  side the centre sat 13 Lab units from the white stickers beside it, nearly
  all of it lightness (L 52 against 65-67 in the evening recording): a
  darker, pinker tile on the scan screen, though the cube still solved. Both
  readers now sample through `sticker_lab`: a patch 0.6 of a spacing across
  and the densest cluster of its pixels, not the median. Over 84 reads the
  centre is 2.0 from its neighbours; `cube_scan board` is unchanged (4 of 4
  exact) and held-cube matching is a little tighter (633 to 638 of 645 steady
  reads matched). The drawn tests cover a wordmark over 62% of the patch.
- **Three sides at a corner do not give the other three.** Asked whether a
  corner view's 27 stickers fix the 27 hidden ones: no. Enumerating every
  valid cube that agrees with the visible three sides, over random cubes,
  fixes 6 of the 27 hidden stickers (the three opposite centres and the third
  sticker of the three corners that have two showing), and a median of 7,680
  cubes still fit (1,152 to 17,280). Nothing can be filled in from it. What a
  corner view does give is three sides per frame, which is the faster scan
  that R5(b) names as not done.

## Why it does not work today

*As of the verdict above, 2026-10-13: this describes the pipeline that R6
has since removed from the app (see Progress).*

1. **The read path depends on a tracker that cannot know which face is
   which.** A cube looks identical under 24 rotations, so pose alone never
   yields face identity. Gauges, epochs, stitching, registration,
   sub-epochs and transplants all exist to rebuild identity from sticker
   content — which gives identity directly, with no pose at all. The
   tracker is also weakest face-on (a square silhouette under-constrains
   pose), which is how people naturally show a cube.
2. **Eleven serial fail-closed gates** stand between a frame and the result
   in track mode. Several bars are described in their own comments as thin
   or unswept (`track_fusion.hpp:120`, `detect_track_loop.hpp:114`,
   `capture_routing.hpp:246`). The product of the pass rates is close to
   zero, and it fails silently.
3. **Phases 2-6 were gated on synthetic renders and a cardboard box.** The
   real corpus was lost on 2026-07-30 and the real cube was not to hand.
   Both "exact-state matches" are synthetic clips. No real session has ever
   completed, and no real session has a recorded true state.
4. **The scan screen misreports.** In the default mode `cubeDetected` can
   never be true (`scan_session.cpp:925`; the observations are only
   assigned after the track-mode early return at `:2048`), so the screen
   shows "Looking for the cube…" permanently, even at 97.6% locked. Face
   chips, the capture flash and confidence bars read stores that are never
   fed. Failure reasons are computed and discarded. There is no timeout,
   cancel, manual correction or hand entry, and a solver failure shows a
   solved cube with zero moves.
5. **It could not ship as built.** The tracker compiles only on desktop;
   Android and iOS cameras are stubs; the detector weights are
   AGPL-derived (`assets/models/cube_corner_pose_PROVENANCE.md`).

The Jul 26 handoff (`handoff-grid-fit.md`, at `archive/tracking-first`)
already recommended square-on per-face capture from the daylight overlays.
The Jul 30 plan went the other way.

## Keep and remove

| | lines | fate |
|---|---|---|
| solver, cube model | 2,450 | keep |
| assembly (`assemble_scanned_faces`, 4,096-roll search) | 975 | keep — the spike re-implements it; use the tested one |
| colour (`resolve_colors`, `FaceAggregator`) | 1,235 | keep |
| recording, replay, ground truth | 1,650 | keep |
| legacy cascade | 9,430 | remove after R4 |
| track pipeline + M3T adapter | 4,090 | remove from the read path after R4 |
| `ScanSession` | 3,490 | rewrite to a fraction of this |

Everything removed stays reachable under an archive tag.

*Done: the cascade, the track pipeline and the M3T adapter were removed in R6,
brought forward ahead of R4 (see Progress); `ScanSession` is gone and
`DirectScan` replaced it.*

## Milestones

One gate from here on: **real recorded sessions that end in the exact true
state.** No synthetic clip counts toward a gate.

**R0 — Make it safe.** 207 commits exist only on this machine;
`origin/develop` stops at 2026-07-18. Push every branch, tag the P6 tip
`archive/tracking-first`, and copy `D:\cubeulator-corpus` to a second
location.

**R1 — A real scoreboard.** Record 8-10 sessions with a known state: start
solved, apply a written scramble (the state is computed, nothing to label),
then scan as you naturally would. Vary lamp / daylight / window behind,
near / far, and face-by-face versus tumbling. One command prints sessions
solved out of total for any reader. Row zero is the P6 runner; row one is
the spike.

**R2 — Face reader in C++.** Port the spike as two small units:
`face_reader` (sticker blobs → 3x3 lattice → nine Lab samples; OpenCV core
and imgproc only) and `scan_accumulator` (group reads by whole-face
signature with roll alignment, per-face median, which centres are still
missing). Feed the existing `resolve_colors`, `assemble_scanned_faces` and
`solve`. Handle the two measured hazards: lock exposure and white balance
before the first read and take each face from its best dwell rather than
the whole session; require a full nine-blob lattice for a face's first
read. Gate: at least 90% of R1 sessions exact offline, and no wrong state
ever accepted.

**R3 — An honest scan screen and a review screen** (alongside R2). Draw the
lattice on the face being read. Six chips keyed by centre colour, each
showing the 3x3 actually read. The prompt names the missing face by colour.
Then a review net of all 54 stickers: tap to correct, a validity light, and
Solve. That screen is also hand entry. Add cancel, a hint after a few
seconds without progress, and a real error when the solver fails.

**R4 — The live gate.** Five fresh scrambles at the desk, evening and
daylight. Pass: four of five go from camera to correct solve steps with no
manual fix, five of five with one. A failure joins the corpus only if
recording was on for that scan. Recording is a developer opt-in: the app
stores nothing unless `CUBE_SCAN_RECORD_DIR` names a folder, and the scan
screen says so whenever it is recording.

**R5 — Free handling.** Only after R4, in steps, each scored on the corpus:
(a) no deliberate pause — read any frame where a face is within about 35°
of square-on, using a homography; (b) two or three faces per frame from a
corner-on view; (c) only if those fall short, the existing corner detector
as a face-quad proposer, once its licence is resolved. A pose tracker
returns only to drive the cosmetic 3D mirror cube, never the read.

**R6 — Demolition.** Remove the legacy cascade, the tracking read path, the
M3T and onnxruntime dependencies and the environment-variable modes. Merge
to `develop`, then `main`. The demolition is on `develop` (see Progress).

## What is not known

- **One complete real session.** The spike was written after looking at it.
  R1 exists to find out how far the result generalises before more code is
  written.
- **Hard light.** Backlit and daylight-glare conditions are unproven and
  the measured drift is larger than the red/orange gap. The review screen
  is the backstop.
- **Other cubes.** The blob step relies on dark plastic between stickers.
  Stickerless and white-bodied cubes are out of scope until asked for - and
  measured: the July corpus is such a cube and reads 0 of 14. It is also
  the test data for that work when it is wanted.
- **R5 is where the earlier approach died.** It may land only partly. The
  difference is that the product works without it.
