# Architecture

## Source layout

```
src/
  lib/            cube:: — GUI-free core (solver, vision; i18n is a placeholder)
    variants/       the two cube/profiling.hpp variants CMake chooses between
  app/
    qt/            Qt 6 Quick/QML shell, shared by every platform
    desktop/        the shell's desktop-only code (window geometry, keep-awake)
    i18n/           the language list and the translation catalogues
    platform/       one directory per OS: its implementations of each
                    interfaces/ header, and the platform.cmake that selects them
    interfaces/     camera.hpp, paths.hpp, platform_info.hpp,
                    crash_handler.hpp, desktop_shell.hpp
tests/
  unit/
  integration/
  system/         draws the real app in a window
  perf/           budgets on the solver and the reader
  i18n/
  platform/       test cases only one OS can run
tools/            cube_scan, cube_solve_bench, the generators and the CI checks
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

Every platform — desktop (Windows/Linux/macOS), mobile (Android/iOS) and the
browser (WebAssembly) — shares the same Qt 6 Quick/QML shell
(`src/app/qt/`). There is no native-per-platform shell split: Android is
packaged via `androiddeployqt`, iOS via Qt's Xcode integration, and the web
build is Qt for WebAssembly's one `.wasm` module, all loading the same
`Main.qml`. (That is the design; the iOS app target is not built yet - iOS is
build-only today, see the [FAQ](faq.md).)

## What's implemented today vs. planned

`src/lib/`'s solver and vision modules are both real, and wired
together end to end. `cube::solver::solve()` solves a cube via the classic
beginner (layer-by-layer) method - cross, white corners, middle edges,
2-look OLL, 2-look PLL - the same staged structure the app teaches. The
first two layers are found by targeted per-piece search; the last layer
composes the standard 2-look algorithms (edge flip, Sune, A-perm, U-perm),
which keeps every solve bounded to seconds for interactive mobile use (see
`src/lib/src/solver/`). The stages' moves are then tidied in one linear pass,
so no two neighbouring moves turn the same side - including across a stage
boundary, where a stage can end up with no moves at all
(`merge_adjacent_moves`). That is `SolveMethod::Beginner`, the default; two
shorter methods sit beside it behind the same entry point - `Intermediate`
(CFOP, about 52 moves) and `Advanced` (Kociemba's two-phase search, about 20) -
described, with their measurements, in [Solve methods](solver-methods.md). `cube::vision` reads a cube one side at a time:
`read_side` finds the sticker lattice of whichever side is held square to
the camera and samples its nine colours, `SideScan` groups the reads into
sides by their own content, and `resolve_colors` and
`cube::solver::assemble_scanned_faces` turn six sides into a cube (see
`src/lib/src/vision/` and `src/lib/src/solver/scan_assembly.cpp`). There is no
pose tracker and no learned model.
The image pipeline end to end - capture, the photometry lock, the reader,
the side grouping, and where colour decisions are made - is described in
[Vision pipeline](vision-pipeline.md), together with its known limits.
The scan controller (`src/app/qt/controllers/direct_scan.hpp`) reads the
cube's sides with `cube::vision::read_side`, and once six sides make exactly
one valid cube hands a `cube::solver::Cube` to
`src/app/qt/solve_controller.hpp` (`solveCube`), which solves it on a worker
thread. Which of the three methods it uses is `SolveController.method`,
chosen on the solve screen and in settings (`SolveMethodPicker.qml`) and
kept by `SettingsState.solveMethod`; changing it mid-solve solves on from
the cube as it stands, off the UI thread, and keeps the hold the camera has
seen (`solutionContinuesCube`). The screens word each stage by its
`cube::solver::Stage` value, since the methods have different stages. A scan
that does not yet make a valid cube is never solved: the scan
screen keeps asking for the sides again. The solve screen draws the cube in 3D
(`Cube3DView.qml`): the layer the next move turns nudges a little way in the
direction it has to go and back (`guideFace`), and stepping turns it all the way
(`playMove()`). `CubeView.qml`, the flat technical-drawing cube, is what the
completion screen draws. `cube::i18n` in `src/lib` is still a placeholder
(`not_yet_implemented()`); translations are loaded by the app shell's
`LanguageManager` (`src/app/qt/i18n/`), described in [Translations](i18n.md).

Planned, in the order of the [Recovery plan](recovery-plan.md):

1. A review screen showing all 54 stickers, with tap to correct, which is also
   hand entry for a cube the camera cannot read.
2. The live gate: fresh scrambles at the desk, from camera to correct solve
   steps with no manual fix.
3. Reading a side held up to about 35 degrees off square-on, so that nobody
   has to hold still.

## GPU rendering

There is one renderer, and it is Qt's. The app shell's own 3D cube
(`src/app/qt/Cube3DView.qml`, shown on the launch screen, in Settings and on the
solve screen) and its three render styles — Physically-Based Realism, Instructional Glow,
Borderlands Cel-Shading — are implemented entirely in Qt Quick3D. Quick3D's
own `CustomMaterial`/`Effect`/`ExtendedSceneEnvironment` extensibility covered
all three (hard-banded cel shading, inverted-hull ink outlines via a front-face-
culled duplicate mesh, and a halftone/vignette post-processing pass); the flat
technical-drawing cube is the QML Canvas in `CubeView.qml`. Which graphics
API runs underneath (Direct3D 11/12, Vulkan, OpenGL, Metal) is Qt's RHI
choice, surfaced through `platform_info`.

The progress overlay and step animation that were once pencilled in for a
separate bgfx pipeline are UI animation, which belongs in QML (move animation is
`Cube3DView.playMove()`). bgfx itself was a declared vcpkg dependency that nothing
linked, behind a stub target with no rendering code; it was removed so that
every CI leg, including the mobile ones, stops building seven ports for it.
If custom GPU drawing is ever needed inside the Qt Quick scene, the supported
route is `QQuickRhiItem` (Qt Quick, in the Qt 6.12 install), which draws with
Qt's own RHI instead of bringing a second graphics abstraction and swap chain.
