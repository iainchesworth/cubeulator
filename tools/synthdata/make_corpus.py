"""Synthetic corpus generator (pipeline-plan Phase 0): recording-format
clips with EXACT auto-generated ground truth, so Phases 1-2 have a fully
labelled development corpus before real footage exists.

One command emits five clip classes, each an on-disk recording per
src/lib/include/cube/vision/frame_recording.hpp (rec_%06d.jpg JPEG q90 at
640x480 + index.csv) with manifest.json and a groundtruth/ sidecar per
docs/ground-truth-format.md:

    steady        240 frames, near-static, sub-pixel jitter
    freerot       400 frames, smooth SO(3) slerp trajectory
    occlusion     300 frames, sweeping finger occluders + 3 full gaps
    absent        120 frames, background only
    exit-reentry  300 frames, 4 full frame exits and returns

Labels are analytic (projected from the known pose), hence written with
"interpolated": false - they are exact, better than hand-clicked.

Layout / convention notes (the load-bearing part):
  - manifest state strings use the cube.hpp facelet grid (the normative
    table in docs/ground-truth-format.md), which differs from
    cube_model._F's Kociemba-style layout on R, D and B. The conversion is
    done geometrically per sticker 3-D position, never via hand permuation
    tables, and is verified by the pixel-colour selftest below.
  - corner model_id is Singmaster (0 URF .. 7 DRB); render_numpy's binary
    corner order maps through BINARY_TO_SINGMASTER = [6,7,2,3,5,4,1,0],
    verified both algebraically and on a rendered pose.
  - quarter turns are implemented geometrically (rotate sticker positions
    about the face normal); 4-turn identity, census preservation, and a
    known single-move expansion are asserted, and every emitted manifest
    is additionally validated by the real C++ solver oracle via
    `cube_pipeline_runner check`.

Usage:
    .venv/Scripts/python make_corpus.py --out D:/cubeulator-build/synth-corpus --seed 0
"""

from __future__ import annotations

import argparse
import json
import math
import sys
from collections import Counter
from pathlib import Path

import cv2
import numpy as np

SCRIPT_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(SCRIPT_DIR))

import cube_model as cm  # noqa: E402
from render_numpy import BASE_COLORS, SKIN_TONES, project  # noqa: E402

# --- recording format constants ---------------------------------------------

W, H = 640, 480
SS = 2  # supersampling for the cube draw
DT_MS = 33
JPEG_Q = 90
FACE_ORDER = "URFDLB"
FACE_COS_MIN = 0.15  # facing-cosine threshold for emitting a face label

# Fixed synthetic pinhole camera (written to intrinsics.json per clip).
FX = 700.0
K = np.array([[FX, 0.0, W / 2], [0.0, FX, H / 2], [0.0, 0.0, 1.0]])

# Fixed lighting (deterministic; light = direction the light travels,
# render_numpy convention: camera-facing faces have n_cam.z < 0).
LIGHT = np.array([0.25, 0.35, 0.9])
LIGHT = LIGHT / np.linalg.norm(LIGHT)
AMBIENT, DIFFUSE = 0.42, 0.62
STICKER_FRAC = 0.88
BODY_BGR = (28.0, 28.0, 31.0)

# =============================================================================
# Manifest-layout state model + quarter-turn move engine
# =============================================================================
#
# The grid committed in cube.hpp / docs/ground-truth-format.md, with body
# axes x: L->R, y: D->U, z: B->F:
#     U and D: row = z+1, col = x+1
#     R and L: row = 1-y, col = z+1
#     F and B: row = 1-y, col = x+1
# Expressed here as (outward normal, row direction, col direction); sticker
# (row r, col c) of a face sits at n + (r-1)*(2/3)*rd + (c-1)*(2/3)*cd.

SPEC_GRID = {
    "U": ((0, 1, 0), (0, 0, 1), (1, 0, 0)),
    "D": ((0, -1, 0), (0, 0, 1), (1, 0, 0)),
    "R": ((1, 0, 0), (0, -1, 0), (0, 0, 1)),
    "L": ((-1, 0, 0), (0, -1, 0), (0, 0, 1)),
    "F": ((0, 0, 1), (0, -1, 0), (1, 0, 0)),
    "B": ((0, 0, -1), (0, -1, 0), (1, 0, 0)),
}

FACE_OFFSET = {f: i * 9 for i, f in enumerate(FACE_ORDER)}


def _grid_vecs(face: str):
    return tuple(np.array(v, dtype=np.int64) for v in SPEC_GRID[face])


def spec_sticker_pos3(face: str, r: int, c: int) -> tuple[int, int, int]:
    """Sticker-centre position in integer coords scaled by 3 (face plane at
    +-3, tangential cell centres at -2/0/+2) - exact under 90-deg turns."""
    n, rd, cd = _grid_vecs(face)
    return tuple(int(v) for v in 3 * n + 2 * (r - 1) * rd + 2 * (c - 1) * cd)


#: position -> index into the 54-char manifest state string (and back).
POS_TO_INDEX: dict[tuple[int, int, int], int] = {}
for _f in FACE_ORDER:
    for _r in range(3):
        for _c in range(3):
            POS_TO_INDEX[spec_sticker_pos3(_f, _r, _c)] = FACE_OFFSET[_f] + _r * 3 + _c

