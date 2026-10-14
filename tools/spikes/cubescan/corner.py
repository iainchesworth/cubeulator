"""Spike for recovery plan R5(b): two or three sides in one view.

A cube turned corner-on shows three sides at once, each a sheared 3x3 lattice,
and where two lattices meet they share an edge. That shared edge is the one
thing six square-on reads cannot give: which way up one side is relative to
its neighbour.

  frame -> sticker blobs (looser shape test than face-on)
        -> up to three 3x3 affine lattices, by RANSAC over blob triples
        -> which pairs share an edge, and which edge on each

Run:  python corner.py <session-dir> <frame-index> [<frame-index> ...]
      writes <scratch>/corner_<n>.png with the lattices drawn on the frame.
"""
import itertools
import math
import os
import sys

import cv2
import numpy as np


def blobs_oblique(bgr):
    """Sticker-sized compact quads, allowing the shear of a side seen at an angle."""
    h, w = bgr.shape[:2]
    v = cv2.GaussianBlur(bgr.max(axis=2), (5, 5), 0)
    block = (int(min(h, w) * 0.12) | 1)
    binary = cv2.adaptiveThreshold(v, 255, cv2.ADAPTIVE_THRESH_MEAN_C,
                                   cv2.THRESH_BINARY, block, 8)
    binary = cv2.morphologyEx(binary, cv2.MORPH_OPEN, np.ones((3, 3), np.uint8))
    contours, _ = cv2.findContours(binary, cv2.RETR_LIST, cv2.CHAIN_APPROX_SIMPLE)
    lo, hi = h * w * 0.0003, h * w * 0.03
    out = []
    for c in contours:
        area = cv2.contourArea(c)
        if not lo < area < hi:
            continue
        hull = cv2.convexHull(c)
        harea = cv2.contourArea(hull)
        if harea <= 0 or area / harea < 0.85:           # compact, not ragged
            continue
        poly = cv2.approxPolyDP(hull, 0.08 * cv2.arcLength(hull, True), True)
        if len(poly) != 4:                              # a quadrilateral
            continue
        m = cv2.moments(hull)
        if m["m00"] <= 0:
            continue
        cx, cy = m["m10"] / m["m00"], m["m01"] / m["m00"]
        (_, _), (rw, rh), _ = cv2.minAreaRect(hull)
        if min(rw, rh) < 1 or max(rw, rh) / min(rw, rh) > 2.4:
            continue
        out.append((cx, cy, math.sqrt(area)))
    return out


def lattice_through(blobs, i, j, k, tol=0.3):
    """Hypothesis: blob i is a cell, j is its right-hand neighbour, k the one
    below. Returns (cells, rms) for the 3x3 window holding most blobs."""
    p = np.array([(b[0], b[1]) for b in blobs], np.float64)
    u, v = p[j] - p[i], p[k] - p[i]
    det = u[0] * v[1] - u[1] * v[0]
    if abs(det) < 1e-6:
        return None
    if abs(det) / (np.linalg.norm(u) * np.linalg.norm(v)) < 0.35:   # nearly collinear
        return None
    inv = np.linalg.inv(np.column_stack([u, v]))
    ab = (p - p[i]) @ inv.T
    ia = np.rint(ab).astype(int)
    err = np.abs(ab - ia).max(axis=1)
    keep = np.where((err < tol) & (np.abs(ia).max(axis=1) <= 2))[0]
    if len(keep) < 6:
        return None
    best = None
    for oa in range(-2, 1):
        for ob in range(-2, 1):
            inside = [n for n in keep
                      if oa <= ia[n][0] <= oa + 2 and ob <= ia[n][1] <= ob + 2]
            cells = {(ia[n][0] - oa, ia[n][1] - ob): n for n in inside}
            if len(cells) < len(inside):
                continue                                 # two blobs on one cell
            span_a = max(c[0] for c in cells) - min(c[0] for c in cells) if cells else -1
            span_b = max(c[1] for c in cells) - min(c[1] for c in cells) if cells else -1
            if span_a != 2 or span_b != 2:
                continue
            if best is None or len(cells) > len(best):
                best = cells
    if best is None or len(best) < 6:
        return None
    idx = list(best.values())
    A = np.array([[c[0], c[1], 1.0] for c in best])
    M, *_ = np.linalg.lstsq(A, p[idx], rcond=None)       # [col row 1] -> xy
    resid = np.linalg.norm(A @ M - p[idx], axis=1)
    scale = 0.5 * (np.linalg.norm(M[0]) + np.linalg.norm(M[1]))
    return best, M, float(np.sqrt((resid ** 2).mean()) / scale)


