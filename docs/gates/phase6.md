# Phase 6 gate — fusion, commit, assembly: end-to-end scans

Status: **open — exact-state match NOT reached on any clip.** The
mechanism chain P6.2–P6.7 is landed and regression-pinned; everything
between the camera and the assembler is measured working, and exactly
ONE defect remains between the pipeline and an exact-state match — and
it is user-gated (verdict section). Code: P6.2 read grade (daea262),
P6.2b gauge stitching (d115913), P6.3 content registration (2724499),
P6.3b scoring floor (9ea6d1e), P6.4 RotationHint + handedness fix
(5efd756), P6.5a store-to-assembly seam (e00f0ee), P6.5b
store-convention hint (93347a5), P6.6 content-keyed sub-epochs
(7eb0753), P6.7 shading-invariant assignment (b748cdd), on
feature/pipeline-p6-endtoend.

Suites at head (fast tag, i.e. excluding the solver stages):
cube_unit_tests "All tests passed (94781 assertions in 296 test
cases)"; cube_qt_controller_tests "All tests passed (92 assertions in
15 test cases)"; cube_integration_tests 18 cases — 15 passed, 3
skipped, 62/62 assertions.

## Work items, measured

- **P6.2 read grade.** One per-frame read_grade — track live, health
  CURRENT (within kHealthGapFrames) and passing, quasi-static (turn
  rate within the gauge bound) — folded into capture_grade, so
  sampling, classification, palette feeding and fusion inherit a
  single gate; this retires the accept-streak/gauge-margin
  compensations on the tracked path. Measured: freerot 5/6 → 6/6 faces
  converged (mid-turn and stale-health reads were the poison) and the
  chain reached resolve_colors for the first time — answering
  resolve-failed CORRECTLY, since freerot's registration slips can
  alias two epoch-local faces to one physical face. Attributed cost:
  sweep fell 6/6 → 3/6 observed (an epoch split now bounds coverage);
  recovered by P6.3/P6.6 below.
- **P6.2b gauge stitching.** Benign health-drop re-anchors relabel
  into the old epoch's frame when the old→new model rotation snaps to
  one of the 24 cube rotations (guards: last health <= 0.30, re-anchor
  within 5 frames, snap within 30 degrees; relabels compose across
  stitches). Exit-reentry improved to 4/6 observed and converged. The
  sweep does NOT stitch, and the instrumented snap angles closed the
  geometric road with finality: 38.1 and 55.4 degrees — at or past the
  45-degree nearest-of-24 basin midpoint, genuinely ambiguous. Sweep
  identity across drops therefore requires CONTENT (P6.3), exactly
  where the plan put it. stitch_snap_deg rides the trace.
- **P6.3 cell-level content registration.** Cross-epoch relabel must
  be one of the 24 rotations' face permutations; each candidate is
  scored as mean Lab distance over every shared face's nine cells
  (cell mapping derived geometrically — the roll falls out). Bars:
  commit radius 16 Lab, margin 8 over the runner-up, per-face centre
  verify on merge; router invariants (never-overwrite, counted
  refusals) hold. Measured walk: centre-only scoring needs two shared
  faces (sweep epochs share one); a geometric shortlist fixed the
  first boundary decisively (cost 6.9 vs runner-up 108.5) but the
  second drop's >65-degree drift put the truth outside any shortlist
  (both candidates >114 Lab); cell-level scoring subsumes both — one
  content-rich face discriminates all 24 alone. Sweep merged its first
  boundary: 4/6, up from 2/6 at phase start.
- **P6.3b scoring floor — necessary, not sufficient.** Registration
  scoring/verify accepts a face at two agreeing ring observations
  (capture and final selection stay at kMinObservations; thin content
  never reaches the assembled result). Adversarial verify: a no-op on
  every single-gauge clip; wrong-roll sweep candidates score 68+ Lab
  vs ~4 for the true match. The floor was predicted to unlock the
  sweep's second boundary and did NOT — the refutation mattered more:
  the boundary face's two transit reads (seq 372–373, grade 1, purity
  0.79–0.84) DID feed fusion, then were EVICTED when hold 6's 44
  post-flip observations refilled the same aggregator — the tracker
  slipped a quarter-turn during the 5.6 deg/frame flip, so two
  physical faces shared one believed id and the 8-deep ring destroyed
  the evidence before finalize. Same identity-outlives-content
  pathology as freerot; the fix is P6.6, not a threshold.
