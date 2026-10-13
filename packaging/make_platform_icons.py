#!/usr/bin/env python3
"""Turn the identity SVGs into the icon files each desktop platform wants.

    python packaging/make_platform_icons.py \\
        --identity docs/design/identity --out build/icons

Reads ``icon.svg`` and ``icon-small.svg`` (drawn by docs/design/tools/make_icon.py
from the colours in tokens.json) and writes, under --out:

    Cubeulator.ico                          Windows: 16 20 24 32 40 48 64 256
    Cubeulator.icns                         macOS: 16 to 1024, the artwork at 80%
                                            on a rounded square of the paper colour
    png/icon-<n>.png                        the window icon, 16 to 256
    hicolor/scalable/apps/<id>.svg          Linux, the icon theme layout
    hicolor/<n>x<n>/apps/<id>.png           Linux: 48 64 128 256

Nothing here is drawn by hand: every pixel comes from the SVGs. The small sizes
(16, 20, 24, 32) use icon-small.svg, which drops the grid lines and thickens the
outline, as identity/README.md says.

The rasteriser is a small exact-area one so that the script needs nothing but
the Python standard library and gives byte-identical files on every platform.
It understands the vocabulary make_icon.py emits - <rect>, <polygon>, <line>,
solid fills, strokes with round joins and round or butt caps - and stops with an
error on anything else, so a change of drawing style cannot be rendered wrongly
in silence.
"""
from __future__ import annotations

import argparse
import math
import re
import struct
import sys
import zlib
from array import array
from pathlib import Path
from xml.etree import ElementTree

SVG_NS = "{http://www.w3.org/2000/svg}"

WINDOWS_SIZES = (16, 20, 24, 32, 40, 48, 64, 256)
WINDOWS_SMALL = (16, 20, 24, 32)          # drawn from icon-small.svg
WINDOW_ICON_SIZES = (16, 20, 24, 32, 48, 64, 128, 256)
HICOLOR_SIZES = (48, 64, 128, 256)
ICNS_SIZES = (16, 32, 64, 128, 256, 512, 1024)
ICNS_SMALL = (16, 32)                     # drawn from icon-small.svg

# The macOS icon grid: an 824 px rounded square centred on a 1024 px canvas,
# corner radius 22.37% of the square. The artwork sits inside it at 80%.
MAC_SQUARE = 824 / 1024
MAC_RADIUS = 0.2237
MAC_ARTWORK = 0.80

# (icns type, pixel size). The @2x types repeat the next size up.
ICNS_TYPES = (
    (b"icp4", 16), (b"icp5", 32), (b"icp6", 64),
    (b"ic07", 128), (b"ic08", 256), (b"ic09", 512), (b"ic10", 1024),
    (b"ic11", 32), (b"ic12", 64), (b"ic13", 256), (b"ic14", 512),
)


# ---------------------------------------------------------------------------
# SVG subset
# ---------------------------------------------------------------------------

class Shape:
    """One drawable: a closed polygon or a line, with optional fill and stroke."""

    def __init__(self, points, closed, fill, stroke, stroke_width, linecap):
        self.points = points
        self.closed = closed
        self.fill = fill                  # (r, g, b) in 0..1, or None
        self.stroke = stroke              # (r, g, b) in 0..1, or None
        self.stroke_width = stroke_width
        self.linecap = linecap


def parse_colour(value):
    if value is None or value.strip().lower() == "none":
        return None
    m = re.fullmatch(r"#([0-9a-fA-F]{6})", value.strip())
    if not m:
        raise ValueError(f"unsupported colour {value!r} (only #RRGGBB and none)")
    n = int(m.group(1), 16)
    return ((n >> 16) / 255.0, ((n >> 8) & 255) / 255.0, (n & 255) / 255.0)


