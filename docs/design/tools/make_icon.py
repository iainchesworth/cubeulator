#!/usr/bin/env python3
"""Draw the app icon proposal into docs/design/identity/.

    python docs/design/tools/make_icon.py

The icon is the app's own drawing of a cube, in the flat technical-drawing
manner of the screens, caught with its top layer part-way through a turn.
The turning layer is the one red thing. Geometry is the isometric
projection CubeView.qml uses: x = (i - j) * h, y = (i + j) * u / 2 + k * u.

Colours come from tokens.json, so the icon cannot drift from the app.
"""
from __future__ import annotations

import json
import math
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "identity"
TOKENS = json.loads((ROOT / "tokens.json").read_text(encoding="utf-8"))["color"]
TURN = math.radians(20)   # how far round the top layer is


def icon(size, *, dark=False, detail=True, mono=False, background=True, scale=1.0):
    theme = "dark" if dark else "light"
    paper, ink, red = TOKENS["paper"][theme], TOKENS["ink"][theme], TOKENS["accent"][theme]
    if mono:
        paper, ink, red = "none", "#000000", "#000000"
    u = size * 0.125 * scale
    h = math.cos(math.radians(30)) * u
    cx, top = size / 2, size / 2 - 3 * u
    heavy, light = size * 0.027 * (1 if detail else 1.9), size * 0.0125

    def p(i, j, k):
        return (cx + (i - j) * h, top + (i + j) * 0.5 * u + k * u)

    def turned(i, j, k):
        di, dj = i - 1.5, j - 1.5
        c, s = math.cos(TURN), math.sin(TURN)
        return p(1.5 + di * c - dj * s, 1.5 + di * s + dj * c, k)

    def poly(points, fill, width=heavy):
        d = " ".join(f"{x:.1f},{y:.1f}" for x, y in points)
        return (f'<polygon points="{d}" fill="{fill}" stroke="{ink}" stroke-width="{width:.1f}" '
                f'stroke-linejoin="round"/>')

    def line(a, b, width=light):
        return (f'<line x1="{a[0]:.1f}" y1="{a[1]:.1f}" x2="{b[0]:.1f}" y2="{b[1]:.1f}" '
                f'stroke="{ink}" stroke-width="{width:.1f}" stroke-linecap="round"/>')

    out = [f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {size} {size}" width="{size}" height="{size}">']
    if background and not mono:
        out.append(f'<rect width="{size}" height="{size}" fill="{paper}"/>')

    # The two lower layers: the cut face on top, then the right and front sides.
    # In one colour the cut face stays open, or it merges with the layer above it.
    out.append(poly([p(0, 0, 1), p(3, 0, 1), p(3, 3, 1), p(0, 3, 1)], "none" if mono else ink))
    out.append(poly([p(3, 0, 1), p(3, 3, 1), p(3, 3, 3), p(3, 0, 3)], paper))
    out.append(poly([p(0, 3, 1), p(3, 3, 1), p(3, 3, 3), p(0, 3, 3)], paper))
    if detail:
        for n in (1, 2):
            out.append(line(p(3, n, 1), p(3, n, 3)))
            out.append(line(p(n, 3, 1), p(n, 3, 3)))
        out.append(line(p(3, 0, 2), p(3, 3, 2)))
        out.append(line(p(0, 3, 2), p(3, 3, 2)))

    # The top layer, turned. Two of its sides face the viewer.
    out.append(poly([turned(3, 0, 0), turned(3, 3, 0), turned(3, 3, 1), turned(3, 0, 1)], paper))
    out.append(poly([turned(0, 3, 0), turned(3, 3, 0), turned(3, 3, 1), turned(0, 3, 1)], paper))
    if detail:
        for n in (1, 2):
            out.append(line(turned(3, n, 0), turned(3, n, 1)))
            out.append(line(turned(n, 3, 0), turned(n, 3, 1)))
    out.append(poly([turned(0, 0, 0), turned(3, 0, 0), turned(3, 3, 0), turned(0, 3, 0)], red))
    if detail:
        for n in (1, 2):
            out.append(line(turned(n, 0, 0), turned(n, 3, 0)))
            out.append(line(turned(0, n, 0), turned(3, n, 0)))
    out.append("</svg>")
    return "\n".join(out) + "\n"


def main():
    OUT.mkdir(exist_ok=True)
    files = {
        "icon.svg": icon(1024),
        "icon-dark.svg": icon(1024, dark=True),
        "icon-small.svg": icon(64, detail=False, scale=1.12),
        "icon-mono.svg": icon(1024, mono=True, detail=False),
        # Android adaptive foreground: the drawing inside the middle 61% so every mask clears it.
        "icon-android-foreground.svg": icon(1024, background=False, scale=0.72),
    }
    for name, svg in files.items():
        (OUT / name).write_text(svg, encoding="utf-8", newline="\n")
    print("wrote " + ", ".join(files))


if __name__ == "__main__":
    main()
