# cube3d_prototype

The layer-lift renderer for a cube of any size, built to answer one question before a
screen is built on it: **can the app draw a 6x6, a 7x7, a 9x9 and turn a layer of it,
as smoothly as it draws a 3x3?** ([docs/nxn-app-plan.md](../../docs/nxn-app-plan.md),
decision 4, and the size range of decision 3, which depends on the answer.)

It is a measurement, not a feature: a project of its own (like `lightprobe`), not part
of the application's build, and nothing ships from it.

## What is in it

- **Lift** (`LiftCube.qml`): the cube at rest is one mesh. The mesh (`cube_mesh.*`, no
  Qt) is the visible face of every cubie as a black quad the size of the face with a
  coloured sticker on it, in one vertex-coloured `QQuick3DGeometry` (`cube_geometry.*`).
  While a layer turns the cube is two meshes, the layer on a pivot and everything else,
  with black quads on the cut faces so the cut looks solid part-way round. A move is:
  build two meshes, turn the pivot, apply the move to the cube, build one mesh. Three
  scene objects at every size.
- **Naive** (`NaiveCube.qml`): what scaling the 3x3's `Cube3DView.qml` up would be. One
  node for every cubie that shows anything, a body and a sticker model for each face
  that shows, a material of its own on each sticker bound to its facelet's colour, and
  a layer lifted by hanging its cubies off a pivot. 872 models at 9x9.
- The cube and its moves are the real library (`cube::nxn`, compiled from source, no
  OpenCV): a scrambled cube and, with `--solve`, its actual solution.

`cube3d_prototype --selftest` checks the mesh against the library at every size from
2x2 to 9x9: for each layer of each axis, the stickers the mesh puts in the layer are the
ones `Geometry::turn` moves (all of them, but the middle of the turning face), and the
layer and the rest together make the cube and its cuts.

## Running it

```
cmake -S tools/cube3d_prototype -B build/cube3d -DCMAKE_PREFIX_PATH=<Qt prefix> \
      -DFMT_INCLUDE_DIR=<a directory holding fmt/format.h>
cmake --build build/cube3d
build/cube3d/cube3d_prototype --mode lift --size 7 --moves 40 --solve --report out.jsonl
build/cube3d/cube3d_prototype --mode naive --size 7 --moves 40 --solve --report out.jsonl
```

It opens a window (behind the others, without taking focus), watches the still cube,
plays the moves 160 ms a turn, prints one line of JSON and quits. The render loop is
`basic` and vsync is off unless `--vsync` is given. `--screenshot` and
`--screenshot-mid` save pictures. What the numbers are:

| field | meaning |
|---|---|
| `moving.syncRenderMeanMs` | the scene graph's own work in a frame, from the animations having advanced to the frame being swapped in: the figure that says whether a frame fits in 16.7 ms |
| `moving.p50Ms`, `p95Ms` | the interval between frames; with vsync it is the display's 16.7 ms when the frame fits, and longer when it does not |
| `moveWall.meanMicros` | what one move took on the clock (a turn is `--turn-ms`, more if frames were late) |
| `pickUp`, `setDown` | the time in lifting a layer and setting it down |
| `sceneReadyMs`, `peakWorkingSetMB` | to the first frame; peak memory (`run-proto.ps1`-style wrappers add it) |

**WebAssembly.** Configure with the Qt wasm kit's `qt-cmake` (Emscripten 5.0.5, and
`-DQT_HOST_PATH=<the desktop Qt>`), serve the directory with
`Cross-Origin-Opener-Policy: same-origin` and `Cross-Origin-Embedder-Policy:
require-corp`, and open
`run.html?mode=lift&size=7&moves=40&solve=1` (the page `run.html`, copied beside the
build, passes the address's query to the program as its command line). A page that is not on
screen is not drawn, so do not measure it in a hidden tab: a headless Chromium with the
GPU on (`--headless=new --use-angle=d3d11`, driven over the DevTools protocol to read
the console) is.

**A phone.** `qt-cmake` from the Qt Android kit, target `apk`, signed with any key
(the unsigned APK will not install). With a phone on USB debugging and the screen on:

```
python3 tools/cube3d_prototype/android_matrix.py --apk cube3d_prototype-signed.apk
```

installs it, runs the lift renderer and the naive one at 3x3, 5x5, 7x7 and 9x9, and
prints the same figures from the device's log (`CUBE3D-RESULT`).

## What it found (2026-10-16)

On the 32-core development machine; D3D11 for the native build, headless Edge 155 with
ANGLE D3D11 on the same GPU for the browser, the cube's own solution for 40 moves,
a 160 ms quarter turn, a 900 x 900 window with 4x MSAA. The scene graph's work per
frame, mean (p95), in milliseconds, and the frame rate the browser held:

| | native lift | native naive | browser lift | browser naive |
|---|---|---|---|---|
| 3x3 | 0.22 (0.37) | 0.61 (0.72) | 0.47 (0.70), 60 fps | 3.5 (3.9), 60 fps |
| 5x5 | 0.19 (0.30) | 1.9 (2.5) | 0.38 (0.50), 60 fps | 9.8 (10.6), 60 fps |
| 7x7 | 0.22 (0.34) | 2.9 (3.3) | 0.38 (0.52), 60 fps | **30.6 (36.4), 31 fps** |
| 9x9 | 0.20 (0.33) | 6.7 (8.4) | 0.44 (0.58), 60 fps | **47.8 (57.9), 20 fps** |

- **The lift renderer costs the same at every size**: 3 scene objects and 108 to 972
  quads (a 9x9 is 3,888 vertices), a fifth to half a millisecond of scene-graph work a
  frame, 60 fps in the browser at 9x9. The time to pick a layer up and set it down is
  0.1 to 0.2 ms (mesh builds included, `O(N^3)` on a loop over cubies, which at 9x9 is
  nothing).
- **The naive view grows with the cube** (80, 248, 512, 872 models): in the browser it
  is over a third of a frame at 5x5 and misses 60 fps at 7x7; every sticker is a model
  and a material and a binding. On a phone, three to five times slower, the 5x5 would.
- Memory: the lift process peaks at 108 MB at 3x3 and 130 MB at 9x9 (Qt itself is most
  of that); the naive one at 118 MB and 263 MB.
- A first frame in the browser takes 1.1 to 1.2 s whatever the cube (shader work), and
  the page's first visit another few seconds to compile its 35 MB of WebAssembly; the
  cube is not the cost of either.

So the renderer is not what limits the size. Nothing here is a phone yet: the Android
build is made (`cube3d_prototype-signed.apk`, 74 MB) and `android_matrix.py` is what
runs it; until it has, the phone's figure is an estimate (0.2 to 0.4 ms of scene graph
on the desktop, so about 1 to 2 ms on a phone five times slower, with the GPU's own
time on top, which a thousand quads do not make a problem).

## What it is not

It has none of what makes `Cube3DView.qml` look like the app: no rounded cubies or
bevels, no image-based light (the sides facing away from the light are dark), no
nudge, no hold, no stickers' gloss, no scan overlays. Those carry over in P1, on top of
this. The mesh builder would move into the library (`src/lib`, with unit tests)
when it does.
