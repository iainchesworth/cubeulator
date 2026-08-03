"""Contact-sheet previewer: draws the corner keypoints from labels.jsonl
onto each image and tiles them so label correctness is one glance away.

    tools/synthdata/.venv/Scripts/python tools/synthdata/preview.py \
        --dataset tools/synthdata/sample_out --limit 32

Markers: filled circle = v2 (visible), ring = v1 (occluded-but-annotated),
small x = v0 (not visible; drawn from jsonl full metadata only, at the true
projected position).  Corner ids 0-7 labelled beside each marker; yellow
box = YOLO bbox; thin grey wireframe joins the projected corners along the
12 cube edges as a geometry sanity check.
"""

from __future__ import annotations

import argparse
import json
import math
import sys
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(SCRIPT_DIR))


def main(argv=None) -> None:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--dataset", default=str(SCRIPT_DIR / "sample_out"))
    p.add_argument("--limit", type=int, default=64, help="max samples on the sheet")
    p.add_argument("--tile", type=int, default=320, help="tile width in px")
    p.add_argument("--out", default=None, help="output PNG (default <dataset>/contact_sheet.png)")
    args = p.parse_args(argv)

    try:
        import cv2
        import numpy as np
    except ImportError:
        sys.exit(
            "numpy/opencv-python missing -- run with the bundled venv: "
            f"{SCRIPT_DIR / '.venv' / 'Scripts' / 'python.exe'} preview.py ..."
        )

    from cube_model import EDGES

    ds = Path(args.dataset)
    jsonl = ds / "labels.jsonl"
    if not jsonl.exists():
        sys.exit(f"{jsonl} not found -- run generate.py first")
    records = [json.loads(line) for line in jsonl.read_text(encoding="utf-8").splitlines() if line]
    records = records[: args.limit]
    if not records:
        sys.exit("labels.jsonl is empty")

    # Distinct colours per corner id (BGR), stable across tiles.
    kp_colors = [
        (0, 0, 255),      # 0 red
        (0, 165, 255),    # 1 orange
        (0, 255, 255),    # 2 yellow
        (0, 220, 0),      # 3 green
        (255, 200, 0),    # 4 cyan-ish
        (255, 0, 0),      # 5 blue
        (255, 0, 255),    # 6 magenta
        (255, 255, 255),  # 7 white
    ]

    tiles = []
    for rec in records:
        img = cv2.imread(str(ds / rec["image"]))
        if img is None:
            continue
        h, w = img.shape[:2]
        corners = rec["corners_px"]
        vis = rec["visibility"]

        # Wireframe (thin, grey) between projected corners.
        for a, b in EDGES:
            pa = (int(round(corners[a][0])), int(round(corners[a][1])))
            pb = (int(round(corners[b][0])), int(round(corners[b][1])))
            cv2.line(img, pa, pb, (128, 128, 128), 1, cv2.LINE_AA)

        x0, y0, x1, y1 = (int(round(v)) for v in rec["bbox_xyxy"])
        cv2.rectangle(img, (x0, y0), (x1, y1), (0, 255, 255), 1)

        for i, ((px, py), v) in enumerate(zip(corners, vis)):
            pt = (int(round(px)), int(round(py)))
            col = kp_colors[i]
            if v == 2:
                cv2.circle(img, pt, 5, col, -1, cv2.LINE_AA)
                cv2.circle(img, pt, 5, (0, 0, 0), 1, cv2.LINE_AA)
            elif v == 1:
                cv2.circle(img, pt, 6, col, 2, cv2.LINE_AA)
            else:
                if 0 <= pt[0] < w and 0 <= pt[1] < h:
                    cv2.drawMarker(img, pt, col, cv2.MARKER_TILTED_CROSS, 8, 1, cv2.LINE_AA)
            if 0 <= pt[0] < w and 0 <= pt[1] < h:
                cv2.putText(
                    img, str(i), (pt[0] + 6, pt[1] - 4),
                    cv2.FONT_HERSHEY_SIMPLEX, 0.45, (0, 0, 0), 3, cv2.LINE_AA,
                )
                cv2.putText(
                    img, str(i), (pt[0] + 6, pt[1] - 4),
                    cv2.FONT_HERSHEY_SIMPLEX, 0.45, col, 1, cv2.LINE_AA,
                )

        cv2.putText(
            img, Path(rec["image"]).stem, (4, h - 6),
            cv2.FONT_HERSHEY_SIMPLEX, 0.5, (255, 255, 255), 1, cv2.LINE_AA,
        )

        tw = args.tile
        th = int(round(tw * h / w))
        tiles.append(cv2.resize(img, (tw, th), interpolation=cv2.INTER_AREA))

    cols = int(math.ceil(math.sqrt(len(tiles))))
    rows = int(math.ceil(len(tiles) / cols))
    th, tw = tiles[0].shape[:2]
    sheet = np.zeros((rows * th, cols * tw, 3), dtype=np.uint8)
    for k, tile in enumerate(tiles):
        r, c = divmod(k, cols)
        sheet[r * th : (r + 1) * th, c * tw : (c + 1) * tw] = tile

    out = Path(args.out) if args.out else ds / "contact_sheet.png"
    cv2.imwrite(str(out), sheet)
    print(f"wrote {out} ({rows}x{cols} tiles, {len(tiles)} samples)")


if __name__ == "__main__":
    main()
