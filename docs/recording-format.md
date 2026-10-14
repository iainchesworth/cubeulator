# Recording format

What the developer-only scan recorder writes, and what `cube_scan` reads. The
code is `src/lib/include/cube/vision/recording_v2.hpp` (recorder, manifest,
integrity check) and `frame_recording.hpp` (file names and the index rows).
`cube_scan` itself uses only the frame files and `truth.txt`; the index, the
manifest and the integrity check are for `check_recording` and for people.

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
  the index (`frame_overwritten`) and ignored. `cube_scan` does not read the
  index: it replays the `rec_*.jpg` files in file-name order, which is
  chronological only for a scan shorter than 3600 frames (about two minutes
  at 30 Hz).

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
| `state` | The cube's true state as a 54-character face-major string in **U R F D L B** order, each face's nine stickers row-major, each character naming the face whose centre colour that sticker shows (so positions 4, 13, 22, 31, 40, 49 are always `U R F D L B`). Validated through `Cube::from_facelets` and solvability at record time; a string that cannot build a solvable cube refuses to record. `null` when not known. |
| `lighting` | Free text, e.g. `"warm tungsten front lamp"`. |
| `motion_script` | Free text. The app writes `"direct scan"`. |
| `camera` | Camera identifier. |
| `intrinsics` | Path of a camera intrinsics file relative to the recording directory, or `null`. Nothing reads intrinsics today. |
| `cube` | Which physical cube was recorded. |

Consumers ignore unknown keys.

## Integrity

`check_recording(dir)` returns the first problem or nothing: the index parses,
passes are contiguous, `t_ms` does not decrease, every row that has not been
overwritten has its frame (and `fullres/` file when that stream exists), the
manifest's `state`, if it has one, still validates, and no recorder-side drops
were flagged. The recorder's queue holds 256 frames (about
8.5 s at 30 Hz); a frame pushed to a full queue is dropped and counted, the
count is written to `recording_stats.json` when the recorder stops, and
`check_recording` fails a clip that has any.

## `truth.txt`

Optional, for scoring with `cube_scan board`. Six words of nine letters from
`WYROGB` (white, yellow, red, orange, green, blue), one per side, each side
row-major as seen from outside the cube, in any orientation and any order.
Nothing in the app writes it; it is typed from the cube in hand.
