# Testing & coverage

Four layers, all Catch2 (a few script gates sit beside them, listed below):

- **`tests/unit/`** — fast, isolated, one behaviour each. Currently exercises
  `cube::error` (`Result<T>`/`CubeError`), `cube::version`, the solver, the
  direct-scan reader and its sides, the corner reader, colour resolution, the
  recorder, and the held-cube fit. The Qt-side tests live here too, each in a
  binary of its own because it needs a Qt application object:
  `cube_qt_controller_tests` (the controllers), `cube_qml_tests` (the app's own
  QML, see [QML tests](#qml-tests-the-solve-screen)) and `cube_desktop_tests`
  (window geometry, keep-awake). The rest is `cube_unit_tests`.
- **`tests/integration/`** — exercises several components together
  (`cube_integration_tests`); the current tests drive the CMake-selected
  platform implementations (`cube::platform::platform_info`,
  `cube::platform::camera`) end to end rather than any single interface in
  isolation. What only one OS can run (Linux's XDG `cube::platform::paths`, the
  desktop shell's keep-awake) is in `tests/platform/<name>/`.
- **`tests/system/`** — the whole application, from `Main.qml`, driven by
  mouse, touch and key events in a real window. See
  [System tests](#system-tests).
- **`tests/perf/`** — budgets on what the solver and the reader do, and an
  opt-in wall-time check, so that a change that makes the app meaningfully
  slower fails a test. See [Performance tests](#performance-tests).
- **`tests/web/`** — the app in a browser, through its own page, with
  Playwright; and the unit, integration and performance binaries again as
  WebAssembly under Node. See [Browser tests](#browser-tests).

Tests do not ask which OS they are on (see
[Platform isolation](platform-isolation.md)): what differs per platform comes
from the platform's `platform.cmake` (the Qt plugin to default to, how a
desktop session reaches the process), and a test case only one platform can run
lives in `tests/platform/<name>/`. The `platform_fragments_satisfy_contract`
test holds every platform's `platform.cmake` to the contract on whichever host
it runs, so the platforms a build does not target are still checked.

The other ctest entries that are scripts rather than Catch2 cases: the `i18n_*`
gates (label `i18n`, [Translation tests](#translation-tests)),
`perf_tracy_check_selftest` (label `perf`, [Tracy](#tracy-per-zone-local-and-nightly-optional)),
`release_tools_selftest` (label `release`: the changelog and release tooling,
see [Cutting a release](releasing.md)) and `third_party_notices_complete` (label
`licensing`: fails when something that ships has no notice, see
[Licensing](licensing.md)). The last needs the app build.

Run them with `ctest --preset <preset>` after building. Tests are
independent Catch2 cases, so parallel runs are safe - pass `-j <n>` (or set
`execution.jobs` in a personal `CMakeUserPresets.json` test preset, where
machine-specific tuning belongs; the committed presets leave it unset so
CI's 4-core runners aren't oversubscribed).

That leaves out the **system tests**, which draw the real app in a window (see
[System tests](#system-tests)): every committed test preset excludes the
`system` label, so a plain `ctest` never opens a window. They are run with the
matching `<preset>-system` preset.

## Debug is slow - use `fastdebug` for local iteration

A plain `Debug` build (`windows-msvc-debug`/`linux-gcc-debug`/`macos-clang-debug`)
compiles unoptimized *and* enables ASan/UBSan (see
[Sanitizers.cmake](https://github.com/iainchesworthlabs/cubeulator/blob/main/cmake/Sanitizers.cmake)). The solver's first-two-layer
searches (`cube::solver::detail::piece_search`, see
[piece_search.hpp](https://github.com/iainchesworthlabs/cubeulator/blob/main/src/lib/src/solver/piece_search.hpp)) visit up to
hundreds of thousands of nodes per stage, and an unoptimized build alone -
independent of the sanitizers - is well over an order of magnitude slower
there than Release, turning a fast Release corpus run into many Debug
minutes.

For local dev/test iteration, prefer the `fastdebug` preset
(`windows-msvc-fastdebug`/`linux-gcc-fastdebug`/`macos-clang-fastdebug`):
optimizations on (`RelWithDebInfo`), sanitizers and iterator-debug checks off
(both are gated on `CMAKE_BUILD_TYPE=Debug` specifically), debug info still
present for breakpoints/stepping. Reach for a true `*-debug` preset only when
you actually want ASan/UBSan coverage.

## Slow tests

The solver's corpus tests (`solve_cross`, `solve_corners`,
`solve_middle_edges`, `solve_oll`, `solve_pll`), the `solve` cases that run
whole solves (`solve_test.cpp`), the piece search's comparisons with the search
it replaced, and the `SteppedSolution` tests (each of the latter runs a full
solve) are tagged `[slow]` - every one drives one or more random scrambles
through the searched first-two-layer stages. (The last layer itself is
fixed-algorithm and takes
milliseconds - see [last_layer.hpp](https://github.com/iainchesworthlabs/cubeulator/blob/main/src/lib/src/solver/last_layer.hpp) -
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

`cube_qml_tests` loads the app's own QML - `Cube3DView.qml`, `SolveScreen.qml`
and `DirectScanScreen.qml`, with the app's own controllers behind them - and
reads the 3D cube back out of its scene: where each sticker sits and which way
it faces, taken from the nodes' transforms. That is held to
`cube::solver::Cube`, so the direction of every animated turn is checked
against `Cube::apply` on what is actually drawn. The camera previews on the
scan and solve screens are held to being mirrored as a picture only: the
picture and the dots on it flip together, and the status, chips and buttons
never do. No window is shown and nothing is rendered; it runs headless
(offscreen platform off Windows) and is part of `ctest`.

`solution_screen_test.cpp` does the same for `SolutionScreen.qml`, the review
of a finished solve: the cube it draws is the solver's cube at every position,
stepped forward to solved and back, while the solve's own position - which it
must not move - stays where the test left it. It has no translations installed,
so it asserts structure and ids; what the screen says, and the direction words
against the turn sentences, are the system tests'.

Camera frames in these and the controller tests are drawn
([drawn_cube.hpp](https://github.com/iainchesworthlabs/cubeulator/blob/main/tests/unit/drawn_cube.hpp)), never recorded: a
recording shows whoever was holding the cube.

The same suite holds the complete screen's stats card to the design system's
contrast in both themes and both layouts
([complete_screen_test.cpp](https://github.com/iainchesworthlabs/cubeulator/blob/main/tests/unit/complete_screen_test.cpp)):
what each piece of text in the card is drawn over is read from the Rectangles
it sits in, which needs no GPU. A Rectangle with no `color` is opaque white, so
a card left unfilled reads fine in the light theme and is near-white ink on
white in the dark one. The system suite's `render_test.cpp` checks the same
card's pixels.

To *look* at the solve screen rather than check it, a hidden case puts the
real screen in a window parked off-screen, drives it with drawn frames, and
saves what it renders - one picture per run, chosen by name:

```
set CUBE_GALLERY_DIR=C:\some\dir
set CUBE_GALLERY_SHOT=3-matched-another-way
cube_qml_tests "[.gallery]"
```

The shot names are in
[solve_screen_gallery_test.cpp](https://github.com/iainchesworthlabs/cubeulator/blob/main/tests/unit/solve_screen_gallery_test.cpp).
It needs a GPU, so `ctest` never selects it. The scan screen has a hidden case
of its own under the same tag and directory variable (`scan-mirrored.png` and
`scan-unmirrored.png`, in `direct_scan_screen_test.cpp`; `"[.gallery][direct-scan]"`
runs it alone), so the command above writes those too.

To *use* the solve screen without scanning a cube first, start the app with
`CUBE_SOLVE_DEMO=<seed>`: it opens on the solve screen with a cube scrambled
from that seed, camera off. `CUBE_SOLVE_DEMO_MOVE=<n>` opens it `n` moves in,
and the solution is logged (run with `QT_FORCE_STDERR_LOGGING=1` to see it).

To find out why the solve screen's camera took a move the way it did, switch
the diagnostics channel on (Settings > Developer, or `Ctrl+Shift+D`; see
[Live diagnostics](diagnostics.md)). Each time what it makes of the cube
changes, a `held` record is written: the move asked for, the verdict, how many
of the nine stickers were found and their colours, and how far the side in view
sits from the cube as it stood and from the cube after the move. Numbers
only - no picture is kept. (This replaces the `CUBE_HELD_LOG` environment
variable, which is no longer read.)

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
  [`interfaces/camera.hpp`](https://github.com/iainchesworthlabs/cubeulator/blob/main/src/app/interfaces/camera.hpp)) on real
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
ctest --preset <preset>-system --output-on-failure
```

`<preset>-system` is the test preset that selects only the `system` label;
`<preset>` itself excludes it, and a command-line `-L system` cannot widen a
preset that excludes the label (ctest applies both and finds nothing). A
personal `CMakeUserPresets.json` test preset that wants the suite inherits
`base-system` instead of `base`.

That is a handful of ctest entries: `system.walk.<n>of<count>` (the suite bar
the tour, cut into shards, five by default - a run of the app each),
`system.tour.narrow` and `system.tour.wide` (the screenshot tour, below, one
run of its own per layout), `system.reach` (the reach gate, below; merges what
the shards saw, needs no display) and `system.relaunch` (a second launch with
a setting already in the store). `ctest --preset <preset>-system -R
system.walk` runs the shards; `-j` runs entries side by side, which on four
cores took the whole set from 8.7 to 4.4 minutes (below).

The relaunch case needs the settings store seeded before the app exists, which
only its ctest entry does (`CUBE_SYSTEM_TEST_THEME`). Picked up through a tag it
shares with other cases (`[settings]`, `[system]`) it is reported as skipped,
with that reason; run alone without the seed, the binary exits 4, so the
entry cannot pass without the check having been made.

On a desktop that opens a window and drives it; on a headless Linux box give
it a display and software GL (this is what CI does):

```
QT_QPA_PLATFORM=xcb LIBGL_ALWAYS_SOFTWARE=1 \
  xvfb-run -a -s "-screen 0 1280x1024x24" \
  ctest --preset linux-gcc-coverage-system --output-on-failure
```

**The window you do not see.** The window is shown, exposed and rendered for
real - that is the point - but on a platform where that works (Windows today;
`CUBEULATOR_PLATFORM_TEST_HIDE_WINDOW` in the platform's `platform.cmake`) it is
created fully transparent, click-through, without a taskbar button and never
activated. A run therefore does not flick screens across the desktop it is
started from, and does not take the keyboard from whatever is being typed in
(a shown window is the foreground window for the whole run). Clicks, key
presses and `grab()` are sent to the window object, not to the screen, so none
of it matters to them. To watch a case, set `CUBE_SYSTEM_TEST_VISIBLE=1`:

```
set CUBE_SYSTEM_TEST_VISIBLE=1
cube_system_tests "[journey]"
```

A hidden window is never the active window, so a case about keyboard focus or a
window-scoped shortcut should be looked at once with `CUBE_SYSTEM_TEST_VISIBLE=1`
before it is trusted: nothing in the suite depends on activation today, but
that is the one thing the OS no longer gives it.

Do not minimise the window, or move it off every screen, to the same end: a
minimised window never gets its size (every click lands outside it), and a
window outside every monitor is never sent an expose, so the app never counts
as shown.

It writes `<build>/system-test-output/` (under ctest; run by hand it is
`system-test-output/` beside the executable, or `$CUBE_SYSTEM_TEST_OUT` when
that is set): `tour/<layout>/screens/*.png` (the screenshot tour) and
`reach-shard-<n>-of-<count>.json` with `.txt` beside each (what that shard
visited; a whole walk run by hand writes `reach.json`). CI uploads that
directory as the `system-test-output` artifact.

While it runs it prints a line to stderr as each case and section starts and
ends, with the seconds since the process began (`[system +  12.3s] case: ...`,
from `progress_listener.cpp`). Catch2's own reporter says nothing about a case
until it ends, so a run that hits ctest's timeout would otherwise leave a log
with nothing in it; these are flushed as they are written and show which case
the time went on. Each case also prints what its reset cost (`[system]   reset:
12.3s`), and the end of a run what the reach sampler cost the app's thread: on
a software renderer neither is small, and nothing else says how much of a
case's time they are.

The `system` label keeps it out of the plain `ctest` runs - the committed
presets exclude it, and CI's build legs say `-LE system` as well: it needs
somewhere to draw. CI runs it in its own Linux job (`System tests (Linux, Xvfb
+ software GL)`, the command above, on the Qt the app ships on), and the
Windows and macOS build legs run `<preset>-system` as a step that reports but
does not block.

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
- **Two files are added from `tests/CMakeLists.txt`**, not listed in
  `tests/system/CMakeLists.txt`: `tests/i18n/system/language_test.cpp` (the
  language switched live in the real window; see
  [Translation tests](#translation-tests)) and, where the platform has a
  desktop shell, `tests/system/desktop_shell_test.cpp` (the placeholder About).
  They are in the walk like any other case.
- **Nothing of a person's is read or written.** The settings store is a
  temporary directory (`QSettings::setPath`), and so is the app's data folder,
  where the history of solves is kept: `system_test_main.cpp` points
  `CUBEULATOR_DATA_DIR` at a directory that goes away with the process, as
  `cube_qml_tests` and `cube_i18n_screen_tests` do in their own mains. The
  controllers open the history when they are made and any case that steps a
  solve writes to it, so a binary that makes them must do the same. `App::fresh`
  empties it and switches it back on.
- **A solve is kept the way the app keeps one.** `history_test.cpp` makes the
  entries by scanning a cube into the solve controller and stepping it (and
  opening the complete screen), not by writing the history's file, so what is
  asserted is that a person's solve reaches the list and that what the list
  offers works: carrying on, deleting and clearing after asking, and the two
  switches in Settings. The store and the controller behind it are covered
  without a window by `tests/unit/solve_history_test.cpp` (round trip, the
  cap of 100, a file that cannot be read, the off switch, resuming).

### Time budget

Under Xvfb with Mesa's llvmpipe the cube is drawn on the CPU, so what costs
time there is frames, not logic: the whole walk was around an hour on a CI
runner, against a few minutes on a desktop GPU, and is now under ten minutes
(measured below). One ctest entry for that hit its timeout and hid which
case was slow, so the walk is cut into
**shards** - Catch2's `--shard-count`/`--shard-index` split the
declaration-order list into contiguous runs, `CUBE_SYSTEM_WALK_SHARDS` of them
(5). Every case is in exactly one, a case added tomorrow is in one without
being told, and each shard's `TIMEOUT` (1200 s) is a safety net, not a budget.
Cases are independent - each begins with `App::fresh()` - so a shard needs
nothing from the one before.

What a case costs there is mostly **the first frame of a 3D cube**: the
launch and solve screens (and the settings preview) are a `View3D` with MSAA, a
procedural light probe and six lights, and the first frame of each one - which
is every return to a screen that has it - spends eight seconds on llvmpipe
prefiltering the probe. Measured with `QSG_RENDER_TIMING` on the CI preset
(`linux-gcc-coverage`, Qt 6.8.3, four cores): in the cube-drag case eight such
frames were 66 of its 82 seconds and the other 147 frames were 0.7 s in all;
the same case with the probe left out is 16 s. (Antialiasing off changed
nothing; a never-removed 3D view to keep Qt's caches alive changed nothing.)
Two loops did it 36 times each (every control inside the window; the same in
Hebrew), and were 555 s and 1349 s of a 55-minute walk.

The reach gate is the reason the shards write reports of their own: it merges
the `reach-shard-*` files and judges the union, and fails if one of them is
missing, so a shard that did not run cannot pass as a smaller suite.

All entries of `-L system` with the CI preset, Qt 6.8.3, pinned to four cores
(a hosted runner's four vCPUs): 55 minutes, five shards and a tour, before the
changes below; 8.7 minutes one entry at a time and 4.4 minutes three at a time
after them (the shards 26-79 s each, the tour halves 120 and 133 s). A runner
is slower than that machine by something like a third to a half. Those
figures, and the 66 of 82 seconds above, are from Qt 6.8.3, which the Linux
job ran when they were taken; it now runs Qt 6.12.0 and they have not been
taken again.

Seven choices keep each entry short, and each is worth keeping when a case is
added:

- **Start where the case is going.** `App::fresh(App::Start::elsewhere)` leaves
  the app on the permission screen (nothing drawn, no camera) instead of the
  launch screen; a case that is about to go to another screen uses it, so it
  does not pay to bring the launch cube up and tear it down. A loop over
  screens and sizes does one reset per screen and takes the window through the
  sizes by resizing, which is also a thing a person does.

- **Short scrambles.** The cases that need a solved cube to come back from a
  scan use `cube::systest::scramble(seed)` (`app.hpp`, `kScrambleMoves`),
  ten moves, not the 25 of a full random scramble: what they check is that the
  scan reads the cube and the solution is shown and stepped through, not how
  hard the cube is, and the solver's own tests cover its time on a hard one.
- **No light probe unless the case is about how the cube looks.** Before a
  view's first frame is synchronised the harness takes the probe off its
  environment, so the cube is lit by its six lights alone and the first frame
  is not eight seconds. `App::fresh(Start, Scene3D::full)` keeps it, and the
  screenshot tour and the render checks ask for that. A pixel comparison
  between two runs of the cheap cube is still a comparison of the same thing.
- **A press for each kind of move, not for each move.** The journey presses
  Next for the first and last few moves and for the first of every kind of turn
  (`R`, `U'`, `F2`...), and steps the rest through the controller; the tour
  steps through the controller throughout. Ninety turns played out on a
  software renderer, per layout, was a third of the walk.
- **A still cube by default.** `App::fresh()` turns the cube's auto-rotate
  off. A spinning `View3D` is a scene that is re-rendered every frame whether
  or not anything has changed, which on a software renderer is the CPU the
  next click is waiting for. `render_test.cpp` and the settings sweep turn it
  on where the spin is the thing under test.
- **Pump, do not settle, where position is all that matters.**
  `App::settle()` waits out Main.qml's 280 ms fade-in and two frames. The
  sweep's way back from a screen and the layout cases, which only need to
  know where things are, pump 80 ms instead; every press in a sweep is
  followed by one of these, so it adds up on a slow renderer.
- **A sampler that is not the busiest thing in the room.** The reach sampler
  looks every 50 ms, not every 25.

If a shard still times out, the progress lines above say which case it was in
and the reset lines (`reset: 12.0s (launch 0.5 cameras 0.0 language 11.3 ...)`)
how much of each case was before it began and in which phase; a phase that
shows the seconds is usually the first one that ran the event loop long enough
for a frame, not the one that did the work.

Two things the Linux runs showed about the harness, kept here because they
look like test bugs and are not:

- **The window reports its new size before the scene has it** (X11: the
  geometry change arrives as an event). `App::resize` waits for the content
  item, and `App::layout()` reads the scene's width, not the window's; before
  that the sampler credited the wide layout's controls to `@narrow` for the
  moments between the two, and the solve screen's `wide` read stale after a
  resize.
- **Qt crashed twice on the launch-to-other-screen change** that is the cost
  above: under the threaded loop with Qt 6.12 on Windows (render thread), and
  with the basic loop on Qt 6.8.3 under llvmpipe (main thread, inside the batch
  renderer's RHI call, `QSGBatchRenderer::Renderer::prepareRenderPass`, a
  frame after the old `View3D` was destroyed). Both are in Qt, and the app
  makes the same screen change at "Start scanning". Fewer of these in the
  walk makes both rarer; it does not make them go away.

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

**The basic render loop with vsync off, and a case that says so.**
`system_test_main.cpp` sets `QSG_RENDER_LOOP=basic` and `QSG_NO_VSYNC=1`
(unless the environment already has them). With the platform's threaded loop
and vsync on, two things went wrong on Qt 6.12 (Windows, D3D11; the same suite
passed on Qt 6.8):

- QML `Timer`s and animations did not advance: a `Timer` reported `running` and
  never fired. The scan screen hands over to the solution on a Timer, so every
  journey case finished its solve, stayed on the scan screen, and waited out its
  timeout - the walk's whole budget went on two cases, and on Linux CI the job
  ran into ctest's 900 s with no output to say why. Vsync off cured this with
  the threaded loop kept. The `[timers]` case, first in the walk, starts a Timer
  and a NumberAnimation in the live scene and fails in two seconds, by name, if
  either does not advance, so a regression of this kind is not a timeout again.
- About one run in ten, the walk died with an access violation on the render
  thread, always at the press that leaves the launch screen (the one that
  destroys its 3D view). A minidump showed the render thread, inside
  Qt Quick's scene-graph sync with the main thread waiting on it, calling
  through a freed connection. It happened in a different case each time but
  at that same press, and four walks at once made it happen about one in eight.
  The basic loop renders on the GUI thread, so there is no second thread to
  race: 24 walks at four at a time did not do it.

A test wants frames that happen when it asks, at the pace it sets, so it pins
both. The application itself runs with whatever the platform picks. A person
using it passes the same press (Start scanning), so the second of these is
not only a test's problem; how likely it is outside a tight click-and-pump
loop is not known.

### The screenshot tour

`tour_test.cpp` writes a PNG per screen - and per state of the scan and
solve screens - narrow (390x844) and wide (900x640), light and dark:
`launch`, `settings`, `permission`, `complete`, `scan-camera-unavailable`,
`scan-looking`, `scan-reading`, `scan-three-sides`, `scan-cube-read`,
`solve-start`, `solve-middle`, `solve-end`, `complete`, and the solution
reviewed from it: `solution-start`, `solution-middle`, `solution-end`; then the
history: `history` (a finished solve and one left part-way) and `history-empty`.
Names are
`<screen>-<layout>-<theme>.png`. Frames are drawn; the launch cube's idle
spin is switched off so pictures do not depend on timing. The test asserts
each exists and is not a blank window; judging them is for a person.

The pictures in the [user guide](guide/index.md) (`docs/guide/img`) are these.
`python tools/docs/update_guide_screenshots.py <build>/system-test-output`
copies the ones the guide uses out of a tour run and saves them as WebP; its
docstring says how to get a plain wall behind the drawn cube instead of the
tour's cluttered room. Refresh them when a screen the guide shows changes.

It is two ctest entries of its own (`system.tour.narrow` and `.wide`, writing
to `<build>/system-test-output/tour/<layout>`) because it is the slowest part
by far - 68 grabs of a window that is being rendered in software on Linux CI
- and a picture that cannot be taken should not hide what the walk found, nor
spend the walk's time budget. Each layout is a half of the pictures and can
run beside the other. The solution is stepped to its middle and its end
through the controller, not by pressing Next ninety times: the pictures want
the cube there, and the journey presses the control.

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
would have given. The rows of a scrolling list (`ListView`) are one control
between them, keyed `<list's objectName>.row` (`App::control_key`): the rows
that exist depend on where the list has scrolled to, so a key for each would
turn every row some case happened to scroll past into a gap. A row of the
solution's list, pressed in each layout, is what the gate asks; which row is
the case's own (`journey_test.cpp`). It measures *what was exercised*, not which lines ran: a
screen can be reached and half its handlers never fire.

State ids are `screen.state`, the form `docs/design/journey.md` uses, so the
inventory in `reach.cpp` can be replaced by its ids as screens are rebuilt.

### Layout defects the suite found

`layout_test.cpp` checks that every control is inside the window at six
sizes (375x667 to 1280x800). It found two screens that still size by
subtraction (plan, "What was found"): on the permission screen "Not now" was
below the window on a phone and at 719x640 (and "Open settings", when the
platform reports camera access as denied), and on the complete screen both
buttons are below a 1280x800 window. The permission screen has since been
rebuilt and fits; the complete screen's two buttons (`complete.review`,
`complete.scanAnother`) are still listed in the test as they are. A new
overflow fails it; the day one of these is fixed the test says so as a warning
rather than failing, because the list was written from the Windows and Linux
fonts and a control that fits on another platform is not a fix.

A control inside the window can still sit on another, which that check cannot
see. On the solve screen the camera's preview (`solve.frame`), its words
(`solve.words`), the move label (`solve.move`) and the cube (`solve.cube`) share
the stage, and the cube was sized without them: on a phone the words ran across
its edge and its top was under the move label. A case of its own scans a cube,
goes to the solve screen and holds those four boxes clear of one another and
inside the window at the smallest window, the phones, either side of the
breakpoint and the desktop sizes - with the words as they are and as long as they
get, and then at the smallest window through every move of the solution, since
the label's width changes with the move and the cube must not answer to it. It
compares boxes, not pixels: a cube's box has air round the cube, which it needs
to turn a layer in, so a box that is clear is a cube that is clear at any angle.

### Known today

`known_today_test.cpp` asserts, as they are, the behaviours the
[experience plan](app-experience-plan.md) lists as defects: a button does
nothing (**Review the solution**, which the solution-review branch builds),
there is no back or keyboard focus on the settings screen, and two settings
drive a controller no screen uses. (**Solve history** did nothing until the
history screen was built; it is a journey step now, `history_test.cpp`.) Each is tagged `[known-today]`,
cites its plan line and owner, and is written to **fail the day it is
fixed** - that is the prompt to turn it into a journey step. Nothing in the
suite fixes them.

Three used to be here and are journey steps now: the last step of the solution
is **Finish** and leads to the complete screen, **Rescan** is the way out of the
solve screen, and **Review the solution** on the complete screen opens the
solution, which is stepped through and left again (`journey_test.cpp`). The solve screen keeps the camera running to watch how
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

## Performance tests

A change that makes the app meaningfully slower fails a test, the same way a
change that makes it wrong fails a unit test. `cube_perf_tests` is that tier:
its own binary, linked against `cube::lib` alone (no Qt, no window), with every
number it holds the app to in one file,
[`tests/perf/budgets.hpp`](https://github.com/iainchesworthlabs/cubeulator/blob/main/tests/perf/budgets.hpp).

It has two halves, and the split is the design:

| | What it holds | Exact? | Runs |
|---|---|---|---|
| **Work budgets** | what the solver and the reader *do*: search nodes visited, pixel buffers allocated, moves in a solution | yes, to the node | in every plain `ctest`, label `perf` |
| **Wall-time checks** | what the same code *takes*, as a multiple of a calibration workload run in the same process | no, and loose on purpose | only when asked: `CUBE_PERF_TIMING=1`, the `*-perf` test presets, or the `perf` CI leg; label `timing` |

```
cube_perf_tests                                  # the work budgets (what ctest runs)
ctest --preset <preset> -L perf                  # the same, through ctest
ctest --preset windows-msvc-perf                 # budgets *and* wall time (also linux-gcc-, macos-clang-)
cube_perf_tests "[.perf-report]"                 # every metric beside its budget, and the corpus per seed
cube_perf_tests "[.perf-report-timing]"          # just the wall-time metrics
```

### What is measured

Corpus: `corpus_scramble(seed)` for seeds 0-27, 25 moves each - the same
convention the solver tests use. It draws from
`std::mt19937` modulo the number of choices, **not** through
`cube::solver::random_scramble`: that goes through
`std::uniform_int_distribution`, whose algorithm is each standard library's own,
so the same seed is a different cube on MSVC, libstdc++ and libc++ and a node
budget would be a budget for one of them. With this one the counts are the same
on every platform (checked: MSVC 14.51 and GCC 15 on libstdc++ give identical
counts, to the node, for the first twelve seeds and for all three methods, on
the facelet search the piece search has since replaced; the replacement visits the
same signatures, and the budgets held across it unchanged).

| Group | Metrics (each as `max` over the corpus, `mean`, and where it applies the largest single search) | Hard cap it stays under |
|---|---|---|
| `beginner.<stage>` | nodes per stage: `cross`, `white_corners`, `middle_edges`, `oll`, `pll` | `kMaxVisitedNodes` 2,000,000 per search (bfs); `kMaxLastLayerStates` 100,000 (oll, pll) |
| `beginner.solve`, `.search`, `.moves` | nodes per solve, the largest search of any stage, solution length | as above |
| `intermediate.*` | the same, for CFOP (both-ends pair searches; the last layer is a table) | |
| `advanced.*` | nodes of the two-phase search (about five million by design: it keeps improving for up to `kImprovementNodes` after the first solution) | `kHardNodeLimit` 60,000,000 |
| `read.<w>x<h>.*` | `cv::Mat` buffers and bytes one `read_side()` allocates, at 640x360, 1280x720 and 1920x1080, with the side in view and with an empty room; and that a bigger frame costs no more than the working-width one (the documented contract: wide frames are reduced first) | |
| `corner.<w>x<h>.*` | the same for `read_corner()`, which runs on every frame beside `read_side()`: two sides meeting at an edge at 640x360 and 1920x1080, and a room. About a thousand small buffers a frame, which is the number to look at first in a profile of the scan | |
| `scan.add`, `scan.scanned` | pixel buffers fed through `SideScan::add` (none) and `scanned()` (the handful of single-colour conversions) | |
| `timing.*` | the fastest run of a seed-0 beginner / intermediate / advanced solve, of a 640x360 and a 1920x1080 read, of 100 `SideScan::add`s and of one `scanned()`, and of `read_corner` at both sizes, each as a ratio to the calibration workload | |

The node counts come from `cube/solver/search_work.hpp`, a thread-local tally
the searches add to once as each ends (`piece_search` and its fallback
`bfs_search`, the last-layer search, the CFOP pair search, the two-phase
search). It costs one add per search and nothing
per node, and nothing in the application reads it. The beginner figures are
taken stage by stage through the public stage functions, and one case holds that
chain equal to what `solve()` itself spends, so the numbers cannot go on being
exact about something that is no longer the solve.

### Why counts first

A stopwatch on a shared runner is wrong for reasons of its own. A node count is
not: it is the same on every machine, compiler and build type, so it can sit
within a quarter of the truth instead of an order of magnitude, and it fails
with a number and a seed rather than "slower". It also catches the regression a
profile finds last: a search that has lost its signature reduction visits ten
times the nodes the day it lands, which is the difference between a solve that
is a second here and one that is ten. The solver's own header says a solve
must be seconds on a phone, always; this is what holds it, on the machine that
is running the tests rather than on a phone nobody has to hand.

Work budgets are measured, then set about a quarter above what the corpus gives
(the report prints the room each metric has left). Wall-time budgets are the
loose part.

### Where it runs

| Build | Work budgets | Wall time |
|---|---|---|
| Release / RelWithDebInfo / MinSizeRel (every CI build leg, `*-fastdebug`) | run, with the plain `ctest`: about 3 s here, now that the first-two-layers searches run on piece positions (it was 20 to 25 s on the facelet search) | skipped, unless `CUBE_PERF_TIMING` is set |
| Debug (ASan/UBSan), and `linux-gcc-coverage` | **skipped, not failed**: the same counts are held by the optimised legs, and the corpus is minutes there | skipped |
| An optimised build with coverage compiled in (`CUBEULATOR_ENABLE_COVERAGE`) | run | skipped: a clock reading is not about the program |

"Skipped" is Catch2's `SKIP()`, which ctest reports as *Skipped* (return code
4), so the cases are visible in a run and cannot be mistaken for passes. Which
build is which is decided by CMake and passed in as `CUBE_PERF_OPTIMISED` /
`CUBE_PERF_INSTRUMENTED`, read as `constexpr bool`s in
[`perf_build.hpp`](https://github.com/iainchesworthlabs/cubeulator/blob/main/tests/perf/perf_build.hpp); nothing uses the preprocessor
(the platform-isolation job holds that for all of `tests/`).

The wall-time checks are opt-in because they are the one test that can fail
without anything having changed. They are `RUN_SERIAL`, so `ctest -j` runs them
alone and not beside a compiler. For CI there is one leg, dispatch-only and not
part of `legs=all`:

```
gh workflow run ci.yml --ref <branch> -f legs=perf
```

It builds only `cube_perf_tests` on the Linux Release preset (no Qt, no app)
and runs `ctest --preset linux-gcc-perf`. The three required build legs run the
work budgets in their ordinary `ctest -LE system` step and add no step of their
own.

### Reading a failure

```
check.hpp(55): FAILED:
  CHECK( m.measured <= m.budget )
with expansion:
  1148612.0 <= 1000000.0
with message:
  metric beginner.middle_edges.nodes.max (nodes), worst corpus seed 5
```

(That is the real output with the middle-edge budget lowered to a million, from
before the corpus was widened to 28 seeds; over today's corpus the same
experiment reports 1,270,904 and may name another seed.)

- **The metric name** is `<group>.<thing>.<statistic>`; the seed is the corpus
  scramble that gave a maximum. Reproduce it with `"[.perf-report]"`, which
  prints every seed's per-stage counts.
- **A count that doubled or tenfolded** is a lost reduction: a signature that
  no longer collapses states, a search with a wider face set, an algorithm
  alphabet that stopped preserving the first two layers. Look at the diff of
  `piece_search.hpp`, `bfs_search.hpp` and the stage's `signature` (in
  `cross.cpp`, `corners.cpp` or `middle_edges.cpp`).
- **`...search.max` near its cap** (the budgets are held at most half of the
  solver's own hard caps by a `static_assert` in `metrics.cpp`) means the cap is
  about to turn the search into `no_solution`: the answer is never "raise the
  budget".
- **`read.*.buffers` or `.bytes`** up means the reader allocates more per frame:
  a conversion of the whole frame, a buffer per sticker. `*.bytes_vs_640x360`
  above 1.1 (it measures 1.00) means a bigger camera frame is no longer reduced
  first.
- **A `timing.*` failure** with the work budgets green is a constant-factor
  slowdown (a copy where there was a reference, an allocation per node, a lost
  inline). The message carries the fastest run of the task and of the
  calibration and both medians; a failing ratio with a healthy calibration is
  the code, and with a median far above the fastest run is a busy machine -
  run it again before suspecting anything.

### Moving a budget

A budget moves when the work genuinely changed and a person chose that, never to
make a red run green:

1. Run `cube_perf_tests "[.perf-report]"` from an optimised build and read the
   metric beside its budget and the corpus table above it.
2. Edit the number in `tests/perf/budgets.hpp` (and the "Measured" comment
   above it), to about a quarter above what the report now shows. Work budgets
   are exact; do not pad them to an order of magnitude.
3. Say why in the PR: which change costs more, and why that is worth it. A
   change that makes the work *smaller* tightens its budget in the same PR - the
   report shows how much room is left, and a budget with 300% of it is a
   regression that can land unseen.
4. Wall-time budgets move only when the ratio has really changed: run the
   `"[.perf-report-timing]"` case several times and set it a good deal above the
   worst.

### How the wall-time checks are made trustworthy

- **A ratio, not seconds.** Each task is timed alternately with a calibration
  workload (about ten milliseconds of hash-table inserts and 56-byte record
  appends and reads, touching none of the code under test, so a regression in it
  cannot also slow the calibration and cancel out). The calibrated ratio is
  what a budget holds, so a slower machine does not fail it.
- **The fastest run, not the median.** Interference only ever adds time, so
  the fastest of the runs is what the code cost, and a burst of load that lands
  on half of them cannot move it; a regression slows every run, so it still
  shows. (A median of fifteen 1080p reads, a 4 ms task, was seen at 83 ms on
  a loaded machine.) The medians are printed beside it.
- **A calibration that is like the task.** A beginner solve keeps tens of
  megabytes of nodes and hash entries, which a busy neighbour costs several
  times what it costs a task that fits in cache: measured on the facelet
  search, the same solve read 2.5 times its ratio with fourteen other solvers
  running, against a light calibration that did not move. The solve is
  calibrated against a heavy workload of the same footprint (about 0.1 s); its
  ratio then stayed between 8.7 and 12.6 across that load and the machine as it
  was, and on the piece search between 0.65 and 0.82. Reads, scan steps and the
  two faster methods use the light one.
- **One thread.** OpenCV is held to one thread while timing: its pool waits on
  cores another process may hold, which measures that process and not the code.
  The same read, with its default pool, went from 4 ms to 83 ms under load.
- **Generous.** Each budget is three to ten times what the ratio measured, or
  more, across the runs made on one busy 32-core machine (the ranges are in
  `budgets.hpp`), and GCC 15 on Linux measured 7.8 for the beginner solve where
  MSVC measured 9 to 13 (both on the facelet search). They are
  order-of-magnitude detectors for what a node count cannot see; they are not
  10% gates and must not be tightened into being one.
- **Not Catch2's `BENCHMARK`.** It runs a hundred samples and bootstraps a
  confidence interval, which for an 0.8 s solve is a minute and a half for a
  figure that goes to the reporter and is not a value an assertion can read. A
  stopwatch around a lambda is the same measurement, and it can be paired with
  the calibration and compared against a number.

### Tracy: per-zone, local and nightly (optional)

`tools/perf/tracy_check.py` is the same idea one level down: it runs
`cube_perf_tests` from a profiling build under `tracy-capture`, exports the
zones with `tracy-csvexport`, and holds each one - its call count, exactly, and
its fastest call as a multiple of the fastest call of the `perf.calibration`
zone the binary also records - to a checked-in baseline,
`tools/perf/tracy_baseline.json`. It finds what a ratio over a whole solve
cannot: that the cost has moved from one stage to another, or that a zone is
entered more often than it was.

```
cmake --preset windows-msvc-profiling         # or linux-gcc-profiling / macos-clang-profiling
cmake --build --preset windows-msvc-profiling --target cube_perf_tests
python tools/perf/tracy_check.py --preset windows-msvc-profiling            # compare
python tools/perf/tracy_check.py --preset windows-msvc-profiling --update   # rewrite the baseline
```

A comparison is about a minute. Things it does on purpose, each learned the hard
way on a busy shared machine:

- **The fastest call, not the mean.** Over a dozen calls of a millisecond zone
  a mean moved by a factor of five between two captures of the same binary; the
  fastest call moved by 0.7 to 1.2. Zones under about ten microseconds (a
  search whose start already meets its goal, a cached `prepare`) are held to
  their count only.
- **Counts are exact.** The workload is fixed, so a different count means the
  work or the instrumentation changed, and the baseline is stale or the change
  did more than it should. Two captures of the same build agree to the call.
- **A port of its own.** Tracy's default, 8086, is every Tracy user's on the
  machine: a capture that attaches to someone else's program, or a program that
  is attached to by someone else's capture, captures nothing. The tool picks
  one (`TRACY_PORT` for the client, `-p` for the capture).
- **Time to attach and to drain.** The client is on-demand and records nothing
  until a capture is attached, and it answers the capture's questions about
  zone names from a thread of its own; a program that exits as its work does
  leaves some unanswered and the zones appear as `???`. `cube_perf_tests` sleeps
  for `CUBE_PERF_PROFILER_SETTLE_MS` before its first case and
  `CUBE_PERF_PROFILER_DRAIN_MS` after its last, when they are set
  (`tests/perf/profiler_listener.cpp`; nothing happens otherwise), and a capture
  that still has a nameless zone is run again, four attempts in all
  (`--attempts`).

CI does not run it and nothing depends on it: no CI leg builds Tracy, and the
Tracy-on build's `system.walk` has been seen failing intermittently. The
captures are of the headless perf binary for that reason, not of the app. Its
self-test (`perf_tracy_check_selftest`, on a canned export) is the only part
that runs in `ctest`.

Startup to first frame is a Tracy question too (`app.i18n_install`,
`app.load_qml` and the first `UI` frame mark, from a profiling build of the app
or of `cube_system_tests`; see [Profiling](profiling.md)); it is not a wall-time
check here, because it is dominated by the GPU driver and window system, which
no calibration workload in a process can stand in for.

### What this does not cover

- **Startup to first frame**, as above, and anything that needs a window or a
  camera: the Qt controllers' per-frame paths (`DirectScan::processFrame`, the
  preview's `cv::Mat` to `QImage` copy) and the recorder. They are Tracy
  zones; they are not budgeted, because they sit behind a QCoreApplication and
  a device the perf binary deliberately does not have.
- **A search that does not report its work.** `search_work.hpp` is called by the
  five searches that exist; a sixth (a faster replacement for one of them, as
  `piece_search` was for `bfs_search`) must call it too, or its nodes are never
  counted and every node budget would pass on a zero. This is not hypothetical:
  the first run of this tier against the merged piece search failed
  `beginner.uncounted_stages` with 28 of 28 solves flagged, which is how the one
  hook it needed was found. The `*.uncounted_*` metrics are what refuse that: a solve or a
  stage that found moves and visited no nodes fails them, by name, with the
  header to read. (`the stages measured one at a time are the solve that solve()
  runs` is the other half: it notices the chain `solve()` runs changing.)
- **Phones.** The counts are the same there; the milliseconds are not, and no
  ratio taken on a desktop says what a phone does.

## Browser tests

The web build ([Try it in your browser](web.md)) is tested in two ways, because
it has two things worth testing: that the code is right when it is
WebAssembly, and that the app is right in a page.

### The library under Node

The unit, integration and performance binaries are built for WebAssembly and
run by `ctest`, with Node as the emulator CMake's Emscripten toolchain already
names. These are the same sources and the same test cases as the desktop legs;
what changes is the compiler, the C++ runtime and the 32-bit address space, so
what they find is what only WebAssembly gets wrong (a `size_t` that is 32 bits
in a hash, a stack of 64 KB, a thread that waits for a Web Worker nobody has
started, exceptions compiled out by default).

```sh
cmake --preset web-emscripten-tests
cmake --build --preset web-emscripten-tests
ctest --test-dir build/web-emscripten-tests -j 8 -LE slow
```

(A personal `CMakeUserPresets.json` preset that inherits
`web-emscripten-tests` moves the build tree and the vcpkg cache; see
[Building](building.md#webassembly-browser).) About 215 cases run in about a
minute. What does not:

| Left out | Why |
|---|---|
| the Qt tests (`cube_qt_controller_tests`, `cube_qml_tests`, `cube_i18n_*`, `cube_system_tests`) | they need the app, which is built as a page: the browser suite below runs the same screens |
| the camera cases under `tests/integration` (`CUBEULATOR_PLATFORM_TEST_NATIVE_CAMERA`) | the browser's camera is `getUserMedia()`, which has to be asked for by a page; its tests are the browser suite's |
| the recording cases that write JPEG | the browser's OpenCV has no JPEG codec (the app never records), and says so: they are skipped, with the reason. The cases for a frame that cannot be written (`recording_v2_test.cpp`) need no codec and run |

The flags a WebAssembly test program has to be given - a pool of Web Workers
big enough that none is waited for, a stack and a heap it fits in, the host's
files - are the platform fragment's (`CUBEULATOR_PLATFORM_TEST_LINK_OPTIONS`),
not each target's.

C++ exceptions are on in the web build (`-fexceptions`, Qt's own mode, in the
vcpkg triplet and the platform fragment). Emscripten's default is to compile `throw`
to an abort and `catch` to nothing, which Catch2 cannot report a failure
through and which would make the app's own handlers (a degenerate camera frame
drops one frame, not the scan) dead code in the one build nobody could see it
in.

### The app in a page

`tests/web` drives the real app, in Chromium, through the page a visitor gets,
with [Playwright](https://playwright.dev). The browser is real; the camera is
a canvas the test draws the six sides of a scrambled cube on and hands to the
page as a `MediaStream` (`support/fake_camera.js`), so everything from the
`<video>` element down is the page's own path. Pressing is real too: pointer,
touch and key events sent to the page, the way a person does.

Reading is the part that needs help. Qt for WebAssembly draws into one canvas
and mirrors none of its items into the DOM's accessibility tree, so a locator
has nothing to find. The diagnostics build therefore carries a read-only
**driver** (`window.cubeulatorDriver`,
`src/app/platform/web/driver_web.{cpp,js}`): every item that has an
`objectName` or text with where it is and whether a person can see it, the QML
singletons' properties, and the six sides of the cube a seed scrambles. It is
the browser's counterpart of `tests/system/app.hpp` - find by `objectName`,
press at the centre, read back what is on show - and a test written against one
reads like a test written against the other.

```sh
cmake --preset web-emscripten-diagnostics
cmake --build --preset web-emscripten-diagnostics

cd tests/web
npm ci                       # @playwright/test only
npx playwright install chromium
CUBEULATOR_WEB_DIR=../../build/web-emscripten-diagnostics/bin npx playwright test
```

`CUBEULATOR_WEB_DIR` is the build's `bin/`; `support/serve.mjs` serves it with
the two cross-origin-isolation headers the app's threads need.

| File | What it holds |
|---|---|
| `boot.spec.mjs` | isolation, title, the first screen, no console error while starting, the diagnostics |
| `settings.spec.mjs` | each setting through its control and kept through a reload made at once (the page writes what the app is still holding as it is hidden or left); what an earlier visit stored is read; every language, the CJK scripts, mirroring for Hebrew and Arabic |
| `layout.spec.mjs` | each screen in a phone and a desktop viewport: which layout it chose, nothing cut off |
| `journey.spec.mjs` | launch, scan a scrambled cube, solve it, walk the solution to the end; abandon, start over and rescan |
| `camera.spec.mjs` | a camera that is refused, absent or busy, and the recovery; the camera is let go of |
| `camera-device.spec.mjs` | the browser's own path, with Chromium's built-in fake device: a real `getUserMedia()` and `enumerateDevices()`, frames the browser makes |
| `demo.spec.mjs` | `?demo=<seed>&move=<n>`: stepping, switching method, the completion screen |
| `input.spec.mjs` | a mouse drag of the cube, a finger, the keyboard |
| `package.spec.mjs` | the `package` project: any built copy of the site, the release zip unzipped included, starts, is isolated, draws and is quiet (it needs nothing the diagnostics build adds) |
| `perf.spec.mjs` | the journeys again, read from the recorder; see below |

A page per test, with the browser's storage cleared first: the module compiles
in about a second and the first screen is up in two, so nothing a test leaves
in the browser's storage reaches the next. (A test that needs an earlier visit's
settings says what the storage holds when the app starts: `appOptions.storage`.)
A failure attaches the page's console and a screenshot (and a Playwright trace
on the retry).

**The light probe.** On a headless runner the page draws in software
(SwiftShader), and Qt Quick 3D prefilters the cube's light probe on the first
frame of every 3D view: some twenty seconds of every core, for every page, and
the same again when the page is closed. The suite leaves the probe out (the
cube is still lit by its lights), as `tests/system` does natively
(`App::Scene3D::cheap`): the context's init script sets
`window.cubeulatorEnvironment = { CUBEULATOR_NO_LIGHT_PROBE: '1' }`, which the
page passes to the app as environment and the driver reads
(`driver_web.cpp`). `appOptions: { lightProbe: true }` keeps it, and the `perf`
project does, so that what it measures is the app as shipped. The first
version of the suite did not, and spent half a minute per test closing pages.

**One worker.** SwiftShader's threads spin on every core they find, so two pages
at once measured seven times slower each than one. `CUBEULATOR_WEB_WORKERS`
raises it on a machine with cores to spare.

**Known today.** A few cases say what is true now and are marked "EXPECTED TO
CHANGE", as in `tests/system/known_today_test.cpp`: a toggle's label does not answer a press (only its box does), Tab
reaches no control and Escape leaves nothing on most screens, and a chosen
language still shows English where its catalogue's entries are unfinished. They fail
when the app is fixed, which is the prompt to update them.

### Performance in the browser

`perf.spec.mjs` is its own project (`--project=perf`, one worker: a stopwatch
cannot share a machine). Each journey - start-up, a scan and solve, the three
solving methods, forty presses of Next - clears the recorder
([Profiling](profiling.md#in-the-browser)), makes the journey, and reads what
happened back from it. As in `tests/perf`, two kinds of check:

- **what the app did**: how many solves, how many frames read, that nothing was
  dropped from the ring, how far the heap grew. Always run, and the same on
  every machine.
- **how long it took**: budgets well above what a software-rendered runner
  measures, so an order of magnitude fails and noise does not. Only run when
  `CUBEULATOR_WEB_PERF_TIMING=1`, as `CUBE_PERF_TIMING` gates the native ones.

Either way each journey leaves `reports/perf/<journey>.json` (the summary, the
renderer string and the heap), `<journey>.trace.json` (open it in the DevTools
Performance panel or Perfetto) and `<journey>.csv` (`tracy-csvexport`'s
columns), which CI uploads.

### What this does not cover

- **Other browsers.** Chromium only. The app needs threads and WebGL 2; Firefox
  and Safari have both, and a leg for either is a `projects` entry and the
  browser install, not new tests.
- **A real camera and a real GPU.** The page's `getUserMedia()` path is
  exercised, with a stream the test draws and with Chromium's built-in fake
  device; neither is a lens. Colour under a real webcam is the corpus tools'
  (`tools/scan`).
- **The published site.** These test the diagnostics build, which differs from
  `web-emscripten-release` by the recorder and the driver only; whether the
  deployed copy is served with the right headers is the docs workflow's check.

## Translation tests

`ctest -L i18n` (or `-R i18n` plus the `[i18n]` Catch cases) runs the
translation gates: the catalogues against `docs/design/strings.json`, a lint
for user-visible literals, the ids the code asks for, what each catalogue
holds, a raw id never reaching the screen, the language manager, the browser
build's Japanese and Chinese font subsets against the catalogues, and the
converted screens switched live between English, the pseudo-locale and Hebrew
(and checked in the real Japanese and Chinese). The Catch cases are in
`cube_i18n_tests` and `cube_i18n_screen_tests`; the system tests add the same
switch in the real window (`tests/i18n/system/language_test.cpp`, tagged
`[language]`). They need Python 3 and Qt's Linguist tools, which the Qt install
has, and are part of the app build, so a `CUBEULATOR_BUILD_APP=OFF` configure
has none of them. What each one is for, and what to do when it fails, is in
[Translations](i18n.md).

## Coverage gate

CI's `Coverage (Linux, GCC)` job builds the `linux-gcc-coverage` preset (which
sets `CUBEULATOR_ENABLE_COVERAGE=ON`; the job adds `-DCUBEULATOR_BUILD_APP=OFF`
and installs no Qt) and runs `ctest --preset linux-gcc-coverage`, then holds
the result to:

```
gcovr --root . --filter 'src/.*' \
  --fail-under-line 80 --fail-under-branch 80
```

(The job's real command line also excludes throw and unreachable branches and
reads past gcc's negative-counter bug, 68080; see `ci.yml`.) So the gate is the
library and the selected platform's code, not the Qt app. The system-tests job
builds the same preset with the app and reports the coverage of `src/app/qt/`
from the controller and system tests - not gated, and QML is not in it (see
[Reach](#reach-the-stand-in-for-qml-coverage)).

New behaviour should come with a test in the same PR (TDD by convention);
`src/lib/` in particular has no platform/GUI dependency to complicate
mocking, so it should clear meaningfully more than the 80% floor.
