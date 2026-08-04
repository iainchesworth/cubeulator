# Pipeline metrics definitions

The measured vocabulary of the tracking-first plan
([pipeline-plan.md](pipeline-plan.md)). Every phase gate cites metrics
defined HERE, computed by the runner (`tools/runner`) — a gate criterion
with no definition on this page is a plan bug. Where a metric needs ground
truth, its denominator and referent come from the sidecar formats in
[ground-truth-format.md](ground-truth-format.md); metrics computable from
traces alone are marked (trace).

General rules (from the plan's ground rules):

- **Pixel-error metrics are scored on hand-clicked keyframes only**
  (`interpolated:false`). Interpolated labels are a labelling aid, never a
  referent.
- **Every distribution metric reports median AND p95 (and max where the
  gate says so), per clip.** No cross-clip averaging: a clip passes or
  fails alone.
- **Rate denominators come from segment annotations**, never from the
  pipeline's own verdicts. "Sustained" = ≥5 consecutive frames.
- `extent(f)` — the cube's projected extent on frame f: the longest side
  of the bounding box of the labelled visible corners (keyframes), or of
  the detection/track under test where a gate explicitly says so.
  Percent-of-extent errors divide by this.

## Detection (Phase 1)

- **detection rate | segment class** (needs segments): fraction of frames
  in segments of a class (`in_frame` unoccluded / `occluded`) where the
  detector reports `found`. Frames in `absent` segments are excluded from
  the rate and feed the false-positive metric instead.
- **false positives, sustained** (needs segments): count of runs of ≥5
  consecutive `found` frames wholly inside `absent` segments. Gate target
  is zero.
- **corner localisation error** (needs keyframes): for each labelled
  visible corner with a matched detected corner, the Euclidean pixel
  distance, as % of extent. Unmatched labelled corners count as errors at
  the gate's max bound (missing a corner is not a free pass). Reported
  median / p95 / max per clip.

## Tracking (Phase 2)

- **locked-and-correct rate** (needs segments + keyframes): fraction of
  `in_frame` frames where the tracker self-reports locked AND — on every
  labelled keyframe in the window — model-corner reprojection error is
  within the gate bound. A locked keyframe above bound counts AGAINST the
  rate (the stable-but-wrong trap is the failure mode this metric exists
  to catch). Frames between keyframes inherit no correctness judgement;
  the rate's correctness term is evaluated on keyframes and the locked
  term on all frames. Correctness is GEOMETRIC: reprojection is
  minimised over the 24 cube rotations, the same convention as corner
  localisation — a quarter-turn-slipped pose still projects onto the
  cube's corners, and whether the rotational registration survived is
  the next metric's job, not this one's.
- **registration hold** (needs keyframes; diagnostic, no gate): each
  locked run pins the best-of-24 gauge on its first scored frame; this
  reports the fraction of subsequent scored frames on which the pinned
  gauge stayed within the gate bound, plus the count of re-pins — frames
  where the best gauge moved while the pinned one went out of bound,
  i.e. the tracker slipped a cube symmetry (silhouettes are invariant
  under quarter turns, so region tracking alone cannot forbid this and
  the loop's best-of-24 health check cannot see it). This is the number
  that quantifies free-rotation slippage for the Phase 2 refinement and
  Phase 3 identity work.
- **reprojection error** (needs keyframes): per labelled corner, distance
  between the tracked pose's projected model corner and the label, % of
  extent. Median / p95 per clip.
- **pose bias — the anti-live5 criterion** (needs keyframes): per clip,
  the magnitude of the MEAN residual vector per corner id (mean over
  keyframes of the 2-D signed residual, then magnitude, then worst corner
  id). Random jitter cancels in the mean; a systematic offset survives it.
  This is the number that must stay ≤0.5% of extent.
- **pose jitter** (trace + segments): over each `steady` segment, the
  standard deviation of each projected model corner across frames
  (tracked pose, no labels needed — jitter is self-relative), % of
  extent, worst corner. Median over segments per clip.
- **re-acquisition latency** (needs segments): for each `reentry_event`
  frame r, the time from r until the first locked-AND-correct frame
  (correctness per the locked-and-correct rule above — a re-lock onto a
  wrong pose must not stop the clock), converted to ms via the
  recording's timestamps. An event with no such frame before the cube
  goes absent again (or the clip ends) is a FAILURE; the median ranks
  failures worst, so a corpus whose median event failed reports no
  finite latency rather than the median of the survivors. Median over
  events.
- **turn survival** (needs segments): fraction of `turning` segments
  wholly free of `lost` frames.

## Geometry (Phases 3–4)

- **face-quad corner error** (needs keyframes): per labelled face quad
  corner vs the pipeline's face view quad, % of extent, median / p95.
- **rectified-face stability** (trace + segments): mean absolute pixel
  displacement (dense, e.g. mean |diff| of consecutive rectified 96×96
  faces after intensity normalisation) between consecutive frames of a
  `steady` segment, per face. The "no simmering" number.
- **cell-centre error** (needs keyframes with sticker centres): per
  labelled sticker centre vs the pipeline's cell-quad centre, as % of
  sticker pitch (pitch = median distance between adjacent labelled
  centres on that face). Median / p95 per clip, plus:
- **whole-cell misassignments** (needs keyframes): count of cells whose
  centre error exceeds 50% of pitch — i.e. the sample sits on the wrong
  sticker. Gate target zero.
- **patch purity** (trace): as measured by the sampler
  (`FaceObservation::cell_purity` semantics; 0 = unmeasured is excluded,
  never counted as pure).
- **per-cell Lab stability** (trace + segments): per cell position, the
  per-channel deviation of Lab between readings of the same physical face
  within a `steady` segment; reported per position class (centre / edge /
  corner) and worst position.

## Colour (Phase 5)

- **per-cell colour accuracy** (needs keyframes with face identity +
  manifest): on labelled keyframes only. The label fixes WHICH manifest
  face is visible and the quad corner order fixes the roll; the manifest
  state then names every sticker. Accuracy = correctly named cells /
  labelled cells. A pipeline identity error on a labelled frame scores
  its 9 cells as wrong — identity failures are colour failures here, not
  exclusions (anything else re-opens the self-referential trap).

## End to end (Phase 6)

- **assembly distance** (trace): the `[.assembly-probe]` metric — minimum
  single-relabels / census-preserving two-swaps / three-cycles to reach a
  cube the real oracle (`from_facelets` + `is_solvable`) accepts. The ONE
  metric that counts; census and consistency are diagnostics.
- **exact-state match** (needs manifest): the assembled cube equals the
  manifest state **up to whole-cube rotation**: iterate the 24 cube
  orientations of the manifest cube; match iff any orientation's 54
  facelets equal the assembled cube's under centre-colour keying. The
  recorder validates manifest states through the oracle at record time,
  and the comparison has a self-validating test (transcribe → rotate →
  compare-equal) so a transcription slip cannot masquerade as a pipeline
  failure.
- **slot-fill / refusal breakdown** (trace, routing harness): per clip,
  slots filled and every refused commit attributed to its specific gate
  (registration insufficient / ambiguous, verify, holdoff, quarantine,
  force-lost, identity, frontal, motion, staleness) with the score vs its
  bar. The "0 of 6, cause unattributed" failure mode is retired by
  construction.

## Performance (all phases, trace)

- **stage timings**: per-stage wall-clock ms (mean / p95 / max per clip)
  from the trace `ms` fields; the runner's metrics.json carries totals.
  Budgets are phase-gated only from Phase 7, but the numbers exist from
  Phase 0 so regressions are visible the day they appear.