def parse_svg(path: Path):
    """Returns (view_box_width, view_box_height, [Shape])."""
    root = ElementTree.parse(path).getroot()
    if root.tag != SVG_NS + "svg":
        raise ValueError(f"{path}: not an SVG document")
    vb = [float(v) for v in root.get("viewBox", "").replace(",", " ").split()]
    if len(vb) != 4 or vb[0] != 0 or vb[1] != 0:
        raise ValueError(f"{path}: needs a viewBox starting at 0 0")
    shapes = []
    for el in root:
        tag = el.tag.replace(SVG_NS, "")
        fill = parse_colour(el.get("fill", "#000000"))
        stroke = parse_colour(el.get("stroke"))
        width = float(el.get("stroke-width", "1"))
        join = el.get("stroke-linejoin", "miter")
        cap = el.get("stroke-linecap", "butt")
        if stroke is not None and tag == "polygon" and join != "round":
            raise ValueError(f"{path}: only round stroke-linejoin is supported, got {join!r}")
        if cap not in ("round", "butt"):
            raise ValueError(f"{path}: unsupported stroke-linecap {cap!r}")
        if tag == "rect":
            if el.get("rx") or el.get("ry") or el.get("transform"):
                raise ValueError(f"{path}: rounded or transformed <rect> is not supported")
            x, y = float(el.get("x", 0)), float(el.get("y", 0))
            w, h = float(el.get("width")), float(el.get("height"))
            pts = [(x, y), (x + w, y), (x + w, y + h), (x, y + h)]
            shapes.append(Shape(pts, True, fill, stroke, width, cap))
        elif tag == "polygon":
            nums = [float(v) for v in re.split(r"[\s,]+", el.get("points").strip())]
            pts = list(zip(nums[0::2], nums[1::2]))
            shapes.append(Shape(pts, True, fill, stroke, width, cap))
        elif tag == "line":
            pts = [(float(el.get("x1")), float(el.get("y1"))),
                   (float(el.get("x2")), float(el.get("y2")))]
            shapes.append(Shape(pts, False, None, stroke, width, cap))
        else:
            raise ValueError(f"{path}: unsupported element <{tag}>")
    return vb[2], vb[3], shapes


# ---------------------------------------------------------------------------
# Exact-area scanline coverage (signed-area accumulation)
# ---------------------------------------------------------------------------

def _orient(poly):
    """Counter-clockwise, so that overlapping pieces add up rather than cancel."""
    area = 0.0
    for (x0, y0), (x1, y1) in zip(poly, poly[1:] + poly[:1]):
        area += x0 * y1 - x1 * y0
    return poly if area >= 0 else poly[::-1]


def _accumulate_edge(acc, stride, h, w, ax, ay, bx, by):
    if ay == by:
        return
    if ay < by:
        direction, x0, y0, x1, y1 = 1.0, ax, ay, bx, by
    else:
        direction, x0, y0, x1, y1 = -1.0, bx, by, ax, ay
    dxdy = (x1 - x0) / (y1 - y0)
    x = x0
    if y0 < 0:
        x -= y0 * dxdy
        y_start = 0
    else:
        y_start = int(y0)
    y_end = min(h, int(math.ceil(y1)))
    wf = float(w)
    for y in range(y_start, y_end):
        line = y * stride
        dy = min(y + 1, y1) - max(y, y0)
        x_next = min(max(x + dxdy * dy, 0.0), wf)
        d = dy * direction
        if x < x_next:
            xa, xb = x, x_next
        else:
            xa, xb = x_next, x
        xa_floor = math.floor(xa)
        xai = int(xa_floor)
        xb_ceil = math.ceil(xb)
        xbi = int(xb_ceil)
        if xbi <= xai + 1:
            xmf = 0.5 * (x + x_next) - xa_floor
            acc[line + xai] += d - d * xmf
            acc[line + xai + 1] += d * xmf
        else:
            s = 1.0 / (xb - xa)
            xaf = xa - xa_floor
            a0 = 0.5 * s * (1.0 - xaf) * (1.0 - xaf)
            xbf = xb - xb_ceil + 1.0
            am = 0.5 * s * xbf * xbf
            acc[line + xai] += d * a0
            if xbi == xai + 2:
                acc[line + xai + 1] += d * (1.0 - a0 - am)
            else:
                a1 = s * (1.5 - xaf)
                acc[line + xai + 1] += d * (a1 - a0)
                for xi in range(xai + 2, xbi - 1):
                    acc[line + xi] += d * s
                a2 = a1 + (xbi - xai - 3) * s
                acc[line + xbi - 1] += d * (1.0 - a2 - am)
            acc[line + xbi] += d * am
        x = x_next


