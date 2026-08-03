"""Pure numpy+OpenCV renderer: projects the 3x3x3 sticker model with a
pinhole camera at a random pose and composites onto a randomised background
with lighting/blur/JPEG/occluder augmentation.

Convexity does the hidden-surface work: only front-facing faces are drawn
and, for a convex solid, front faces never overlap in projection, so a
painter's pass (body silhouette hull, then stickers) is exact.  Rendering is
done at 2x and downsampled (INTER_AREA) for anti-aliasing.
"""

from __future__ import annotations

import math

import cv2
import numpy as np

import cube_model as cm
from labels import VIS_NONE, VIS_OCCLUDED, VIS_VISIBLE, Sample

SS = 2  # supersampling factor

# Base sticker colours, BGR uint8-ish floats.
BASE_COLORS = {
    "W": (235.0, 235.0, 235.0),
    "Y": (40.0, 210.0, 235.0),
    "R": (35.0, 30.0, 190.0),
    "O": (25.0, 110.0, 240.0),
    "G": (70.0, 165.0, 35.0),
    "B": (185.0, 95.0, 15.0),
}

SKIN_TONES = [  # BGR
    (150.0, 178.0, 224.0),
    (125.0, 155.0, 205.0),
    (98.0, 128.0, 178.0),
    (66.0, 92.0, 138.0),
    (44.0, 62.0, 96.0),
]


# --- pose / camera -----------------------------------------------------------


