"""Backend-independent label writing: YOLO-pose .txt + labels.jsonl.

Any backend (numpy renderer, Blender, ...) produces a ``Sample`` and hands
it to ``write_sample``; the label format therefore cannot drift between
backends.

YOLO-pose line (single class 0 = cube, kpt_shape [8, 3]):

    0 cx cy w h  x0 y0 v0  x1 y1 v1  ...  x7 y7 v7

with everything normalised to [0, 1] and visibility flags
    2 = visible, 1 = occluded-but-annotated (position known, covered by an
    occluder), 0 = not visible (self-occluded rear corner or out of frame;
    x y written as 0 0 per Ultralytics convention).
"""

from __future__ import annotations

import dataclasses
import json
from pathlib import Path

VIS_VISIBLE = 2
VIS_OCCLUDED = 1
VIS_NONE = 0


@dataclasses.dataclass
class Sample:
    """Everything a backend must report for one rendered image."""

    image_name: str  # e.g. "img_000012.jpg" (basename, lives in images/)
    width: int
    height: int
    state: str  # 54-char URFDLB face-major colour string
    rvec: list[float]  # model->camera Rodrigues rotation (OpenCV convention)
    tvec: list[float]
    fx: float
    fy: float
    cx: float
    cy: float
    corners_px: list[list[float]]  # 8 x [x, y], in `corner_order` order,
    #   full-float projections (may lie outside the image)
    visibility: list[int]  # 8 flags, VIS_* above
    bbox_xyxy: list[float]  # clipped to the image
    meta: dict  # backend/augmentation details, free-form
    corner_order: str = "binary_xyz_x_fastest"  # cube_model.CORNER_ORDER_*
    corner_perm: list[int] | None = None  # slot j -> model-frame corner id
    #   (identity when corner_order is the model-frame binary order)


def yolo_line(s: Sample) -> str:
    x0, y0, x1, y1 = s.bbox_xyxy
    cx = (x0 + x1) / 2 / s.width
    cy = (y0 + y1) / 2 / s.height
    w = (x1 - x0) / s.width
    h = (y1 - y0) / s.height
    parts = [f"0 {cx:.6f} {cy:.6f} {w:.6f} {h:.6f}"]
    for (px, py), v in zip(s.corners_px, s.visibility):
        if v == VIS_NONE:
            parts.append("0 0 0")
        else:
            nx = min(max(px / s.width, 0.0), 1.0)
            ny = min(max(py / s.height, 0.0), 1.0)
            parts.append(f"{nx:.6f} {ny:.6f} {v}")
    return " ".join(parts)


def jsonl_record(s: Sample) -> dict:
    return {
        "image": f"images/{s.image_name}",
        "width": s.width,
        "height": s.height,
        "state": s.state,
        "rvec": [round(v, 6) for v in s.rvec],
        "tvec": [round(v, 6) for v in s.tvec],
        "K": [[s.fx, 0.0, s.cx], [0.0, s.fy, s.cy], [0.0, 0.0, 1.0]],
        "corners_px": [[round(x, 2), round(y, 2)] for x, y in s.corners_px],
        "visibility": list(s.visibility),
        "corner_order": s.corner_order,
        "corner_perm": list(s.corner_perm) if s.corner_perm is not None else list(range(8)),
        "bbox_xyxy": [round(v, 2) for v in s.bbox_xyxy],
        "meta": s.meta,
    }


class DatasetWriter:
    """Creates <out>/images/, <out>/labels/, <out>/labels.jsonl."""

    def __init__(self, out_dir: str | Path):
        self.out = Path(out_dir)
        (self.out / "images").mkdir(parents=True, exist_ok=True)
        (self.out / "labels").mkdir(parents=True, exist_ok=True)
        self._jsonl = open(self.out / "labels.jsonl", "w", encoding="utf-8")

    def image_path(self, image_name: str) -> Path:
        return self.out / "images" / image_name

    def write_sample(self, s: Sample) -> None:
        stem = Path(s.image_name).stem
        (self.out / "labels" / f"{stem}.txt").write_text(yolo_line(s) + "\n", encoding="utf-8")
        self._jsonl.write(json.dumps(jsonl_record(s)) + "\n")

    def write_negative(self, image_name: str, width: int, height: int, meta: dict) -> None:
        """Pure-background hard negative: EMPTY label file (Ultralytics
        treats it as a background image) + a minimal jsonl record with
        ``negative: true`` and no cube fields."""
        stem = Path(image_name).stem
        (self.out / "labels" / f"{stem}.txt").write_text("", encoding="utf-8")
        rec = {
            "image": f"images/{image_name}",
            "width": width,
            "height": height,
            "negative": True,
            "meta": meta,
        }
        self._jsonl.write(json.dumps(rec) + "\n")

    def close(self) -> None:
        self._jsonl.close()

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.close()
