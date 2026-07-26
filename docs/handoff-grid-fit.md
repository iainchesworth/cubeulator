# Handoff: camera recording → fitted cube model that assembles

You are picking up a hard, well-diagnosed problem in the Cubeulator repo. A
previous multi-day effort narrowed the failure. Your job is to make a
recorded scan produce a **physically valid, solvable** cube. Read this whole
brief before touching code — it exists to stop you repeating a day of dead
ends.

## LATEST UPDATE (2026-07-26) — read this first, it changes the direction

A neutral-daylight, square-on capture (`recordings/daylight`) was recorded
and the fitted grid was inspected directly with `[.frame-debug]` +
`CUBE_DEBUG_OVERLAY`. The overlays are kept as evidence at
`docs/evidence/daylight-grid-overlay-{1,2}.png` (source frame:
`daylight-frame-1.jpg`). They change the picture materially:

- **The grid is placed CORRECTLY on still, square-on, neutrally-lit faces.**
  The nine sample boxes sit squarely on the nine stickers, and **7 of 9
  cells classify correctly**. The "systematic geometric mislocalisation"
  below is a property of the BACKLIT, heavily-rotated `live5` clip — it is
  NOT a property of a clean square-on capture. Geometry is not fundamentally
  broken.
- On the good daylight frames the residual is **two colour-boundary errors,
  consistent across frames** (both visible in the overlays):
  - **green → `unknown`**: the cube's green renders desaturated, cell Lab
    ≈ (56, −9, −3), chroma ~9 — below the white chroma floor, then abstained
    by the lightness floor. The green *centre* is equally desaturated, so
    session-relative classification (against observed centres) fixes it;
    an absolute floor cannot.
  - **orange → red**: cell hue ≈ 40°, just under the fixed 44° red/orange
    band edge, so orange is named red. Again the orange *centre* is equally
    hue-shifted — relative classification fixes it.
- Therefore the problem decomposes into **(a) coverage** — the daylight clip
  committed **0 of 6** faces despite 493 good detected frames, the
  free-rotation routing/tracker gates rejecting everything — and **(b) two
  known colour-boundary fixes**. Neither is deep grid-fit surgery.

**Revised recommended direction:** build **guided per-face capture** (hold
one face still, square-on — which is exactly the condition under which the
overlays show geometry already works, and which also removes the coverage
wall), plus the two session-relative colour-boundary fixes above. This is a
better-evidenced and smaller target than the grid-fit refinement the rest of
this brief describes. The material below remains accurate for the
free-rotation `live5` path and is the right reading if you pursue that
instead — but the daylight overlays are the reason to prefer guided capture.

Lighting matters and is measured: WARM tungsten light is a distinct failure
mode (it crushes green/white so badly they will not capture — see the
`goodlit`/`goodlit2` clips, 4–5 faces, green missing); NEUTRAL daylight is
required. Backlighting (`live5`) degrades geometry. Front, neutral, diffuse
is the capture target.

## The goal, stated as a measurable

Take a recorded webcam scan of a real cube and produce six faces of nine
stickers that `cube::solver::assemble_scanned_faces_with_mapping` accepts
as a valid, solvable cube. Today **no recorded scan assembles.** The best
clip is **≥4 stickers away** from any solvable cube.

## The ONE metric that counts — everything else has lied

Use the assembly-distance probe, `[.assembly-probe]` (committed on develop,
`tests/unit/assembly_probe_test.cpp`). It takes six face strings via
`CUBE_ASM_FACES` (`;`-separated, e.g. `BRYWOGOBO;OBWRRWYOB;...`) and, using
the **real solver oracle**, reports how many single relabels / two-swaps /
three-cycles are needed to make the cube assemble. It self-validates
(solved cube = distance 0; one-corruption = distance 1).

Two metrics have repeatedly fooled this project — do not trust either:

- **Census (9-of-each colour) proves nothing.** `resolve_colors` forces
  exactly nine of each by construction, so a census-perfect scan can be
  many stickers from solvable. `live5` has a perfect resolved census
  `W9Y9R9O9G9B9` and is still ≥4 from valid.
- **Cross-frame / cross-run consistency proves nothing.** A systematically
  wrong reader is perfectly consistent with itself.

**Only "does the probe distance drop toward 0" counts.** Measure it before
and after every change. If a change improves the census but not the
distance, it is cosmetic.

## The root cause, proven — do not re-litigate it

The colours are **right**; they are sampled from the **wrong cells**. The
grid fit mislocalises: a clean sample patch sits fully on a *neighbouring*
sticker, so ≥4 cells carry the correct colour of the wrong position.

This was established by elimination, each step measured:

1. Not colour classification — the red/orange/yellow clusters are cleanly
   hue-separated (R 24.8°, O 45.6°, Y 79.7°); if naming were the fault a
   swap among them would assemble. None does.
