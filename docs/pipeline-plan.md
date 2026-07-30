# The tracking-first pipeline plan

This is the governing plan for restructuring the vision system from
"face-first" (every frame independently hunts for a sticker grid via colour
masks) to "tracking-first" (detect and 6-DoF-track the cube **body**,
content-agnostically, and derive faces, cells and colours from the locked
pose). It replaces ad-hoc iteration with a phased pipeline where **no phase
begins until the previous phase's exit gate is passed on real footage**.

Read [handoff-grid-fit.md](handoff-grid-fit.md) first if you haven't — the
measured history there is binding on this plan. This document was reviewed
adversarially before adoption; where a gate looks pedantic, it is because a
looser version was shown to be vacuous, circular, or blocked on
out-of-order work.

## Why invert the pipeline — the evidence

Every root cause diagnosed over the last weeks is a consequence of the
current ordering, in which sticker-colour masks bootstrap everything and
pose is a downstream by-product:

1. **Nothing locates the cube body.** All three detectors
   (`observe_face`/`observe_corner`/`observe_edge`) start from
   colour/brightness candidate contours; pose correspondences are derived
   from the fitted sticker lattice, never the reverse
   (`face_observation.cpp`, `pose_estimation.cpp`). The one silhouette
   fallback (`outline_transform`) is a last-ditch hack.
2. **Every frame re-fits from scratch.** No ROI, no pitch prior, no
   translation prior; `observe_face` cannot even accept a hint. The
   previous frame's accepted pose never constrains the next frame's
   detection. This is the mechanism behind the **systematic per-cell
   mislocalisation** (live5 dominant reads ≥4 stickers from solvable —
   fusion cannot average out a consistent bias) and the per-frame grid-fit
   jitter identified as the free-rotation blocker.
3. **The gate stack exists to compensate — and the registration layer is
   the live blocker.** The 24-element gauge resolution, basin/margin/omega
   gates, epoch churn (25 epochs on live1), registration bars, and the
   completion/phase-plausibility machinery all exist because each frame
   arrives with unknown identity and unknown translation. The freshest
   evidence (2026-07-30 daylight replay log): the tracker was locked
   427/527 passes, yet coverage stuck at 1–2 slots because
   **`register_epoch_gauge` refused everything** — `reg=insufficient`
   scores 11.07–13.22 vs the 6.0 bar, `reg=ambiguous` roll ties 38.9–48.3
   vs the 20.0 bar, across 8 epochs. `RoutingConfig`'s bars were never
   swept because no routing replay harness exists
   (`capture_routing.hpp:246`). Tracking-first attacks the epoch churn
   that generates this traffic, but the bars themselves must also be
   re-derived, with tooling — see Phases 0 and 6.
4. **Geometry is not fundamentally broken.** Square-on, still,
   neutrally-lit frames place the grid correctly (7/9 cells classify,
   daylight overlays). What's missing is a pose that stays locked while
   the cube moves — exactly what a tracker provides and a per-frame fitter
   cannot.
5. **The user-visible requirement is tracking.** The product goal is: hold
   the cube anywhere in frame and see it locked — wireframe pinned to the
   physical cube, the rendered 3D cube mirroring its motion. That is a
   tracking contract, not a detection contract, and it is gated
   numerically in Phase 2 (including live, on screen), not just eyeballed.

What this plan is **not**: it is not another colour-side fix (those moved
census, never assembly distance), not a parallel classifier
(SessionColorModel lesson: live path only), and not a re-litigation of the
proven facts in the handoff.

## The pipeline

Discrete stages, each a typed transform with its own trace record, overlay
layer, metrics, and tests. Data flows one way; no stage reaches around
another.

```
A. Acquire   camera/replay → FramePacket {bgr, t_ms, seq, intrinsics}
B. Detect    FramePacket → CubeDetection {silhouette, corner candidates, roi, confidence}
C. Track     CubeDetection + history → TrackState {SE3 pose, quality, state, epoch, velocity}
D. Faces     TrackState → FaceSet {per visible face: body-face id, quad, frontal°, rectified image}
E. Cells     FaceSet → CellGrid {per face: 9 cell quads, sub-pixel refinement, quality}
F. Sample    CellGrid → CellSamples {per cell: Lab median, purity, MAD}
G. Classify  CellSamples + session context → FaceReads {provisional colours, confidence}
H. Fuse      FaceReads keyed by tracked identity → ScanState {slots, fused faces}
I. Assemble  ScanState → resolve_colors → assemble (+RotationHint tie-break) → validated Cube → solve
```

