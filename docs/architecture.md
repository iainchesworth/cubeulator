# Architecture

## Source layout

```
src/
  lib/            cube:: — GUI-free core (solver, vision, render, i18n)
  app/
    qt/            Qt 6 Quick/QML desktop shell
    platform/       one implementation per OS behind each interfaces/ header
    interfaces/     camera.hpp, gpu_surface.hpp, inference_env.hpp, paths.hpp
  render/          bgfx setup and the technical-drawing render pipeline
tests/
  unit/
  integration/
```

`src/lib/` is Qt-free and platform-free by design: independently unit-testable,
reusable behind any shell. Platform differences are isolated to one file per
OS under `src/app/platform/`, selected by CMake at configure time
(`if(ANDROID) / elseif(IOS) / elseif(WIN32) / elseif(APPLE) / elseif(UNIX)`)
— never `#ifdef` in shared code.

## C++23 patterns in use

- **`Result<T> = std::expected<T, CubeError>`** ([error.hpp](https://github.com/iainchesworth/cubeulator/blob/develop/src/lib/include/cube/error.hpp))
  for every fallible operation, composed with `and_then`/`transform`/`or_else`
  rather than exceptions for expected failure modes.
- **Deducing-`this` fluent builders**
  ([scan_session_config.hpp](https://github.com/iainchesworth/cubeulator/blob/develop/src/lib/include/cube/scan_session_config.hpp))
  for types with several optional construction parameters.
- Build-time git version stamp (`cmake/GenerateVersion.cmake` +
  `version.hpp.in`), re-run on every build so it tracks new commits without a
  reconfigure.

## GUI shell strategy

Every platform — desktop (Windows/Linux/macOS) and mobile (Android/iOS) —
shares the same Qt 6 Quick/QML shell (`src/app/qt/`). There is no
native-per-platform shell split: Android is packaged via `androiddeployqt`,
iOS via Qt's Xcode integration, both loading the same `Main.qml`.

## What's implemented today vs. planned

`src/lib/`'s solver and vision modules are both real. `cube::solver::solve()`
solves a cube via the classic beginner (layer-by-layer) method - cross,
white corners, middle edges, 2-look OLL, 2-look PLL - the same staged
structure the app teaches, found by search rather than a memorised
algorithm table (see `src/lib/src/solver/`). `cube::vision` runs a full
OpenCV capture-to-state pipeline - grid detection, colour classification,
stability/hand-presence gating, auto-advance - with manual per-face
confirmation as the fallback (see `src/lib/src/vision/`). There's no wiring
yet between the two: `src/app/qt/solve_controller.hpp` bridges the solver
into QML off a `random_scramble()`'d cube rather than a real scan.
`CubeView.qml` draws a per-move arrow (face + direction) accurate to the
solver's actual output, with a hidden-face badge for moves on faces this
fixed 3-face iso view doesn't show. `render` (bgfx) and `i18n` remain stub
targets that compile and link but contain no real logic.

Planned, in order:

1. Wire the real scan (`cube::vision`) into `SolveController` in place of
   `random_scramble()`.
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
