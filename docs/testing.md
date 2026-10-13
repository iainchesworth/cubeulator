# Testing & coverage

Three layers, all Catch2:

- **`tests/unit/`** — fast, isolated, one behaviour each. Currently exercises
  `cube::error` (`Result<T>`/`CubeError`), `cube::version`, the solver, the
  direct-scan reader and its sides, the recorder, and the held-cube fit.
- **`tests/integration/`** — exercises several components together; the
  current test drives the CMake-selected platform implementations
  (`cube::platform::paths`, `cube::platform::platform_info`,
  `cube::platform::camera`) end to end rather than any single interface in
  isolation.
- **`tests/system/`** — the whole application, from `Main.qml`, driven by
  mouse, touch and key events in a real window. See
  [System tests](#system-tests).

Tests do not ask which OS they are on (see
[Platform isolation](platform-isolation.md)): what differs per platform comes
from the platform's `platform.cmake` (the Qt plugin to default to, how a
desktop session reaches the process), and a test case only one platform can run
lives in `tests/platform/<name>/`. The `platform_fragments_satisfy_contract`
test holds every platform's `platform.cmake` to the contract on whichever host
it runs, so the platforms a build does not target are still checked.

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
so the corpus cost is the cross/corners/middle-edges searches.) The
intermediate and advanced solvers' corpus tests (`cfop_test.cpp`,
`two_phase_test.cpp`) carry the same tag, though they are far faster - tens of
milliseconds a solve, not seconds - because the advanced method's lookup
tables take a few seconds to build in an unoptimised sanitizer build. Skip
them for a fast local loop, or run only them:

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

To find out why the solve screen's camera took a move the way it did, start
the app with `CUBE_HELD_LOG=<file>`. Each time what it makes of the cube
changes, one line is appended: the move asked for, the verdict, how many of
the nine stickers were found, and how far the side in view sits from the cube
as it stood and from the cube after the move. Numbers only - no picture is
kept.

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

## System tests

`cube_system_tests` boots the application the way `main()` does and walks it
the way a person would. One command runs it:

```
cmake --build --preset <preset> --target cube_system_tests
ctest --preset <preset> -L system --output-on-failure
```

That is three ctest entries: `system.walk` (the suite, one session of the
app), `system.reach` (the reach gate, below; reads the walk's report, needs
no display) and `system.relaunch` (a second launch with a setting already in
the store).

On a desktop that opens a window and drives it; on a headless Linux box give
it a display and software GL (this is what CI does):

```
QT_QPA_PLATFORM=xcb LIBGL_ALWAYS_SOFTWARE=1 \
  xvfb-run -a -s "-screen 0 1280x1024x24" \
  ctest --preset linux-gcc-coverage -L system --output-on-failure
```

It writes `<build>/system-test-output/` (or `$CUBE_SYSTEM_TEST_OUT`):
`screens/*.png` (the screenshot tour), `reach.json` and `reach.txt` (what the
run visited). CI uploads that directory as the `system-test-output` artifact.

The `system` label keeps it out of the plain `ctest` runs of the build legs
(`-LE system`): it needs somewhere to draw.

### How it works

- **One start-up.** `cube::app::AppRuntime` (`src/app/qt/app_runtime.hpp`)
  constructs the controllers, registers them as QML singletons and owns the
  engine. `main()` and the tests construct it identically, so there is no
  second copy of the wiring to drift.
- **A camera that shows what the test says.** `cube::platform::camera` is a
  set of free functions with one definition per OS. The test executable
  defines them itself (`tests/system/fake_camera.cpp`), so the linker never
  needs the platform's camera object and `DirectScan`, `CameraController`
  and the screens run unmodified. The frames are drawn by
  `tests/unit/drawn_cube.hpp`: nobody is in the picture, and none is ever
  committed.
- **Input is input.** Presses go to the `QQuickWindow` through QTest
  (`mouseClick`, `touchEvent`, `keyClick`) at the centre of a control found
  by `objectName`. A control scrolled out of a `Flickable` is scrolled in
  first. Each screen builds its narrow and wide layouts side by side, so a
  name can match twice; `App::find` returns the one on show.
