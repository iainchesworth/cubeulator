# Testing & coverage

Two layers, both Catch2:

- **`tests/unit/`** — fast, isolated, one behaviour each. Currently exercises
  `cube::error` (`Result<T>`/`CubeError`), `cube::version`, and
  `ScanSessionConfig`'s fluent builder.
- **`tests/integration/`** — exercises several components together; the
  current test drives the CMake-selected platform implementations
  (`cube::platform::paths`, `cube::platform::inference_env`,
  `cube::platform::camera`) end to end rather than any single interface in
  isolation.

Run them with `ctest --preset <preset>` after building. Tests are
independent Catch2 cases, so parallel runs are safe - pass `-j <n>` (or set
`execution.jobs` in a personal `CMakeUserPresets.json` test preset, where
machine-specific tuning belongs; the committed presets leave it unset so
CI's 4-core runners aren't oversubscribed).

## Debug is slow - use `fastdebug` for local iteration

A plain `Debug` build (`windows-msvc-debug`/`linux-gcc-debug`/`macos-clang-debug`)
compiles unoptimized *and* enables ASan/UBSan (see
[Sanitizers.cmake](../cmake/Sanitizers.cmake)). The solver's first-two-layer
searches (`cube::solver::detail::bfs_search`, see
[bfs_search.hpp](../src/lib/src/solver/bfs_search.hpp)) visit up to millions
of nodes per stage, and an unoptimized build alone - independent of the
sanitizers - is well over an order of magnitude slower there than Release,
turning a fast Release corpus run into many Debug minutes.

For local dev/test iteration, prefer the `fastdebug` preset
(`windows-msvc-fastdebug`/`linux-gcc-fastdebug`/`macos-clang-fastdebug`):
optimizations on (`RelWithDebInfo`), sanitizers and iterator-debug checks off
(both are gated on `CMAKE_BUILD_TYPE=Debug` specifically), debug info still
present for breakpoints/stepping. Reach for a true `*-debug` preset only when
you actually want ASan/UBSan coverage.

## Slow tests

The solver's corpus tests (`solve_cross`, `solve_corners`,
`solve_middle_edges`, `solve_oll`, `solve_pll`), both `solve` cases, and
the `SteppedSolution` tests (each of the latter runs a full solve) are
tagged `[slow]` - every one drives one or more random scrambles through
the searched first-two-layer stages. (The last layer itself is fixed-algorithm and takes
milliseconds - see [last_layer.hpp](../src/lib/src/solver/last_layer.hpp) -
so the corpus cost is the cross/corners/middle-edges searches.) Skip them
for a fast local loop, or run only them:

```
ctest --preset <preset> -LE slow   # everything except the slow solver corpus tests
ctest --preset <preset> -L slow    # just the slow solver corpus tests
```

## QML tests (the solve screen)

`cube_qml_tests` loads the app's own QML - `Cube3DView.qml` and
`SolveScreen.qml`, with the app's own controllers behind them - and reads the
3D cube back out of its scene: where each sticker sits and which way it
faces, taken from the nodes' transforms. That is held to
`cube::solver::Cube`, so the direction of every animated turn is checked
against `Cube::apply` on what is actually drawn. No window is shown and
nothing is rendered; it runs headless (offscreen platform off Windows) and
is part of `ctest`.

Camera frames in these and the controller tests are drawn
([drawn_cube.hpp](../tests/unit/drawn_cube.hpp)), never recorded: a
recording shows whoever was holding the cube.

To *look* at the solve screen rather than check it, a hidden case puts the
real screen in a window parked off-screen, drives it with drawn frames, and
saves what it renders - one picture per run, chosen by name:

```
set CUBE_GALLERY_DIR=C:\some\dir
set CUBE_GALLERY_SHOT=3-matched-another-way
cube_qml_tests "[.gallery]"
```

The shot names are in
[solve_screen_gallery_test.cpp](../tests/unit/solve_screen_gallery_test.cpp).
It needs a GPU, so `ctest` never selects it.

To *use* the solve screen without scanning a cube first, start the app with
`CUBE_SOLVE_DEMO=<seed>`: it opens on the solve screen with a cube scrambled
from that seed, camera off. `CUBE_SOLVE_DEMO_MOVE=<n>` opens it `n` moves in,
and the solution is logged (run with `QT_FORCE_STDERR_LOGGING=1` to see it).

## Hardware tests (camera)

Two integration cases open a real camera, so they are fenced twice over: a
leading dot in the tag hides them from a default Catch2 run (Catch2 only
runs a hidden tag when it is named explicitly), *and* each SKIPs unless its
own environment variable is set. Neither can run in CI or on a machine with
no camera, and `ctest` never selects them.

```
set CUBE_CAMERA_PROBE=1      && cube_integration_tests "[.camera-probe]"
set CUBE_CAMERA_LOCK_TEST=1  && cube_integration_tests "[.camera-lock]"
```

- `[.camera-probe]` reports what the camera driver will actually let the app
  control: what each property reads, whether a write survives a read-back,
  and - separately - whether it moves the pixels. A property that reads back
  but leaves the image alone is a common DirectShow trap, and the report
  distinguishes the two.
- `[.camera-lock]` verifies the photometry lock (see
  [`interfaces/camera.hpp`](../src/app/interfaces/camera.hpp)) on real
  hardware: frame-to-frame drift of mean L/chroma with the automatics
  running versus locked, the explicit-override path, that `stop()` puts the
  driver's settings back, and open+first-frame latency before and after.
  Point the camera at a lit, static scene first - on a dark or covered lens
  both modes read zero and look equally stable, which the test warns about
  rather than passing off as success.

Neither asserts that the hardware supports anything: a driver is allowed to
refuse, and reporting that honestly is the point. They fail only if the app
itself misbehaves.

## Translation tests

`ctest -L i18n` (or `-R i18n` plus the `[i18n]` Catch cases) runs the
translation gates: the catalogues against `docs/design/strings.json`, a lint
for user-visible literals, the ids the code asks for, what each catalogue
holds, a raw id never reaching the screen, the language manager, and the
converted screens switched live between English, the pseudo-locale and Hebrew.
They need Python 3 and Qt's Linguist tools, which the Qt install has. What
each one is for, and what to do when it fails, is in [Translations](i18n.md).

## Coverage gate

CI's `Coverage (Linux, GCC)` job builds the `linux-gcc-coverage` preset
(`CUBEULATOR_ENABLE_COVERAGE=ON`, `CUBEULATOR_BUILD_APP=OFF`) and runs:

```
gcovr --root . --filter 'src/.*' \
  --fail-under-line 80 --fail-under-branch 80
```

New behaviour should come with a test in the same PR (TDD by convention);
`src/lib/` in particular has no platform/GUI dependency to complicate
mocking, so it should clear meaningfully more than the 80% floor.
