# Phase 4 gate — sticker cell subdivision

Status: **provisional — synthetic corpus only**; formal close pends the
P0.3 real-cube re-score (which also re-measures kStickerFraction against
the physical cube). Code: P4.1a sticker-extent cells + exonerated
sampler (a501d3f), P4.1b sub-pixel gap refinement (0d1a02d), on
feature/pipeline-p4-cells.

## Exit gate (vs labelled sticker centres, per clip)

Bounds: cell-centre error median <=10% of sticker pitch, p95 <=25%,
ZERO whole-cell misassignments on the labelled set. Track-sourced cells
score under the best of 8 grid alignments (declared face-local frame vs
the labels' body-frame order); slip/re-anchor frames are epoch-marked
by P3.3 and reported inside the totals below, not hidden.

| clip | median | p95 | misassigned | verdict |
|---|---|---|---|---|
| steady | 1.3% | 2.1% | 0 / 6480 | **PASS** (x8 margin) |
| occlusion | 1.7% | 28.8% | 35 / 8100 | median PASS; p95/misassign = occlusion-window pose residue |
| freerot | 1.7% | 96% | 846 / 7542 | median PASS; tail = quarter-turn slip windows (P3.3-marked) |
| exit-reentry | 13.2% | 141% | 868 / 3114 | re-anchor convergence residue (Phase 2 open item) |

Per-cell Lab deviation bound (<=8 across all nine positions): scored at
the real-corpus session alongside the pitch re-measure - the synthetic
palette's flatness makes it trivially clean and therefore not evidence.

Work-item notes:
- Cells sit at sticker extent (kStickerFraction 0.89 of pitch, a MODEL
  constant) - gap/neighbour pixels excluded by geometry.
- Sub-pixel refinement fits the internal gap lines in rectified space,
  clamped +-0.2 pitch (polish, never re-seat). Flat-trough centring
  learned by measurement: first-minimum-plus-parabola sat 1.5 px off on
  wide gaps; the refiner centres the near-minimum run.
- Sampling is patch_stats_for_corners (quad-masked Lab median + purity)
  via the sample_cell_patch seam - the estimator the purity audit
  exonerated, unchanged. The swap retired a P3.4 8U-vs-float Lab
  convention bug.
- Legacy lattice machinery (fit_lattice_detailed, refit_affine,
  kMinInlierCells, multi-mask union) stays quarantined with the app's
  legacy colour path; deletion lands when the scan-to-solve route moves
  (P5/P6), full removal Phase 7.

## Remaining to close Phase 4

- [ ] P0.3 real-cube re-score, including the physical kStickerFraction
      measure and the Lab-deviation bound.
- [ ] Stressed-clip tails: owned by the Phase 2/3 open items
      (re-anchor convergence, registration slips), not by this phase's
      geometry.