def coverage(polys, width, height):
    """Coverage of the union of polygons, as (x0, y0, w, h, rows) or None.

    rows is a list of h lists of w floats in 0..1. Everything outside the
    returned box is uncovered.
    """
    polys = [_orient(p) for p in polys if len(p) >= 3]
    if not polys:
        return None
    xs = [x for p in polys for x, _ in p]
    ys = [y for p in polys for _, y in p]
    bx0 = max(0, int(math.floor(min(xs))) - 1)
    by0 = max(0, int(math.floor(min(ys))) - 1)
    bx1 = min(width, int(math.ceil(max(xs))) + 1)
    by1 = min(height, int(math.ceil(max(ys))) + 1)
    w, h = bx1 - bx0, by1 - by0
    if w <= 0 or h <= 0:
        return None
    stride = w + 2
    acc = array("d", bytes(8 * stride * h))
    for poly in polys:
        pts = [(min(max(x - bx0, 0.0), float(w)), y - by0) for x, y in poly]
        for (ax, ay), (bx, by) in zip(pts, pts[1:] + pts[:1]):
            _accumulate_edge(acc, stride, h, w, ax, ay, bx, by)
    rows = []
    for y in range(h):
        line = y * stride
        total = 0.0
        row = [0.0] * w
        for x in range(w):
            total += acc[line + x]
            c = total if total >= 0 else -total
            row[x] = 1.0 if c > 1.0 else c
        rows.append(row)
    return bx0, by0, w, h, rows


def disc(cx, cy, r):
    n = max(12, min(64, int(math.ceil(2 * math.pi * r))))
    return [(cx + r * math.cos(2 * math.pi * i / n), cy + r * math.sin(2 * math.pi * i / n))
            for i in range(n)]


def stroke_polygons(points, closed, width, linecap):
    """A stroke as the union of one quad per segment plus a disc per round join/cap."""
    half = width / 2.0
    segments = list(zip(points, points[1:] + (points[:1] if closed else [])))
    polys = []
    for (x0, y0), (x1, y1) in segments:
        dx, dy = x1 - x0, y1 - y0
        length = math.hypot(dx, dy)
        if length == 0:
            continue
        nx, ny = -dy / length * half, dx / length * half
        polys.append([(x0 + nx, y0 + ny), (x1 + nx, y1 + ny), (x1 - nx, y1 - ny), (x0 - nx, y0 - ny)])
    if closed:
        round_at = points                      # round joins at every vertex
    elif linecap == "round":
        round_at = [points[0], points[-1]]     # round caps at the two ends
    else:
        round_at = []
    polys.extend(disc(x, y, half) for x, y in round_at)
    return polys


# ---------------------------------------------------------------------------
# Canvas
# ---------------------------------------------------------------------------