SOLVED_FACELETS = "".join(f * 9 for f in FACE_ORDER)


def _quarter_cw_perm(face: str) -> dict[int, int]:
    """String-index permutation of one clockwise quarter turn (seen from
    outside the face): rotate every sticker with position component >= 2
    along the face normal by -90 deg about that normal,
    v' = n (v.n) - n x v."""
    n = np.array(SPEC_GRID[face][0], dtype=np.int64)
    perm = {}
    for pos, idx in POS_TO_INDEX.items():
        v = np.array(pos, dtype=np.int64)
        if int(v @ n) < 2:
            continue
        rotated = n * int(v @ n) - np.cross(n, v)
        perm[idx] = POS_TO_INDEX[tuple(int(x) for x in rotated)]
    return perm


TURN_PERM = {f: _quarter_cw_perm(f) for f in FACE_ORDER}


def turn_cw(state: str, face: str) -> str:
    s = list(state)
    for src, dst in TURN_PERM[face].items():
        s[dst] = state[src]
    return "".join(s)


def apply_move(state: str, move: str) -> str:
    face, suffix = move[0], move[1:]
    for _ in range({"": 1, "2": 2, "'": 3}[suffix]):
        state = turn_cw(state, face)
    return state


def apply_moves(state: str, moves: list[str]) -> str:
    for m in moves:
        state = apply_move(state, m)
    return state


def random_scramble(rng: np.random.Generator, count: int = 20) -> list[str]:
    moves: list[str] = []
    prev = None
    while len(moves) < count:
        face = FACE_ORDER[rng.integers(6)]
        if face == prev:
            continue
        moves.append(face + ["", "'", "2"][rng.integers(3)])
        prev = face
    return moves


# =============================================================================
# Corner ids, face quads, sticker centres (label geometry)
# =============================================================================

SINGMASTER_NAMES = ["URF", "UFL", "ULB", "UBR", "DFR", "DLF", "DBL", "DRB"]


def corner_name(p) -> str:
    letters = {
        "U" if p[1] > 0 else "D",
        "R" if p[0] > 0 else "L",
        "F" if p[2] > 0 else "B",
    }
    return next(nm for nm in SINGMASTER_NAMES if set(nm) == letters)


#: render_numpy binary corner order (bit0=x, bit1=y, bit2=z sign) ->
#: Singmaster model_id. Derived, then pinned by the selftest.
BINARY_TO_SINGMASTER = [SINGMASTER_NAMES.index(corner_name(cm.CORNERS[b])) for b in range(8)]

#: binary corner id -> the three faces meeting at it.
CORNER_FACES = [
    (
        "R" if cm.CORNERS[b][0] > 0 else "L",
        "U" if cm.CORNERS[b][1] > 0 else "D",
        "F" if cm.CORNERS[b][2] > 0 else "B",
    )
    for b in range(8)
]

QUAD_KEYS = {"tl": (0, 0), "tr": (0, 2), "br": (2, 2), "bl": (2, 0)}


def face_quad_corners3(face: str) -> dict[str, np.ndarray]:
    """The face's 4 cube corners keyed tl/tr/br/bl in the SPEC grid order
    (docs table: e.g. R's tl is UBR, not URF - L and B mirrored, etc.)."""
    n, rd, cd = (v.astype(np.float64) for v in _grid_vecs(face))
    return {k: n + (r - 1) * rd + (c - 1) * cd for k, (r, c) in QUAD_KEYS.items()}


def face_centres3(face: str) -> np.ndarray:
    """(9, 3) sticker centres, row-major in the SPEC grid."""
    n, rd, cd = (v.astype(np.float64) for v in _grid_vecs(face))
    return np.array(
        [n + (r - 1) * (2 / 3) * rd + (c - 1) * (2 / 3) * cd for r in range(3) for c in range(3)]
    )


def face_cosines(R: np.ndarray, tvec: np.ndarray) -> dict[str, float]:
    """Facing cosine toward the camera per face: cos between the outward
    normal and the direction from face centre to camera. > 0 iff the face
    is front-facing (same test the renderer uses)."""
    out = {}
    for f in FACE_ORDER:
        n_cam = R @ cm.FACE_NORMALS[f]
        c_cam = R @ cm.FACE_NORMALS[f] + tvec
        out[f] = float(-np.dot(n_cam, c_cam) / np.linalg.norm(c_cam))
    return out


def frame_labels(R: np.ndarray, rvec: np.ndarray, tvec: np.ndarray):
    """Exact analytic keyframe content (corners + faces) for one pose."""
    cos = face_cosines(R, tvec)
    corners_px = project(cm.CORNERS, rvec, tvec, K)

    corners = []
    for b in range(8):
        if not any(cos[f] > 0.0 for f in CORNER_FACES[b]):
            continue  # self-occluded by the cube body (the hidden corner(s))
        corners.append(
            {
                "model_id": BINARY_TO_SINGMASTER[b],
                "x": round(float(corners_px[b][0]), 2),
                "y": round(float(corners_px[b][1]), 2),
            }
        )
    corners.sort(key=lambda c: c["model_id"])

    faces = []
    for f in FACE_ORDER:
        if cos[f] <= FACE_COS_MIN:
            continue
        quad3 = face_quad_corners3(f)
        quad_px = {k: project(v.reshape(1, 3), rvec, tvec, K)[0] for k, v in quad3.items()}
        centres_px = project(face_centres3(f), rvec, tvec, K)
        faces.append(
            {
                "face": f,
                "quad": {
                    k: {"x": round(float(p[0]), 2), "y": round(float(p[1]), 2)}
                    for k, p in quad_px.items()
                },
                "centres": [
                    {"x": round(float(x), 2), "y": round(float(y), 2)} for x, y in centres_px
                ],
            }
        )
    return corners, faces


