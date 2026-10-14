# Performance

Where Cubeulator's time goes, measured with the [Tracy instrumentation](profiling.md)
on a real build and ranked by what a person waits for - what was done about the
worst of it, and what was left alone and why.

Everything here is one desktop. A phone has not been measured; where this page
says what a phone would see it is a multiple of a desktop figure, marked as an
estimate.

## Budgets

| Path | Budget | Where it comes from |
|---|---|---|
| One camera frame, `direct_scan.process_frame` | 33 ms, all of it, on the GUI thread | `DirectScan` polls at about 30 Hz (`kPollIntervalMs`); a pass longer than the interval delays the next frame and everything else on that thread |
| One solve, `solver.solve` | seconds on a phone, for every input | the solve screen waits for it; no search path may be unbounded (`solver.hpp`) |
| Launch to a drawn first screen | no figure is committed; under about 2 s on a desktop is proposed here | nothing waits on it but the person |
| One UI frame | 16.7 ms (60 Hz) | the display |

## How the numbers were taken

- **Build.** `windows-msvc-profiling` (RelWithDebInfo, Tracy in on-demand
  mode), MSVC 19.51, Windows 11 x64, 32 cores. Qt 6.12.0, the version the
  application ships on, for everything that runs Qt.
- **Machine.** Shared with other builds, so wall time carries noise of tens of
  percent and the tails (p99, max) are not to be read. What holds is the
  ratio: before and after were run alternately on the same corpus, at High
  priority, with the same binary layout.
- **Capture.** `tracy-capture`, then `tracy-csvexport -u` (one row per zone
  event, the visited-node count as its value), the workflow in
  [profiling.md](profiling.md). Percentiles are over those events.
- **Solves.** `cube_solve_bench` (`tools/solve_bench`): seeds 0 to 27, 25-move
  scrambles. It prints a digest of every solution, so "the same solutions" is a
  comparison of one number.
- **The camera frame.** `cube_scan replay` over 200 frames of a locally
  recorded scan (the repository is public and recorded frames are never
  committed), at the recorder's 640-wide analysis size and at the 1920x1080 the
  camera delivered. In the app itself, the journey case of the system tests
  under Tracy: a real window, the fake camera's drawn 640x360 frames.
- **Startup.** The launch screen, four warm runs, medians, with timestamps
  taken in QML and C++ in experiment builds that are not committed.
- **Not measured:** the camera capture thread (no camera was involved) and a
  phone.

## Findings

