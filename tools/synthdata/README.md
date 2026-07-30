# Synthetic cube training data (`tools/synthdata/`)

Pipeline-plan **Phase 0 item 9** (started in parallel, per the ground
rules) feeding **Phase 1 track 2**: renders randomised Rubik's-cube images
with exact 2D corner-keypoint labels for training a small YOLO-pose-class
detector that emits the up-to-7 visible cube corners + visibility,
content-agnostically.

Everything here is self-contained Python; nothing links into the C++
build.

## Setup

The system Python needs numpy + opencv-python; if they are missing, use
the bundled venv (created once):

```powershell
cd tools/synthdata
python -m venv .venv
.venv/Scripts/pip install numpy opencv-python
```

`.venv/` and `sample_out/` are gitignored here.

## Usage

```powershell
# 32-image smoke set + eyeball sheet
.venv/Scripts/python generate.py --backend numpy --count 32 --out sample_out --seed 0
.venv/Scripts/python preview.py --dataset sample_out --limit 32

# a real training set
.venv/Scripts/python generate.py --backend numpy --count 20000 --out D:/cubeulator-synth/train --seed 1 --backgrounds D:/path/to/random/photos
.venv/Scripts/python generate.py --backend numpy --count 2000  --out D:/cubeulator-synth/val   --seed 2 --backgrounds D:/path/to/other/photos
```

Knobs: `--width/--height` (default 640x640), `--occluder-prob` (default
0.5; 0 disables the finger-like occluders), `--jpeg-prob`, `--blur-prob`,
`--backgrounds DIR` (random real photos as backgrounds -- strongly
recommended for a real training run; without it backgrounds are solid
colours and gradients only).

Measured on this workstation: ~120 ms/image at 640x640 (single process).
The generator is embarrassingly parallel across seeds/output dirs if more
throughput is needed.

`preview.py` tiles N samples with keypoints drawn into
`<dataset>/contact_sheet.png`: filled circle = visible (v2), ring =
occluded-but-annotated (v1), small x = not-visible (v0, drawn from the
jsonl metadata at the true projected position), plus corner ids, the YOLO
bbox and a thin wireframe. Label correctness is one glance away.

## Corner-id convention (the load-bearing contract)

Model frame: x right, y up, z toward the F face; cube spans [-1, +1]^3.
Corner ids are **binary order, x fastest**: bit0 = x sign, bit1 = y sign,
bit2 = z sign, so corner 0 = (-1,-1,-1) ... corner 7 = (+1,+1,+1).

```
        y                       6----------7            z = -1  (rear)      z = +1  (front)
        |                      /|         /|            2----3              6----7
        |                     / |        / |            |    |              |    |
        o----- x             4----------5  |            |    |              |    |
       /                     |  2-------|--3            0----1              4----5
      /                      | /        | /
     z  (toward F/viewer)    |/         |/          (each square seen with +x right, +y up)
                             0----------1
```

Horizontal image flips map corner i -> i XOR 1, i.e. Ultralytics
`flip_idx: [1, 0, 3, 2, 5, 4, 7, 6]`.

Faces in the model frame: U = +y, R = +x, F = +z, D = -y, L = -x, B = -z.
State strings are 54 chars, URFDLB face-major, row-major per face as seen
from outside (Kociemba-style layout), colour letters W/Y/R/O/G/B; solved
scheme U=W R=R F=G D=Y L=O B=B. This matches the manifest convention in
`docs/pipeline-plan.md` Phase 0 item 2 (that one stores facelet letters,
this one colour letters -- centres key the mapping).

Note: the *existing* C++ pose code (`src/lib/src/vision/pose_estimation.cpp`
`kCubeModel`) uses a different, face-anchored 8-corner model (near face at
z=0, tl/tr/br/bl order). When Phase 1 wires detector output into PnP, map
detector corner ids to whatever model that code is given -- the mapping is
a fixed permutation plus scale/offset; do it in one place and test it.

## Label formats (backend-independent, `labels.py`)

