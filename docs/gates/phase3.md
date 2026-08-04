# Phase 3 gate — face extraction and identity from pose

Status: **provisional — synthetic corpus only**, same caveat and same
producer as docs/gates/phase2.md; formal close pends the P0.3 real-cube
re-score. Code: P3.1 derive_faces (a791754), P3.2 rectify_face
(3c4a2d8), P3.3 online gauge machinery (717e9ee), P3.4 stage D-F cut
(3a574ee).

## Criteria (plan bounds vs measured, synthetic, per-clip intrinsics)

1. **Face-quad corner error <= the Phase 2 reprojection bound** —
   **PASS on steady**: 720/720 faces matched, quad err 0.25% median /
   0.51% p95 of extent (bounds 1.5%/3%); the face layer adds no error
   beyond the pose. Stressed clips carry exactly the Phase 2 pose
   tails (freerot slip drifts, exit-reentry re-anchors) — owned there,
   not here.
2. **Rectified-face stability <= 1 px at 96x96 between consecutive
   steady frames** — **PASS**: phase-correlation shift 0.200 px median
   / 0.485 px p95 over 239 pairs. The "no simmering" bar per-frame
   fitting could never meet, met by construction.
3. **Identity verified externally (>=99% on labelled keyframes; six
   faces under a single epoch on sweep clips)** — **PENDING**: tracked
   identity is epoch-local gauge by design; mapping it to labelled body
   identity requires the centre-colour arbiter and the disagreement
   counter, which land with the colour phases. The gauge'S OWN
   machinery is measured: online slip detector at 0 false positives /
   840 clean frames, all freerot slips covered (11 declarations / 3 GT
   slips, drift-window over-fire = safe direction). Six-face sweep
   clips do not exist synthetically — real-corpus item.
4. **Overlay with face quads + ids, reviewed** — **PENDING** user
   review; the runner's overlay still draws legacy geometry and grows
   the derived quads alongside the real-corpus session.

Downstream evidence the cut is right (P3.4, cell centres as fraction
of pitch, tracked vs legacy detectors):

| clip | tracked | legacy |
|---|---|---|
| steady | 0.011, 0/6480 misassigned | 0.063, 1993 |
| occlusion | 0.016, 35/8100 | 0.991, 2387 |
| freerot | 0.019 + slip residue | 1.901, 3736 |
| exit-reentry | 0.147 + re-anchor residue | 1.891, 992 |

Cells exist THROUGH occluders because they are projections, not
detections. Track-sourced cells score under the best of 8 grid
alignments (declared face-local frame vs the labels' body-frame order —
the assembler's roll search absorbs this); legacy cells stay strictly
indexed.

## Remaining to close Phase 3

- [ ] P0.3 real-cube corpus re-score (all criteria; six-face sweep and
      labelled identity for criterion 3).
- [ ] Centre-colour arbiter + disagreement counter (colour phases).
- [ ] Derived face quads + ids in the runner overlay, user-reviewed.
- [ ] Slip-window and re-anchor cell residue: tracked by Phase 2's
      open items (registration, re-anchor convergence); P3.3 epochs
      mark the affected frames so aggregation can refuse them.