- **One session, reset between cases.** The suite is one session of the app
  (the controllers are process-wide singletons), and each case begins with
  `App::fresh()`: launch screen, light theme, desktop size, camera idle.
- **A camera that takes a moment.** The fake camera's `start()` sleeps 150 ms
  on the pool thread, as a real one takes longer, so the scan screen's
  "opening the camera" state is a state the suite sees rather than a race.
- **Sweeps.** `press_every_control` (`sweep.hpp`) presses everything the live
  item tree offers on a screen, once; a control added tomorrow is pressed
  with no test change, and what it does is for a case of its own.
- **QML warnings are failures.** `diagnostics.cpp` collects what the app's own
  QML printed; the journey cases assert it is nothing.

### Decisions

**Catch2 with QTest input, not Qt Quick Test (`TestCase`).** Qt Quick Test
makes the QML the harness: `QUICK_TEST_MAIN` creates its own engine, the
controllers would have to be injected from a setup hook, and its runner does
not know `catch_discover_tests`, labels or the repo's reporters. This app is
composed in C++ - five controllers constructed and registered by `main()` -
so the harness has to be C++ to share that wiring. Catch2 +
`QTest::mouseClick(window, ...)` on the real `QQuickWindow` costs one helper
(`App`) and sends the same synthetic input a `TestCase` would, and the
assertions can read the C++ controllers directly.

**A real window on a real render path, not the offscreen platform.** Qt
Quick 3D has no software adaptation, and a platform with no GL draws nothing
for `View3D` - the window and the controls are all there, the cube's box is
just the page colour. Screenshots need real rendering too. So the suite
renders: the desktop session where there is one, and in CI Xvfb with Mesa's
llvmpipe (`LIBGL_ALWAYS_SOFTWARE=1`) behind the `xcb` platform, which gives
Qt's RHI an OpenGL context. `render_test.cpp` proves the cube is drawn - its
box has to hold a spread of colour and has to change as it turns - so an
environment that silently loses 3D fails loudly instead of producing blank
screenshots.

### The screenshot tour

`tour_test.cpp` writes a PNG per screen - and per state of the scan and
solve screens - narrow (390x844) and wide (900x640), light and dark:
`launch`, `settings`, `permission`, `complete`, `scan-camera-unavailable`,
`scan-looking`, `scan-reading`, `scan-three-sides`, `scan-cube-read`,
`solve-start`, `solve-middle`, `solve-end`. Names are
`<screen>-<layout>-<theme>.png`. Frames are drawn; the launch cube's idle
spin is switched off so pictures do not depend on timing. The test asserts
each exists and is not a blank window; judging them is for a person.

### Reach: the stand-in for QML coverage

`gcov` sees C++ only. QML and the JavaScript inside it are invisible to it,
and the tool that measures them as part of the Qt toolchain (Qt Coco) is a
paid product. What the suite measures instead is **reach** (`reach.hpp`):
against an inventory (`reach.cpp`), which screens it showed in each layout,
which states of each screen it saw, and which controls it pressed. Nothing
is self-reported: a sampler watches the running app, states are predicates
over properties the controllers already expose, and controls are discovered
from the live item tree.

`system.reach` fails when something in the app has no visit:

- a `*Screen.qml` in the module that is in neither the screen inventory nor
  the list of known-dead files;
- a screen never shown narrow, or never shown wide;
- a state in the inventory that was never seen (states the environment
  cannot produce carry a written reason in `reach.cpp` and are reported as
  known gaps, not hidden);
- a control that was on screen, enabled, and never pressed.

A new button therefore fails the gate until a test presses it - and the
sweeps press it without being told - which is the property line coverage
would have given. It measures *what was exercised*, not which lines ran: a
screen can be reached and half its handlers never fire.

State ids are `screen.state`, the form `docs/design/journey.md` uses, so the
inventory in `reach.cpp` can be replaced by its ids as screens are rebuilt.

