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

This repository is currently the Spec 00 skeleton: `src/lib/`'s
solver/vision/render/i18n modules and `src/render/` are stub targets that
compile and link but contain no real logic. That logic lands in later specs,
in this order:

1. Cube state model + Kociemba solving from a hardcoded scramble.
2. OpenCV capture-to-state pipeline with manual per-face confirmation.
3. Edge-AI live tracking (coverage detection, auto-advance).
4. bgfx technical-drawing render (progress overlay, step animation).

The app shell's own 3D cube (`src/app/qt/Cube3DView.qml`, shown on the launch
screen and in Settings) and its three render styles — Physically-Based
Realism, Instructional Glow, Borderlands Cel-Shading — are implemented
entirely in Qt Quick3D, not `src/render/`'s bgfx pipeline. Quick3D's own
`CustomMaterial`/`Effect`/`ExtendedSceneEnvironment` extensibility covered all
three (hard-banded cel shading, inverted-hull ink outlines via a front-face-
culled duplicate mesh, and a halftone/vignette post-processing pass), so
Spec 4's bgfx scope above is untouched and still available for its own
progress-overlay/step-animation work.