- **P6.4 RotationHint producer, and the handedness defect it
  exposed.** The producer (tracked identity + resolved centres → six
  quarter-turn digits) refused to produce: on some faces the
  body-to-solver cell permutation is not a rotation at all. The
  assembler's premise — "reflections cannot occur optically" — is
  FALSE for every real capture convention: the solver's cross-unfolded
  layout is camera-handed on U/F/L and MIRROR-handed on D/R/B (the
  guided path's per-face tables in scan_conversion.cpp encode exactly
  this fact), so an outside view of D/R/B is a reflection no
  quarter-rotation bridges. Only self-consistent solver-layout test
  arrays had ever reached the assembler. Three-legged fix:
  face_projection samples one uniform outside-view convention;
  assembly applies the centre-pinned D/R/B flip itself and its header
  states the input convention; the P6.3 cell mapping and the hint
  producer follow suit. rotation_hint_for_tracked_scan proven across
  all 24 body orientations; degraded inputs yield an advice-free hint,
  never a guess. Hint is tie-break advice only — the 4^6 search still
  runs and a unique survivor wins regardless.
- **P6.5a store-to-assembly seam.** The live path had the same latent
  defect: the capture store keeps slots in the SOLVER layout of each
  body face (the D/R/B mirror baked in on ingestion — which is what
  keeps the store closed under kBodyRelabel), and finalizeIfComplete
  fed slots to the assembler raw — correct under the old assembler for
  only 3 of 24 gauges, under the outside-view contract for none. Fix
  is one seam (slot_to_outside_view: a column flip on D/R/B slots);
  the store's internal convention and every invariant on it are
  untouched. The missing test now exists both ways: a 12-move scramble
  expressed as a store under EVERY one of the 24 gauges assembles to
  the exact cube, and a second case pins the defect as a property —
  WITHOUT the seam, no gauge assembles at all. ScanSessionConfig
  (dead, zero users) deleted per the plan's "wired or deleted".
- **P6.5b store-convention hint.** rotation_hint_for_store_scan — the
  store-side producer: slot roll reference is the body face's solver
  layout (post-seam), body labels + resolved centres pin the rotation,
  the assembler's D/R/B correction composed in. Proven across all 24
  gauges against doc-restated conventions (nothing lifted from the
  implementation); every guard degrades the whole hint to no-advice.
  The near-solved ambiguous fixture, expressed as a store under all 24
  gauges, is LIFTED to the true cube by the hint where unhinted
  assembly correctly refuses. Wired into finalizeIfComplete.
- **P6.6 content-keyed sub-epochs.** Both exit-gate blockers were one
  pathology, measured twice: a mid-run quarter-turn slip (no
  re-anchor, no epoch bump) lets a different physical face refill an
  existing believed-id slot and the 8-deep ring evicts the old
  evidence — freerot lost R's 79-observation content, the sweep lost
  its second boundary's only shared-face evidence. Fusion is now keyed
  by sub-epoch (gauge_epoch x 1000 + generation), with an EPOCH-WIDE
  generation bump whenever a read's centre jumps >kTakeoverJumpLab
  (24) from its slot's fused centre — a slip moves every label at
  once. Spurious splits (same-face illumination drift measures up to
  32 Lab) are accepted and HEALED by registration: centre verify moved
  to a structural backstop (40 Lab — above the drift, below the 57.3
  minimum cross-face separation), real bars untouched, transplants
  re-index cells through the winning relabel
  (FaceAggregator::relabel_cells), and a below-floor incumbent yields
  to a floor-clearing transplant (fires exactly once corpus-wide).
  Measured: freerot resolve-failed → ASSEMBLY-FAILED at 6/6 converged
  in one re-unified sub-epoch (3 merges, 12 splits); sweep 4/6 → 5/6
  (2 merges), final hold still orphaned by the mid-flip slip — no
  shared content, unfixable by any fusion change; steady byte-identical
  with zero splits (the misfire canary); occlusion's 33 splits are the
  detector doing its stated job on hand-occluded reads (jumps
  24.6–65.7 Lab; 22 clean fragments re-merged, 32 contaminated
  refused — silencing them would need a threshold above real slips).
- **P6.7 shading-invariant assignment.** resolve_colors sent a
  shade-darkened orange cell to red, and the exactly-9 census cascaded
  three bright reds into orange — 6 misnames — because shading acts as
  a near-scalar on the whole Lab vector. An L-only down-weight is
  measured NOT to fix it (the shaded cell is nearer red even in pure
  chroma, 19.0 vs 31.2). Shipped metric splits lightness/chroma/hue:
  d^2 = w*(dL^2 + dC^2) + dH^2 with kShadingAxisWeight 0.2 — the two
  shading-carried axes share one down-weight, the shading-invariant
  hue residual keeps full weight. Zero freerot misnames across the
  whole w in [0.05, 0.5] plateau; w = 0 collapses white (neutrals
  carry no hue evidence). The white/yellow worst case improves: the
  warm-cast-white vs washed-yellow margin goes -2.0 (census-rescued)
  → +3.5. Seed separation and cluster naming stay in plain Lab. The
  regression fixture embeds all 54 measured freerot fused Lab values
  with manifest truth and fails under the old metric with exactly the
  diagnosed 6 misnames.

