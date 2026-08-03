"""Synthetic Rubik's-cube training data generator (pipeline-plan Phase 0
item 9 / Phase 1 item 2).

Renders randomised cube images with exact 2D corner-keypoint labels in
YOLO-pose format plus a full-metadata labels.jsonl.  See README.md for the
corner-id convention and usage.

Backends:
    numpy    pure numpy+OpenCV projection renderer (works today)
    blender  invokes scripts/blender_scene.py via `blender --background`
             (stub -- Blender was absent on this machine when written)

Run with the bundled venv:
    tools/synthdata/.venv/Scripts/python tools/synthdata/generate.py \
        --backend numpy --count 32 --out tools/synthdata/sample_out --seed 0
"""

from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
import time
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(SCRIPT_DIR))

from labels import DatasetWriter  # noqa: E402


def parse_args(argv=None):
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--backend", choices=["numpy", "blender"], default="numpy")
    p.add_argument("--count", type=int, default=32, help="number of images")
    p.add_argument("--out", default=str(SCRIPT_DIR / "sample_out"), help="output dataset dir")
    p.add_argument("--seed", type=int, default=0)
    p.add_argument("--width", type=int, default=640)
    p.add_argument("--height", type=int, default=640)
    p.add_argument("--backgrounds", default=None, help="optional dir of background images")
    p.add_argument(
        "--corner-order",
        choices=["view", "model"],
        default="view",
        help="keypoint corner-id order: 'view' = viewer-canonical (learnable; "
        "the default -- model-frame ids are unobservable for scrambled/random "
        "stickers and collapse keypoint training), 'model' = raw model-frame "
        "binary order (legacy; NOT suitable for YOLO-pose training)",
    )
    p.add_argument("--occluder-prob", type=float, default=0.5, help="probability of finger-like occluders per image (0 disables)")
    p.add_argument("--jpeg-prob", type=float, default=0.7, help="probability of JPEG-artifact augmentation")
    p.add_argument("--blur-prob", type=float, default=0.35, help="probability of directional motion blur")
    p.add_argument(
        "--neg-frac",
        type=float,
        default=0.0,
        help="fraction of images rendered as pure-background hard negatives "
        "(no cube, EMPTY label file; ~0.15 recommended when --backgrounds "
        "points at real deployment-scene photos)",
    )
    p.add_argument(
        "--size-min", type=float, default=0.12,
        help="min projected cube bbox height as a fraction of frame height",
    )
    p.add_argument(
        "--size-max", type=float, default=0.70,
        help="max projected cube bbox height as a fraction of frame height",
    )
    p.add_argument(
        "--face-on-prob", type=float, default=0.20,
        help="probability of a near-face-on presentation (single face "
        "square to the camera, <=10 deg tilt) instead of a uniform SO(3) pose",
    )
    p.add_argument(
        "--bg-image-prob", type=float, default=0.75,
        help="probability a positive uses a real --backgrounds photo (half "
        "full-frame, half crops); the rest are solid/gradient fills. "
        "Negatives use max(this, 0.9)",
    )
    p.add_argument("--blender-path", default=os.environ.get("BLENDER", "blender"), help="blender executable (blender backend only)")
    return p.parse_args(argv)


def load_backgrounds(bg_dir: str | None):
    if not bg_dir:
        return None
    import cv2

    exts = {".jpg", ".jpeg", ".png", ".bmp", ".webp"}
    imgs = []
    for f in sorted(Path(bg_dir).iterdir()):
        if f.suffix.lower() in exts:
            img = cv2.imread(str(f))
            if img is not None:
                imgs.append(img)
    print(f"loaded {len(imgs)} background images from {bg_dir}")
    return imgs or None


def run_numpy(args) -> None:
    try:
        import cv2  # noqa: F401
        import numpy as np
    except ImportError:
        venv_py = SCRIPT_DIR / ".venv" / "Scripts" / "python.exe"
        alt = SCRIPT_DIR / ".venv" / "bin" / "python"
        hint = venv_py if venv_py.exists() else alt
        sys.exit(
            "numpy/opencv-python not available in this interpreter.\n"
            f"Use the bundled venv:  {hint} {Path(__file__).name} ...\n"
            "or create it:  python -m venv .venv && .venv/Scripts/pip install numpy opencv-python"
        )

    import cv2

    import cube_model as cm
    from render_numpy import render_negative, render_sample

    rng = np.random.default_rng(args.seed)
    bg_images = load_backgrounds(args.backgrounds)

    t0 = time.perf_counter()
    n_neg = 0
    with DatasetWriter(args.out) as writer:
        for i in range(args.count):
            name = f"img_{i:06d}.jpg"
            if rng.random() < args.neg_frac:
                img, meta = render_negative(
                    rng,
                    args.width,
                    args.height,
                    bg_images,
                    jpeg_prob=args.jpeg_prob,
                    blur_prob=args.blur_prob,
                    bg_image_prob=max(args.bg_image_prob, 0.9),
                )
                cv2.imwrite(str(writer.image_path(name)), img, [cv2.IMWRITE_JPEG_QUALITY, 95])
                writer.write_negative(name, args.width, args.height, meta)
                n_neg += 1
                continue
            img, sample = render_sample(
                rng,
                name,
                args.width,
                args.height,
                bg_images,
                occluder_prob=args.occluder_prob,
                jpeg_prob=args.jpeg_prob,
                blur_prob=args.blur_prob,
                size_range=(args.size_min, args.size_max),
                face_on_prob=args.face_on_prob,
                bg_image_prob=args.bg_image_prob,
            )
            if args.corner_order == "view":
                R, _ = cv2.Rodrigues(np.array(sample.rvec))
                perm = cm.view_canonical_perm(R, sample.tvec)
                sample.corners_px = [sample.corners_px[j] for j in perm]
                sample.visibility = [sample.visibility[j] for j in perm]
                sample.corner_order = cm.CORNER_ORDER_VIEW
                sample.corner_perm = perm
            cv2.imwrite(str(writer.image_path(name)), img, [cv2.IMWRITE_JPEG_QUALITY, 95])
            writer.write_sample(sample)
    dt = time.perf_counter() - t0
    print(
        f"wrote {args.count} samples ({n_neg} negatives) to {args.out} in {dt:.2f}s "
        f"({dt / max(args.count, 1) * 1000:.1f} ms/image)"
    )


def run_blender(args) -> None:
    # TODO(blender): Blender was not installed on this machine when this was
    # written (`blender --version` failed).  The scene script below is a
    # stub; once Blender exists, flesh out scripts/blender_scene.py (cube
    # mesh + sticker materials, cycles lighting, HDRI backgrounds, hand
    # models) and it will emit the SAME label format via labels.py.
    exe = args.blender_path
    if shutil.which(exe) is None:
        sys.exit(
            f"blender executable not found ({exe!r}).\n"
            "Install Blender or pass --blender-path / set BLENDER.\n"
            "Until then use --backend numpy, which is fully functional."
        )
    script = SCRIPT_DIR / "scripts" / "blender_scene.py"
    cmd = [
        exe,
        "--background",
        "--python",
        str(script),
        "--",
        "--count",
        str(args.count),
        "--out",
        args.out,
        "--seed",
        str(args.seed),
        "--width",
        str(args.width),
        "--height",
        str(args.height),
    ]
    print("running:", " ".join(cmd))
    raise SystemExit(subprocess.call(cmd))


def main(argv=None) -> None:
    args = parse_args(argv)
    if args.backend == "numpy":
        run_numpy(args)
    else:
        run_blender(args)


if __name__ == "__main__":
    main()
