"""Throwaway spike: read a Rubik's cube from a recorded session with a plain
face-on 3x3 lattice reader. No 6-DoF pose, no tracker, no learned model.

  frame -> sticker blobs -> 3x3 lattice -> 9 Lab samples        (per frame)
  reads -> grouped by whole-face signature (roll-invariant)     (per session)
  54 Lab -> 6 colours x 9, seeded by the six centres            (joint)
  6 faces x 4 rolls -> the physically valid cube(s)             (4096 search)
"""
import itertools
import json
import math
import os
import sys

import cv2
import numpy as np
from scipy.optimize import linear_sum_assignment

# ---------------------------------------------------------------- per frame

def sticker_blobs(bgr):
    """Bright-ish compact squares bounded by darker plastic."""
    h, w = bgr.shape[:2]
    v = cv2.GaussianBlur(bgr.max(axis=2), (5, 5), 0)
    block = (int(min(h, w) * 0.12) | 1)
    binary = cv2.adaptiveThreshold(v, 255, cv2.ADAPTIVE_THRESH_MEAN_C,
                                   cv2.THRESH_BINARY, block, 8)
    binary = cv2.morphologyEx(binary, cv2.MORPH_OPEN, np.ones((3, 3), np.uint8))
    contours, _ = cv2.findContours(binary, cv2.RETR_LIST, cv2.CHAIN_APPROX_SIMPLE)
    lo, hi = h * w * 0.0006, h * w * 0.03
    out = []
    for c in contours:
        area = cv2.contourArea(c)
        if not lo < area < hi:
            continue
        (cx, cy), (rw, rh), _ = cv2.minAreaRect(c)
        if min(rw, rh) < 1 or max(rw, rh) / min(rw, rh) > 1.45:
            continue
        if area / (rw * rh) < 0.72:
            continue
        out.append((cx, cy, math.sqrt(rw * rh)))
    return out


def fit_lattice(blobs):
    """Best 3x3 lattice through the blobs. Returns (affine 2x3, n_inliers, rms/pitch)."""
    best = None
    pts_all = np.array([(b[0], b[1]) for b in blobs], np.float64)
    for seed in blobs:
        grp = [b for b in blobs
               if 0.72 < b[2] / seed[2] < 1.4
               and math.hypot(b[0] - seed[0], b[1] - seed[1]) < 3.4 * seed[2]]
        if len(grp) < 6:
            continue
        pts = np.array([(b[0], b[1]) for b in grp], np.float64)
        d = np.linalg.norm(pts[:, None] - pts[None], axis=2)
        np.fill_diagonal(d, 1e9)
        nn = d.argmin(axis=1)
        pitch = float(np.median(d.min(axis=1)))
        if pitch < 4:
            continue
        vec = pts[nn] - pts
        ang = np.arctan2(vec[:, 1], vec[:, 0]) * 4.0          # fold mod 90deg
        theta = math.atan2(np.sin(ang).mean(), np.cos(ang).mean()) / 4.0
        e1 = np.array([math.cos(theta), math.sin(theta)])
        e2 = np.array([-e1[1], e1[0]])
        rel = pts - pts[0]
        a, b = rel @ e1 / pitch, rel @ e2 / pitch
        ia, ib = np.rint(a).astype(int), np.rint(b).astype(int)
        ok = (np.abs(a - ia) < 0.3) & (np.abs(b - ib) < 0.3)
        ia, ib, p = ia[ok], ib[ok], pts[ok]
        if len(p) < 6:
            continue
        ia -= ia.min(); ib -= ib.min()
        if ia.max() != 2 or ib.max() != 2:
            continue                                            # need the full 3x3 span
        if len({(i, j) for i, j in zip(ia, ib)}) != len(p):
            continue
        A = np.column_stack([ia, ib, np.ones(len(p))])
        M, *_ = np.linalg.lstsq(A, p, rcond=None)               # (3,2): [i j 1] -> xy
        rms = float(np.sqrt(((A @ M - p) ** 2).sum(axis=1).mean())) / pitch
        key = (len(p), -rms)
        if best is None or key > best[0]:
            best = (key, M, len(p), rms)
    if best is None:
        return None
    return best[1], best[2], best[3]


def upright(M):
    """Re-index the lattice so cell (col 0,row 0) is image top-left."""
    best = None
    for k in range(4):
        cells = np.array([[(i, j) for i in range(3)] for j in range(3)], float)  # [row][col]=(i,j)
        cells = np.rot90(cells, k)
        xy = np.concatenate([cells.reshape(9, 2), np.ones((9, 1))], axis=1) @ M
        right = xy[2] - xy[0]; down = xy[6] - xy[0]
        score = right[0] + down[1]
        if best is None or score > best[0]:
            best = (score, xy)
    return best[1]                                              # 9 x 2, row-major