Contracts that hold at every stage:

- **Typed I/O.** Each stage's input/output is a plain struct in
  `src/lib/src/vision/pipeline/` (Qt-free). No stage consumes another's
  internals. The structs, the trace schema, and **legacy adapter shims**
  (wrapping today's interleaved `observe_*` cascade into the B–E stage
  shape so the runner can replay the current pipeline) are Phase 0
  deliverables — nothing here is assumed to pre-exist.
- **Trace everything, enforced.** Each stage appends a record to a
  per-frame `PipelineTrace`; the offline runner serialises traces as JSONL
  (schema committed in Phase 0) and burns per-stage overlays into an
  annotated video. The runner has a self-check test that **fails** on any
  frame missing a per-stage trace record — discreteness is a test, not an
  aspiration.
- **One image stream.** Stages B–E consume the analysis-scale stream —
  the same downscale the live app analyses — in both live and replay, so
  gates measure the shipping path. Full-resolution frames are recorded
  alongside for labelling and future work, never silently substituted.
- **Deterministic replay.** Same recording in → same traces out. Pin the
  OpenCV RNG for RANSAC; the offline runner (not the QML app) is the
  acceptance surface, because in-app replay drops frames under load.
- **Timing per stage,** recorded in the trace. Workstation CPU/GPU budget
  is generous for now; the numbers exist so the later phone optimisation
  is measurement, not archaeology.

## Ground rules (apply to every phase)

- **Exit gates are hard.** A phase is done when its gate criteria pass on
  the corpus, are written up in the phase's doc section, and the gate tests
  are merged. Not before. If a gate cannot be met, the honest,
  measurement-backed "cannot" plus a revised plan is also a valid exit.
- **Every behavioural test must fail pre-implementation** — including gate
  criteria: a gate the legacy pipeline already passes is not a gate.
- **Pixel-error gates are scored on hand-clicked keyframes only.** The
  labelling tool may interpolate between keyframes as a convenience, but
  an interpolated frame does not count as labelled until per-frame
  corrected — otherwise tight gates measure interpolation error, not
  detector error.
- **Percentile discipline.** Every distribution gate carries a median AND
  a tail bound (p95 and/or max), evaluated **per clip** (a good clip must
  not average out a bad one).
- **The end-to-end metric is assembly-probe distance** (`[.assembly-probe]`),
  strengthened where the corpus allows it to **exact-state match** (new
  clips are recorded with the cube's true state in the manifest, so the
  scan must reproduce *that* cube, not just *a* solvable cube). Census and
  cross-frame consistency remain diagnostics only — both have lied.
- **No corpus-tuned constants.** A fix is a principled rule. Regressions
  are checked against the full clip table, which the runner emits as one
  machine-readable report.
- **Live path only.** Anything added must be load-bearing in the shipping
  pipeline; no parallel shadow implementations.
- **Gitflow.** One `feature/pipeline-p<N>-<name>` branch (and worktree) per
  phase, merged to `develop` at gate. Later-phase *preparation* that
  doesn't depend on an unpassed gate (notably: synthetic training-data
  generation) may and should overlap.
- **UI contract is preserved.** `ScanSession`'s Q_PROPERTY/signal surface
  (`scanCompleted`, `scanNeedsRetry(reason)`, `scanInvalidated(reason)`,
  slot semantics, mirrored-preview isolation) keeps its semantics
  throughout; phases swap what feeds it, never what it means. Reflections
  stay display-side — captured cell order must never see the mirror.

---

## Phase 0 — Foundations: corpus, ground truth, observability spine

**Goal:** the instruments exist before the experiments. Every later gate is
measured by tooling built here; every later gate's denominator is defined
here.