## Corpus end-to-end (synthetic v2, deterministic replay, at head)

Observed = converged on every clip in this run. Merges/refusals/splits
are the per-clip registration accounting the exit gate demands — a
repeat of "0 of 6, cause unattributed" is structurally impossible.

| clip | end-to-end | faces (best sub-epoch) | merges | refusals | splits | unknown cells |
|---|---|---|---|---|---|---|
| absent | no-tracked-reads | 0/6 | 0 | 0 | 0 | 0.0% |
| steady | insufficient-faces | 2/6 | 0 | 0 | 0 | 5.6% |
| occlusion | insufficient-faces | 2/6 | 22 | 32 | 33 | 0.0% |
| exit-reentry | insufficient-faces | 4/6 | 0 | 0 | 2 | 26.9% |
| freerot | **assembly-failed** | 6/6 | 3 | 0 | 12 | 1.1% |
| sweep | insufficient-faces | 5/6 | 2 | 0 | 1 | 0.2% |

Absent is the false-positive canary (a cube-free clip must produce
zero reads). Steady/occlusion/exit-reentry are not six-face-coverage
clips — their static or interrupted views never show six faces, so
insufficient-faces is the honest refusal, not a defect. The six-face
clips are sweep and freerot, scored in the verdict below.

## Exit gate (plan wording, verdict per criterion)

1. **Exact-state match (assembly distance 0, equal to the manifest
   state up to whole-cube rotation) on every six-face sweep clip and
   at least one free-rotation clip** — **BLOCKED, user-gated. Not
   reached on any clip.** The single remaining defect chain: the
   solver's colour canon is the MIRROR of the physical Western scheme.
   Measured isolation: freerot's fused capture is 54/54 cells correct
   against the manifest (the P6.7 fixture); feeding hue-true,
   scheme-fixed content yields exactly ONE assembler survivor, equal
   to the manifest oracle — everything from tracker to resolver is
   cleared, and a real cube (which wears the physical Western scheme)
   would fail identically, so re-rendering the generator would only
   move the failure to the first real scan. Decision pending with the
   user: fix the canon (recommended) vs fix the generator.
2. **Six faces committed on every six-face-coverage clip, per-gate
   refusal breakdown published** — **PARTIAL.** Freerot: 6/6 observed
   and converged. Sweep: capped at 5/6 — the final hold is orphaned by
   a quarter-turn slip during the clip's 5.6 deg/frame 180-degree
   flip, motion beyond the read-gate's quasi-static spec, leaving that
   face with no shared content any fusion change could score; a
   gentle-flip corpus variant is the proposed fix, not a threshold.
   The refusal breakdown itself: published per clip (table above), so
   this criterion's accounting half is met.
3. **Full-corpus table, no clip regresses on faces captured or
   assembly distance** — **PARTIAL.** Within Phase 6 the corpus is
   regression-free commit over commit (steady byte-identical is the
   standing canary; sweep's P6.2 dip to 3/6 was attributed and
   recovered to 5/6). The criterion's named baseline is the Phase 0
   real-corpus table, which does not exist yet — the standing P0.3
   caveat every prior gate carries.
4. **Live demo: a real scan on the workstation webcam completes to a
   solved-state readout** — **BLOCKED (needs the user).** Not
   attempted; also behind the criterion-1 canon decision, since a real
   cube hits the same defect.

Exonerated by measurement during the criterion-1 diagnosis:
- FaceAggregator transplants / relabel_cells: the 54/54-perfect fused
  capture rode through them, and the thin-incumbent transplant fires
  exactly once corpus-wide, correctly.
- The tracked capture stack end-to-end (track → identity → read grade
  → sub-epoch fusion → registration → resolve): 54/54 against
  manifest truth leaves it nothing to answer for.

## Remaining to close Phase 6

- [ ] **USER-GATED:** the colour-canon chirality decision — fix the
      solver canon to the physical Western scheme (recommended) or
      re-render the generator to the mirrored canon. Either unblocks
      exact-state on freerot; only the canon fix also serves real
      cubes.
- [ ] Sweep's sixth face: a gentle-flip corpus variant with the flip
      inside the read-gate's quasi-static bound (the recorded
      5.6 deg/frame slip-orphaned hold is out of spec by design).
- [ ] Live webcam demo (user at the camera).
- [ ] P0.3 real-cube corpus re-score (inherited standing caveat; the
      criterion-3 baseline comparison lands there).