def read_face(bgr, lab):
    blobs = sticker_blobs(bgr)
    fit = fit_lattice(blobs) if len(blobs) >= 6 else None
    if fit is None:
        return None
    M, n, rms = fit
    u, v = M[0], M[1]
    lu, lv = np.linalg.norm(u), np.linalg.norm(v)
    cosang = abs(float(u @ v) / (lu * lv))
    if not 0.85 < lu / lv < 1.18 or cosang > 0.17 or rms > 0.12:
        return None                                             # not face-on enough
    xy = upright(M)
    r = max(2, int(0.22 * (lu + lv) / 2))
    cells = []
    for x, y in xy:
        x, y = int(round(x)), int(round(y))
        patch = lab[max(0, y - r):y + r + 1, max(0, x - r):x + r + 1].reshape(-1, 3)
        if len(patch) < 9:
            return None
        cells.append(np.median(patch, axis=0))
    return dict(xy=xy, lab=np.array(cells), pitch=(lu + lv) / 2, inliers=n, rms=rms)

# ---------------------------------------------------------------- per session

ROLL = [np.rot90(np.arange(9).reshape(3, 3), k).reshape(9) for k in range(4)]


def hue_dist(p, q):
    """Shading-tolerant Lab distance: lightness and chroma down-weighted, hue kept."""
    dL = p[..., 0] - q[..., 0]
    cp, cq = np.hypot(p[..., 1], p[..., 2]), np.hypot(q[..., 1], q[..., 2])
    dC = cp - cq
    dE2 = dL ** 2 + (p[..., 1] - q[..., 1]) ** 2 + (p[..., 2] - q[..., 2]) ** 2
    dH2 = np.maximum(dE2 - dL ** 2 - dC ** 2, 0)
    return np.sqrt(0.25 * (dL ** 2 + dC ** 2) + dH2)


def face_dist(a, b):
    ds = [hue_dist(a[r], b).mean() for r in ROLL]
    k = int(np.argmin(ds))
    return ds[k], k


def group_reads(reads, tau):
    groups = []                                                 # each: list of aligned 9x3
    for rd in reads:
        best = None
        for g in groups:
            d, k = face_dist(rd['lab'], g['ref'])
            if best is None or d < best[0]:
                best = (d, k, g)
        if best and best[0] < tau:
            _, k, g = best
            g['items'].append(rd['lab'][ROLL[k]])
            g['frames'].append(rd['frame'])
            g['ref'] = np.median(np.array(g['items']), axis=0)
        else:
            groups.append(dict(ref=rd['lab'].copy(), items=[rd['lab']], frames=[rd['frame']]))
    return groups


def name_colours(faces):
    """54 Lab -> 6 names x 9. Seeds = the six centres; exactly nine of each."""
    lab = np.array(faces).reshape(54, 3)
    centres = np.array([f[4] for f in faces])
    cost = np.array([[hue_dist(s, c) for c in centres for _ in range(9)] for s in lab])
    rows, cols = linear_sum_assignment(cost)
    cluster = np.empty(54, int)
    cluster[rows] = cols // 9
    chroma = np.hypot(centres[:, 1], centres[:, 2])
    white = int(np.argmin(chroma))
    hue = np.degrees(np.arctan2(centres[:, 2], centres[:, 1])) % 360
    hue = (hue + 30) % 360                                      # red just above the wrap
    order = [i for i in np.argsort(hue) if i != white]
    names = {white: 'W'}
    for i, n in zip(order, 'ROYGB'):
        names[i] = n
    margin = np.sort(cost.reshape(54, 6, 9)[:, :, 0], axis=1)
    return [names[c] for c in cluster], names, float((margin[:, 1] - margin[:, 0]).min())

# ---------------------------------------------------------------- assembly

U, R, F, D, L, B = [list(range(9 * k, 9 * k + 9)) for k in range(6)]
CORNER_F = [(U[8], R[0], F[2]), (U[6], F[0], L[2]), (U[0], L[0], B[2]), (U[2], B[0], R[2]),
            (D[2], F[8], R[6]), (D[0], L[8], F[6]), (D[6], B[8], L[6]), (D[8], R[8], B[6])]
CORNER_C = ['URF', 'UFL', 'ULB', 'UBR', 'DFR', 'DLF', 'DBL', 'DRB']
EDGE_F = [(U[5], R[1]), (U[7], F[1]), (U[3], L[1]), (U[1], B[1]), (D[5], R[7]), (D[1], F[7]),
          (D[3], L[7]), (D[7], B[7]), (F[5], R[3]), (F[3], L[5]), (B[5], L[3]), (B[3], R[5])]
EDGE_C = ['UR', 'UF', 'UL', 'UB', 'DR', 'DF', 'DL', 'DB', 'FR', 'FL', 'BL', 'BR']


def parity(p):
    seen, par = [False] * len(p), 0
    for i in range(len(p)):
        if seen[i]:
            continue
        j, n = i, 0
        while not seen[j]:
            seen[j] = True; j = p[j]; n += 1
        par ^= (n - 1) & 1
    return par


