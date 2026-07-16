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

The desktop shell (`src/app/qt/`) is Qt 6 Quick/QML. Mobile shells are
planned to be **native** (SwiftUI on iOS, Jetpack Compose on Android) rather
than an extended QML target — a deliberate default, not a QML limitation, to
give the camera/GPU/on-device-ML flagship flow a platform-native feel. This
default should be revisited deliberately before mobile UI work starts, not
left standing by default alone.

## What's implemented today vs. planned

This repository is currently the Spec 00 skeleton: `src/lib/`'s
solver/vision/render/i18n modules and `src/render/` are stub targets that
compile and link but contain no real logic. That logic lands in later specs,
in this order:

1. Cube state model + Kociemba solving from a hardcoded scramble.
2. OpenCV capture-to-state pipeline with manual per-face confirmation.
3. Edge-AI live tracking (coverage detection, auto-advance).
4. bgfx technical-drawing render (progress overlay, step animation).
