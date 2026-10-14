#!/usr/bin/env python3
"""Refresh the pictures in the user guide (docs/guide/img) from the screenshot tour.

The user guide shows the real app, and the app changes. The pictures are not
photographs of anyone's screen: they are what `tests/system/tour_test.cpp`
renders from drawn camera frames (no webcam, nobody in them). This script
copies the ones the guide uses out of a tour run and saves them as WebP, which
is about a tenth the size of the PNGs and reads the same.

    1. Build the system tests and run the tour (docs/testing.md, "The screenshot
       tour"):
           cmake --build <build> --target cube_system_tests
           ctest --test-dir <build> -R system.tour
       It writes <build>/system-test-output/tour/{narrow,wide}/screens/*.png.
    2. python tools/docs/update_guide_screenshots.py <build>/system-test-output
    3. Look at them, then commit docs/guide/img.

Notes for whoever does step 2:

* The scan pictures in the guide were taken with `room()` in
  tests/unit/drawn_cube.hpp made a plain wall: the tour's own room is a grey
  field with forty coloured boxes (so the reader has something to ignore), and
  that reads as a glitch in a manual. To match, temporarily replace the body
  of `room()` with a vertical gradient from BGR (176, 182, 186) at the top to
  (130, 136, 142) at the bottom, and no boxes. Do not commit that change: the
  unit tests want the clutter.
* The tour runs at 900x640 (wide) and 390x844 (narrow), light and dark. The
  Settings page scrolls, so its pictures show the top of it.
* Needs Pillow (`pip install pillow`); nothing else in the repository does.

Usage:
    update_guide_screenshots.py TOUR_OUTPUT_DIR [--out DIR] [--quality N]
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

# (screen, layout, themes). A screen the app draws the same in both themes
# (the scan screen sits over the camera picture) needs only one.
USED = [
    ("launch", "wide", ("light", "dark")),
    ("launch", "narrow", ("light",)),
    ("scan-camera-unavailable", "wide", ("light",)),
    ("scan-looking", "wide", ("light",)),
    ("scan-reading", "wide", ("light",)),
    ("scan-three-sides", "wide", ("light",)),
    ("scan-cube-read", "wide", ("light",)),
    ("solve-start", "wide", ("light", "dark")),
    ("solve-middle", "wide", ("light", "dark")),
    ("solve-end", "wide", ("light", "dark")),
    ("complete", "wide", ("light",)),
    ("settings", "wide", ("light", "dark")),
    ("settings", "narrow", ("light",)),
]


def find_source(tour: Path, screen: str, layout: str, theme: str) -> Path | None:
    name = f"{screen}-{layout}-{theme}.png"
    for candidate in (
        tour / "tour" / layout / "screens" / name,
        tour / layout / "screens" / name,
        tour / layout / name,
        tour / name,
    ):
        if candidate.is_file():
            return candidate
    return None


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("tour_output", type=Path, help="the system-test output directory")
    parser.add_argument(
        "--out",
        type=Path,
        default=Path(__file__).resolve().parents[2] / "docs" / "guide" / "img",
        help="where the WebP files go (default: docs/guide/img)",
    )
    parser.add_argument("--quality", type=int, default=90, help="WebP quality (default 90)")
    args = parser.parse_args()

    try:
        from PIL import Image
    except ImportError:
        print("This needs Pillow: pip install pillow", file=sys.stderr)
        return 2

    args.out.mkdir(parents=True, exist_ok=True)
    missing = []
    written = 0
    for screen, layout, themes in USED:
        for theme in themes:
            source = find_source(args.tour_output, screen, layout, theme)
            if source is None:
                missing.append(f"{screen}-{layout}-{theme}.png")
                continue
            target = args.out / f"{screen}-{layout}-{theme}.webp"
            with Image.open(source) as picture:
                picture.convert("RGB").save(
                    target, "WEBP", quality=args.quality, method=6
                )
            print(f"{target.name}  {target.stat().st_size // 1024} KB")
            written += 1

    if missing:
        print("Not found in the tour output:", *missing, sep="\n  ", file=sys.stderr)
        return 1
    print(f"{written} pictures written to {args.out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
