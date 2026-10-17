# Recording format

What the developer-only scan recorder writes, and what `cube_scan` reads. The
code is `src/lib/include/cube/vision/recording_v2.hpp` (recorder, manifest,
integrity check) and `frame_recording.hpp` (file names and the index rows).
`cube_scan` itself uses the frame files, `truth.txt` or `layout.txt`, and the
times in the index (the capture time of each frame, which says how far a side
moved between frames in time and not only in frames); the manifest and the
integrity check are for `check_recording` and for people.

## When anything is written

Never by default. The app stores nothing unless a developer sets
`CUBE_SCAN_RECORD_DIR` to a folder before starting it; the scan screen then
shows a recording marker so a recording is never silent. Each scan gets its own
sub-folder, named for when it began (`yyyyMMdd-HHmmss-zzz`), and the solve
screen's camera never records. Only the frames the scan is built from are
written: none while the camera is still settling (see
[Camera photometry](vision-pipeline.md#camera-photometry)) and none once the
cube has been read.

Recordings show whatever the camera sees, which can be the person holding the
cube. They stay out of the repository (`/tests/data/captures/` and
`/docs/evidence/` are ignored for that reason) and out of anything published.

The recorder is also what the [live diagnostics channel](diagnostics.md) uses
when it is asked to keep frames - a separate switch from the channel itself,
off by default. It then writes this format to `frames/` beside the diagnostics
file, one folder per scan, and the same recording marker is shown. The channel
on its own writes no frames: its records are numbers and names, in JSON Lines,
described in [diagnostics.md](diagnostics.md).

## Directory layout

One scan's sub-folder of `CUBE_SCAN_RECORD_DIR`:

```
<recording-dir>/
  index.csv          # one row per recorded frame
  rec_000000.jpg ... # the frames, at the reader's analysis scale
  manifest.json      # per-clip metadata
  recording_stats.json  # written when the recorder stops: pushed/written/dropped/queue_peak
                        # (and failed/error, only if a write failed)
  fullres/           # only if full-resolution frames were asked for
    rec_000000.jpg
  truth.txt          # optional, added by hand for scoring (see below)
```

The scan recorder does not write `fullres/`: the full-resolution copy shows
more of the room and nothing the reader uses.

### Frames

- JPEG quality 90, at most 640 pixels wide (`kAnalysisWidth`): exactly the
  stream the reader runs on, so a replay reproduces the live read.
- The file for pass `n` is `rec_%06d.jpg` of `n % 3600`: the files wrap at a
  3600-frame ring so a recorder left on bounds its disk use, while `index.csv`
  keeps growing. A row whose file has since been overwritten is detected from
  the index (`frame_overwritten`) and ignored. `cube_scan` replays the
  `rec_*.jpg` files in file-name order, which is chronological only for a scan
  shorter than 3600 frames (about two minutes at 30 Hz), and takes the time
  since the frame before from the index's `t_ms` for the file of that name
  (none for a session with no index, or a frame whose row has gone: the
  nominal interval, as before).

### `index.csv`

Header `pass,t_ms,w,h,full_w,full_h`.

| Column | Meaning |
| --- | --- |
| `pass` | Recording pass number, contiguous from the first row. Not a row index: a recording need not start at 0. |
| `t_ms` | Capture timestamp in milliseconds from a steady clock; only differences are meaningful. |
| `w`, `h` | Size of the recorded frame. |
| `full_w`, `full_h` | Size of the original camera frame. |

### `manifest.json`

Written by the recorder when it starts.

```json
{
  "state": null,
  "lighting": "",
  "motion_script": "direct scan",
  "camera": "",
  "intrinsics": null,
  "cube": ""
}
```

| Field | Meaning |
| --- | --- |
| `state` | The cube's true state as a `6 * size^2`-character face-major string (54 for a 3x3) in **U R F D L B** order, each face's `size^2` stickers row-major, each character naming the face whose centre colour that sticker shows. On a 3x3 positions 4, 13, 22, 31, 40, 49 are always `U R F D L B`; on any other odd size the middle sticker of each face is, likewise, that face's letter (an even size has no middle sticker and no such rule). Validated at record time, by `Cube::from_facelets` and solvability for a 3x3 and by `cube::nxn::validate` ([the oracle](nxn-solver.md#is-this-a-cube)) for any other size; a string that cannot build a cube a person could hold refuses to record. `null` when not known. |
| `size` | The cube's side length, 2 to 9. **Written only when it is not 3**, so a 3x3's manifest is the file it always was; a manifest without it is a 3x3. A `size` outside 2 to 9, or a `state` of another length than `6 * size^2`, refuses to record. |
| `lighting` | Free text, e.g. `"warm tungsten front lamp"`. |
| `motion_script` | Free text. The app writes `"direct scan"`. |
| `camera` | Camera identifier. |
| `intrinsics` | Path of a camera intrinsics file relative to the recording directory, or `null`. Nothing reads intrinsics today. |
| `cube` | Which physical cube was recorded. |

Consumers ignore unknown keys.

## Integrity

`check_recording(dir)` returns the first problem or nothing: the recorder did
not fail to write a frame, the index parses, passes are contiguous, `t_ms` does
not decrease, every row that has not been overwritten has its frame (and
`fullres/` file when that stream exists), the manifest's `state`, if it has one,
still validates for the manifest's `size`, and no recorder-side drops were flagged. The recorder's queue
holds 256 frames (about 8.5 s at 30 Hz); a frame pushed to a full queue is
dropped and counted, the count is written to `recording_stats.json` when the
recorder stops, and `check_recording` fails a clip that has any.

### A frame that cannot be written

The disk can fill, the folder can vanish, and a build of OpenCV may have no JPEG
encoder (the browser's has none: the web app never records). The writer then
does not retry and does not skip ahead:

- The frame is not indexed. A row is appended only after the frame's files are
  written, so `index.csv` never claims a frame that is not on disk.
- The writer stops at the first failure. That frame, every frame still queued,
  and every frame pushed afterwards are counted as `failed` (so
  `pushed == written + dropped + failed`), and the first error message is kept.
- `recording_stats.json` gains `"failed": <n>` and `"error": "<message>"`. They
  are present only when something failed, so a recording that wrote every frame
  has the same file as before.
- `check_recording` reports it before anything else, with the message, because
  it is the cause of an index with no rows or too few: `recorder failed to
  write 4 frame(s): cannot write <path>`.

What was written before the failure stays on disk and its rows stay in the
index; the clip as a whole fails the audit.

## `truth.txt`

Optional, for scoring with `cube_scan board`. Six words of nine letters from
`WYROGB` (white, yellow, red, orange, green, blue), one per side, each side
row-major as seen from outside the cube, in any orientation and any order.
Nothing in the app writes it; it is typed from the cube in hand.

For a cube that is not a 3x3 (the manifest says `size`), the words are `size^2`
letters long, and `cube_scan replay` and `board` read the session as that size
and score the cube against them (odd sizes: an even size has no middle sticker to
put its sides together by). The other `cube_scan` commands are a 3x3's.
`cube_scan_s0 --session <dir> <size> <seed>` writes a drawn cube as such a session. The app
records one when it is started with `CUBE_SCAN_SIZE=5` (an odd size, 3 to 9) as well as
`CUBE_SCAN_RECORD_DIR`: the scan from the launch screen is then of that size, and the manifest
says so. The entry screen still offers the camera for a 3x3 only.

## `layout.txt`

Optional, instead of `truth.txt`, for a cube whose six sides make more than one
cube: six flat sides name no single cube then, which is the whole of what makes
the scan ambiguous. One line, the cube as `cube_scan replay` prints one -
`U:YYWYYYWWY D:YWWWWWWYY L:RRRRRRRRR R:OOOOOOOOO F:BBGGGGGGB B:BBGBBBGGB` -
each side as the solver lays it out (not as seen from outside). `cube_scan`
scores a decision against it. `cube_tumble_sim` writes it with the frames it
draws, and its `pose.csv` (`frame,seconds,U,D,L,R,F,B`: each side's angle in
degrees to the camera axis, 0 square-on, per frame), which nothing else reads.
