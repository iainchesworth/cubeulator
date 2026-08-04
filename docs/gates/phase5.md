# Phase 5 gate — session-relative colour on the live path

Status: **mechanism landed and regression-pinned; every gate criterion
pends the real corpus or the user.** Code: P5.1 (320fa98) on
feature/pipeline-p5-colour.

## What is in

- SessionCentrePalette: live session-relative classification from the
  cube's own observed centres (<=6 clusters, running means, epoch-scoped
  to the P3.3 gauge epoch). Absolute bands remain exactly where the plan
  puts them: pre-centre bootstrap and out-of-sanity-reach fallback.
- Cluster naming = nearest REFERENCE colour (resolve_colors' rule),
  never the banded classifier - chosen after banded naming was measured
  failing: a drifted cube drifts its centres out of the bands too.
- The two recorded daylight boundary failures are regression tests
  built as physical HSV drifts with drift-together physics (a cube's
  centre and cells drift as one): desaturated green and deep hue-40
  orange both classify correctly by construction once the session has
  seen their own centres (tests/unit/session_palette_test.cpp).
- Synthetic behaviour matches the design: freerot (all faces observed)
  unknowns 5.8% -> 4.5%; steady holds 5.6% honestly unknown - its
  static view never observes centres for the unseen colours, and
  out-of-reach cells are never guessed.

## Exit gate — all pending

- [ ] Per-cell colour accuracy >=99% on keyframes with labelled face
      identity (real corpus; pipeline identity errors count as
      failures, not exclusions).
- [x] The two daylight boundary errors reproduced as fixed (regression
      tests above; absolute verdicts documented non-fatally beside
      them).
- [ ] live5 criterion: dominant per-face raw reads on free-rotation
      clips reach assembly distance <=1 (real corpus; geometry P4 +
      naming P5 jointly).
- [ ] Photometry-lock verification (lit re-run) and per-lighting
      envelopes; warm tungsten documented as a surfaced failure mode,
      not a silent one (user at the camera).
- [ ] App scan-to-solve route onto the new pipeline (paired with
      Phase 6 fusion/commit work; the legacy colour path and its
      lattice machinery delete then).
