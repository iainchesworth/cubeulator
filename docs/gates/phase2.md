# Phase 2 gate — 6-DoF pose lock and tracking

Status: **provisional — synthetic corpus only.** Every offline number
below is measured on the exact-label synthetic corpus
(`D:\cubeulator-build\synth-corpus`); the gate formally closes when the
same runner re-scores the real-cube corpus (P0.3, needs the user at the
camera) and the two live procedures at the bottom have been run once.
Metric definitions: docs/pipeline-metrics.md. Producer:
`cube_pipeline_runner corpus <root> --detector <onnx> --tracker m3t`.

## Offline criteria (plan bounds vs measured, synthetic)

Scored with each clip's own intrinsics (fx=700 renders; the f=width
approximation assumed 640 — the runner auto-loads intrinsics.txt/.json
per clip and says so).

| Criterion | Bound | steady | occlusion | exit-reentry | freerot | Verdict |
|---|---|---|---|---|---|---|
| Locked-and-correct (free-rotation) | >=95% | 100% | — | — | 71.0% | **OPEN** |
| Locked-and-correct (occlusion-stress) | >=85% | — | 99.6% | 39.9% | — | **OPEN** (exit-reentry re-anchor convergence) |
| Corner reprojection median | <=1.5% extent | 0.25% | 0.30% | 3.1% | 0.52% | **OPEN** on exit-reentry only |
| Corner reprojection p95 | <=3% extent | 0.49% | 5.8% | 33.9% | 20.9% | **OPEN** (slip/re-anchor tails) |
| Bias (anti-live5) | <=0.5% extent | **0.28% PASS** | 1.64% | 7.2% | 3.54% | steady PASSES; stressed clips carry re-anchor/slip residue |
| Jitter (steady segments) | <=0.3% extent | 0.41% | n/a | n/a | n/a | **OPEN** (close) |
| Re-acquisition median | <=300 ms, >=10 events | — | 0 ms / 3 ev | failed (2 of 4 events) | — | **OPEN** — see re-anchor note |
| Layer-turn survival | >=90% of turn segments | n/a — no synthetic layer-turn clip; real corpus item | | | | **OPEN** |

Intrinsics note: correcting the camera passed the steady bias gate and
cut steady/occlusion errors ~30%, but exit-reentry measures WORSE under
the honest camera (was 55.6% locked-correct / 231 ms / 0 failed under
the approximation): M3T's post-reentry convergence is sensitive to the
projection model, and 4 events is small-N. Re-anchor convergence was
already this table's biggest open item; the corrected camera makes its
true size visible. It re-scores on the real corpus with >=10 events.

What the numbers say (P2.7 measurement, P2.8 refinement — see those
commits for the full A/B history):

- The tracker holds through full occlusion (re-acquisition 0 ms: it
  never actually loses the cube) and re-locks in 231 ms median after a
  frame exit.
- Free rotation slips a quarter-turn registration ~2x per 400 frames;
  the slips are invisible to the loop's best-of-24 health check by
  construction and were measured NOT fixable by edge/lattice
  refinement (feedback A/B in the P2.8 commit). Registration is
  identity — Phase 3's gauge machinery owns it. The out-of-bound
  frames around slip drifts are what hold free-rotation
  locked-and-correct at 82.2%.
- Post-reentry re-anchors land fast but converge sloppily —
  exit-reentry's 55.6% is re-anchor quality, not steady tracking.
- Steady jitter (0.40%) and bias (0.59%) sit a third of the way from
  their pre-refinement values (0.46%/0.86%) to the gates
  (0.30%/0.50%). Both re-score on the real corpus with calibrated
  intrinsics before any further tuning is spent on them.

## Live criteria (scripted 60-second run — needs the user)

Bounds: on-screen locked fraction >=90% while the cube is in frame;
camera-to-overlay latency <=120 ms; 3D model mirrors rotation and
position with no visible lag or detachment (recorded for the record).

**Run 1 (2026-08-04, cardboard-02, workstation webcam, 105 s, 2058
passes at 19.5 Hz — clip + CSV at D:\cubeulator-corpus\gates\
live-gate-01):**

- Latency: median 3 ms, p95 48 ms — **PASS** (with the +33 ms camera
  allowance still well under 120 ms).