**The corpus problem, stated plainly:** the replayable recordings
(`rec_%06d.jpg` + `index.csv` per clip) are **absent from this machine** —
`D:/cubeulator-build/vision-test-frames/recordings/` does not exist and an
exhaustive drive search finds no copy. Only the gitignored 6-frame
`tests/data/captures/<clip>/debug/` dumps survive (note: `.gitignore:27`'s
`[Dd]ebug/` pattern is what keeps them out of git — a trap to fix). The old
clips were also weak evidence: 640-wide already-downscaled analysis frames,
JPEG 4:2:0 chroma-subsampled, no ground truth, no manifest. We re-record
better rather than mourn the old corpus (restoring it, if found, is a
bonus for regression continuity).

Work:

1. **Pipeline scaffolding.** The A–I stage structs, `PipelineTrace` +
   committed JSONL schema, legacy adapter shims mapping the current
   `observe_face`/`observe_corner`/`observe_edge` cascade into stages B–E,
   and the runner self-check test (fails on any missing per-stage trace).
2. **Recorder v2.** Asynchronous writer (bounded queue) recording, per
   camera frame: the analysis-scale stream (the replay/acceptance input)
   plus full-resolution frames (JPEG quality ≥95, 4:4:4 where the encoder
   allows) for labelling. Measure sustained throughput on this
   workstation **before** the recording session; the exit gate includes a
   zero-dropped-frames check (index.csv gap scan) so t_ms/dt fidelity is
   guaranteed. Camera photometry lock engaged (already landed).
   Per-clip `manifest.json`:
   - the cube's true state as a 54-char face-major string in a documented
     convention (URFDLB order, row-major per face), **validated at record
     time** through `Cube::from_facelets` + `is_solvable` so a
     transcription slip cannot silently poison the headline gates;
   - per-face reference photos captured at record time;
   - lighting descriptor, motion script, camera id, intrinsics file ref.
3. **Corpus v2 recording session** (user-assisted, ~1–1.5 h). Mandatory
   clip classes, minimum counts — these are gate denominators, not
   nice-to-haves:
   - ≥3 six-face guided sweeps (neutral daylight / front lamp);
   - ≥2 free-rotation clips (cube manipulated continuously in hand);
   - ≥2 occlusion-stress clips with, across them, ≥10 full-occlusion or
     exit/re-entry events;
   - ≥1 layer-turn clip with ≥10 turns;
   - ≥1 cube-absent clip (scene, hands, no cube);
   - ≥1 warm-tungsten stressor clip (documented failure mode, kept
     honest);
   - two different cubes if available. Every clip's true state in its
     manifest.
4. **Ground-truth store + labelling tool.** Sidecar format per clip:
   - **Segment annotations** (frame ranges): cube in-frame / absent /
     partially-occluded / steady / turning, plus re-entry event frames.
     These define every rate-gate denominator; "sustained" is defined as
     ≥5 consecutive frames.
   - **Keyframe labels** (mandatory fields): visible cube corners,
     per-visible-face **identity**, and **9 sticker centres per visible
     face**. Identity + manifest state is what makes per-cell colour
     ground truth computable; centres are what Phase 4's gate scores
     against. Corner-only labels do not satisfy the gate.
   - Tool: minimal clicking UI (OpenCV window or small HTML page) with
     interpolation as a labelling aid (see ground rules: interpolated
     frames must be corrected before they count).
5. **Offline pipeline runner** (`tools/` + hidden Catch2 tag or small
   exe): replays any recording through the pipeline, emits (a) per-frame
   JSONL traces, (b) an **annotated overlay video** (cv::VideoWriter —
   silhouette, corners, wireframe, face quads, cell grids, classified
   colours, tracker state, gate verdicts, stage timings burned in), (c) a
   per-clip metrics summary, (d) a whole-corpus table in one command.
   This subsumes the single-frame `[.frame-debug]` overlay as the
   project's eyes.
6. **Routing replay harness.** The registration layer is the measured
   daylight blocker and has never had a replay/sweep harness
   (`capture_routing.hpp:246` says so). Build the harness now (drive
   `CaptureRouter` + registration from recorded traces, emit per-gate
   refusal attribution) so Phase 6 can re-derive the bars instead of
   hoping.
7. **Seams export.** Move the gate constants that `[.cross-session]` and
   `[.frame-debug]` hand-transcribe into shared headers so harnesses can't
   silently desynchronise during the restructure.
