#!/usr/bin/env python3
"""Measure every colour pairing in docs/design/tokens.json.

Prints the tables that docs/design/system.md and accessibility.md quote, and
exits non-zero if any pairing misses its target. Run from anywhere:

    python docs/design/tools/check_tokens.py            # check, print summary
    python docs/design/tools/check_tokens.py --markdown # print the tables

Contrast is WCAG 2.x relative-luminance contrast. Colour-vision figures use
the Machado, Oliveira and Fernandes (2009) matrices at full severity, with
distances in OKLab x 100.
"""
from __future__ import annotations

import itertools
import json
import math
import sys
from pathlib import Path

TOKENS = Path(__file__).resolve().parent.parent / "tokens.json"

# The worst thing a camera can put behind an overlay panel is pure white.
WORST_BEHIND = "#FFFFFF"
# Below this OKLab x 100 distance two swatches are not reliably told apart.
TELLABLE = 10.0

CVD = {
    "normal": [[1, 0, 0], [0, 1, 0], [0, 0, 1]],
    "protanopia": [[0.152286, 1.052583, -0.204868], [0.114503, 0.786281, 0.099216], [-0.003882, -0.048116, 1.051998]],
    "deuteranopia": [[0.367322, 0.860646, -0.227968], [0.280085, 0.672501, 0.047413], [-0.011820, 0.042940, 0.968881]],
    "tritanopia": [[1.255528, -0.076749, -0.178779], [-0.078411, 0.930809, 0.147602], [0.004733, 0.691367, 0.303900]],
}
KINDS = ["normal", "protanopia", "deuteranopia", "tritanopia", "no colour"]


def rgb(hex_):
    h = hex_.lstrip("#")
    return tuple(int(h[i:i + 2], 16) for i in (0, 2, 4))


def linear(c):
    c /= 255
    return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4


def luminance(hex_):
    r, g, b = (linear(x) for x in rgb(hex_))
    return 0.2126 * r + 0.7152 * g + 0.0722 * b


def contrast(a, b):
    la, lb = sorted((luminance(a), luminance(b)), reverse=True)
    return (la + 0.05) / (lb + 0.05)


def blend(fg, bg, alpha):
    f, b = rgb(fg), rgb(bg)
    return "#%02X%02X%02X" % tuple(round(alpha * f[i] + (1 - alpha) * b[i]) for i in range(3))


def simulate(hex_, kind):
    r, g, b = (linear(x) for x in rgb(hex_))
    if kind == "no colour":
        y = 0.2126 * r + 0.7152 * g + 0.0722 * b
        return (y, y, y)
    m = CVD[kind]
    return tuple(min(1.0, max(0.0, m[i][0] * r + m[i][1] * g + m[i][2] * b)) for i in range(3))


def oklab(lrgb):
    r, g, b = lrgb
    l = (0.4122214708 * r + 0.5363325363 * g + 0.0514459929 * b) ** (1 / 3)
    m = (0.2119034982 * r + 0.6806995451 * g + 0.1073969566 * b) ** (1 / 3)
    s = (0.0883024619 * r + 0.2817188376 * g + 0.6299787005 * b) ** (1 / 3)
    return (0.2104542553 * l + 0.7936177850 * m - 0.0040720468 * s,
            1.9779984951 * l - 2.4285922050 * m + 0.4505937099 * s,
            0.0259040371 * l + 0.7827717662 * m - 0.8086757660 * s)


def distance(a, b, kind):
    return 100 * math.dist(oklab(simulate(a, kind)), oklab(simulate(b, kind)))


