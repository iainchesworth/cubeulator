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
    p.add_argument("--occluder-prob", type=float, default=0.5, help="probability of finger-like occluders per image (0 disables)")
    p.add_argument("--jpeg-prob", type=float, default=0.7, help="probability of JPEG-artifact augmentation")
    p.add_argument("--blur-prob", type=float, default=0.35, help="probability of directional motion blur")
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

    from render_numpy import render_sample

    rng = np.random.default_rng(args.seed)
    bg_images = load_backgrounds(args.backgrounds)

    t0 = time.perf_counter()
    with DatasetWriter(args.out) as writer:
        for i in range(args.count):
            name = f"img_{i:06d}.jpg"
            img, sample = render_sample(
                rng,
                name,
                args.width,
                args.height,
                bg_images,
                occluder_prob=args.occluder_prob,
                jpeg_prob=args.jpeg_prob,
                blur_prob=args.blur_prob,
            )
            cv2.imwrite(str(writer.image_path(name)), img, [cv2.IMWRITE_JPEG_QUALITY, 95])
            writer.write_sample(sample)
    dt = time.perf_counter() - t0
    print(
        f"wrote {args.count} samples to {args.out} in {dt:.2f}s "
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