def find_lattices(blobs, max_lattices=3):
    remaining = list(range(len(blobs)))
    found = []
    for _ in range(max_lattices):
        sub = [blobs[n] for n in remaining]
        best = None
        for i in range(len(sub)):
            near = [n for n in range(len(sub)) if n != i and
                    math.hypot(sub[n][0] - sub[i][0], sub[n][1] - sub[i][1]) < 1.9 * sub[i][2]]
            for j, k in itertools.permutations(near, 2):
                res = lattice_through(sub, i, j, k)
                if res is None:
                    continue
                cells, M, rms = res
                if rms > 0.14:
                    continue
                key = (len(cells), -rms)
                if best is None or key > best[0]:
                    best = (key, cells, M, rms)
        if best is None:
            break
        _, cells, M, rms = best
        used = {remaining[n] for n in cells.values()}
        found.append(dict(M=M, n=len(cells), rms=rms,
                          cells={c: sub[n][:2] for c, n in cells.items()}))
        remaining = [n for n in remaining if n not in used]
    return found


def corners_of(M):
    """Image positions of a lattice's four outer corners, in (col,row) order
    (-0.5,-0.5) (2.5,-0.5) (2.5,2.5) (-0.5,2.5)."""
    pts = np.array([[-0.5, -0.5, 1], [2.5, -0.5, 1], [2.5, 2.5, 1], [-0.5, 2.5, 1]])
    return pts @ M


def shared_edges(lattices, tol=0.45):
    """Pairs of lattices that have an edge in common: two outer corners within
    `tol` sticker pitches of two of the other's."""
    out = []
    for a, b in itertools.combinations(range(len(lattices)), 2):
        ca, cb = corners_of(lattices[a]["M"]), corners_of(lattices[b]["M"])
        pitch = 0.5 * (np.linalg.norm(lattices[a]["M"][0]) + np.linalg.norm(lattices[a]["M"][1]))
        match = []
        for ia in range(4):
            for ib in range(4):
                if np.linalg.norm(ca[ia] - cb[ib]) < tol * pitch * 1.0:
                    match.append((ia, ib))
        if len(match) >= 2:
            out.append((a, b, match))
    return out


def main():
    session, frames = sys.argv[1], [int(x) for x in sys.argv[2:]]
    names = sorted(f for f in os.listdir(session) if f.startswith("rec_") and f.endswith(".jpg"))
    scratch = os.environ.get("SCRATCH", ".")
    for n in frames:
        bgr = cv2.imread(os.path.join(session, names[n]))
        blobs = blobs_oblique(bgr)
        lattices = find_lattices(blobs)
        edges = shared_edges(lattices)
        print("frame %d: %d blobs, %d lattices %s, shared edges %s" % (
            n, len(blobs), len(lattices),
            [(l["n"], round(l["rms"], 3)) for l in lattices],
            [(a, b, len(m)) for a, b, m in edges]))
        view = bgr.copy()
        for b in blobs:
            cv2.circle(view, (int(b[0]), int(b[1])), 2, (0, 255, 255), -1)
        palette = [(0, 255, 0), (255, 128, 0), (255, 0, 255)]
        for li, lat in enumerate(lattices):
            col = palette[li % 3]
            quad = corners_of(lat["M"]).astype(np.int32)
            cv2.polylines(view, [quad], True, col, 1, cv2.LINE_AA)
            for (c, r), xy in lat["cells"].items():
                cv2.putText(view, "%d%d" % (c, r), (int(xy[0]) - 6, int(xy[1]) + 3),
                            cv2.FONT_HERSHEY_PLAIN, 0.7, col, 1)
        cv2.imwrite(os.path.join(scratch, "corner_%d.png" % n), view)


if __name__ == "__main__":
    main()
