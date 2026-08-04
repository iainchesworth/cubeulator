# Phase 2 gate — 6-DoF pose lock and tracking

Status: **provisional — synthetic corpus only.** Every offline number
below is measured on the exact-label synthetic corpus
(`D:\cubeulator-build\synth-corpus`); the gate formally closes when the
same runner re-scores the real-cube corpus (P0.3, needs the user at the
camera) and the two live procedures at the bottom have been run once.
Metric definitions: docs/pipeline-metrics.md. Producer:
`cube_pipeline_runner corpus <root> --detector <onnx> --tracker m3t`.

## Offline criteria (plan bounds vs measured, synthetic)

| Criterion | Bound | steady | occlusion | exit-reentry | freerot | Verdict |
|---|---|---|---|---|---|---|
| Locked-and-correct (free-rotation) | >=95% | 100% | — | — | 82.2% | **OPEN** |
| Locked-and-correct (occlusion-stress) | >=85% | — | 99.6% | 55.6% | — | **OPEN** (exit-reentry re-anchor convergence) |
| Corner reprojection median | <=1.5% extent | 0.35% | 0.45% | 0.98% | 0.59% | PASS (provisional) |
| Corner reprojection p95 | <=3% extent | 0.74% | 5.2% | 28.9% | 14.9% | **OPEN** (slip/re-anchor tails) |
| Bias (anti-live5) | <=0.5% extent | 0.59% | 1.59% | 5.7% | 2.33% | **OPEN** (close on steady) |
| Jitter (steady segments) | <=0.3% extent | 0.40% | n/a | n/a | n/a | **OPEN** (close) |
| Re-acquisition median | <=300 ms, >=10 events | — | 0 ms / 3 ev | 231 ms / 4 ev | — | PASS on 7 events; real corpus must supply >=10 |
| Layer-turn survival | >=90% of turn segments | n/a — no synthetic layer-turn clip; real corpus item | | | | **OPEN** |

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
3. Wiring the stored intrinsics into the live loop and the runner
   (replacing `approximate_camera_matrix` end to end, with the
   approximation kept as fallback) is the remaining P2.9 code item —
   tracked as its own change so the before/after shows up in the
   corpus table.

## Remaining to close Phase 2

- [ ] User: 60-second scripted live run (procedure above).
- [ ] User: chessboard calibration capture.
- [ ] Wire stored intrinsics through detect-track loop + runner.
- [ ] P0.3 real-cube corpus session; re-score this table on it
      (including >=10 re-acquisition events and a layer-turn clip).
- [ ] Freerot locked-and-correct + registration slips: owned by
      Phase 3 gauge machinery (measured out of refinement's reach).
