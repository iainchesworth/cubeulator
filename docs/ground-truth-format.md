# Ground-truth sidecar format (corpus v2)

This is the committed spec for per-clip ground truth, Phase 0 work item 4
of [pipeline-plan.md](pipeline-plan.md). Every rate-gate denominator and
every pixel-error gate in Phases 1-6 is scored against files in this
format. The labelling tool that produces them lives at
`tools/labelling/label_tool.html` (see `tools/labelling/README.md`).

## Directory convention

Ground truth is a sidecar directory inside the recording directory,
except for `manifest.json`, which is written by the recorder itself and
lives at the recording root:

```
<recording-dir>/
  index.csv                     # recorder output (pass,t_ms,w,h,full_w,full_h)
  rec_000000.jpg ...            # recorder output, analysis-scale frames
  manifest.json                 # recorder output (schema below)
  groundtruth/
    segments.json               # frame-range annotations + re-entry events
    keyframes/
      000123.json               # one file per labelled keyframe
      000188.json
      ...
```

Keyframe files are named by pass number, zero-padded to six digits to
match `rec_%06d.jpg` (so listings sort chronologically).

## Frame identity and coordinates

- **Frames are recording pass numbers** - the `pass` column of
  `index.csv` (see `src/lib/include/cube/vision/frame_recording.hpp`).
  Pass numbers, not row indices: a recording need not start at pass 0.
  Frame *files* wrap at the 3600-frame ring; rows whose file has been
  overwritten (`max_pass - pass >= 3600`) must not be labelled and are
  ignored by consumers.