2. Not sample-patch contamination — patches are already masked to the
   projected quad (`face_observation.cpp` `patch_stats_for_corners`,
   `PatchIntent::kCellColour`), purity median **1.000**, 85–92% of cells
   ≥0.95. Clean single-sticker reads.
3. Not face roll or slot routing — `assemble_scanned_faces_with_mapping`
   already searches **all 4⁶ = 4096** per-face rotations and keys faces by
   centre colour. The INVALID verdict is the result of trying every roll.
   Empirically rolling a face 90° changes the distance by **zero**.
4. Not a mid-scan layer turn — checked on live5 by grouping every good
   per-frame read by centre colour and canonicalising the ring over the
   four rolls. Every face has ONE dominant pattern spanning the whole clip
   (yellow `BBRWYORWG`, 72 of 77 reads, passes 27–282); the multi-cell
   variants are isolated 1–2 frame transients, never a persistent post-turn
   regime. So the six faces are one cube configuration; the ≥4 error is in
   the pipeline, not the user's hands.
5. Therefore: per-cell **geometric mislocalisation** — a clean patch sits
   on the wrong sticker.

**And the error is SYSTEMATIC, not random jitter — this is the sharpest
narrowing, and it corrects the "jitter" framing above.** Averaging is the
test: if the mislocalisation were random per-frame grid jitter, fusing many
frames would wash it out and the dominant per-face read would be correct.
It is not. The *stable, noise-free dominant reads* — the most common ring
per face, with transient misreads removed — are THEMSELVES ≥4 from solvable
(0 hue-adjacent repairs to distance 4, `[.faces-confusable]`). So the same
cells are read from a consistently-wrong position (or a consistently
glare-corrupted one) in essentially every frame. You cannot average it
away; you must correct a fixed geometric bias. This is the "stable-but-
wrong trap": a systematically mislocated cell renders as a plausible real
colour, so it never looks like an error.

The stage to fix is **which sticker each quad sits on**, and specifically a
CONSISTENT sub-cell offset in it: `grid_detection.cpp`
(`fit_lattice_detailed`, acceptance floor `kMinInlierCells = 4`),
`face_grid_fit.cpp` (`refit_affine`), and `face_observation.cpp`
(`lattice_to_frame_transform`). Look for a fixed bias — a grid that sits
slightly off the true lattice the same way every frame, an off-by-one cell
indexing/phase error, or (on the backlit live5 clip) a specular-glare
pattern that corrupts the same positions repeatedly. NOT the sampling
estimator, NOT colour, NOT the roll/gauge/routing stage, and NOT temporal
stabilisation of randomness (there is no randomness to stabilise).

Caveats on the above, stated honestly: live5's orange face's best read
carries one `unknown` cell (a second-best substitution was used to run the
assembly test), and live5 is severely BACKLIT — some systematic error may
be glare a front-lit capture would remove. The front-lit `good1/good2`
clips were meant to test this but captured 0–1 faces, so the glare-vs-
geometry split is not yet isolated. A clean front-lit six-face recording
would settle how much of the systematic error is lighting.

## Dead ends already tried and measured — do not repeat

- Session-relative colour, centre-referenced palette, per-face exposure
  normalisation, raising/lowering white chroma floors, red/orange boundary
  tuning: all improved census, **none moved assembly distance**.
- Sampling-purity / mask-to-quad: already shipped; patches are clean.
- Face-roll / gauge correction: mathematically cannot help (see 3 above).
- A second parallel classifier (`SessionColorModel`): built, never wired,
  diverged from the real classifier, **deleted**. Do not resurrect a
  parallel colour path — anything you add must be on the live path.
- Free-rotation orientation tracking (24-element lattice, epochs): works
  for *identity* but is not the colour/geometry problem; don't touch it.

## Hypotheses worth trying (in rough priority) — but MEASURE, don't assume

1. **Sub-pixel grid refinement.** The lattice fit is affine/RANSAC over
   noisy candidate centres with a 4-inlier floor; there is no
   `cornerSubPix`-style refinement of the grid against the actual image
   edges. A grid off by a fraction of a cell per frame, fused, could land
   patches on neighbours. Consider refining the fitted lattice to the
   cube's real sticker-boundary lines before sampling.
2. **Temporal grid stabilisation.** Each frame's grid is fit independently
   from scratch; it jitters. Constraining a frame's fit with the previous
   (the tracker pose already exists) could stop the per-cell wander.
3. **Raise the anchor floor / weight by fit quality.** A 4-anchor grid
   extrapolates 5 of 9 cells; those are the ones that land wrong. Prior
   measurement: reading quality tracks anchors-per-frame almost linearly
   (≥6 clean, ~4 garbage). Refusing low-anchor frames trades coverage for
   correctness — measure both.
4. **The honest alternative: guided capture.** Every working open-source
   cube scanner studied requires the user to hold one face square-on to the
   camera (often to an on-screen 3×3 guide), because grid-fit is most
   accurate on a static, fronto-parallel face. Free-rotation is the source
   of the jitter. A guided per-face flow may be what actually closes this,
   and reuses the whole existing colour/assembly stack. Consider building it
   as the shipping path with free-rotation as an experimental layer. This is
   a legitimate answer, not a retreat.

