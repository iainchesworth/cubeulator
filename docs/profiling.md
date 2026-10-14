# Profiling with Tracy

Cubeulator is instrumented with [Tracy](https://github.com/wolfpld/tracy), the
same profiler and the same workflow the rest of the application family uses
(ICL Forge, Aqualink Automate), so a trace from any of them is read, captured
and exported the same way. The instrumentation goes all the way through the
code the application runs: the camera capture thread, the direct scan's read
and the held-cube judging, the recorder, startup, the Qt Quick frame, and every
stage of the solver.

It is **off by default and free when off**. A normal build compiles the
instrumentation to nothing and does not need Tracy installed at all.

What a capture of the application found, and what was done about it, is in
[Performance](performance.md).

## Building a profiling binary

```sh
cmake --preset windows-msvc-profiling      # or linux-gcc-profiling / macos-clang-profiling
cmake --build --preset windows-msvc-profiling
```

The preset is `fastdebug` (RelWithDebInfo: optimised, with symbols - the right
shape for a profiler) plus `CUBEULATOR_ENABLE_TRACY=ON` and sanitizers off.
That one option does everything: it turns the macros in `cube/profiling.hpp`
real and adds the `profiling` manifest feature to vcpkg, which supplies

- the Tracy **client** (`tracy[on-demand]`) the application links, and
- Tracy's **command-line tools** (`tracy-capture`, `tracy-csvexport`), built
  for the *host* so a mobile cross-build does not try to build them for the
  device.

To flip it on for an existing configuration, reconfigure with
`-DCUBEULATOR_ENABLE_TRACY=ON`. Other presets work the same way; the Android
presets take the option too, but nothing here has been run on a device.

Avoid profiling a `debug` build. It carries AddressSanitizer, whose cost
dominates every number; configure prints a warning if you do.

### The profiler listens on the network

A Tracy client opens a TCP data port (8086) and answers UDP discovery
broadcasts, **independently of anything the application does**. That is the
point of a neutral profiler, and it is why release presets leave this off:
a build with Tracy in it is a development build, and should be run on a
trusted network.

Tracy's `TRACY_ONLY_LOCALHOST` and `TRACY_NO_BROADCAST` are compile-time
definitions of the *client library*, so they cannot be set from this project;
they are baked in by building the `tracy` port with them (Aqualink Automate
does it with a `*-diagnostic` vcpkg triplet). Nothing here does that yet.
Until it does, reach a remote device through an SSH tunnel and treat the open
port as exposed.

## Capturing a trace

The client runs in **on-demand** mode: it records nothing until a profiler
connects, so an instrumented build left idle does no work beyond an atomic
load and a branch per zone.

Use the server tools at the **same version as the client** (the wire protocol
changes between releases). The pin is whatever `tracy` resolves to in
`deps/vcpkg` - currently 0.13.1.

Headless, from the tools vcpkg built into the build tree:

```sh
TOOLS=build/windows-msvc-profiling/vcpkg_installed/x64-windows/tools/tracy   # x64-linux, arm64-osx elsewhere

# start the capture first; it waits for the application to appear
$TOOLS/tracy-capture -o scan.tracy -a 127.0.0.1 -f &
build/windows-msvc-profiling/bin/Cubeulator.exe   # cubeulator on Linux, Cubeulator.app on macOS

# the capture ends when the application exits; then export it
$TOOLS/tracy-csvexport scan.tracy > zones.csv         # per-zone statistics
$TOOLS/tracy-csvexport -u scan.tracy > events.csv     # every zone event
$TOOLS/tracy-csvexport -m scan.tracy > messages.csv   # the messages
$TOOLS/tracy-csvexport -u -p scan.tracy > plots.csv   # the plot samples
```

`tracy-capture`: `-o` output file, `-a` the application's address, `-f`
overwrite an existing output, `-s <seconds>` stop after that long.
`tracy-csvexport`: by default one row per zone with its count and
total/mean/min/max/standard deviation; `-f <text>` filters zone names (so
`-f solver.` is the solver), `-e` reports self time rather than inclusive.
That is the neutral interchange: plain CSV, readable by anything.
`tracy-import-chrome` converts a Chrome-trace JSON the other way, and
`tracy-update` upgrades an old capture to the current format.

Interactively, run the `tracy-profiler` GUI for the same version (a release
from <https://github.com/wolfpld/tracy/releases>, or built from that tag), press
**Connect** and enter `127.0.0.1`, or pick the application from the discovery
list. The GUI is not part of the build.

## What is instrumented

Zone names are `<layer>.<thing>`, lower case, dotted. They are what the CSV
and the GUI show, so they are the stable interface; rename one deliberately.

### Frames

Three frame sets, because three unrelated real-time cadences on one set would
interleave into a single meaningless graph:

| Frame set | One mark per | Where |
| --- | --- | --- |
| *(unnamed)* | camera frame processed by the direct scan (the vision pass) | `DirectScan::processFrame` |
| `Capture` | camera frame delivered by the device | `OpenCvCaptureLoop::run` |
| `UI` | Qt Quick presentation (`frameSwapped`) | `main.cpp` |

### Threads

`main` (GUI), `camera-capture`, `recorder-writer`. Pool threads that run a
solve keep their default name - they are shared, and a name is written once.

### Zones

| Layer | Zones |
| --- | --- |
| **capture** | `capture.read` (the blocking device read: its length *is* the camera's frame interval), `capture.publish`, `capture.start`, `capture.stop`, `capture.enumerate_devices`, `capture.photometry_lock`, `capture.photometry_restore` |
| **direct scan** | `direct_scan.poll_frame`, `direct_scan.process_frame`, `direct_scan.record`; `vision.prepare_frame` (the working-width reduction, threshold and outlines both readers start from, once per frame), `vision.direct_scan.read_side` (the per-frame sticker read), `vision.corner_view.read_corner` (the two-sides read, only on a frame with no square-on side), `vision.corner_view.fit_corner` (one such read held against one candidate cube, only when the six sides make more than one), `vision.direct_scan.add` (fusing a read into the scan), `vision.direct_scan.scanned` and `vision.direct_scan.diagnose` (colour resolution and assembly: one judgement of the scan, which the app makes whenever a held side's fused colours moved), `vision.direct_scan.assemble` (putting the named sides together into a cube: one event for each time it is done, which a scan whose sides make no cube does not repeat while what it rests on is as it was) |
| **held cube** | `held_cube.on_frame_watched`, `held_cube.judge`, `vision.held_cube.fit` |
| **colour and recording** | `vision.resolve_colors`, `vision.analysis_downscale`, `vision.recorder.push` (the GUI-thread clone) and `vision.recorder.write_job` (the writer thread's disk write) |
| **solver** | `solver.solve`; per stage `solver.cross`, `.corners`, `.middle_edges`, `.oll`, `.pll`, then `solver.merge_adjacent_moves`; the other two methods are `solver.cfop` and `solver.two_phase` (the latter's lookup tables are built once: in `solver.two_phase.prepare` when asked for ahead of time, otherwise inside the first `solver.two_phase`); underneath, `solver.piece_search` (the first-two-layer searches, run on piece positions) and `solver.algorithm_search` (a template, so each stage's instantiation is its own row in the statistics) carry the **visited-node count** as a zone value (the first-two-layer searches' real cost, which is why the [slow tests](testing.md#slow-tests) are slow); `solver.bfs_search` is the same search over whole cubes, which only a cube that cannot be turned from solved ever reaches; `solver.assemble_scan` and `solver.assemble_all` (the one cube the six sides make, and every cube they make) |
| **app** | startup: `app.i18n_install`, `app.load_qml` (the largest single startup cost); `app.solve_stepped` (the worker), `app.solve.apply_result`; `app.frame_image.request` (the preview's `cv::Mat` to `QImage` copy) |

### Messages

| Name | Meaning |
| --- | --- |
| `direct_scan.completed` | the scan produced a cube |
| `solve.started`, `solve.finished` | the solve worker's bounds |

There are no plots today; `CUBE_PLOT` is available for the next one (a
per-frame statistic worth correlating with the zones).

The trace's **information** window carries the build (`cube::version_details()`:
version, commit, branch, dirty flag), so a capture says what it measured.

### What is not instrumented

Code with no caller in the application carries no zones, which could never
fire. The old scan pipeline's observation, orientation-tracking and
capture-routing code was of that kind until it was removed (step R6 of the
[recovery plan](recovery-plan.md)); if something like it is wired back in,
instrument it then.

## Adding instrumentation

Include `<cube/profiling.hpp>` (it is on `cube::lib`'s public include path, so
every target already has it) and use the macros. Nothing else in Tracy's API
is available, on purpose: the macros are what compiles to nothing.

| Macro | Use |
| --- | --- |
| `CUBE_ZONE_SCOPED_N("layer.name")` | a zone for the rest of the C++ scope. The default choice. `CUBE_ZONE_SCOPED()` names it after the function |
| `CUBE_ZONE_BEGIN(var, "layer.name")` / `CUBE_ZONE_END(var)` | a span that is not one scope - several stages that share variables in one long function. Nothing needs it today; it is here, tested, because this family's other apps have it. Exception safe: the zone closes on unwind if `END` was not reached |
| `CUBE_ZONE_VALUE(var, n)` / `CUBE_ZONE_SCOPED_VALUE(n)` | attach an unsigned number (a count, a size) to the zone |
| `CUBE_FRAME_MARK()` / `CUBE_FRAME_MARK_NAMED("Name")` | frame boundary on the default / a named set |
| `CUBE_PLOT("name", n)` | a number on a timeline; integer, floating point or bool |
| `CUBE_MESSAGE("name")` | a discrete event |
| `CUBE_THREAD_NAME("name")` | name the calling thread, **once** per thread - every call adds a table entry, so never in a per-frame or per-task path |
| `CUBE_APP_INFO(text)` | free text for the trace's information window |

Rules that keep it honest:

- **Names are string literals.** Tracy stores the pointer, not a copy.
- **Arguments are not evaluated when profiling is off.** Keep what goes into
  `CUBE_PLOT` / `CUBE_ZONE_VALUE` free of side effects, and never let it be the
  only use of a variable (an unused-variable warning is an error under
  `-Werror`, in exactly the build nobody profiles).
- **Put a zone where there is work, not on a tight inner loop.** A zone costs
  tens of nanoseconds while a profiler is attached and an atomic load and a
  branch otherwise. A pass is milliseconds; a per-cell zone inside it is noise.
- **Zones that can return early on the idle path** (a poll that finds an empty
  mailbox) go *after* the early return, or the trace fills with zero-length
  zones.
- **A manual zone is for the one function that needs it.** Prefer the scoped
  form, and wrap a statement in a lambda or block when you need to isolate it.

## Holding a zone to a baseline

The zone names above are the interface two more things read. The performance
tier's budgets count what the solver and the reader *do* (search nodes, pixel
buffers) with no profiler in the loop, and `tools/perf/tracy_check.py` runs the
headless `cube_perf_tests` under `tracy-capture` and compares each zone's call
count and fastest call, as a multiple of the test binary's own
`perf.calibration` zone, with `tools/perf/tracy_baseline.json`. Renaming a zone
is therefore a deliberate act that updates that file as well. Both are
described in [Performance tests](testing.md#performance-tests).

## In the browser

Tracy cannot be used on the web build, and not for want of a port: a Tracy
client **opens a TCP listener** and answers UDP discovery, and a browser tab can
do neither. What takes its place is a recorder inside the page.

The same macros (`CUBE_ZONE_SCOPED`, `CUBE_PLOT`, `CUBE_FRAME_MARK_NAMED`, ...)
have a third implementation, chosen the same way as the other two: by CMake,
with `-DCUBEULATOR_ENABLE_PROFILE_RECORDER=ON`, which puts
`src/lib/variants/profiling-recorder/` on the include path.
Zones, frames, messages and plots then go to `cube::profile::Recorder`
(`cube/profile/recorder.hpp`): a fixed ring of lock-free slots, two clock reads
and a few relaxed atomic stores per zone, nothing allocated and nothing locked
while recording. It is plain C++ and is built into `cube::lib` on every
platform, so it is unit-tested natively (`profile_recorder_test.cpp`); only its
export to the page is web code (`src/app/platform/web/profile_web.{cpp,js}`).

```sh
cmake --preset web-emscripten-diagnostics
cmake --build --preset web-emscripten-diagnostics
```

That preset is Release with function names kept (`--profiling-funcs`: a trace
reads `cube::solver::detail::bfs_search`, not `wasm-function[4171]`, without the
size of full DWARF), the recorder, and the read-only driver the
[browser tests](testing.md#browser-tests) use. It is for measuring; the site
that is published is the plain `web-emscripten-release`.

### Reading it

The page gets `window.cubeulatorProfile`:

| | |
|---|---|
| `recording` | `true` in this build; `false` in any other, where everything below is empty |
| `summary()` | per zone: `count`, `total_ms`, `mean_ms`, `min_ms`, `max_ms`, `stddev_ms`; and how many frames and messages of each name |
| `trace()` | Chrome's Trace Event Format, as text |
| `csv()` | zone statistics in `tracy-csvexport`'s columns, so `tools/perf/tracy_check.py` and any script written against a Tracy export read it unchanged |
| `clear()` | forget what has been recorded (the ring holds 131 072 events, about a quarter of an hour of a scan, then overwrites the oldest and counts them in `dropped`) |
| `mark(text)` | a message on the timeline and a `performance.mark` of the same name: where a step begins |
| `now()` | the recorder's clock, microseconds |
| `memory()` | the wasm heap's size, and in Chromium the JavaScript heap's |
| `download('trace' \| 'csv' \| 'summary')` | saves it as a file |

From the DevTools console:

```js
cubeulatorProfile.summary().zones['solver.solve']
copy(cubeulatorProfile.trace())      // or download('trace')
```

A trace opens in **DevTools > Performance > Load profile**, in
[Perfetto](https://ui.perfetto.dev), and, through Tracy's `tracy-import-chrome`,
in the Tracy GUI - so the one place the family's habit of reading a timeline in
Tracy still works.

### What to keep in mind

- **The clock is `performance.now()`.** A page that is cross-origin isolated
  (this one must be, for its threads) gets it at 5 microseconds; a zone shorter
  than that reads as zero.
- **DevTools open slows the page down**, and a page not in front is throttled.
  Record with it closed and read the numbers from `summary()`, or record from a
  script (Playwright's `page.evaluate`, which is what the suite does).
- **The first runs are not the steady state**: WebAssembly is compiled by a
  baseline tier first and optimised in the background. A number worth keeping is
  taken after a warm-up, as the tests do.
- **A headless runner draws in software** (SwiftShader), so frame times there
  measure the CPU renderer; the solver and reader zones are CPU in any case.
  A test that reads a frame time asserts the renderer first.
- **Threads are Web Workers**: each shows as its own `thread N` track; the main
  thread is the page's.
- **Nothing here is shipped.** The ring is not allocated unless something
  records into it, but the build is a development build like the Tracy one.

The performance tests that read this - budgets on how long a solve and a
scan take in the browser, and the report each run leaves - are in
[Browser tests](testing.md#browser-tests).

## Licence

Tracy is BSD-3-Clause. It is present only in a profiling build, which is a
development build and is not packaged; the
[generated third-party notices](licensing.md#6-how-the-notices-are-produced)
are derived from the application's link line, so a package made from one would
list it without anyone adding a line by hand (checked: `third_party_notices_complete`
passes with Tracy on). The list also names `capstone`, `ppqsort` and `zstd`,
which vcpkg records as dependencies of the `tracy` port because of its
command-line tools; nothing links them, so that is over-inclusion, not a
shipped component.

## Why it is built this way

- **CMake chooses the header, not an `#ifdef`.** `cube/profiling.hpp` exists
  three times, under `src/lib/variants/profiling-tracy_{enabled,disabled}/` and
  `profiling-recorder/`, and `src/lib/CMakeLists.txt` puts one directory on the
  include path - the
  mechanism ICL Forge uses for the same seam, and the one
  [architecture.md](architecture.md) already requires of platform
  differences. A unit test (`profiling_test.cpp`) pins the variant against the
  option, so the wrong directory cannot be selected unnoticed.
- **An opt-in vcpkg feature, not a base dependency.** `opencv4` already makes
  a cold build heavy; a normal configure never resolves Tracy, and the binary
  cache is untouched.
- **Tracy's crash handler is off** (`default-features: false` drops the port's
  `crash-handler`). On Windows it installs an unhandled-exception filter *when
  a profiler connects* - after `main()` has installed the app's own minidump
  writer - which replaces it, suspends every other thread and does not chain
  to what it replaced. The app already owns crash reporting, and a hard fault
  while someone is profiling is exactly when `cube_app_crash.dmp` has to be
  written. A fault still ends the capture; you lose Tracy's crash callstack,
  not the minidump.
- **On-demand.** Zones are only recorded while a profiler is attached, which
  keeps an idle instrumented build close to a normal one and means the
  instrumentation never has to be removed to run it.
- **No sanitizers, no coverage.** Both would be what you measured.