- **All 2-D points are in analysis-scale pixels** - the coordinate frame
  of the recorded `rec_*.jpg` itself, whose size is that pass's `w`,`h`
  in `index.csv`. Never full-resolution pixels (`full_w`,`full_h` exist
  only to recover the live pipeline's upscale factor).
- Origin is the **top-left** of the frame; `x` grows right, `y` grows
  down. Values are floats; sub-pixel precision is expected. Points may
  lie slightly outside `[0,w) x [0,h)` when the cube is partially out of
  frame - consumers must not clamp them.

## `groundtruth/segments.json`

Frame-range annotations. These define every rate-gate denominator
(detection rate per segment class, jitter on steady segments, turn-clip
survival, false positives on absent segments).

```json
{
  "version": 1,
  "clip": "clip-2026-07-30-freerot-a",
  "segments": [
    { "start": 0,   "end": 116, "tags": ["in_frame", "steady"] },
    { "start": 117, "end": 233, "tags": ["in_frame", "turning"] },
    { "start": 234, "end": 250, "tags": ["absent"] },
    { "start": 251, "end": 527, "tags": ["in_frame", "occluded"] }
  ],
  "reentry_events": [251]
}
```

| Field | Meaning |
| --- | --- |
| `version` | Format version, currently `1`. |
| `clip` | Recording directory name (informational; the file's location is authoritative). |
| `segments` | Array of tagged pass ranges. `start` and `end` are pass numbers, **inclusive on both ends**. |
| `segments[].tags` | One or more of the tag vocabulary below. |
| `reentry_events` | Array of pass numbers at which the cube **re-enters** after a full occlusion or a frame exit - the first pass on which any part of the cube is visible again. Drives the re-acquisition-latency gate. |

Tag vocabulary:

| Tag | Meaning |
| --- | --- |
| `in_frame` | Any part of the cube is visible in the frame. |
| `absent` | No part of the cube is visible (out of frame, or fully occluded). |
| `occluded` | The cube is partially occluded (typically by the hand): visible, but with part of its silhouette covered. Implies `in_frame`. |
| `steady` | The cube is effectively stationary (held still); used for jitter/stability gates. |
| `turning` | A layer turn is in progress. |

Semantics:

- Segments may **overlap**; a frame's effective tag set is the **union**
  of the tags of every segment covering it. (Label one long `in_frame`
  range, then shorter `steady`/`turning`/`occluded` sub-ranges on top.)
- `in_frame` and `absent` are mutually exclusive on any one frame, as
  are `steady` and `turning`. `occluded` implies `in_frame`.
- Frames covered by no segment are *unannotated* and appear in no gate
  denominator. The Phase 0 exit gate requires complete coverage
  (every surviving pass covered by at least one segment) for every
  corpus clip.
- **"Sustained" is defined as >=5 consecutive frames** (passes). Gates
  that speak of sustained behaviour (e.g. "zero sustained false
  positives on absent segments") use this constant.

## `groundtruth/keyframes/<pass>.json`

One file per hand-labelled keyframe. All three top-level content fields
are **mandatory**; a keyframe missing any of them does not satisfy the
Phase 0 exit gate (corner-only labels explicitly do not count).

```json
{
  "version": 1,
  "pass": 123,
  "interpolated": false,
  "corners": [
    { "model_id": 0, "x": 412.3, "y": 187.6 },
    { "model_id": 1, "x": 300.1, "y": 179.9 }
  ],
  "faces": [
    {
      "face": "U",
      "quad": {
        "tl": { "x": 305.0, "y": 120.4 },
        "tr": { "x": 401.7, "y": 128.9 },
        "br": { "x": 409.2, "y": 210.5 },
        "bl": { "x": 298.4, "y": 201.1 }
      },
      "centres": [
        { "x": 322.1, "y": 136.2 }, { "x": 354.0, "y": 139.0 }, { "x": 385.8, "y": 141.9 },
        { "x": 320.3, "y": 163.9 }, { "x": 352.6, "y": 166.7 }, { "x": 384.6, "y": 169.8 },
        { "x": 318.6, "y": 191.2 }, { "x": 351.0, "y": 194.1 }, { "x": 383.2, "y": 197.4 }
      ]
    }
  ]
}
```

| Field | Meaning |
| --- | --- |
| `version` | Format version, currently `1`. |
| `pass` | The frame's pass number. Must match the filename. |
| `interpolated` | `false` = hand-labelled; **only these frames are valid for gate scoring**. `true` = tool-interpolated aid, NOT yet valid for gates (see "Interpolation" below). |
| `corners` | The **visible** outer cube corners (typically up to 7; fewer under occlusion). Each entry: `model_id` (0-7, table below) plus `x`,`y` in analysis-frame pixels. Occluded/invisible corners are simply omitted. Each `model_id` appears at most once. |
| `faces` | One entry per **visible** face (up to 3). Fields below. |
| `faces[].face` | Body-face identity: one of `U R F D L B`. Identity is keyed by **centre colour**: the face whose centre sticker shows colour C is the face whose centre is C in the clip's `manifest.json` state (use the per-face reference photos captured at record time). This is what makes per-cell colour ground truth computable - `faces[].face` + the manifest state give every sticker's true colour. |
| `faces[].quad` | The face's 4 projected outer corners, keyed `tl`/`tr`/`br`/`bl` **in the face-local grid order defined below** - not "as it appears on screen". This ordering fixes the roll for gate scoring. |
| `faces[].centres` | Exactly **9 sticker-centre points, row-major** in the same face-local grid: index `i = row*3 + col`, so `centres[0]` is the sticker at (row 0, col 0), `centres[4]` the centre sticker, `centres[8]` (row 2, col 2). |

### Corner `model_id` convention

Singmaster order (the corner is named by the three faces it touches):

| `model_id` | Corner | `model_id` | Corner |
| --- | --- | --- | --- |
| 0 | URF | 4 | DFR |
| 1 | UFL | 5 | DLF |
| 2 | ULB | 6 | DBL |
| 3 | UBR | 7 | DRB |

### Face-local grid and quad corner order

Each face's (row, col) grid is the one committed in
`src/lib/include/cube/solver/cube.hpp` (shared with `CubeView.qml`).
With body axes x: L->R, y: D->U, z: B->F, each in {-1, 0, +1}:

- **U and D**: row = z+1, col = x+1
- **R and L**: row = 1-y, col = z+1
- **F and B**: row = 1-y, col = x+1

`tl` is the face corner at (row 0, col 0), `tr` at (row 0, col 2),
`br` at (row 2, col 2), `bl` at (row 2, col 0). In cube-corner names:

| Face | `tl` | `tr` | `br` | `bl` |
| --- | --- | --- | --- | --- |
| U | ULB | UBR | URF | UFL |
| R | UBR | URF | DFR | DRB |
| F | UFL | URF | DFR | DLF |
| D | DBL | DRB | DFR | DLF |
| L | ULB | UFL | DLF | DBL |
| B | ULB | UBR | DRB | DBL |

Label by the corner **names** (the tool prompts with them); for L and B
this order is intentionally mirrored relative to a naive
"as-seen-from-outside" reading - it matches the repo's facelet storage,
so sticker `i` of labelled face F corresponds exactly to character
`offset(F) + i` of the manifest state string (offsets below).

### Interpolation

The labelling tool may linearly interpolate corner/quad/centre
positions between two hand-labelled keyframes as a labelling aid. Such
frames are written with `interpolated: true` and **do not count as
labelled**: gates score only `interpolated: false` frames (pipeline
ground rule - otherwise tight gates measure interpolation error, not
detector error). After the labeller adjusts the points on an
interpolated frame and explicitly accepts it, the tool rewrites it with
`interpolated: false` and it becomes gate-eligible.

## `<recording-dir>/manifest.json`

Written by recorder v2 at record time (not by the labelling tool; the
tool only reads it, to display the true state).

```json
{
  "state": "UUUUUUUUURRRRRRRRRFFFFFFFFFDDDDDDDDDLLLLLLLLLBBBBBBBBB",
  "lighting": "neutral daylight, window left",
  "motion_script": "six-face guided sweep",
  "camera": "logitech-c920-01",
  "intrinsics": "calib/c920-01.yaml",
  "cube": "gan356m-stickerless"
}
```

| Field | Meaning |
| --- | --- |
| `state` | The cube's true state as a **54-character face-major string**: faces in **U R F D L B** order (offsets 0, 9, 18, 27, 36, 45), each face's 9 stickers row-major per the face-local grid above. Each character is one of `U R F D L B`, naming the face whose **centre colour** that sticker shows (so positions 4, 13, 22, 31, 40, 49 are always `U`,`R`,`F`,`D`,`L`,`B` respectively). **Validated by the recorder at record time** through `Cube::from_facelets` + solvability, so a transcription slip cannot silently poison the headline gates. |
| `lighting` | Free-text lighting descriptor (e.g. `"neutral daylight"`, `"warm tungsten front lamp"`). |
| `motion_script` | Free-text description of the scripted motion (e.g. `"six-face guided sweep"`, `"free rotation in hand"`, `"10 exit/re-entry events"`). |
| `camera` | Camera identifier. |
| `intrinsics` | Path (relative to the recording dir) of the camera intrinsics file, or `null` if the camera is uncalibrated. |
| `cube` | Physical cube identifier (which cube was recorded). |

Consumers must ignore unknown extra keys (the recorder may add fields;
per-face reference photos live as image files alongside the manifest,
not inside it).

## Bundle format (tool export convenience)

The labelling tool runs from the browser and cannot write into the
recording directory; besides per-file downloads it offers a single
"download bundle" JSON that packs one clip's entire ground truth:

```json
{
  "version": 1,
  "clip": "clip-2026-07-30-freerot-a",
  "segments": { ... exact contents of segments.json ... },
  "keyframes": {
    "123": { ... exact contents of keyframes/000123.json ... },
    "188": { ... exact contents of keyframes/000188.json ... }
  }
}
```

Mapping to files: `segments` becomes `groundtruth/segments.json`
verbatim; each `keyframes[<pass>]` value (keys are unpadded decimal
pass numbers) becomes `groundtruth/keyframes/<pass zero-padded to 6
digits>.json` verbatim. The split is mechanical; the sidecar files on
disk, not the bundle, are the format of record. The tool re-imports
either form (it auto-loads `groundtruth/` when you pick the recording
directory).

## Complete tiny example

`<recording-dir>/manifest.json` (solved cube):

```json
{
  "state": "UUUUUUUUURRRRRRRRRFFFFFFFFFDDDDDDDDDLLLLLLLLLBBBBBBBBB",
  "lighting": "neutral daylight",
  "motion_script": "static test clip",
  "camera": "test-cam",
  "intrinsics": null,
  "cube": "test-cube"
}
```

`groundtruth/segments.json`:

```json
{
  "version": 1,
  "clip": "example-clip",
  "segments": [
    { "start": 0, "end": 11, "tags": ["in_frame", "steady"] }
  ],
  "reentry_events": []
}
```

`groundtruth/keyframes/000005.json` - one visible face (F), four
visible corners:

```json
{
  "version": 1,
  "pass": 5,
  "interpolated": false,
  "corners": [
    { "model_id": 1, "x": 200.0, "y": 100.0 },
    { "model_id": 0, "x": 380.0, "y": 100.0 },
    { "model_id": 4, "x": 380.0, "y": 280.0 },
    { "model_id": 5, "x": 200.0, "y": 280.0 }
  ],
  "faces": [
    {
      "face": "F",
      "quad": {
        "tl": { "x": 200.0, "y": 100.0 },
        "tr": { "x": 380.0, "y": 100.0 },
        "br": { "x": 380.0, "y": 280.0 },
        "bl": { "x": 200.0, "y": 280.0 }
      },
      "centres": [
        { "x": 230.0, "y": 130.0 }, { "x": 290.0, "y": 130.0 }, { "x": 350.0, "y": 130.0 },
        { "x": 230.0, "y": 190.0 }, { "x": 290.0, "y": 190.0 }, { "x": 350.0, "y": 190.0 },
        { "x": 230.0, "y": 250.0 }, { "x": 290.0, "y": 250.0 }, { "x": 350.0, "y": 250.0 }
      ]
    }
  ]
}
```
