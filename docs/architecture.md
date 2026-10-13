# Architecture

## Source layout

```
src/
  lib/            cube:: — GUI-free core (solver, vision, render, i18n)
  app/
    qt/            Qt 6 Quick/QML desktop shell
    platform/       one directory per OS: its implementations of each
                    interfaces/ header, and the platform.cmake that selects them
    interfaces/     camera.hpp, gpu_surface.hpp, inference_env.hpp, paths.hpp,
                    platform_info.hpp, crash_handler.hpp
  render/          bgfx setup and the technical-drawing render pipeline
tests/
  unit/
  integration/
```

`src/lib/` is Qt-free and platform-free by design: independently unit-testable,
reusable behind any shell. Platform differences are isolated to one directory
per OS under `src/app/platform/`; `cmake/Platform.cmake` picks the directory
once, and its `platform.cmake` names the files compiled and the libraries
linked. No source file, QML file or other CMake file branches on the
platform — see [Platform isolation](platform-isolation.md) for the rule, how a
platform is added, and the CI check that holds it.

## C++23 patterns in use

- **`Result<T> = std::expected<T, CubeError>`** ([error.hpp](https://github.com/iainchesworthlabs/cubeulator/blob/develop/src/lib/include/cube/error.hpp))
  for every fallible operation, composed with `and_then`/`transform`/`or_else`
  rather than exceptions for expected failure modes.
- Build-time git version stamp (`cmake/GenerateVersion.cmake` +
  `version.hpp.in`), re-run on every build so it tracks new commits without a
  reconfigure.

## GUI shell strategy

Every platform — desktop (Windows/Linux/macOS) and mobile (Android/iOS) —
shares the same Qt 6 Quick/QML shell (`src/app/qt/`). There is no
native-per-platform shell split: Android is packaged via `androiddeployqt`,
iOS via Qt's Xcode integration, both loading the same `Main.qml`.

## What's implemented today vs. planned

`src/lib/`'s solver and vision modules are both real, and now wired
together end to end. `cube::solver::solve()` solves a cube via the classic
beginner (layer-by-layer) method - cross, white corners, middle edges,
2-look OLL, 2-look PLL - the same staged structure the app teaches. The
first two layers are found by targeted per-piece search; the last layer
composes the standard 2-look algorithms (edge flip, Sune, A-perm, U-perm),
which keeps every solve bounded to seconds for interactive mobile use (see
`src/lib/src/solver/`). The stages' moves are then tidied in one linear pass,
so no two neighbouring moves turn the same side - including across a stage
boundary, where a stage can end up with no moves at all
(`merge_adjacent_moves`). `cube::vision` runs a full OpenCV capture-to-state
pipeline - a RANSAC-style lattice fit for grid detection (tolerant of
uneven lighting, background clutter, and a minority of missed/occluded
cells, not just a clean dead-on shot), colour classification,
stability/hand-presence gating, auto-advance - with manual per-face
confirmation as the fallback (see `src/lib/src/vision/`).
The image pipeline end to end - capture, the per-pass analysis stages, the
GUI-thread commit path, and where colour decisions are made - is diagrammed
in [Vision pipeline](vision-pipeline.md), together with the measured
weaknesses of each stage.
The scan controller (`src/app/qt/controllers/direct_scan.hpp`) reads the
cube's sides with `cube::vision::read_side`, and once six sides make exactly
one valid cube hands a `cube::solver::Cube` to
`src/app/qt/solve_controller.hpp` (`solveCube`), which solves it on a worker
thread. A scan that does not yet make a valid cube is never solved: the scan
screen keeps asking for the sides again. `CubeView.qml` draws a per-move arrow (face + direction) accurate to
the solver's actual output, with a hidden-face badge for moves on faces
this fixed 3-face iso view doesn't show. `render` (bgfx) and `i18n` remain
stub targets that compile and link but contain no real logic.

Planned, in order:

1. A trained ML-based cube/sticker detector, as a more robust alternative
   to the classical OpenCV lattice fit under messier backgrounds/lighting
   than it was designed to tolerate.
2. Real (not snap/no-op) `CubeView.qml` move animation, now that the
   separate fully-rendered 3D cube work has landed - it already has a
   single seam (`animateMove()`) for this to plug into.
3. bgfx technical-drawing render pipeline, if the QML Canvas approach turns
   out not to be sufficient on its own.
4. i18n locale resource loading.

The app shell's own 3D cube (`src/app/qt/Cube3DView.qml`, shown on the launch
screen and in Settings) and its three render styles — Physically-Based
Realism, Instructional Glow, Borderlands Cel-Shading — are implemented
entirely in Qt Quick3D, not `src/render/`'s bgfx pipeline. Quick3D's own
`CustomMaterial`/`Effect`/`ExtendedSceneEnvironment` extensibility covered all
three (hard-banded cel shading, inverted-hull ink outlines via a front-face-
culled duplicate mesh, and a halftone/vignette post-processing pass), so
the bgfx scope above is untouched and still available for its own
progress-overlay/step-animation work.