## Guardrails (learned the hard way this project)

- **Measure before you fix.** Localise the defect with the probe/harnesses
  and prove the mechanism before writing code. Several days were lost fixing
  correctly-diagnosed defects that were not the binding constraint.
- **Do not overfit to the corpus.** There are eleven clips. Tuning a
  constant until one clip passes is overfitting. A fix must be a principled
  rule, and must not regress the eleven-clip table (faces captured, distinct
  centre names) — build a develop control and measure before/after.
- **Every behavioural test must be verified to FAIL pre-fix.** A test that
  passes both ways is worthless; this has happened here.
- **Report honestly.** If a change doesn't move the distance, say so first.
  A precise negative that redirects is a good outcome.

## The corpus and tools

- Recordings: `D:/cubeulator-build/vision-test-frames/recordings/` —
  `runA runB runC runD live1 live2 live3 live4 live5 good1 good2`, each a
  dir of `rec_%06d.jpg` + `index.csv`. `runA` and `live5` are the only two
  that fill six faces (both ≥4 from solvable). `good1/good2` were shot under
  a front light + neutral backdrop; `live5` is backlit (worst photometry,
  best coverage). **Frames are JPEG 4:2:0** — chroma is subsampled, so
  measure colour cautiously; production does not subsample.
- Replay harnesses (hidden Catch2 tags, env-gated, skip without corpus):
  - `[.cross-session]` (`CUBE_CROSS_SESSION_DIR=<dir>`): per-face 9 classified
    colours + 9 mean Lab, resolved faces, census, assembly verdict.
  - `[.pattern-id]` (`CUBE_PATTERN_ID_DIR=<dir>`): per reading 9 Lab triples,
    cell-quad corners, per-cell purity/MAD/area, anchor count.
  - `[.assembly-probe]` (`CUBE_ASM_FACES=...`): the distance metric above —
    single-relabel / two-swap / three-cycle distance of a resolved cube.
  - `[.faces-confusable]` (`CUBE_ASM_FACES=...`): searches only HUE-ADJACENT
    repairs (R↔O, Y↔O, G↔Y, W↔Y) to distance 4. If a scan is not repairable
    by hue-adjacent flips, its errors are right-colour-wrong-cell (geometric),
    not colour-naming. This is the tool that proved the fault is positional.
  - `[.faces-probe]` (`CUBE_ASM_FACES=...`): assemble any six explicit face
    strings through the real oracle — hand-driven companion to the above.
  - `[.raw-probe]` (`CUBE_RAW_PROBE_DIR`): assembly distance on the RAW
    per-cell reads, bypassing `resolve_colors`' 9-per-colour forcing.
  - `[.frame-debug]` (`CUBE_DEBUG_FRAME=<png>`, `CUBE_DEBUG_OVERLAY=<out.png>`):
    replay one frame through every stage; the overlay draws the fitted grid
    over the image — **this is your eyes on whether the grid is misplaced.**
- The live app records with `CUBE_VISION_RECORD_DIR`, replays a recording
  with `CUBE_VISION_REPLAY_DIR`, auto-starts scanning with
  `CUBE_VISION_AUTOSTART_SCAN`, dumps a per-pass log with
  `CUBE_VISION_DEBUG_DIR`.

## Build

- Repo: `R:/Cubeulator`, branch `develop` at `e544976`. **Create your own
  worktree and branch off develop** (gitflow naming, e.g.
  `git worktree add C:/aa-wt/Cubeulator/grid-fit -b feature/grid-fit develop`).
- Windows native build needs the gitignored `CMakeUserPresets.json`
  (redirects vcpkg to `D:`); copy it from `R:/Cubeulator/CMakeUserPresets.json`
  into your worktree. Configure/build with preset
  `windows-msvc-fastdebug-d` under an MSVC (`vcvars64`) environment.
- **Fast test tags only** — `[vision] [cube] [scan_assembly]
  [scan_conversion] [model] [lattice] [tracker] [routing] [replay]
  [tracker_sweep]` and the hidden `[.…]` tags. **NEVER** run the slow solver
  tags `[cross] [corners] [middle_edges] [oll] [pll] [solve]
  [stepped_solution]` — they take minutes each.
- Known issue: the **clang** preset has a pre-existing `-Werror` break in
  `hand_presence.cpp` (~line 25) unrelated to this work; use the MSVC preset.

## Definition of done

`live5` (and ideally `runA`) assembles at probe distance 0 into a valid,
solvable cube, with the eleven-clip table not regressed and the fix a
principled rule rather than a corpus-tuned constant. If you conclude the
free-rotation grid-fit cannot reach distance 0 and guided capture is the
answer, that conclusion — backed by measurement — is also done.