Zone names are in [profiling.md](profiling.md#zones). Each row is `develop` as
it was when the measurement was taken.

### The solver

The default method (`Beginner`), 28 solves, one capture.

| Zone | Per solve | Share | Visited nodes per solve | Cost per node |
|---|---|---|---|---|
| `solver.middle_edges` | 1818 ms | 76% | 758 000 | 2.4 us |
| `solver.cross` | 337 ms | 14% | 99 000 | 3.4 us |
| `solver.corners` | 219 ms | 9% | 129 000 | 1.7 us |
| `solver.oll`, `.pll`, `.merge_adjacent_moves` | under 0.1 ms | 0% | | |
| **`solver.solve`** | **2387 ms** (capture attached) | | 985 000 | |

Unattached, alternated with the fix below: **1.77 to 1.86 s** a solve, 3.0 s the
worst of 28. All of it is the breadth-first search over the pieces of the first
two layers. A visit cost microseconds because a node was a 54-sticker cube:
turning it copied the cube, its signature was a dozen `locate_*` scans, and
every visited signature was a heap allocation in an `unordered_set`.

The other methods are not a problem: `Intermediate` 2.3 ms a solve, `Advanced`
70 ms (and 40 to 110 ms, once, for its tables, which the app builds on a worker
at startup).

A cube that is physically valid and not solvable (a twisted corner, a flipped
edge) took **2.5 to 6 s** to say `no_solution`: three searches, each run to its
node budget. Nothing in the app produces one, but `solve()` is public and
promises bounded work on every input.

### The camera frame

200 frames of a scan in progress: about two thirds are a side held still, the
rest the cube being turned. p50 per frame.

| | 640 wide | 1080p |
|---|---|---|
| a side held still: `read_side` | 2.0 ms | 4.0 ms |
| the cube turned: `read_side` and then `read_corner` | **4.2 ms** (2.0 + 2.2) | **8.2 ms** (4.0 + 4.2) |

Inside `read_side` at 640 wide: the adaptive threshold 0.57 ms, the contours
0.74, the blur 0.16, the shape filter 0.15, the nine patches 0.14, the lattice
fit 0.03; at 1080p the area resize adds 1.25 ms. `read_corner` began with the
same resize, threshold and contours again, which is why it cost about what
`read_side` did. The rest of the per-frame work is small: `SideScan::add`
0.007 ms; assembling a cube (`vision.direct_scan.scanned`) 1 to 3 ms but only
when the sides change, with 0.5 ms for each of up to 16 assembly attempts.

In the application, with the fake camera's 640x360 frames
(`direct_scan.process_frame`, 2085 frames): p50 2.2 ms, p99 2.9 ms. About 0.4 ms
of that is not the vision pass: the signals to the screen, the preview, the
held-cube judge. `app.frame_image.request` (the preview's `cv::Mat` to `QImage`
copy) is 0.07 ms at this size.

### The UI frame

Qt's own render timing (`QSG_RENDER_TIMING`) on the launch screen: a steady
frame is 2 ms (p50) on a 16 ms interval, 8 ms at the 99th percentile. The
expensive frames are the first two: 492 ms (487 of it scene-graph sync, the 3D
views' models) and 347 ms (332 of it creating pipelines).

Tracy's `UI` and `Capture` frame sets record the same cadence but
`tracy-csvexport` does not export frame times, so they are read in the GUI.

### Startup

From the start of `app.load_qml` to its end, four warm runs at High priority,
medians:

| Step | ms |
|---|---|
| process start to the first QML singleton (`app.i18n_install` is 5 to 12) | 163 |
| creating the launch screen | 803 |
| showing the window: native window, graphics device, first sync | 577 |
| **`app.load_qml`** | **1544** |
| then the first frame renders | about 350 |

What the 803 ms is. Each row is an experiment build; none of it is committed.

| Launch screen | creation | `app.load_qml` |
|---|---|---|
| as it is | 803 | 1544 |
| one of its two 3D cubes removed (the layout not on show) | 650 | 1480 |
| both 3D cubes removed | 526 | 1274 |
| `Theme.fontHeading` and `fontMono` set to families that are installed | 241 | 1051 |
| the six font files `feature/ax-kit` bundles, loaded with `addApplicationFont` (20 ms) | 200 | 995 |

So the launch screen's cost is mostly **two font families that are named and
not bundled** (`Archivo`, `IBM Plex Mono`: the font lookup for a family that is
not there is about 550 ms, once, at the first text), then about **140 ms for
each `Cube3DView` it creates**, of which it creates two - the narrow and the
wide layout are both built, and one is hidden - and each builds four `Model`s
and materials per sticker, of which one is ever drawn.

When the solve finishes, `app.solve.apply_result` (the result landing on the GUI
thread, which includes creating the solve screen and its cube) was 57 ms and
168 ms in two solves: a hitch of three to ten frames as the screen appears.

## Ranking

By what a person waits for, and how far it is from its budget, as found:

1. **The solve**: 1.8 s on the desktop for the default method, 3 s the worst.
   Several seconds on a phone, over a budget of "seconds".
2. **Startup**: 1.5 s on a fast desktop before the first frame, more than half
   of it fonts that are not installed and a second 3D cube nobody sees.
3. **A turned cube in the camera pass**: 4 ms of 33 at 640 wide, 8 ms at
   1080p; times a phone's slower core, the one place the frame budget could be
   in danger.
4. **An unsolvable cube**: 2.5 to 6 s, for an input the app cannot produce.
5. Everything else is far inside its budget: a steady frame 2 ms, `add`
   microseconds, the last layer and the other two methods.

## What was fixed

| | Before | After | |
|---|---|---|---|
| The first-two-layers searches follow pieces, not cubes | 1.77 to 1.86 s a solve, 3.0 s worst | **0.12 s**, 0.21 to 0.24 s worst; the same 985 000 visits and the same solutions (digest `1bc470006f988409`) | [#75](https://github.com/iainchesworthlabs/cubeulator/pull/75) |
| An unsolvable cube is answered at once | 2.5 to 6 s | under 0.1 ms, the same `no_solution` | [#74](https://github.com/iainchesworthlabs/cubeulator/pull/74) |
| A frame is prepared once for both readers | 4.2 ms turned, 8.2 ms at 1080p | 2.2 ms, 3.6 ms; the same reads | [#78](https://github.com/iainchesworthlabs/cubeulator/pull/78) |

The solver's cost per visit fell from 1.7 to 3.4 us to 0.15 to 0.29 us; what it
did not change is the number of visits, and that is the one lever left (below).
Over 2000 further scrambles the new search had no failure, a mean of 104 ms, a
95th percentile of 186 ms and a worst of 341 ms; 120 more gave the digest the
old search gave.

## What was not fixed

- **The fonts.** The cost is the fonts being named and not shipped, which
  `feature/ax-kit` already fixes by bundling six font files and loading them.
  Measured here with that branch's files: **-550 ms of startup**, and the
  kit's loader (`bundled_fonts.cpp`) costs about 20 ms of it back. The thing to
  check when the kit lands is the startup timeline above, not just the glyphs.
- **Two cubes per screen, four models per sticker.** About 140 ms per instance
  to create, and the 57 to 168 ms the solve screen costs to arrive. Building
  the layout that is on show only (a `Loader` on the width breakpoint), and the
  shader style that is in use only (a `Loader3D` over the three sticker
  models), would remove most of it. It is in the screens the experience plan's
  E3 is rebuilding, so it is a recommendation to that lane and not a patch
  here; the hidden cube's `autoRotate` timer also fires at 60 Hz for a view
  nobody sees, which is worth the same Loader.
- **Showing the window and the first frame**, 577 ms and about 350 ms: the
  native window, the graphics device and the first pipelines. It does not
  change with the 3D cubes removed (the experiment above), so it is Qt and the
  driver; the graphics API picker in the settings is the one lever and it is
  the person's.
- **The solver's visit count.** About a million visits a solve, 24 bytes each
  in an arena. The searches could be made to visit a small fraction of that
  with the same solutions: a ball around the goal, to a few moves, gives an
  exact lower bound on the distance left, and a search that skips a node whose
  depth plus that bound exceeds the length of the answer returns the first
  answer a plain breadth-first search does, because it only skips what cannot
  be on a path of that length (the bound never overstates, and it changes by at
  most one a move). It would be a five to twenty fold cut in visits. It is not
  done because a solve is already 0.12 s on a desktop, which is under the
  budget with room for a phone several times slower, and the change would need
  its own proof; the golden digests in `piece_search_test.cpp` are the check it
  would be held to.
- **Assembly retries, the median in `SideScan::add`**: microseconds to a
  couple of milliseconds, and only on frames that change a side.
- **A phone and the capture thread**, which were not measured. The estimate for
  a phone, 3 to 5 times a desktop core: a solve 0.4 to 0.6 s; a turned cube at
  1080p 11 to 18 ms of the 33. Both are inside their budgets with that
  multiple, which is the thing to confirm on the first device.

## Reproducing

```sh
cmake --preset windows-msvc-profiling
cmake --build --preset windows-msvc-profiling

TOOLS=build/windows-msvc-profiling/vcpkg_installed/x64-windows/tools/tracy
BIN=build/windows-msvc-profiling/bin

# the solver: a seeded corpus, zones by stage, a digest of every solution
$TOOLS/tracy-capture -o solver.tracy -a 127.0.0.1 -f &
$BIN/cube_solve_bench --method beginner --count 28 --seed 0 --moves 25
$TOOLS/tracy-csvexport -u solver.tracy > solver-events.csv     # value = visited nodes

# the camera frame: recorded frames through the reader the app uses
$TOOLS/tracy-capture -o scan.tracy -a 127.0.0.1 -f &
$BIN/cube_scan replay <session-dir>            # <session-dir>/fullres for 1080p

# the application, the journey case of the system tests
$TOOLS/tracy-capture -o journey.tracy -a 127.0.0.1 -f &
$BIN/cube_system_tests "[journey]"

# Qt's own frame timings: launch the app with
#   QSG_RENDER_TIMING=1  QT_LOGGING_RULES=qt.scenegraph.time.renderloop=true
```

Startup is read from the `app.load_qml` zone of a capture of the application
itself (`Cubeulator.exe` on Windows; see
[profiling.md](profiling.md#capturing-a-trace)). The pieces of it in the table
above need timestamps inside QML, which the experiment builds added and the
tree does not carry.