def random_rotation(rng: np.random.Generator) -> np.ndarray:
    """Uniform random rotation matrix (Shoemake quaternion method)."""
    u1, u2, u3 = rng.random(3)
    q = np.array(
        [
            math.sqrt(1 - u1) * math.sin(2 * math.pi * u2),
            math.sqrt(1 - u1) * math.cos(2 * math.pi * u2),
            math.sqrt(u1) * math.sin(2 * math.pi * u3),
            math.sqrt(u1) * math.cos(2 * math.pi * u3),
        ]
    )
    x, y, z, w = q
    return np.array(
        [
            [1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
            [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
            [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)],
        ]
    )


def face_on_rotation(rng: np.random.Generator) -> np.ndarray:
    """Near-face-on presentation: one face square to the camera, small tilt.

    Real users routinely hold a face flat at the lens (the round-1 detector
    never saw this and missed close-up face-on cubes entirely).  Roll is
    mostly near-axis-aligned (how people actually hold a cube) with a
    uniform tail; tilt is 0-10 degrees off perfectly square-on.
    """
    if rng.random() < 0.65:
        roll = 90.0 * rng.integers(4) + rng.uniform(-12.0, 12.0)
    else:
        roll = rng.uniform(0.0, 360.0)
    tilt = math.radians(rng.uniform(0.0, 10.0))
    phi = rng.uniform(0.0, 2 * math.pi)
    rt, _ = cv2.Rodrigues(np.array([math.cos(phi), math.sin(phi), 0.0]) * tilt)
    rz, _ = cv2.Rodrigues(np.array([0.0, 0.0, math.radians(roll)]))
    # Random proper cube symmetry so every model face can be the front one.
    syms = [S for S in cm.SYMMETRIES_48 if np.linalg.det(S) > 0]
    return rz @ rt @ syms[rng.integers(len(syms))]


def sample_camera_and_pose(
    rng: np.random.Generator,
    w: int,
    h: int,
    size_range: tuple[float, float] = (0.12, 0.70),
    face_on: bool = False,
):
    """Returns (K, rvec, tvec) with the cube mostly in frame.

    ``size_range`` is the target projected bbox HEIGHT as a fraction of the
    frame height (round 2: 0.12-0.70, matching real footage where a face-on
    cube fills half the frame).  The z distance is solved iteratively so the
    *actual* projected extent hits the target regardless of pose.
    """
    f = w * rng.uniform(0.8, 1.4)
    cx = w / 2 + rng.uniform(-0.03, 0.03) * w
    cy = h / 2 + rng.uniform(-0.03, 0.03) * h
    K = np.array([[f, 0, cx], [0, f, cy], [0, 0, 1]], dtype=np.float64)

    R = face_on_rotation(rng) if face_on else random_rotation(rng)
    rvec, _ = cv2.Rodrigues(R)
    rvec = rvec.reshape(3)

    target_px = rng.uniform(*size_range) * h
    # Initial z from the circumsphere (diameter 2*sqrt(3)), then refine so the
    # measured projected bbox height matches the target.
    z = f * 2 * math.sqrt(3) / target_px
    for _ in range(3):
        pts = project(cm.CORNERS, rvec, np.array([0.0, 0.0, z]), K)
        bh = float(pts[:, 1].max() - pts[:, 1].min())
        z *= bh / target_px

    # Image-plane target for the cube centre: keep it comfortably inside.
    margin = min(target_px * 0.4, 0.45 * min(w, h))
    tx_px = rng.uniform(margin, w - margin) if w > 2 * margin else w / 2
    ty_px = rng.uniform(margin, h - margin) if h > 2 * margin else h / 2
    t = np.array([(tx_px - cx) / f * z, (ty_px - cy) / f * z, z])
    return K, rvec, t


def project(pts3: np.ndarray, rvec: np.ndarray, tvec: np.ndarray, K: np.ndarray) -> np.ndarray:
    out, _ = cv2.projectPoints(
        pts3.reshape(-1, 1, 3).astype(np.float64), rvec, tvec, K, None
    )
    return out.reshape(-1, 2)


# --- backgrounds -------------------------------------------------------------


def make_background(
    rng: np.random.Generator,
    w: int,
    h: int,
    bg_images: list | None,
    image_prob: float = 0.75,
):
    """Round 2: real photos dominate (``image_prob``, default 0.75) and half
    of the photo backgrounds are used FULL-FRAME (aspect-fill), so the model
    sees the deployment scene exactly as the camera does; the rest are
    aspect-correct random crops for variety.  Solid/gradient fills remain as
    a regularising minority."""
    kind = rng.random()
    if bg_images and kind < image_prob:
        img = bg_images[rng.integers(len(bg_images))]
        ih, iw = img.shape[:2]
        # Half full-frame (scale 1: the largest aspect-correct window), half
        # zoomed-in random crops.
        scale = 1.0 if rng.random() < 0.5 else rng.uniform(0.4, 1.0)
        cw = max(8, int(min(iw, ih * w / h) * scale))
        ch = max(8, int(cw * h / w))
        if cw <= iw and ch <= ih:
            x0 = rng.integers(0, iw - cw + 1)
            y0 = rng.integers(0, ih - ch + 1)
            crop = img[y0 : y0 + ch, x0 : x0 + cw]
        else:
            crop = img
        return cv2.resize(crop, (w, h), interpolation=cv2.INTER_AREA).astype(np.float32), "image"
    rest = (kind - image_prob) / max(1.0 - image_prob, 1e-9) if bg_images else kind
    if rest < 0.5:
        c = rng.uniform(20, 235, size=3).astype(np.float32)
        return np.full((h, w, 3), c, dtype=np.float32), "solid"
    # Linear gradient between two random colours at a random angle.
    c0 = rng.uniform(10, 245, size=3)
    c1 = rng.uniform(10, 245, size=3)
    ang = rng.uniform(0, 2 * math.pi)
    xs, ys = np.meshgrid(np.linspace(-0.5, 0.5, w), np.linspace(-0.5, 0.5, h))
    tt = (xs * math.cos(ang) + ys * math.sin(ang) + 0.5).clip(0, 1)[..., None]
    return (c0 * (1 - tt) + c1 * tt).astype(np.float32), "gradient"


# --- occluders ---------------------------------------------------------------


def draw_occluders(rng: np.random.Generator, img: np.ndarray, bbox, n: int):
    """Finger-like capsules / ellipses in skin tones crossing the cube.
    Draws in place; returns the occluder mask (uint8, 255 = occluded)."""
    h, w = img.shape[:2]
    mask = np.zeros((h, w), dtype=np.uint8)
    x0, y0, x1, y1 = bbox
    ext = max(x1 - x0, y1 - y0)
    for _ in range(n):
        base = np.array(SKIN_TONES[rng.integers(len(SKIN_TONES))], dtype=np.float64)
        tone = tuple(float(v) for v in (base * rng.uniform(0.85, 1.15)).clip(0, 255))
        thick = int(ext * rng.uniform(0.10, 0.24))
        # Anchor somewhere on the cube, direction random; long enough to
        # cross out of the bbox (finger entering from outside the cube).
        ax = rng.uniform(x0, x1)
        ay = rng.uniform(y0, y1)
        ang = rng.uniform(0, 2 * math.pi)
        length = ext * rng.uniform(0.9, 1.8)
        dx, dy = math.cos(ang), math.sin(ang)
        p0 = (int(ax - dx * length * 0.15), int(ay - dy * length * 0.15))
        p1 = (int(ax + dx * length * 0.85), int(ay + dy * length * 0.85))
        if rng.random() < 0.75:  # capsule "finger"
            shade = tuple(float(v) * 0.82 for v in tone)
            cv2.line(img, p0, p1, shade, thickness=thick + max(2, thick // 6), lineType=cv2.LINE_AA)
            cv2.line(img, p0, p1, tone, thickness=thick, lineType=cv2.LINE_AA)
            cv2.line(mask, p0, p1, 255, thickness=thick + max(2, thick // 6), lineType=cv2.LINE_8)
        else:  # ellipse blob (thumb / palm edge)
            centre = (int(ax), int(ay))
            axes = (int(thick * rng.uniform(0.9, 1.6)), int(thick * rng.uniform(1.4, 2.6)))
            rot = math.degrees(ang)
            cv2.ellipse(img, centre, axes, rot, 0, 360, tone, -1, cv2.LINE_AA)
            cv2.ellipse(mask, centre, axes, rot, 0, 360, 255, -1, cv2.LINE_8)
    return mask


# --- photometric augmentation ------------------------------------------------


def photometric_jitter(rng: np.random.Generator, img: np.ndarray) -> tuple[np.ndarray, dict]:
    meta = {}
    if rng.random() < 0.35:
        # Warm indoor cast (round 2): the real clips are shot under warm
        # lamps with a webcam that pushes reds hard -- whites go pink, blues
        # sink.  Channel order is BGR.
        gains = np.array(
            [rng.uniform(0.60, 0.95), rng.uniform(0.85, 1.05), rng.uniform(1.05, 1.45)]
        )
        meta["cast"] = "warm"
    else:
        gains = rng.uniform(0.85, 1.15, size=3)  # neutral white-balance jitter
    exposure = rng.uniform(0.70, 1.30)
    gamma = rng.uniform(0.70, 1.40)
    meta["wb"] = [round(g, 3) for g in gains]
    meta["exposure"] = round(exposure, 3)
    meta["gamma"] = round(gamma, 3)
    out = img * gains[None, None, :] * exposure
    out = 255.0 * (out.clip(0, 255) / 255.0) ** gamma
    if rng.random() < 0.5:
        # Light hue rotation (degrees) on top of the channel gains.
        hshift = rng.uniform(-10.0, 10.0)
        hsv = cv2.cvtColor((out / 255.0).astype(np.float32), cv2.COLOR_BGR2HSV)
        hsv[..., 0] = (hsv[..., 0] + hshift) % 360.0
        out = cv2.cvtColor(hsv, cv2.COLOR_HSV2BGR).astype(np.float64) * 255.0
        meta["hue_deg"] = round(hshift, 1)
    if rng.random() < 0.8:
        sigma = rng.uniform(1.0, 6.0)
        out = out + rng.normal(0, sigma, out.shape)
        meta["noise_sigma"] = round(sigma, 2)
    return out.clip(0, 255), meta


def motion_blur(rng: np.random.Generator, img: np.ndarray) -> tuple[np.ndarray, dict]:
    length = int(rng.uniform(4, 16))
    ang = rng.uniform(0, math.pi)
    k = np.zeros((length, length), dtype=np.float32)
    c = (length - 1) / 2
    cv2.line(
        k,
        (int(c - c * math.cos(ang)), int(c - c * math.sin(ang))),
        (int(c + c * math.cos(ang)), int(c + c * math.sin(ang))),
        1.0,
        1,
    )
    k /= max(k.sum(), 1.0)
    return cv2.filter2D(img, -1, k), {"blur_len": length, "blur_deg": round(math.degrees(ang), 1)}


# --- main render -------------------------------------------------------------


def render_negative(
    rng: np.random.Generator,
    w: int,
    h: int,
    bg_images: list | None,
    jpeg_prob: float,
    blur_prob: float,
    bg_image_prob: float = 0.9,
) -> tuple[np.ndarray, dict]:
    """Pure-background hard negative (no cube, empty label file downstream).

    Real photo backgrounds are strongly preferred (default 0.9) -- the whole
    point is teaching the detector that the deployment room (curtain, hands,
    face) is not a cube."""
    bg, bg_kind = make_background(rng, w, h, bg_images, image_prob=bg_image_prob)
    meta: dict = {"backend": "numpy", "bg": bg_kind, "negative": True}
    img, pmeta = photometric_jitter(rng, bg)
    meta.update(pmeta)
    if rng.random() < blur_prob:
        img, bmeta = motion_blur(rng, img)
        meta.update(bmeta)
    out = img.clip(0, 255).astype(np.uint8)
    if rng.random() < jpeg_prob:
        q = int(rng.integers(45, 96))
        ok, buf = cv2.imencode(".jpg", out, [cv2.IMWRITE_JPEG_QUALITY, q])
        if ok:
            out = cv2.imdecode(buf, cv2.IMREAD_COLOR)
        meta["jpeg_q"] = q
    return out, meta


def render_sample(
    rng: np.random.Generator,
    image_name: str,
    w: int,
    h: int,
    bg_images: list | None,
    occluder_prob: float,
    jpeg_prob: float,
    blur_prob: float,
    size_range: tuple[float, float] = (0.12, 0.70),
    face_on_prob: float = 0.20,
    bg_image_prob: float = 0.75,
) -> tuple[np.ndarray, Sample]:
    """Renders one image; returns (bgr uint8, Sample)."""
    state = cm.sample_state(rng)
    face_on = rng.random() < face_on_prob

    # Resample poses until enough of the cube is usefully in frame.
    for _ in range(100):
        K, rvec, tvec = sample_camera_and_pose(rng, w, h, size_range, face_on)
        R, _ = cv2.Rodrigues(rvec)
        corners_px = project(cm.CORNERS, rvec, tvec, K)

        # Face visibility: outward normal vs view ray to face centre.
        vis_faces = []
        for f in cm.FACE_ORDER:
            n_cam = R @ cm.FACE_NORMALS[f]
            c_cam = R @ (cm.FACE_NORMALS[f] * 1.0) + tvec  # face centre
            if float(np.dot(n_cam, c_cam)) < 0:
                vis_faces.append(f)

        # Geometric corner visibility: on at least one front-facing face.
        front = np.zeros(8, dtype=bool)
        for f in vis_faces:
            for cid in cm.face_corner_ids(f):
                front[cid] = True

        inside = (
            (corners_px[:, 0] >= 0)
            & (corners_px[:, 0] < w)
            & (corners_px[:, 1] >= 0)
            & (corners_px[:, 1] < h)
        )
        if int((front & inside).sum()) >= 4:
            break
    else:
        raise RuntimeError("could not sample a usable pose in 100 tries")

    # ---- draw at SS x -------------------------------------------------------
    W, H = w * SS, h * SS
    Kss = K.copy()
    Kss[:2] *= SS
    bg, bg_kind = make_background(rng, w, h, bg_images, image_prob=bg_image_prob)
    canvas = cv2.resize(bg, (W, H), interpolation=cv2.INTER_LINEAR)

    # Lighting: ambient + one directional light from the camera hemisphere.
    # `light` is the direction the light TRAVELS; the camera looks down +z,
    # so +z-ish travel means camera-facing faces (n_z < 0) catch the light.
    light = rng.normal(size=3)
    light[2] = abs(light[2]) + 0.5
    light /= np.linalg.norm(light)
    ambient = rng.uniform(0.35, 0.55)
    diffuse = rng.uniform(0.5, 0.8)

    # Body silhouette = convex hull of the 8 projected corners.
    # Round 2: not just dark plastic -- grey and white/light bodies too (the
    # real cardboard cube has white face borders; stickerless cubes exist).
    body_shade = ambient + diffuse * 0.4
    bk = rng.random()
    if bk < 0.65:
        body_base = rng.uniform(8, 45)  # black plastic / dark card
    elif bk < 0.85:
        body_base = rng.uniform(60, 140)  # grey
    else:
        body_base = rng.uniform(180, 250)  # white-bordered cardboard
    body_col = np.array([1.0, 1.0, 1.0]) * body_base * body_shade
    hull_ss = cv2.convexHull((project(cm.CORNERS, rvec, tvec, Kss)).astype(np.float32))
    cv2.fillPoly(
        canvas,
        [np.round(hull_ss).astype(np.int32)],
        tuple(float(v) for v in body_col),
        lineType=cv2.LINE_AA,
    )

    # Per-image palette + sticker geometry jitter.  Round 2 adds a per-image
    # desaturation mix toward grey: printed/cardboard stickers are far more
    # muted than glossy plastic ones.
    desat = rng.uniform(0.0, 0.40)
    palette = {}
    for k, v in BASE_COLORS.items():
        c = np.array(v, dtype=np.float64) * rng.uniform(0.92, 1.08) + rng.uniform(-10, 10, size=3)
        palette[k] = c * (1.0 - desat) + c.mean() * desat
    sticker_frac = rng.uniform(0.84, 0.92)

    for f in cm.FACE_ORDER:
        n_cam = R @ cm.FACE_NORMALS[f]
        c_cam = R @ cm.FACE_NORMALS[f] + tvec
        if float(np.dot(n_cam, c_cam)) >= 0:
            continue  # back-facing
        lambert = max(0.0, float(np.dot(n_cam / np.linalg.norm(n_cam), -light)))
        shade = ambient + diffuse * lambert
        colors = cm.state_face(state, f)
        for quad3, letter in zip(cm.sticker_quads(f, sticker_frac), colors):
            col = palette[letter] * shade * rng.uniform(0.94, 1.06)
            pts = np.round(project(quad3, rvec, tvec, Kss)).astype(np.int32)
            cv2.fillPoly(
                canvas, [pts], tuple(float(v) for v in col.clip(0, 255)), lineType=cv2.LINE_AA
            )

    img = cv2.resize(canvas, (w, h), interpolation=cv2.INTER_AREA)

    # ---- bbox (silhouette hull clipped to the image) ------------------------
    hull = cv2.convexHull(corners_px.astype(np.float32)).reshape(-1, 2)
    bx0 = float(np.clip(hull[:, 0].min(), 0, w - 1))
    bx1 = float(np.clip(hull[:, 0].max(), 0, w - 1))
    by0 = float(np.clip(hull[:, 1].min(), 0, h - 1))
    by1 = float(np.clip(hull[:, 1].max(), 0, h - 1))
    bbox = [bx0, by0, bx1, by1]

    # ---- occluders ----------------------------------------------------------
    meta: dict = {"backend": "numpy", "bg": bg_kind}
    n_occ = 0
    occ_mask = None
    if rng.random() < occluder_prob:
        n_occ = int(rng.integers(1, 4))
        occ_mask = draw_occluders(rng, img, bbox, n_occ)
    meta["occluders"] = n_occ

    # ---- corner visibility flags -------------------------------------------
    ext = max(bx1 - bx0, by1 - by0)
    rad = max(2, int(round(ext * 0.03)))
    visibility = []
    for i in range(8):
        px, py = corners_px[i]
        if not front[i] or not (0 <= px < w and 0 <= py < h):
            visibility.append(VIS_NONE)
            continue
        v = VIS_VISIBLE
        if occ_mask is not None:
            x0s, x1s = max(0, int(px) - rad), min(w, int(px) + rad + 1)
            y0s, y1s = max(0, int(py) - rad), min(h, int(py) + rad + 1)
            patch = occ_mask[y0s:y1s, x0s:x1s]
            if patch.size and float((patch > 0).mean()) >= 0.5:
                v = VIS_OCCLUDED
        visibility.append(v)

    # ---- photometric + blur + jpeg -----------------------------------------
    img, pmeta = photometric_jitter(rng, img)
    meta.update(pmeta)
    if rng.random() < blur_prob:
        img, bmeta = motion_blur(rng, img)
        meta.update(bmeta)
    out = img.clip(0, 255).astype(np.uint8)
    if rng.random() < jpeg_prob:
        q = int(rng.integers(45, 96))
        ok, buf = cv2.imencode(".jpg", out, [cv2.IMWRITE_JPEG_QUALITY, q])
        if ok:
            out = cv2.imdecode(buf, cv2.IMREAD_COLOR)
        meta["jpeg_q"] = q

    sample = Sample(
        image_name=image_name,
        width=w,
        height=h,
        state=state,
        rvec=[float(v) for v in rvec],
        tvec=[float(v) for v in tvec],
        fx=float(K[0, 0]),
        fy=float(K[1, 1]),
        cx=float(K[0, 2]),
        cy=float(K[1, 2]),
        corners_px=[[float(x), float(y)] for x, y in corners_px],
        visibility=visibility,
        bbox_xyxy=bbox,
        meta=meta,
    )
    return out, sample