def main():
    markdown = "--markdown" in sys.argv
    tokens = json.loads(TOKENS.read_text(encoding="utf-8"))
    color = tokens["color"]
    failures = []
    out = []

    # ---- theme pairings ----------------------------------------------------
    out.append("### Text and control pairings\n")
    out.append("| foreground | background | use | target | light | dark |")
    out.append("|---|---|---|---|---|---|")
    for fg, bg, target, use in tokens["pairs"]:
        cells = []
        for theme in ("light", "dark"):
            ratio = contrast(color[fg][theme], color[bg][theme])
            ok = ratio >= target
            if not ok:
                failures.append(f"{fg} on {bg} ({theme}): {ratio:.2f} < {target}")
            cells.append(f"{ratio:.2f}" + ("" if ok else " FAIL"))
        out.append(f"| `{fg}` | `{bg}` | {use} | {target}:1 | {cells[0]} | {cells[1]} |")

    # ---- camera overlay ----------------------------------------------------
    ov = tokens["overlay"]
    panel = blend(ov["panel"]["value"], WORST_BEHIND, ov["panel"]["alpha"])
    out.append("\n### Over the camera\n")
    out.append(f"Panel `{ov['panel']['value']}` at {ov['panel']['alpha']:.2f} opacity. "
               f"Measured against the worst case, a pure white picture behind it, "
               f"where the panel reads as `{panel}`.\n")
    out.append("| foreground | use | target | ratio |")
    out.append("|---|---|---|---|")
    for name, target in (("ink", 4.5), ("inkSecondary", 4.5), ("ok", 4.5), ("caution", 4.5), ("outline", 3.0)):
        ratio = contrast(ov[name]["value"], panel)
        ok = ratio >= target
        if not ok:
            failures.append(f"overlay.{name} on panel: {ratio:.2f} < {target}")
        out.append(f"| `overlay.{name}` | {ov[name]['use']} | {target}:1 | {ratio:.2f}" + ("" if ok else " FAIL") + " |")

    # ---- cube colours ------------------------------------------------------
    cube = {k: v for k, v in tokens["cube"].items() if not k.startswith("$")}
    out.append("\n### Cube colours: mark against sticker, sticker against plastic\n")
    out.append("| colour | value | mark | mark ink on sticker (3:1) | sticker on `cubeBody` |")
    out.append("|---|---|---|---|---|")
    body = color["cubeBody"]["light"]
    for name, c in cube.items():
        if c["mark"] == "none":
            mark = "-"
        else:
            ratio = contrast(c["markInk"], c["value"])
            if ratio < 3.0:
                failures.append(f"mark on {name}: {ratio:.2f} < 3.0")
            mark = f"{ratio:.2f}" + ("" if ratio >= 3.0 else " FAIL")
        out.append(f"| {name} | `{c['value']}` | {c['mark']} | {mark} | {contrast(c['value'], body):.2f} |")

    out.append("\n### Cube colours: how far apart each pair is\n")
    out.append("OKLab distance x 100 between two swatches as seen with each kind of colour "
               f"vision. Under {TELLABLE:.0f} is not reliably tellable from the swatch alone; "
               "the mark has to carry it.\n")
    out.append("| pair | " + " | ".join(KINDS) + " |")
    out.append("|---|" + "---|" * len(KINDS))
    worst = {k: (999.0, "") for k in KINDS}
    for a, b in itertools.combinations(cube, 2):
        row = []
        for k in KINDS:
            d = distance(cube[a]["value"], cube[b]["value"], k)
            if d < worst[k][0]:
                worst[k] = (d, f"{a}/{b}")
            row.append(f"**{d:.1f}**" if d < TELLABLE else f"{d:.1f}")
        out.append(f"| {a} / {b} | " + " | ".join(row) + " |")
    out.append("| closest pair | " + " | ".join(f"{worst[k][0]:.1f} ({worst[k][1]})" for k in KINDS) + " |")

    # Marks must differ within every pair that hue alone cannot separate.
    marks = [c["mark"] for c in cube.values()]
    if len(set(marks)) != len(marks):
        failures.append("two cube colours share a mark")

    if markdown:
        print("\n".join(out))
    else:
        checked = len(tokens["pairs"]) * 2 + 5 + len(cube)
        print(f"{checked} pairings measured, {len(failures)} below target")
        for k in KINDS:
            print(f"  closest cube pair, {k}: {worst[k][0]:.1f} ({worst[k][1]})")
    for f in failures:
        print("FAIL " + f, file=sys.stderr)
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