Per image `labels/<stem>.txt`, YOLO-pose, single class 0 = cube,
`kpt_shape: [8, 3]`:

```
0 cx cy w h  x0 y0 v0  x1 y1 v1  ...  x7 y7 v7      (all normalised 0..1)
```

Visibility: **2** = visible, **1** = occluded-but-annotated (position
known, covered by an occluder), **0** = not visible (self-occluded rear
corner, or out of frame; x y written as `0 0`). A generic pose has 7
visible corners, a square-on pose 4, a two-face pose 6. The bbox is the
projected silhouette hull of all 8 corners clipped to the image (amodal
w.r.t. occluders).

`labels.jsonl` -- one record per image with full metadata: image path,
size, 54-char state, `rvec`/`tvec` (model->camera, OpenCV convention), `K`,
per-corner float pixel coords (unclipped) + visibility, corner-order tag,
bbox, and the augmentation parameters actually applied (`meta`).

Ultralytics `data.yaml` snippet for training:

```yaml
kpt_shape: [8, 3]
flip_idx: [1, 0, 3, 2, 5, 4, 7, 6]
names: {0: cube}
```

## Dataset-size guidance

For a nano-scale pose model: start with **20k train / 2k val** synthetic
images (~40 min single-process; shard across processes with different
seeds to use the cores). Scale to 50-100k if val keypoint error hasn't
plateaued. Real corpus-v2 labelled keyframes are **validation/test only**
-- the Phase 1 gate is scored on real frames, so never train on them.
Use different `--backgrounds` pools (and seeds) for train vs val to keep
the val set honest.

## Domain-randomisation axes

Implemented (numpy backend):

- **Pose/camera:** uniform SO(3) rotation, projected size 28-72% of the
  frame, off-centre placement (partial out-of-frame allowed), focal length
  0.8-1.4x image width, principal-point jitter.
- **State:** 15% solved (solid faces), 45% permutation scramble (9 of each
  colour), 40% fully random stickers -- content-agnosticism is the point.
- **Appearance:** per-image sticker-palette jitter, sticker size/chamfer
  jitter, body-plastic shade jitter.
- **Lighting:** ambient + one random directional light (per-face Lambert
  shading), per-sticker brightness jitter.
- **Photometric:** white-balance gains, exposure, gamma, Gaussian noise.
- **Background:** solid / 2-colour gradient / random crops of user-supplied
  photos (`--backgrounds`).
- **Occlusion:** 1-3 finger-like capsules / ellipse blobs in skin-ish
  tones crossing the cube, with per-corner visibility downgraded to v1 by
  local coverage (>=50% of a disk ~3% of cube extent).
- **Sensor:** directional motion blur, JPEG artifacts at random quality.

TODO (acceptable gaps for a first training round, revisit if real-frame
validation shows a sim-to-real gap):

- Real hand compositing (photo hands, not capsules); the Blender backend's
  hand meshes.
- Specular highlights / glare on stickers (the known warm-light stressor),
  soft shadows, cube-on-table contact shadows.
- Sticker wear, off-brand colour schemes, stickerless-cube bevel geometry
  (sticker gaps rendered as body-colour lines approximate both).
- Rolling-shutter / chromatic aberration; per-face gradient shading within
  a face (currently flat per face + per-sticker jitter).
- Blender backend (photoreal path): `scripts/blender_scene.py` is a
  documented stub -- Blender was not installed on this machine
  (2026-07-30). It must emit labels through `labels.py` so the format
  cannot drift.

## Files

- `generate.py` -- CLI; `--backend numpy` (works today) or `--backend
  blender` (stub dispatch via `blender --background --python`).
- `render_numpy.py` -- the projection renderer + augmentation.
- `cube_model.py` -- corner order, faces, sticker geometry, state
  sampling (shared contract for all backends).
- `labels.py` -- YOLO-pose + jsonl writers (shared by all backends).
- `preview.py` -- contact-sheet eyeball tool.
- `scripts/blender_scene.py` -- Blender scene stub with implementation
  sketch.