### Layout defects the suite found

`layout_test.cpp` checks that every control is inside the window at six
sizes (375x667 to 1280x800). It found two screens that still size by
subtraction (plan, "What was found"): on the permission screen "Not now" is
below the window on a phone and at 719x640 (and "Open settings", when the
platform reports camera access as denied), and on the complete screen both
buttons are below a 1280x800 window. They are listed in the test as they
are; a new overflow fails it, and so does the day one of these is fixed.

### Known today

`known_today_test.cpp` asserts, as they are, the behaviours the
[experience plan](app-experience-plan.md) lists as defects: three buttons do
nothing, there is no back or keyboard focus on the settings screen, and two
settings drive a controller no screen uses. Each is tagged `[known-today]`,
cites its plan line and owner, and is written to **fail the day it is
fixed** - that is the prompt to turn it into a journey step. Nothing in the
suite fixes them.

Two used to be here and are journey steps now, because the solve lane fixed
them: the last step of the solution is **Finish** and leads to the complete
screen, and **Rescan** is the way out of the solve screen
(`journey_test.cpp`). The solve screen keeps the camera running to watch how
the cube is held; the fake camera serves it too, so the journey asserts the
camera is still on there and let go of when the screen is left.

### Seams wanted from the other lanes

Nothing here edits a lane's files; what the suite would like, for whoever
owns them:

- **Scan lane** - `DirectScanScreen.qml`: `objectName`s on the two buttons
  (`scan.cancel`, `scan.startOver`) and on the headline and guidance texts
  (`scan.headline`, `scan.guidance`), with accessible names. Today the suite
  finds them by label, which a translation would break.
- **Permission journeys** - "refused, then granted" cannot be walked on a
  desktop: `AppState.beginScan()` skips the permission screen unless
  the platform says it is mobile (`PlatformFacts.isMobile`, from
  `interfaces/platform_info.hpp`), and `CameraPermission` reads Qt's own
  `QCameraPermission`. A way to set the status for a test (an override read
  at start-up) would let the suite put the screen up through its real route
  and see the denied wording. Experience lane.
- **Solve lane** - the solve screen's controls are named
  (`solve.cube`, `solve.next`, `solve.previous`, `solve.rescan`,
  `solve.rescanFromNote`), which is what this needed. The mistake note
  (`solve.rescanFromNote`) is not staged yet: it needs a camera frame whose
  held cube differs from the solver's, which the drawn frames could supply.

### Carrying this into the new experience

The suite is written against today's six screens, but what it is built from
does not depend on them, and `docs/design/journey.md` (E1) was written to be
its inventory: state ids of the form `screen.state`, "a state nobody visits
fails the reach check". The hand-off, per screen as E2/E3 rebuild it:

| carries over unchanged | to be rewritten with the screen |
|---|---|
| `AppRuntime`, the fake camera, the `App` fixture (find by `objectName`, click, tap, drag, key, wait, grab, shot), QML-warning capture | `reach.cpp`'s screen and state tables - replace them with the ids in `journey.md` (they already use the `screen.state` form) |
| the tour harness and its four variants (add pseudo-locale, text scale and reduced motion as more variants) | cases that look a control up by its label (`find_button`, `find_text`) on the scan and solve screens: they exist only because those screens have no `objectName`s yet |
| the reach gate and its rule that a new screen file, state or control fails until it is visited | every `[known-today]` case: each flips to a journey step the day its defect is fixed |
| the layout checks (breakpoint, one layout on show, every control inside the window) | the `kScreens` list in `layout_test.cpp` and the sizes, once `system.md` fixes the size classes |

What the plan's E3 gate asks for - the whole journey by accessible name
only, by keyboard only, in both themes, in the pseudo-locale - is the same
walk with a different way of finding and pressing controls: `App` already
sends keys, and finding by accessible name is one more lookup beside
`find(objectName)`. The contract that makes it cheap is the plan's: every
interactive item has an `objectName` and an accessible name.

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
