# Ground-truth labelling tool

`label_tool.html` is a self-contained, dependency-free page (vanilla JS,
works from `file://`) for producing the per-clip ground-truth sidecars
specified in [docs/ground-truth-format.md](../../docs/ground-truth-format.md).
Open it in Chrome or Edge (it uses the directory-picker File API and
`createImageBitmap`).

## Workflow

1. **Open** `label_tool.html` in the browser and click **Open recording
   dir**; select the recording directory (the folder holding `index.csv`
   and `rec_*.jpg`). The tool loads the frames, `manifest.json` (the true
   state is shown in the side panel), and any existing `groundtruth/`
   sidecars in the directory. Rows whose frame file has been overwritten
   by the recorder's 3600-frame ring are dropped automatically.
2. **Segment mode** (default; `m` toggles modes). Walk the clip with the
   arrow keys (set the stride box for coarse passes), press `[` and `]`
   to pin the pending range's start/end at the current frame, toggle tags
   with `1`-`5` (`in_frame`, `absent`, `occluded`, `steady`, `turning`),
   and press `Enter` to commit. Ranges are drawn as coloured lanes on the
   scrubber. Press `e` on the frame where the cube re-enters after a full
   occlusion/exit to record a re-entry event. Aim for every frame covered
   by at least one segment (the status bar shows coverage %).
3. **Keyframe mode**. On a chosen frame:
   - **Corners**: press `0`-`7` (or click the cube diagram) to arm a
     corner id (Singmaster order: 0 URF, 1 UFL, 2 ULB, 3 UBR, 4 DFR,
     5 DLF, 6 DBL, 7 DRB), then click the corner in the image. Label
     every visible outer corner.
   - **Faces**: press `u`/`r`/`f`/`d`/`l`/`b` for each visible face.
     The guided sequence asks for the 4 quad corners **by cube-corner
     name** (this fixes the roll - follow the names, not what looks
     top-left on screen), then the 9 sticker centres in face-grid
     row-major order. After the quad, ghost positions are projected for
     the centres: click to place each precisely, or `g` to accept the
     ghost, `G` to accept all remaining. Face identity is keyed by
     centre colour against the manifest state (use the per-face
     reference photos from the recording session).
   - **Edit**: click near any point to select it, drag to move,
     `Alt`+arrows to nudge by one screen pixel, `Delete` to remove a
     corner (or, via confirmation, a whole face). Zoom in hard (wheel)
     before fine placement - coordinates are stored sub-pixel.
4. **Interpolation aid**: on a frame between two hand-labelled
   keyframes, press `i` to generate an interpolated keyframe (drawn
   orange). It is exported with `"interpolated": true` and **does not
   count for gate scoring** until you adjust its points and press `a`
   to accept it as hand-labelled (it turns green).
5. **Export**: the side panel offers `segments.json`, the current
   keyframe file, and a whole-clip bundle JSON. Downloads land in the
   browser's download folder; move them into
   `<recording>/groundtruth/` (keyframe files are named
   `keyframes/<pass, zero-padded to 6>.json`; the bundle-to-files
   mapping is in the format doc). **Import JSON** re-loads a bundle,
   a `segments.json`, or individual keyframe files.

Press `?` at any time for the full key reference. Work in progress is
autosaved to browser `localStorage` per clip name and offered for
restore on the next load - but the exported files are the format of
record, so download the bundle when you stop.

## Notes

- All coordinates are analysis-scale pixels (the `rec_*.jpg` pixel grid),
  origin top-left - exactly what the gates score against.
- The scrubber's green/orange triangles are hand/interpolated keyframes;
  red ticks are re-entry events; the white outline is the pending range.
- The status bar shows pass number, keyframe counts (hand vs
  interpolated), segment count and coverage, re-entry count, and zoom.