# =============================================================================
# Manifest-layout -> renderer-layout state conversion
# =============================================================================


def to_render_state(manifest_state: str) -> str:
    """54-char colour-letter string in cube_model's (Kociemba-style) layout
    from a facelet-letter manifest state in the spec (cube.hpp) layout.

    Purely geometric: each cube_model sticker slot is located by its 3-D
    centre position and looked up in the spec-layout position table - the
    two layouts differ on R, D and B, and this cannot get that wrong the
    way a hand permutation table could."""
    chars = []
    for face in cm.FACE_ORDER:
        spec = cm._F[face]  # cube_model's own layout definition
        start = np.array(spec["start"], dtype=np.int64) * 3
        u = np.array(spec["u"], dtype=np.int64)
        v = np.array(spec["v"], dtype=np.int64)
        for r in range(3):
            for c in range(3):
                pos = tuple(int(x) for x in start + u * (2 * c + 1) + v * (2 * r + 1))
                chars.append(cm.SOLVED_SCHEME[manifest_state[POS_TO_INDEX[pos]]])
    return "".join(chars)


# =============================================================================
# Rendering
# =============================================================================


def rx(a: float) -> np.ndarray:
    c, s = math.cos(a), math.sin(a)
    return np.array([[1, 0, 0], [0, c, -s], [0, s, c]], dtype=np.float64)


def ry(a: float) -> np.ndarray:
    c, s = math.cos(a), math.sin(a)
    return np.array([[c, 0, s], [0, 1, 0], [-s, 0, c]], dtype=np.float64)


#: Generic three-face view: F face-on, tipped to also show U and R.
R_BASE = rx(math.radians(25)) @ ry(math.radians(35)) @ np.diag([1.0, -1.0, -1.0])


def tvec_for(px: float, py: float, z: float) -> np.ndarray:
    """Translation putting the cube centre at image point (px, py), depth z."""
    return np.array([(px - W / 2) / FX * z, (py - H / 2) / FX * z, z])


def make_background(rng: np.random.Generator) -> np.ndarray:
    """Static per-clip background: muted linear gradient (float32 HxW)."""
    c0 = rng.uniform(35, 190, size=3)
    c1 = rng.uniform(35, 190, size=3)
    ang = rng.uniform(0, 2 * math.pi)
    xs, ys = np.meshgrid(np.linspace(-0.5, 0.5, W), np.linspace(-0.5, 0.5, H))
    tt = (xs * math.cos(ang) + ys * math.sin(ang) + 0.5).clip(0, 1)[..., None]
    return (c0 * (1 - tt) + c1 * tt).astype(np.float32)


def shade_for(R: np.ndarray, face: str) -> float:
    n_cam = R @ cm.FACE_NORMALS[face]
    return AMBIENT + DIFFUSE * max(0.0, float(np.dot(n_cam, -LIGHT)))


