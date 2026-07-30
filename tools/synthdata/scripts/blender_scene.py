"""Blender scene script for the synthetic cube generator -- STUB.

TODO(blender): Blender was not installed on this machine when the synthdata
pipeline was built (2026-07-30; `blender --version` failed), so this script
is a documented skeleton, not a working renderer.  The numpy backend in
../render_numpy.py is the functional path today.

Invocation (from generate.py --backend blender):

    blender --background --python scripts/blender_scene.py -- \
        --count N --out DIR --seed S --width W --height H

Contract that MUST hold when this is implemented (backend-independence of
labels is the point):

  * Use the same model frame and canonical corner order as cube_model.py
    (corner 0 = (-1,-1,-1) ... corner 7 = (+1,+1,+1), binary, x fastest) --
    build the cube so its corners land on those coordinates.
  * Compute per-corner 2D projections from Blender's camera
    (bpy_extras.object_utils.world_to_camera_view), visibility flags with
    the same semantics (2 visible / 1 occluder-covered / 0 self-occluded or
    out of frame), and the URFDLB row-major state string.
  * Emit labels through labels.DatasetWriter / labels.Sample (this dir is
    importable from Blender's Python; add it to sys.path as below) so the
    YOLO .txt and labels.jsonl formats cannot drift from the numpy backend.

Implementation sketch:
  1. argparse on sys.argv after the "--" separator.
  2. Build a 2x2x2-unit cube at the origin; parent 54 slightly-inset sticker
     planes (per cube_model.sticker_quads geometry) with per-sticker
     emission/principled materials coloured from cube_model.sample_state.
  3. Randomise: camera pose on a view sphere (uniform rotation), focal
     length, an area/sun light (energy, angle, colour temperature), world
     background (colour, gradient, or HDRI dir), optional hand/finger
     occluder meshes intersecting the cube.
  4. Render with Cycles (denoised, low samples) or EEVEE for speed.
  5. Project corners, compute visibility (ray_cast toward the camera for
     self-occlusion; occluder pass index for finger coverage), write via
     labels.DatasetWriter, one Sample per rendered frame.
"""

import sys
from pathlib import Path

# Make cube_model.py / labels.py importable inside Blender's Python.
sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

raise SystemExit(
    "scripts/blender_scene.py is a stub -- Blender rendering is TODO. "
    "Use `generate.py --backend numpy` instead (fully functional)."
)