class Canvas:
    """Premultiplied float RGBA."""

    def __init__(self, size):
        self.size = size
        n = size * size
        self.r = array("f", bytes(4 * n))
        self.g = array("f", bytes(4 * n))
        self.b = array("f", bytes(4 * n))
        self.a = array("f", bytes(4 * n))

    def paint(self, cov, colour):
        if cov is None or colour is None:
            return
        x0, y0, w, h, rows = cov
        cr, cg, cb = colour
        size = self.size
        R, G, B, A = self.r, self.g, self.b, self.a
        for j in range(h):
            base = (y0 + j) * size + x0
            row = rows[j]
            for i in range(w):
                c = row[i]
                if c <= 0.0:
                    continue
                k = base + i
                if c >= 0.99999:
                    R[k], G[k], B[k], A[k] = cr, cg, cb, 1.0
                else:
                    inv = 1.0 - c
                    R[k] = cr * c + R[k] * inv
                    G[k] = cg * c + G[k] * inv
                    B[k] = cb * c + B[k] * inv
                    A[k] = c + A[k] * inv

    def rgba8(self):
        out = bytearray(4 * self.size * self.size)
        R, G, B, A = self.r, self.g, self.b, self.a
        for k in range(self.size * self.size):
            a = A[k]
            if a <= 0.0:
                continue
            o = 4 * k
            out[o] = min(255, int(R[k] / a * 255.0 + 0.5))
            out[o + 1] = min(255, int(G[k] / a * 255.0 + 0.5))
            out[o + 2] = min(255, int(B[k] / a * 255.0 + 0.5))
            out[o + 3] = min(255, int(a * 255.0 + 0.5))
        return bytes(out)


def draw(canvas, svg, scale, ox, oy):
    """Draw parsed SVG onto the canvas: user unit -> pixel is x * scale + ox."""
    _, _, shapes = svg
    size = canvas.size
    for shape in shapes:
        pts = [(x * scale + ox, y * scale + oy) for x, y in shape.points]
        if shape.fill is not None:
            canvas.paint(coverage([pts], size, size), shape.fill)
        if shape.stroke is not None:
            polys = stroke_polygons(pts, shape.closed, shape.stroke_width * scale, shape.linecap)
            canvas.paint(coverage(polys, size, size), shape.stroke)


def rounded_square(x, y, side, radius):
    pts = []
    steps = max(8, int(radius))
    for cx, cy, start in ((x + side - radius, y + radius, -90), (x + side - radius, y + side - radius, 0),
                          (x + radius, y + side - radius, 90), (x + radius, y + radius, 180)):
        for i in range(steps + 1):
            a = math.radians(start + 90.0 * i / steps)
            pts.append((cx + radius * math.cos(a), cy + radius * math.sin(a)))
    return pts


def render_plain(svg, size):
    """The SVG at size x size, filling the canvas."""
    canvas = Canvas(size)
    draw(canvas, svg, size / svg[0], 0.0, 0.0)
    return canvas.rgba8()


def render_mac(svg, size, paper):
    """macOS: the artwork at 80% on a rounded square of the paper colour."""
    canvas = Canvas(size)
    side = size * MAC_SQUARE
    left = (size - side) / 2.0
    canvas.paint(coverage([rounded_square(left, left, side, side * MAC_RADIUS)], size, size), paper)
    art = side * MAC_ARTWORK
    offset = left + (side - art) / 2.0
    draw(canvas, svg, art / svg[0], offset, offset)
    return canvas.rgba8()


# ---------------------------------------------------------------------------
# File formats
# ---------------------------------------------------------------------------