def valid(f):
    cp, co = [], 0
    for fac in CORNER_F:
        cols = [f[x] for x in fac]
        ori = next((o for o in range(3) if cols[o] in 'UD'), None)
        if ori is None:
            return False
        key = cols[ori] + cols[(ori + 1) % 3] + cols[(ori + 2) % 3]
        if key not in CORNER_C:
            return False
        cp.append(CORNER_C.index(key)); co += ori
    ep, eo = [], 0
    for fac in EDGE_F:
        a, b = f[fac[0]], f[fac[1]]
        if a + b in EDGE_C:
            ep.append(EDGE_C.index(a + b))
        elif b + a in EDGE_C:
            ep.append(EDGE_C.index(b + a)); eo += 1
        else:
            return False
    return (len(set(cp)) == 8 and len(set(ep)) == 12 and co % 3 == 0 and eo % 2 == 0
            and parity(cp) == parity(ep))


SCHEME = {'W': 'U', 'Y': 'D', 'G': 'F', 'B': 'B', 'R': 'R', 'O': 'L'}   # Western


def assemble(face_names):
    """face_names: six 9-lists of colour letters, any order, any roll."""
    by_face = {}
    for cells in face_names:
        by_face[SCHEME[cells[4]]] = [SCHEME[c] for c in cells]
    if len(by_face) != 6:
        return []
    survivors = []
    for rolls in itertools.product(range(4), repeat=6):
        f = []
        for face, k in zip('URFDLB', rolls):
            f += [by_face[face][i] for i in ROLL[k]]
        if valid(f):
            survivors.append(''.join(f))
    # A side that looks the same under a quarter or half turn reaches the same cube
    # by several rolls; those are one answer, not several.
    return sorted(set(survivors))

# ---------------------------------------------------------------- driver

def main():
    src = sys.argv[1]
    out = sys.argv[2] if len(sys.argv) > 2 else None
    names = sorted(n for n in os.listdir(src) if n.startswith('rec_') and n.endswith('.jpg'))
    reads, prev, per_frame = [], None, []
    for n in names:
        bgr = cv2.imread(os.path.join(src, n))
        lab = cv2.cvtColor(bgr.astype(np.float32) / 255.0, cv2.COLOR_BGR2Lab)
        rd = read_face(bgr, lab)
        idx = int(n[4:10])
        state = 'none'
        if rd is not None:
            rd['frame'] = idx
            c = rd['xy'][4]
            steady = (prev is not None and prev['frame'] == idx - 1
                      and np.linalg.norm(c - prev['xy'][4]) < 0.12 * rd['pitch'])
            state = 'steady' if steady else 'moving'
            if steady:
                reads.append(rd)
            prev = rd
        per_frame.append(state)
    total = len(names)
    print(f'frames {total}  lattice-found {sum(s != "none" for s in per_frame)}'
          f'  steady-reads {len(reads)}')

    groups = group_reads(reads, tau=9.0)
    groups.sort(key=lambda g: -len(g['items']))
    print(f'groups {len(groups)}: sizes {[len(g["items"]) for g in groups]}')
    faces = [g for g in groups if len(g['items']) >= 5]
    print(f'faces with >=5 reads: {len(faces)}')
    if len(faces) != 6:
        cents = np.array([g['ref'][4] for g in faces])
        print('centre Lab:\n', np.round(cents, 1))
        print('RESULT: incomplete')
        return
    fused = [np.median(np.array(g['items']), axis=0) for g in faces]
    spread = [float(np.abs(np.array(g['items']) - f).mean()) for g, f in zip(faces, fused)]
    labels, cname, margin = name_colours(fused)
    grid = [labels[i * 9:(i + 1) * 9] for i in range(6)]
    for g, cells, sp in zip(faces, grid, spread):
        print(f'  centre {cells[4]}  reads {len(g["items"]):3d}  frames {g["frames"][0]:3d}..{g["frames"][-1]:3d}'
              f'  within-face spread {sp:4.1f}  | {"".join(cells[0:3])} {"".join(cells[3:6])} {"".join(cells[6:9])}')
    census = {c: labels.count(c) for c in 'WYROGB'}
    print('census', census, ' min assignment margin', round(margin, 1))
    centres = np.array([f[4] for f in fused])
    dm = np.array([[hue_dist(a, b) for b in centres] for a in centres])
    np.fill_diagonal(dm, 1e9)
    print('min centre-to-centre distance', round(float(dm.min()), 1))
    surv = assemble(grid)
    print(f'valid cubes among 4096 roll combinations: {len(surv)}')
    for s in surv[:3]:
        print('  facelets URFDLB:', s)
    print('RESULT:', 'UNIQUE VALID CUBE' if len(surv) == 1 else ('ambiguous' if surv else 'no valid cube'))
    if out:
        json.dump(dict(per_frame=per_frame, grid=grid, survivors=surv,
                       frames=[g['frames'] for g in faces]), open(out, 'w'))


if __name__ == '__main__':
    main()