def draw_occluder(img: np.ndarray, mask: np.ndarray, shp: dict) -> None:
    tone = np.array(SKIN_TONES[shp["tone"]], dtype=np.float64)
    if shp["kind"] == "capsule":
        p0 = tuple(int(round(v)) for v in shp["p0"])
        p1 = tuple(int(round(v)) for v in shp["p1"])
        thick = int(shp["thick"])
        rim = thick + max(2, thick // 6)
        cv2.line(img, p0, p1, tuple(float(v) for v in tone * 0.82), rim, cv2.LINE_AA)
        cv2.line(img, p0, p1, tuple(float(v) for v in tone), thick, cv2.LINE_AA)
        cv2.line(mask, p0, p1, 255, rim, cv2.LINE_8)
    else:  # disc (palm)
        c = tuple(int(round(v)) for v in shp["c"])
        r = int(shp["r"])
        cv2.circle(img, c, r, tuple(float(v) for v in tone * 0.9), -1, cv2.LINE_AA)
        cv2.circle(img, c, r, tuple(float(v) for v in tone * 0.78), 4, cv2.LINE_AA)
        cv2.circle(mask, c, r, 255, -1, cv2.LINE_8)


def render_frame(render_state, R, rvec, tvec, bg_ss, occluders, rng, noise_sigma=2.0):
    """Returns (bgr uint8, hull_px, vis_px, cov_px):
    hull_px  cube-body pixels inside the image (ignoring occluders),
    vis_px   cube pixels actually visible (occluders subtracted),
    cov_px   cube pixels covered by an occluder."""
    canvas = bg_ss.copy()
    draw_cube = R is not None
    if draw_cube:
        Kss = K.copy()
        Kss[:2] *= SS
        hull_ss = cv2.convexHull(project(cm.CORNERS, rvec, tvec, Kss).astype(np.float32))
        cv2.fillPoly(canvas, [np.round(hull_ss).astype(np.int32)], BODY_BGR, lineType=cv2.LINE_AA)
        for f in cm.FACE_ORDER:
            n_cam = R @ cm.FACE_NORMALS[f]
            c_cam = R @ cm.FACE_NORMALS[f] + tvec
            if float(np.dot(n_cam, c_cam)) >= 0:
                continue  # back-facing
            shade = shade_for(R, f)
            colors = cm.state_face(render_state, f)
            for quad3, letter in zip(cm.sticker_quads(f, STICKER_FRAC), colors):
                col = np.array(BASE_COLORS[letter], dtype=np.float64) * shade
                pts = np.round(project(quad3, rvec, tvec, Kss)).astype(np.int32)
                cv2.fillPoly(canvas, [pts], tuple(float(v) for v in col.clip(0, 255)),
                             lineType=cv2.LINE_AA)
    img = cv2.resize(canvas, (W, H), interpolation=cv2.INTER_AREA)

    hull_mask = np.zeros((H, W), dtype=np.uint8)
    if draw_cube:
        hull = cv2.convexHull(project(cm.CORNERS, rvec, tvec, K).astype(np.float32))
        cv2.fillPoly(hull_mask, [np.round(hull).astype(np.int32)], 255)
    occ_mask = np.zeros((H, W), dtype=np.uint8)
    for shp in occluders:
        draw_occluder(img, occ_mask, shp)
    if noise_sigma > 0:
        img = img + rng.normal(0.0, noise_sigma, img.shape)
    out = img.clip(0, 255).astype(np.uint8)

    hull_px = int(np.count_nonzero(hull_mask))
    vis_px = int(np.count_nonzero(hull_mask & ~occ_mask))
    cov_px = int(np.count_nonzero(hull_mask & occ_mask))
    return out, hull_px, vis_px, cov_px


# =============================================================================
# Quaternion helpers (freerot trajectory)
# =============================================================================


def quat_random(rng: np.random.Generator) -> np.ndarray:
    u1, u2, u3 = rng.random(3)
    return np.array(
        [
            math.sqrt(1 - u1) * math.sin(2 * math.pi * u2),
            math.sqrt(1 - u1) * math.cos(2 * math.pi * u2),
            math.sqrt(u1) * math.sin(2 * math.pi * u3),
            math.sqrt(u1) * math.cos(2 * math.pi * u3),
        ]
    )  # x, y, z, w


def quat_angle_deg(q0: np.ndarray, q1: np.ndarray) -> float:
    return math.degrees(2 * math.acos(min(1.0, abs(float(np.dot(q0, q1))))))


def quat_slerp(q0: np.ndarray, q1: np.ndarray, t: float) -> np.ndarray:
    d = float(np.dot(q0, q1))
    if d < 0:
        q1, d = -q1, -d
    if d > 0.9995:
        q = q0 + t * (q1 - q0)
        return q / np.linalg.norm(q)
    th = math.acos(min(1.0, d))
    return (math.sin((1 - t) * th) * q0 + math.sin(t * th) * q1) / math.sin(th)


def R_from_quat(q: np.ndarray) -> np.ndarray:
    x, y, z, w = q / np.linalg.norm(q)
    return np.array(
        [
            [1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
            [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
            [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)],
        ]
    )


# =============================================================================
# Clip emission (recording + ground truth sidecar)
# =============================================================================


def merge_segments(tag_sets: list[frozenset]) -> list[dict]:
    """Runs of identical per-frame tag sets -> non-overlapping segments
    covering every pass (complete coverage by construction)."""
    order = ["in_frame", "absent", "occluded", "steady", "turning"]
    segments = []
    start = 0
    for i in range(1, len(tag_sets) + 1):
        if i == len(tag_sets) or tag_sets[i] != tag_sets[start]:
            segments.append(
                {
                    "start": start,
                    "end": i - 1,
                    "tags": [t for t in order if t in tag_sets[start]],
                }
            )
            start = i
    return segments


def emit_clip(out_root: Path, name: str, specs: list[dict], state: str, motion: str,
              rng: np.random.Generator) -> dict:
    """specs: per-frame {R (3x3 or None), tvec, occluders, steady}. Writes
    the full recording + sidecar; returns measured per-frame info."""
    clip_dir = out_root / name
    kf_dir = clip_dir / "groundtruth" / "keyframes"
    kf_dir.mkdir(parents=True, exist_ok=True)

    bg = make_background(rng)
    bg_ss = cv2.resize(bg, (W * SS, H * SS), interpolation=cv2.INTER_LINEAR)
    render_state = to_render_state(state)

    index_lines = ["pass,t_ms,w,h,full_w,full_h"]
    tag_sets: list[frozenset] = []
    vis_series: list[int] = []
    for p, spec in enumerate(specs):
        R = spec["R"]
        tvec = spec["tvec"]
        rvec = cv2.Rodrigues(R)[0].ravel() if R is not None else None
        img, hull_px, vis_px, cov_px = render_frame(
            render_state, R, rvec, tvec, bg_ss, spec.get("occluders", []), rng
        )
        ok = cv2.imwrite(str(clip_dir / f"rec_{p:06d}.jpg"), img,
                         [cv2.IMWRITE_JPEG_QUALITY, JPEG_Q])
        assert ok, f"failed to write frame {p} of {name}"
        index_lines.append(f"{p},{p * DT_MS},{W},{H},{W},{H}")

        # Tags: absent = no cube pixel visible (out of frame, no cube, or
        # fully covered by an occluder); occluded = visible but partially
        # covered. Measured from the rendered masks, not assumed.
        if vis_px == 0:
            tags = {"absent"}
        else:
            tags = {"in_frame"}
            if cov_px > 0:
                tags.add("occluded")
            if spec.get("steady", False):
                tags.add("steady")
        tag_sets.append(frozenset(tags))
        vis_series.append(vis_px)

        # Keyframe: exact labels whenever the cube body geometrically
        # intersects the image (occluder-covered corners/faces are still
        # emitted - they are positional truth; the `absent` tag carries
        # the visibility story). Empty labels when the cube is genuinely
        # out of frame or there is no cube.
        if R is not None and hull_px > 0:
            corners, faces = frame_labels(R, rvec, tvec)
        else:
            corners, faces = [], []
        keyframe = {
            "version": 1,
            "pass": p,
            "interpolated": False,
            "corners": corners,
            "faces": faces,
        }
        (kf_dir / f"{p:06d}.json").write_text(json.dumps(keyframe, indent=1) + "\n",
                                              encoding="utf-8")

    (clip_dir / "index.csv").write_text("\n".join(index_lines) + "\n", encoding="utf-8")

    # Re-entry events: cube becomes visible again after full absence.
    reentries = [
        i
        for i in range(1, len(vis_series))
        if vis_series[i] > 0 and vis_series[i - 1] == 0 and any(v > 0 for v in vis_series[:i])
    ]
    segments = {
        "version": 1,
        "clip": name,
        "segments": merge_segments(tag_sets),
        "reentry_events": reentries,
    }
    (clip_dir / "groundtruth" / "segments.json").write_text(
        json.dumps(segments, indent=2) + "\n", encoding="utf-8"
    )

    manifest = {
        "state": state,
        "lighting": "synthetic",
        "motion_script": motion,
        "camera": "synthetic",
        "intrinsics": "intrinsics.json",
        "cube": "synthetic",
    }
    (clip_dir / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n",
                                            encoding="utf-8")
    (clip_dir / "intrinsics.json").write_text(
        json.dumps(
            {
                "model": "pinhole",
                "fx": FX,
                "fy": FX,
                "cx": W / 2,
                "cy": H / 2,
                "width": W,
                "height": H,
                "distortion": [],
            },
            indent=2,
        )
        + "\n",
        encoding="utf-8",
    )
    return {"tags": tag_sets, "vis": vis_series, "reentries": reentries,
            "segments": segments["segments"]}


# =============================================================================
# Clip trajectory builders
# =============================================================================


def build_steady(frames: int = 240) -> list[dict]:
    z = 10.1
    specs = []
    for i in range(frames):
        dx = 0.28 * math.sin(i * 0.37) + 0.14 * math.sin(i * 0.113 + 0.5)
        dy = 0.28 * math.cos(i * 0.31) + 0.14 * math.sin(i * 0.171 + 1.2)
        specs.append(
            {"R": R_BASE, "tvec": tvec_for(320 + dx, 248 + dy, z), "occluders": [],
             "steady": True}
        )
    return specs


def build_freerot(rng: np.random.Generator, frames: int = 400, seg: int = 80) -> list[dict]:
    n_waypoints = frames // seg + 1
    waypoints = [quat_random(rng)]
    while len(waypoints) < n_waypoints:
        q = quat_random(rng)
        if 40.0 <= quat_angle_deg(waypoints[-1], q) <= 150.0:
            waypoints.append(q)
    specs = []
    for i in range(frames):
        k, t = divmod(i, seg)
        q = quat_slerp(waypoints[k], waypoints[k + 1], t / seg)
        px = 320 + 24 * math.sin(2 * math.pi * i / 173)
        py = 240 + 18 * math.cos(2 * math.pi * i / 211)
        z = 11.5 + 0.4 * math.sin(i * 0.02)
        specs.append({"R": R_from_quat(q), "tvec": tvec_for(px, py, z), "occluders": []})
    return specs


def build_occlusion(frames: int = 300) -> list[dict]:
    z = 10.6
    centre = np.array([320.0, 244.0])
    ext = 2 * FX * math.sqrt(3) / z  # projected cube diameter, px
    hull_r = FX * math.sqrt(3) / (z - math.sqrt(3))  # worst-case corner radius
    fingers = [
        {"ang": math.radians(65), "period": 97.0, "phase": 0.3, "thick": 0.17},
        {"ang": math.radians(-30), "period": 141.0, "phase": 2.0, "thick": 0.14},
    ]
    gap_starts = [55, 150, 245]  # 7 in + 20 hold + 7 out = 34 frames each
    slide, hold = 7, 20
    palm_off = np.array([1.6 * ext, 1.1 * ext])
    palm_r = int(hull_r * 1.35 + 8)

    specs = []
    for i in range(frames):
        a = math.radians(3) * math.sin(i * 0.045)
        b = math.radians(4) * math.sin(i * 0.031 + 1.1)
        R = rx(a) @ ry(b) @ R_BASE
        occ = []
        for k, fg in enumerate(fingers):
            d = np.array([math.cos(fg["ang"]), math.sin(fg["ang"])])
            perp = np.array([-d[1], d[0]])
            sweep = 0.85 * ext * math.sin(2 * math.pi * i / fg["period"] + fg["phase"])
            c = centre + perp * sweep
            half = 1.1 * ext
            occ.append(
                {
                    "kind": "capsule",
                    "p0": c - d * half,
                    "p1": c + d * half,
                    "thick": max(6, int(ext * fg["thick"])),
                    "tone": k + 1,
                }
            )
        for start in gap_starts:
            t = i - start
            if 0 <= t < slide:
                s = 0.5 - 0.5 * math.cos(math.pi * (t + 1) / slide)
            elif slide <= t < slide + hold:
                s = 1.0
            elif slide + hold <= t < 2 * slide + hold:
                s = 0.5 + 0.5 * math.cos(math.pi * (t - slide - hold + 1) / slide)
            else:
                continue
            c = centre + (1.0 - s) * palm_off * 2.2
            occ.append({"kind": "disc", "c": c, "r": palm_r, "tone": 2})
        specs.append({"R": R, "tvec": tvec_for(*centre, z), "occluders": occ})
    return specs


def build_absent(frames: int = 120) -> list[dict]:
    return [{"R": None, "tvec": None, "occluders": []} for _ in range(frames)]


def build_exit_reentry(frames: int = 300) -> list[dict]:
    z = 10.5
    c = (320.0, 240.0)
    outs = [(1080.0, 240.0), (-440.0, 240.0), (320.0, 1000.0), (320.0, -520.0)]
    waypoints = [c]
    for o in outs:
        waypoints.extend([o, c])  # out and back, four times
    legs = len(waypoints) - 1  # 8
    per_leg = [frames // legs + (1 if k < frames % legs else 0) for k in range(legs)]
    axis = np.array([0.3, 1.0, 0.25])
    axis /= np.linalg.norm(axis)

    specs = []
    frame = 0
    for k in range(legs):
        a = np.array(waypoints[k])
        b = np.array(waypoints[k + 1])
        n = per_leg[k]
        for j in range(n):
            t = 0.5 - 0.5 * math.cos(math.pi * j / max(1, n - 1))
            p = a + (b - a) * t
            R = cv2.Rodrigues(axis * math.radians(0.7 * frame))[0] @ R_BASE
            specs.append({"R": R, "tvec": tvec_for(p[0], p[1], z), "occluders": []})
            frame += 1
    return specs


# =============================================================================
# Selftests (always run; cheap, and they pin every convention)
# =============================================================================


def run_selftests() -> None:
    # --- move engine ---------------------------------------------------------
    base = apply_moves(SOLVED_FACELETS, ["R", "U'", "F2", "D", "L'", "B"])
    for face in FACE_ORDER:
        s = base
        for k in range(4):
            s = turn_cw(s, face)
            census = Counter(s)
            assert all(census[f] == 9 for f in FACE_ORDER), f"census broken by {face} turn {k}"
        assert s == base, f"4x {face} turn is not the identity"
    assert apply_move(apply_move(base, "U"), "U'") == base
    assert apply_move(base, "U2") == turn_cw(turn_cw(base, "U"), "U")
    # Known expansion: U on solved -> F<-R, R<-B, B<-L, L<-F (top rows).
    expected_u = ("U" * 9 + "BBB" + "R" * 6 + "RRR" + "F" * 6 + "D" * 9
                  + "FFF" + "L" * 6 + "LLL" + "B" * 6)
    assert turn_cw(SOLVED_FACELETS, "U") == expected_u, "U-turn expansion wrong"
    # Sexy move has order 6.
    s = base
    for k in range(6):
        s = apply_moves(s, ["R", "U", "R'", "U'"])
        assert (s == base) == (k == 5), f"(R U R' U') order-6 fingerprint broken at {k + 1}"

    # --- corner id mapping ---------------------------------------------------
    assert BINARY_TO_SINGMASTER == [6, 7, 2, 3, 5, 4, 1, 0], BINARY_TO_SINGMASTER
    assert corner_name((1, 1, 1)) == "URF" and BINARY_TO_SINGMASTER[7] == 0

    # --- spec quad-corner table (docs/ground-truth-format.md, incl. the
    # intentionally mirrored L and B) ----------------------------------------
    expected_quads = {
        "U": ("ULB", "UBR", "URF", "UFL"),
        "R": ("UBR", "URF", "DFR", "DRB"),
        "F": ("UFL", "URF", "DFR", "DLF"),
        "D": ("DBL", "DRB", "DFR", "DLF"),
        "L": ("ULB", "UFL", "DLF", "DBL"),
        "B": ("ULB", "UBR", "DRB", "DBL"),
    }
    for f, names in expected_quads.items():
        got = tuple(corner_name(face_quad_corners3(f)[k]) for k in ("tl", "tr", "br", "bl"))
        assert got == names, f"quad corner order for {f}: {got} != {names}"

    # --- rendered mapping test: URF corner label sits exactly on the shared
    # quad corner of U (br), R (tr) and F (tr) at a three-face pose ----------
    tvec = tvec_for(320, 248, 10.1)
    rvec = cv2.Rodrigues(R_BASE)[0].ravel()
    corners, faces = frame_labels(R_BASE, rvec, tvec)
    by_face = {f["face"]: f for f in faces}
    assert set(by_face) == {"U", "R", "F"}, f"steady pose shows {set(by_face)}"
    by_id = {c["model_id"]: (c["x"], c["y"]) for c in corners}
    # Three faces visible (U, R, F) -> 7 corners; only DBL (id 6), the one
    # touching the three hidden faces, is self-occluded.
    assert set(by_id) == {0, 1, 2, 3, 4, 5, 7}, f"visible corner ids {sorted(by_id)}"
    urf = by_id[0]
    for face, key in (("U", "br"), ("R", "tr"), ("F", "tr")):
        q = by_face[face]["quad"][key]
        assert abs(q["x"] - urf[0]) < 0.011 and abs(q["y"] - urf[1]) < 0.011, (
            f"URF corner vs {face}.{key} mismatch: {q} vs {urf}")
    ufl = by_id[1]
    for face, key in (("U", "bl"), ("F", "tl")):
        q = by_face[face]["quad"][key]
        assert abs(q["x"] - ufl[0]) < 0.011 and abs(q["y"] - ufl[1]) < 0.011

    # --- rendered colour test: the pixel under every labelled sticker centre
    # matches the manifest state (catches any layout-conversion error on the
    # R/D/B mirror) -----------------------------------------------------------
    rng = np.random.default_rng(1234)
    bg_ss = cv2.resize(np.full((H, W, 3), 90.0, np.float32), (W * SS, H * SS))
    for trial in range(3):
        state = apply_moves(SOLVED_FACELETS, random_scramble(rng))
        render_state = to_render_state(state)
        R = (R_BASE if trial == 0
             else R_from_quat(quat_random(rng)))
        rvec = cv2.Rodrigues(R)[0].ravel()
        img, _, _, _ = render_frame(render_state, R, rvec, tvec, bg_ss, [], rng,
                                    noise_sigma=0.0)
        _, faces = frame_labels(R, rvec, tvec)
        checked = 0
        for entry in faces:
            f = entry["face"]
            if face_cosines(R, tvec)[f] < 0.35:
                continue  # too oblique for a fair 3x3 pixel probe
            shade = shade_for(R, f)
            palette = {ltr: np.array(BASE_COLORS[cm.SOLVED_SCHEME[ltr]]) * shade
                       for ltr in FACE_ORDER}
            for i, pt in enumerate(entry["centres"]):
                x, y = int(round(pt["x"])), int(round(pt["y"]))
                patch = img[max(0, y - 1): y + 2, max(0, x - 1): x + 2].reshape(-1, 3)
                sample = np.median(patch, axis=0)
                truth = state[FACE_OFFSET[f] + i]
                dists = {ltr: float(np.linalg.norm(sample - p.clip(0, 255)))
                         for ltr, p in palette.items()}
                best = min(dists, key=dists.get)
                assert best == truth and dists[truth] < 40, (
                    f"colour mismatch {f}[{i}]: truth {truth}, sampled {best} "
                    f"(d={dists[truth]:.1f}) at ({x},{y})")
                checked += 1
        # At least one face always has facing cosine >= 1/sqrt(3) > 0.35.
        assert checked >= 9, f"colour test only probed {checked} stickers"
    print("selftests: OK (move engine, corner-id mapping, quad table, "
          "rendered colour/geometry checks)")


# =============================================================================
# Spot-check previews (drawn from the WRITTEN files, not in-memory values)
# =============================================================================


def write_preview(out_root: Path, clip: str, p: int) -> Path:
    img = cv2.imread(str(out_root / clip / f"rec_{p:06d}.jpg"), cv2.IMREAD_COLOR)
    kf = json.loads((out_root / clip / "groundtruth" / "keyframes" / f"{p:06d}.json")
                    .read_text(encoding="utf-8"))
    canvas = img.copy()
    for entry in kf["faces"]:
        q = entry["quad"]
        pts = [(q[k]["x"], q[k]["y"]) for k in ("tl", "tr", "br", "bl")]
        for a in range(4):
            cv2.line(canvas, (int(round(pts[a][0])), int(round(pts[a][1]))),
                     (int(round(pts[(a + 1) % 4][0])), int(round(pts[(a + 1) % 4][1]))),
                     (0, 255, 0), 1, cv2.LINE_AA)
        tl = (int(round(pts[0][0])), int(round(pts[0][1])))
        cv2.rectangle(canvas, (tl[0] - 3, tl[1] - 3), (tl[0] + 3, tl[1] + 3), (255, 128, 0), 1)
        mid = (int(sum(x for x, _ in pts) / 4), int(sum(y for _, y in pts) / 4))
        cv2.putText(canvas, entry["face"], mid, cv2.FONT_HERSHEY_SIMPLEX, 0.6,
                    (0, 255, 0), 2, cv2.LINE_AA)
        for i, c in enumerate(entry["centres"]):
            x, y = int(round(c["x"])), int(round(c["y"]))
            cv2.drawMarker(canvas, (x, y), (255, 0, 255), cv2.MARKER_CROSS, 7, 1, cv2.LINE_AA)
            if i == 0:
                cv2.circle(canvas, (x, y), 5, (255, 0, 255), 1, cv2.LINE_AA)
    for c in kf["corners"]:
        x, y = int(round(c["x"])), int(round(c["y"]))
        cv2.circle(canvas, (x, y), 4, (0, 255, 255), 1, cv2.LINE_AA)
        cv2.putText(canvas, str(c["model_id"]), (x + 6, y - 4), cv2.FONT_HERSHEY_SIMPLEX,
                    0.45, (0, 255, 255), 1, cv2.LINE_AA)
    dest = out_root / "_preview"
    dest.mkdir(exist_ok=True)
    path = dest / f"{clip}_{p:06d}.png"
    cv2.imwrite(str(path), canvas)
    return path


# =============================================================================
# Main
# =============================================================================

ALL_CLASSES = ["steady", "freerot", "occlusion", "absent", "exit-reentry"]


def main(argv=None) -> None:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out", default="D:/cubeulator-build/synth-corpus")
    ap.add_argument("--seed", type=int, default=0)
    ap.add_argument("--classes", default=",".join(ALL_CLASSES),
                    help="comma-separated subset of " + " ".join(ALL_CLASSES))
    ap.add_argument("--no-preview", action="store_true")
    args = ap.parse_args(argv)

    run_selftests()

    out_root = Path(args.out)
    out_root.mkdir(parents=True, exist_ok=True)
    classes = [c.strip() for c in args.classes.split(",") if c.strip()]
    unknown = set(classes) - set(ALL_CLASSES)
    assert not unknown, f"unknown classes: {unknown}"

    infos: dict[str, dict] = {}
    for cls_index, cls in enumerate(ALL_CLASSES):
        if cls not in classes:
            continue
        rng = np.random.default_rng(args.seed * 1000 + cls_index)
        scramble = random_scramble(rng)
        state = apply_moves(SOLVED_FACELETS, scramble)
        scramble_txt = " ".join(scramble)
        name = f"synth-{cls}"
        if cls == "steady":
            specs = build_steady()
            motion = f"synthetic: near-static, sub-pixel jitter; scramble: {scramble_txt}"
        elif cls == "freerot":
            specs = build_freerot(rng)
            motion = f"synthetic: smooth free rotation (slerp waypoints); scramble: {scramble_txt}"
        elif cls == "occlusion":
            specs = build_occlusion()
            motion = ("synthetic: finger occluders sweeping, 3 full-occlusion gaps; "
                      f"scramble: {scramble_txt}")
        elif cls == "absent":
            specs = build_absent()
            motion = f"synthetic: background only, no cube; scramble: {scramble_txt}"
        else:
            specs = build_exit_reentry()
            motion = f"synthetic: 4 full frame exits and re-entries; scramble: {scramble_txt}"

        info = emit_clip(out_root, name, specs, state, motion, rng)
        infos[cls] = info

        # Per-class invariants - measured, then asserted.
        tags = info["tags"]
        absent_runs = [s for s in info["segments"] if "absent" in s["tags"]]
        if cls == "steady":
            assert all(t == frozenset({"in_frame", "steady"}) for t in tags)
            assert info["reentries"] == []
        elif cls == "freerot":
            assert all(t == frozenset({"in_frame"}) for t in tags)
        elif cls == "occlusion":
            lengths = [s["end"] - s["start"] + 1 for s in absent_runs]
            assert len(absent_runs) == 3 and all(12 <= n <= 30 for n in lengths), (
                f"occlusion gaps: {lengths}")
            assert len(info["reentries"]) == 3, info["reentries"]
            assert sum(1 for t in tags if "occluded" in t) >= 60
        elif cls == "absent":
            assert all(t == frozenset({"absent"}) for t in tags)
            assert info["reentries"] == []
        else:  # exit-reentry
            assert len(info["reentries"]) == 4, info["reentries"]
            assert len(absent_runs) == 4 and all(
                s["end"] - s["start"] + 1 >= 5 for s in absent_runs)
            assert "in_frame" in tags[0] and "in_frame" in tags[-1]

        n_abs = sum(1 for t in tags if "absent" in t)
        n_occ = sum(1 for t in tags if "occluded" in t)
        print(f"{name:<20} {len(specs):4d} frames  segments {len(info['segments']):2d}  "
              f"absent {n_abs:3d}  occluded {n_occ:3d}  reentries {info['reentries']}")

    if not args.no_preview:
        picks = []
        if "steady" in infos:
            picks.append(("synth-steady", 120))
        if "freerot" in infos:
            picks.extend([("synth-freerot", 100), ("synth-freerot", 300)])
        if "occlusion" in infos:
            tags = infos["occlusion"]["tags"]
            partial = next(i for i in range(60, len(tags)) if "occluded" in tags[i])
            picks.append(("synth-occlusion", partial))
        if "exit-reentry" in infos:
            tags = infos["exit-reentry"]["tags"]
            first_absent = next(i for i, t in enumerate(tags) if "absent" in t)
            picks.extend([("synth-exit-reentry", 0),
                          ("synth-exit-reentry", max(0, first_absent - 2))])
        for clip, p in picks:
            print("preview:", write_preview(out_root, clip, p))

    print(f"corpus written to {out_root}")


if __name__ == "__main__":
    main()