def png_bytes(rgba, size):
    def chunk(kind, data):
        body = kind + data
        return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF)

    stride = size * 4
    raw = b"".join(b"\x00" + rgba[y * stride:(y + 1) * stride] for y in range(size))
    return (b"\x89PNG\r\n\x1a\n"
            + chunk(b"IHDR", struct.pack(">IIBBBBB", size, size, 8, 6, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(raw, 9))
            + chunk(b"IEND", b""))


def ico_bytes(images):
    """images: [(size, rgba)]. Every entry is a plain 32-bit DIB, not PNG, so that
    NSIS, rc.exe and the shell all accept it, 256 px included."""
    entries, blobs = [], []
    offset = 6 + 16 * len(images)
    for size, rgba in images:
        stride = size * 4
        rows = []
        for y in range(size - 1, -1, -1):          # DIBs are stored bottom-up, as BGRA
            row = rgba[y * stride:(y + 1) * stride]
            bgra = bytearray(row)
            bgra[0::4], bgra[2::4] = row[2::4], row[0::4]
            rows.append(bytes(bgra))
        mask = bytes(((size + 31) // 32) * 4 * size)  # the AND mask: all opaque, alpha does the work
        pixels = b"".join(rows)
        header = struct.pack("<IiiHHIIiiII", 40, size, size * 2, 1, 32, 0, len(pixels) + len(mask), 0, 0, 0, 0)
        blob = header + pixels + mask
        entries.append(struct.pack("<BBBBHHII", size % 256, size % 256, 0, 0, 1, 32, len(blob), offset))
        blobs.append(blob)
        offset += len(blob)
    return struct.pack("<HHH", 0, 1, len(images)) + b"".join(entries) + b"".join(blobs)


def icns_bytes(pngs):
    """pngs: {size: png bytes}."""
    body = b"".join(kind + struct.pack(">I", 8 + len(pngs[size])) + pngs[size] for kind, size in ICNS_TYPES)
    return b"icns" + struct.pack(">I", 8 + len(body)) + body


# ---------------------------------------------------------------------------

def paper_colour(svg_path: Path):
    """The paper colour is the full-bleed rect that icon.svg begins with."""
    first = next(iter(ElementTree.parse(svg_path).getroot()))
    return parse_colour(first.get("fill"))


def build(identity: Path, out: Path, basename: str, app_id: str):
    full_path, small_path = identity / "icon.svg", identity / "icon-small.svg"
    for p in (full_path, small_path):
        if not p.is_file():
            raise SystemExit(f"missing {p}")
    full, small = parse_svg(full_path), parse_svg(small_path)
    paper = paper_colour(full_path)

    cache = {}

    def plain(svg_name, size):
        key = (svg_name, size)
        if key not in cache:
            cache[key] = render_plain(small if svg_name == "small" else full, size)
        return cache[key]

    def write(path: Path, data: bytes):
        path.parent.mkdir(parents=True, exist_ok=True)
        # Leave an unchanged file alone so that build systems do not see a new timestamp.
        if not path.exists() or path.read_bytes() != data:
            path.write_bytes(data)
        print(f"  {path}")

    which = lambda size, small_sizes: "small" if size in small_sizes else "full"  # noqa: E731

    write(out / f"{basename}.ico",
          ico_bytes([(n, plain(which(n, WINDOWS_SMALL), n)) for n in WINDOWS_SIZES]))

    mac_pngs = {}
    for n in ICNS_SIZES:
        art = small if n in ICNS_SMALL else full
        mac_pngs[n] = png_bytes(render_mac(art, n, paper), n)
    write(out / f"{basename}.icns", icns_bytes(mac_pngs))

    for n in WINDOW_ICON_SIZES:
        write(out / "png" / f"icon-{n}.png", png_bytes(plain(which(n, WINDOWS_SMALL), n), n))

    write(out / "hicolor" / "scalable" / "apps" / f"{app_id}.svg", full_path.read_bytes())
    for n in HICOLOR_SIZES:
        write(out / "hicolor" / f"{n}x{n}" / "apps" / f"{app_id}.png", png_bytes(plain("full", n), n))


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--identity", type=Path, required=True, help="docs/design/identity")
    ap.add_argument("--out", type=Path, required=True, help="output directory")
    ap.add_argument("--basename", default="Cubeulator", help="file name of the .ico and .icns")
    ap.add_argument("--app-id", default="com.iainchesworthlabs.cubeulator", help="Linux icon name")
    args = ap.parse_args(argv)
    build(args.identity, args.out, args.basename, args.app_id)
    return 0


if __name__ == "__main__":
    sys.exit(main())