- Locked fraction: 77.3% raw / 97.8% over passes the loop actually ran.
  The gap was a measured BUG, fixed off the back of this run: in track
  mode the idle gate consulted the never-fed legacy tracker (always
  lost) and the cool-hue statistic (near zero on cardboard), so it
  idled 432 passes while M3T was locked. Occlusion behaviour matched
  the synthetic finding (tracker held straight through the palm);
  exit/re-entry cost one 2.6 s gap; genuine track loss totalled 35 of
  2058 passes, 22 re-anchors. Re-run with the fix below.

**Run 2 (2026-08-04, same cube/camera/script, idle-gate fix in — 152 s,
3041 passes at 20.0 Hz, D:\cubeulator-corpus\gates\live-gate-02):**

- Locked fraction: **98.4% raw — PASS** (>=90% bound). Idle skips down
  from 432 to 12; longest untracked run 4 passes (~200 ms); 37
  re-anchors. Caveat stated honestly: the raw fraction counts any
  zombie-locked passes during the out-of-frame segment in both
  numerator and denominator; bounding that segment generously moves the
  number by <0.3 points - it passes any way it is sliced. The labelled
  offline re-score of this clip supersedes this estimate.
- Latency: median 7 ms, p95 40 ms — **PASS**.

Live-gate verdict: **both live bounds met on cardboard**; the formal
close still wants the same script once on a real stickered cube (P0.3
session) for the colour-content-independent claim to be complete.

Procedure:

1. Launch the app with the measurement channels on:

   ```
   CUBE_VISION_GATE_LOG=D:\cubeulator-corpus\gates\live-gate-01.csv
   CUBE_VISION_HUD=1
   CUBE_VISION_RECORD_DIR=D:\cubeulator-corpus\gates\live-gate-01
   ```

2. Start a scan and keep the cube in frame for 60 seconds: slow free
   rotation, two or three deliberate layer turns, one full occlusion
   (palm over the cube), one exit/re-entry.
3. Stop the scan (or close the app). The CSV's `# summary` line carries
   `tracked_frac` (the locked-fraction gate) and
   `latency_median_ms`/`latency_p95_ms`.
4. Latency caveat, stated with the number: the logged latency is
   frame-dequeue -> result-handled on the GUI thread. It excludes
   camera-driver capture latency and the render present; budget ~1
   frame time (33 ms at 30 fps) on top when comparing against the
   120 ms bound.
5. The recording made alongside becomes a real-corpus clip: label it
   and re-score offline for the formal gate table.

## Camera calibration (one-time per camera — needs the user)

The `f = frame width` approximation is a measured weakness (the synth
corpus renders at fx=700 vs the approximation's 640). Calibrate once:

1. Print a chessboard (10x7 squares -> 9x6 inner corners) and record
   ~20 full-board views with the target camera:
   `CUBE_VISION_RECORD_DIR=D:\cubeulator-corpus\calib-01` (the fullres/
   stream is the input; vary pose and fill the frame).
2. Solve and store:

   ```
   cube_pipeline_runner calibrate D:\cubeulator-corpus\calib-01\fullres --pattern 9x6 --out D:\cubeulator-corpus\intrinsics-<camera>.txt
   ```

   The tool prints the measured error of the f=width fallback.
3. Point the live app at the stored file: `CUBE_VISION_INTRINSICS=
   D:\cubeulator-corpus\intrinsics-<camera>.txt` — every camera-matrix
   consumer in the loop (PnP anchor, M3T init, projection, refinement)
   then uses the measured values, rescaled per frame size; unset keeps
   the f=width fallback. The runner auto-loads per-clip
   intrinsics.txt/.json the same way.

## Remaining to close Phase 2

- [ ] User: 60-second scripted live run (procedure above).
- [ ] User: chessboard calibration capture.
- [x] Wire stored intrinsics through detect-track loop + runner
      (auto per-clip offline, CUBE_VISION_INTRINSICS live).
- [ ] P0.3 real-cube corpus session; re-score this table on it
      (including >=10 re-acquisition events and a layer-turn clip).
- [ ] Exit-reentry re-anchor convergence (the table's biggest gap,
      39.9% vs 85%): investigate with the real corpus in hand.
- [ ] Freerot locked-and-correct + registration slips: owned by
      Phase 3 gauge machinery (measured out of refinement's reach).