8. **Metrics definitions** (committed doc + code, emitted by the runner):
   detection rate (per segment class), corner px error (median/p95/max, %
   of projected cube extent), pose reprojection error, **pose bias**
   (per-clip mean residual *vector* magnitude per corner — the anti-live5
   metric), pose jitter (steady-segment reprojection std),
   locked-and-correct rate, re-acquisition latency, cell-centre error as
   fraction of sticker pitch, per-cell colour accuracy (labelled frames
   only), assembly distance, **exact-state match** (equality up to
   whole-cube rotation under centre-colour keying — with a
   self-validating test: transcribe, rotate, compare-equal). Baselines
   for the current pipeline recorded on corpus v2 the day it exists.
9. **Synthetic training data pipeline — starts now, in parallel.**
   Rendering randomised cube states/lighting/backgrounds/hand compositing
   (Quick3D or Blender) for the Phase 1 learned detector. This is
   preparation, allowed to overlap by the ground rules, and it removes
   the long-lead item from the Phase 1 critical path.

**Exit gate:**
- Corpus v2 recorded meeting **all mandatory clip-class minimums** above,
  manifests validated by the solver oracle, zero dropped frames per clip,
  archived redundantly (git-LFS, second drive, or NAS — losing the corpus
  once is enough).
- Segment annotations complete for every clip; keyframe labels (corners +
  face identity + sticker centres) on ≥15 hand-clicked keyframes × ≥6
  clips spanning lighting/motion classes.
- Runner produces annotated video + JSONL + metrics table for every clip
  (payload: the *current* pipeline via the legacy shims); runner
  self-check test merged and passing; routing replay harness produces a
  per-gate refusal breakdown for a replayed clip.
- Current-pipeline baseline table committed. All existing tests green.

Branch: `feature/pipeline-p0-foundations`.

---

## Phase 1 — Cube body detection (content-agnostic)

**Goal:** given a single frame, find the cube — silhouette, visible outer
corners, ROI — regardless of sticker colours, lighting, or state, with
fingers partially occluding.

Approach — two tracks, not an open-ended escalation:

1. **Classical spike, timeboxed to one week.** Edge/adaptive-threshold →
   contours → convex 4/6-gon silhouette fitting + line-segment lattice
   clustering (sticker gaps are content-agnostic dark lines; corner
   candidates from silhouette vertices and lattice intersections).
   *Known prior evidence is against it:* simple-face-pipeline's
   post-mortem showed the outline merging with background clutter ("never
   reduces to 4 vertices at any approxPolyDP tolerance") — a clutter
   failure, not a resolution failure — and fingers break the silhouette
   for most in-hand frames. **Kill criterion:** if it cannot reach the
   occlusion-clip detection bar on 3 designated clips within the week, it
   is demoted to init-assist and the learned track becomes primary.
2. **Learned detector (plan of record if the spike fails, prepared
   regardless):** a small keypoint model (YOLO-pose-class, nano) emitting
   the 7 visible cube corners + visibility, trained on the Phase 0
   synthetic pipeline + corpus-v2 labelled frames for validation,
   exported to ONNX, run via the existing ONNX Runtime infra (second
   bundled model beside the palm detector; wire the DirectML EP for
   workstation GPU, keep CPU fallback). The repo already names an
   ML detector as planned work.

**Exit gate** (denominators from Phase 0 segment annotations; pixel errors
on hand-clicked keyframes; all bounds per clip):
- Detection rate ≥99% on in-frame unoccluded segments; ≥90% on
  partially-occluded segments.
- Visible-corner localisation error: median ≤2% of projected cube extent,
  p95 ≤4%, no labelled frame >1 sticker pitch.
- Zero sustained (≥5 consecutive frames) false positives on the
  cube-absent clip and absent segments.
- Overlay videos reviewed: silhouette and corners visibly correct across
  the corpus (the eyeball check is part of the gate, not a substitute for
  the numbers).

Branch: `feature/pipeline-p1-detection`.

---

## Phase 2 — 6-DoF pose lock and tracking

**Goal:** the headline requirement. From detection, initialise a full 6-DoF
pose (rotation *and* translation — today's tracker carries orientation
only) and keep it locked frame-to-frame while the cube moves freely;
re-acquire fast on loss. The wireframe stays pinned to the physical cube;
the on-screen 3D model mirrors its motion. **Live behaviour is gated
numerically in this phase**, not deferred.

Approach:

1. **Init:** PnP (solvePnPRansac) from Phase 1 corners → initial SE(3).
   **Camera intrinsics get real:** a one-time chessboard/ChArUco
   calibration tool per camera, stored and loaded; the current
   `f = frame width` guess is a measured weakness and poisons everything
   downstream of PnP. (Fallback path keeps the approximation for
   uncalibrated cameras.) While **lost, the detector runs every frame**
   (workstation budget allows it; decimation applies only when locked) —
   the re-acquisition gate is unreachable at a few-Hz re-init cadence.
2. **Frame-to-frame tracking — primary: region-based model tracking.**
   Integrate **M3T** (DLR `3DObjectTracking`, MIT licence, C++/Eigen/
   OpenCV, plus GLEW/GLFW/OpenGL for its renderer — the one stack
   addition; ~1–2 ms/frame/body on one CPU core). The cube CAD model is
   trivial. Region modality (outer silhouette statistics) is
   content-agnostic by construction and benchmarked robust to partial
   occlusion. **Spike week decides:** vcpkg overlay port vs vendored
   subtree, and — mandatory — that M3T runs **headless/offscreen and
   deterministically** in the offline runner (the acceptance surface).
   Evaluate early on corpus v2's in-hand clips — the known risk is the
   local colour-histogram foreground model vs skin contact.
3. **Refinement + fallback: edge/lattice measurement.** RAPID-style 1-D
   search along projected *outer* edges plus the sticker-gap lattice as a
   secondary measurement — with the one-cell-aliasing hazard handled by
   letting it *refine* (sub-cell) but never *anchor* (whole-cell) the
   pose. If M3T integration stalls, this + LK optical-flow prior + SE(3)
   Kalman is the all-OpenCV fallback tracker.
4. **Track lifecycle:** explicit states (lost/acquiring/locked/coasting —
   keep today's vocabulary, the UI already binds it), loss on quality
   collapse, per-frame re-detection while lost. Epochs remain the
   identity lifetime concept at track birth.
5. **Live wiring and cadence.** The tracker update is decoupled from the
   single-in-flight `analyze()` pass (which carries ONNX/colour stages):
   tracking targets ≥25 Hz regardless of the heavier stages' cadence.
   Pose → existing AR overlay canvas (wireframe) and →
   `Cube3DView.externalRotation`, extended with translation/scale on
   `poseNode` so the model moves in space, not just attitude. A **debug
   HUD** (tracker state, last gate verdict, epoch, per-stage timings,
   raw-pose mode with smoothing/hold off) ships in this phase — the
   existing smoothing constants deliberately mask acquisition behaviour,
   and every live gate below needs the unmasked view.
6. **Layer-turn awareness, minimal:** during a turn the cube is two rigid
   bodies; Phase 2 only needs to *not lose the track* — treat the turning
   layer as occlusion and track the dominant block (region trackers
   tolerate ⅓ of the model misbehaving). Full two-body tracking (M3T
   supports revolute-joint multi-body natively) is Phase 7 stretch.

**Exit gate** (offline criteria per clip; pixel errors on hand-clicked
keyframes; "locked-and-correct" = self-declared locked AND, on labelled
frames, reprojection within bound — a locked frame above bound counts
**against**):
- Locked-and-correct ≥95% of in-frame frames on free-rotation clips;
  ≥85% on occlusion-stress clips.
- Model-corner reprojection error: median ≤1.5% of projected extent,
  p95 ≤3%.
- **Bias (the anti-live5 criterion):** per-clip mean residual vector
  magnitude per corner ≤0.5% of extent.
- Jitter: steady-segment reprojection std ≤0.3% of extent.
- Re-acquisition after a full-occlusion or exit/re-entry event: median
  ≤300 ms over the corpus's ≥10 events.
- Layer-turn clip: state ≠ lost through ≥90% of annotated turn segments.
- **Live gates**, scripted 60-second run on the workstation webcam:
  on-screen locked fraction ≥90% while the cube is in frame (HUD-logged),
  camera-to-overlay latency ≤120 ms measured, 3D model mirrors rotation
  and position with no visible lag or detachment (recorded for the
  record).

Branch: `feature/pipeline-p2-tracking`.

---

## Phase 3 — Face extraction and identity from pose

**Goal:** from the locked pose, derive which faces are visible and where —
no more per-frame face hunting, no more screen-sector heuristics.

Work:

1. Visible-face determination from pose (facing cosine), face quads by
   projecting the model — this replaces `observe_face`'s candidate-mask
   pipeline, `observe_corner`'s sector partition, and `observe_edge`
   entirely as geometry sources.
2. Rectify each visible face by homography to a canonical square (the
   sampling substrate for Phases 4–5).
3. **Identity — authority stated plainly:** within an epoch, tracked
   body-face identity **routes** observations (which aggregator, which
   slot). At assembly, **centre-colour keying remains the final arbiter**
   (`face_for_center_color`), as today (`scan_session.hpp`: "face
   identity comes from centre colours + assembly, never from the pose").
   The trace records a **disagreement counter** (tracked identity vs
   centre-colour identity per committed face); changing the arbiter is a
   Phase 6+ decision to be made on that counter's evidence, not assumed.
   Across epochs (track loss), `CaptureRouter`'s content-based
   registration stays — it is pose-method-agnostic and its invariants are
   paid for in corpus blood; its *traffic* should collapse because epochs
   become rare (measured in Phase 6).
4. Retire, behind the pipeline flag: the frontal/corner/edge detector
   cascade as the primary path, the completion stack, the
   phase-plausibility acceptance gate, the obs-to-body permutation tables
   (cells are now emitted directly in a declared canonical frame — state
   which, once, in the stage contract; the assembler's roll search
   absorbs the rest).

**Exit gate:**
- Face-quad corner error vs ground truth ≤ the Phase 2 reprojection bound
  (the quad is a projection; this checks the model geometry constants).
- Rectified-face stability: mean pixel displacement of the rectified image
  between consecutive steady frames ≤1 px at 96×96 (the "no simmering"
  criterion that per-frame fitting could never meet).
- Identity, verified **externally** (not against the pipeline's own
  trace): on keyframes with labelled face identity, tracked identity
  matches the label ≥99%; and on six-face sweep clips, **all six faces
  observed under a single epoch's identity** — a bar the legacy path
  (8–25 epochs per clip) measurably fails.
- Overlay: face quads + ids burned into corpus videos, reviewed.

Branch: `feature/pipeline-p3-faces`.

---

## Phase 4 — Sticker cell subdivision

**Goal:** nine sample regions per visible face that sit on the nine
stickers — the stage whose systematic error sank live5 — now derived from
tracked geometry plus bounded sub-pixel refinement instead of free-floating
lattice fits. **This phase's gate is purely geometric**; colour-dependent
criteria live in Phase 5, so this phase cannot be blocked on colour work.

Work:

1. Cells at fixed model fractions of the tracked face (real cube geometry:
   sticker/gap ratios measured once from the physical cube — constants of
   the model, not of a corpus).
2. **Sub-pixel refinement in rectified space:** with the face rectified,
   sticker gaps are near-axis-aligned dark lines; fit the 4+4 gap lines in
   1-D profiles and refine cell boundaries — displacement clamped to
   ±0.2 cell pitch so refinement can polish but never re-seat the grid on
   a neighbouring sticker (the aliasing failure class, excluded by
   construction).
3. Reuse `patch_stats_for_corners` (quad-masked Lab median + purity/MAD)
   unchanged as the sampling estimator — it was exonerated (purity median
   1.000).
4. Delete/bypass: `fit_lattice_detailed` as the shipping geometry source,
   `refit_affine` per-frame lattice refits, anchor-count machinery
   (`kMinInlierCells` logic), the multi-mask candidate union.

**Exit gate** (all vs labelled sticker centres on hand-clicked keyframes,
per clip):
- Cell-centre error: median ≤10% of sticker pitch, p95 ≤25%, and **zero
  whole-cell misassignments** on the labelled set.
- Per-cell purity distribution not regressed vs baseline (median ≥0.99).
- Stability: per-cell Lab deviation between readings of the same steady
  face — centre cells were 7.1, edge/corner 18–19 in the old pipeline;
  gate: ≤8 across *all* nine positions (kills the radial degradation;
  Lab deviation is colour-independent — it measures sampling geometry).

Branch: `feature/pipeline-p4-cells`.

---

## Phase 5 — Colour: session-relative classification on the live path

**Goal:** name the nine colours of a face correctly under the lighting we
support, using the session's own observed centres — the two known boundary
failures (desaturated green → unknown; orange at hue 40° → red) become
correct by construction.

Work (deliberately small — colour was never the deep problem):

1. Session-relative classification relative to observed centre Lab on the
   **live path** (extending `classify_lab`/`resolve_colors`' seeding, not
   a parallel classifier).
2. Keep absolute bands only as pre-centre bootstrap and sanity bounds.
3. Verify the camera photometry lock with the lit re-run the handoff flags
   as outstanding; document per-lighting envelopes (warm tungsten stays a
   documented failure mode with a UI hint, not a silent one).

**Exit gate:**
- Per-cell colour accuracy ≥99% — computed **only on keyframes with
  labelled face identity** (identity from the label, roll fixed by the
  labelled quad corner order, cells then mapped to the manifest state; a
  pipeline identity error on a labelled frame counts as a **failure**,
  not an exclusion — anything looser is the self-referential
  ground-truth trap). Neutral and front-lit clips.
- The two daylight boundary errors specifically reproduced as fixed
  (tests that failed pre-fix).
- **The live5 criterion (moved here from Phase 4, where it was blocked on
  colour):** dominant per-face raw reads on free-rotation clips reach
  assembly distance ≤1 — geometry (Phase 4) and naming (this phase)
  jointly clear the systematic right-colour-wrong-cell signature before
  fusion or resolution get involved.
- Warm-light clips: measured, documented envelope — no regression vs
  baseline, honest failure surfaced to the UI.

Branch: `feature/pipeline-p5-colour`.

---

## Phase 6 — Fusion, commit, assembly: end-to-end scans

**Goal:** six faces committed and assembled into the *known* cube from the
manifest — the phase where the coverage wall must be measurably dead.

Work:

1. `FaceAggregator` fed by pose-derived reads, keyed by tracked identity
   (survives nearly verbatim).
2. Commit gates re-derived from tracker quality (pose quality, frontal°,
   motion) — retire the accept-streak/gauge-margin compensations; keep
   the photometric commit gates (unknown-cell bound, staleness).
3. **Registration bars re-derived, with the Phase 0 routing harness.**
   Keep `CaptureRouter`'s invariants (never-overwrite, verify, layer-turn
   discrimination) but re-measure `register_epoch_gauge`'s commit/margin
   bars, the newcomer path, and verify radii on corpus v2 — the daylight
   replay proved these bars, not the tracker, were the final blocker.
   The gate below requires a per-gate refusal breakdown, so a repeat of
   "0 of 6, cause unattributed" is impossible. Expect and *measure* the
   traffic collapse (epoch count per clip should drop by an order of
   magnitude).
4. Wire the tracker into assembly's **`RotationHint`** — stated honestly:
   the hint only breaks ties among multiple already-passing survivors
   (≤8); the 4⁶ search still runs (it is sub-millisecond) and a unique
   survivor wins regardless. This closes the rare ambiguous-survivor
   failure mode; it is plumbing, not a load-bearing accuracy fix. If the
   Phase 3 disagreement counter shows tracked identity outperforming
   centre keying, *then* an assembler-contract change is designed here —
   explicitly, because it touches the "identity from centres, never pose"
   invariant.
5. End-to-end wiring behind a `pipeline_v2` switch in `ScanSession`
   (env/build flag), old path intact until Phase 7 flips the default.
   `ScanSessionConfig` either becomes the real option surface here or is
   deleted — not left dead.

**Exit gate — the one that counts:**
- **Exact-state match** (assembly distance 0 *and* equal to the manifest
  state up to whole-cube rotation) on: every neutral/front-lit six-face
  sweep clip and at least one free-rotation clip, in deterministic
  offline replay.
- Six faces committed on every six-face-coverage clip, with the
  **per-gate refusal breakdown** published per clip (the daylight-class
  silent wall is structurally impossible to repeat unattributed).
- Full-corpus table: no clip regresses vs the Phase 0 baseline on faces
  captured or assembly distance.
- Live demo: a real scan on the workstation webcam completes to a solved
  state readout.

Branch: `feature/pipeline-p6-endtoend`.

---

## Phase 7 — Productisation: default flip, UX, layer turns, performance

**Goal:** tracking-first is *the* pipeline, the experience matches the
vision, and the remaining ambitions land as measured increments.

Work, roughly ordered:

1. Flip `pipeline_v2` to default; delete the legacy detector cascade and
   its compensation layers (the deletion list from the survey: completion
   stack, candidate-escalation arms race, sector heuristics, obs-to-body
   tables, legacy MVP path) once the corpus table confirms no regression.
2. UX pass: capture progress on the 3D mirror cube (per-face rings already
   exist), guidance prompts driven by pose ("show me the white face"),
   honest lighting warnings from the Phase 5 envelopes; the Phase 2 debug
   HUD graduates into a polished "stats for nerds" vision panel.
3. Layer-turn tracking, stretch: M3T two-body revolute model during
   detected turns (6 candidate axes, snap back to rigid at ±90°), enabling
   scan-during-inspection and future move-tracking features.
4. Performance: per-stage timing budget for phone targets, DirectML/CPU
   EP selection, decimation policy — measured against the Phase 0 timing
   traces. The solve-time budget (seconds, on a phone, always) already
   holds; assembly is sub-millisecond.
5. Docs: `vision-pipeline.md` rewritten for the new flow;
   `handoff-grid-fit.md` closed out with a pointer here.

**Exit gate:** default-on in the app, legacy code deleted, corpus table
green, live free-rotation scan of a known cube reaches exact-state match
on the first attempt in ≥8 of 10 tries (workstation webcam, neutral
light).

Branch: `feature/pipeline-p7-product`.

---

## What survives, what collapses (from the code survey)

| Component | Fate |
| --- | --- |
| `patch_stats_for_corners`, Lab conventions, purity/MAD | survives (Phase 4 sampler) |
| `classify_lab` / `resolve_colors` + palette constants | survives, gains session-relative rules (Phase 5) |
| `pose_estimation.cpp` primitives (camera matrix, cheirality, quaternion, hints) | survives as tracker plumbing (Phase 2) |
| `FaceAggregator` | survives verbatim (Phase 6) |
| `CaptureRouter` invariants, epoch/gauge algebra (`kLattice24` etc.) | survives; bars re-derived, traffic collapses (Phases 3/6) |
| Stability, IdleGate, hand presence, ONNX infra, record/replay format | survives (ONNX infra grows an EP + second model; recorder v2 extends the format) |
| `assemble_scanned_faces_with_mapping` + unused `RotationHint` seam | survives; hint wired as tie-break (Phase 6) |
| Frontal/corner/edge detector cascade as geometry source | replaced (Phase 3) |
| Per-frame lattice fitting, completion stack, candidate-mask arms race | deleted (Phases 3–4, 7) |
| Per-pass 24-element gauge resolution + its gate stack | subsumed by continuous tracking (Phases 2–3) |
| Accept-streak / gauge-margin commit compensations | re-derived from tracker quality (Phase 6) |
| Legacy MVP path (`analyze_frame`, `detect_cube_grid`, `classify_color`) | deleted (Phase 7) |
| `ScanSessionConfig` (currently dead config) | wired or deleted (Phase 6) |

## Open questions (tracked, none block Phase 0 start)

1. **Where did the recordings go?** `D:/cubeulator-build/vision-test-frames/`
   is gone; the `claude/cleanup-branches-assets-*` session is a suspect.
   If a copy exists (external drive/NAS), restoring it adds regression
   continuity; corpus v2 does not depend on it.
2. **M3T dependency mechanics:** vcpkg overlay port vs vendored subtree;
   MIT licence is compatible either way; headless/deterministic operation
   is a mandatory spike outcome, not an assumption. Decided in Phase 2
   spike week.
3. **Identity arbiter:** does tracked identity ever outperform
   centre-colour keying at assembly? Decided by the Phase 3 disagreement
   counter's evidence, in Phase 6.
4. **Glare vs geometry residual** (handoff open question): expected to be
   answered free by corpus v2's front-lit six-face clips + the Phase 4
   gates.
5. **Phone budget for the tracker:** M3T's CPU numbers suggest comfort,
   but nothing is measured on-device; deferred to Phase 7 by design.
