"""Geometric model of a 3x3x3 stickered cube, backend-independent.

Model frame (the frame rvec/tvec in the labels refer to):
    x right, y up, z toward the F face; the cube spans [-1, +1]^3.

Canonical corner order (the label convention for the 8 keypoints):
    binary order, x fastest -- corner i has
        x = +1 if (i >> 0) & 1 else -1
        y = +1 if (i >> 1) & 1 else -1
        z = +1 if (i >> 2) & 1 else -1
    so corner 0 = (-1,-1,-1) ... corner 7 = (+1,+1,+1).

Under a horizontal image flip the corner ids map i -> i XOR 1 (the x sign
flips); that gives the Ultralytics flip_idx [1, 0, 3, 2, 5, 4, 7, 6].

Face/state convention: URFDLB face order, 9 stickers per face row-major as
seen from OUTSIDE the face, Kociemba-style layout.  The solved colour scheme
is U=W(hite) R=R(ed) F=G(reen) D=Y(ellow) L=O(range) B=B(lue); state strings
use the colour letters W/Y/R/O/G/B (54 chars, face-major).
"""

from __future__ import annotations

import numpy as np

# --- corners -----------------------------------------------------------------

#: (8, 3) float64 -- canonical corner order, binary, x fastest.
CORNERS = np.array(
    [[(i >> 0 & 1) * 2 - 1, (i >> 1 & 1) * 2 - 1, (i >> 2 & 1) * 2 - 1] for i in range(8)],
    dtype=np.float64,
)

#: Ultralytics keypoint flip index for horizontal flips (x sign flip).
FLIP_IDX = [1, 0, 3, 2, 5, 4, 7, 6]

#: The 12 cube edges as corner-id pairs (ids differ in exactly one bit).
EDGES = [(a, b) for a in range(8) for b in range(a + 1, 8) if bin(a ^ b).count("1") == 1]

# --- faces -------------------------------------------------------------------

FACE_ORDER = "URFDLB"

# Per face: outward normal, row-major traversal start corner, u (across a
# row, left->right seen from outside) and v (top->bottom) directions.
# Kociemba-style unfolded layout.
_F = {
    "U": {"n": (0, 1, 0), "start": (-1, 1, -1), "u": (1, 0, 0), "v": (0, 0, 1)},
    "R": {"n": (1, 0, 0), "start": (1, 1, 1), "u": (0, 0, -1), "v": (0, -1, 0)},
    "F": {"n": (0, 0, 1), "start": (-1, 1, 1), "u": (1, 0, 0), "v": (0, -1, 0)},
    "D": {"n": (0, -1, 0), "start": (-1, -1, 1), "u": (1, 0, 0), "v": (0, 0, -1)},
    "L": {"n": (-1, 0, 0), "start": (-1, 1, -1), "u": (0, 0, 1), "v": (0, -1, 0)},
    "B": {"n": (0, 0, -1), "start": (1, 1, -1), "u": (-1, 0, 0), "v": (0, -1, 0)},
}

FACE_NORMALS = {f: np.array(_F[f]["n"], dtype=np.float64) for f in FACE_ORDER}

SOLVED_SCHEME = {"U": "W", "R": "R", "F": "G", "D": "Y", "L": "O", "B": "B"}
COLOR_LETTERS = "WYROGB"

CELL_PITCH = 2.0 / 3.0  # face spans [-1, 1] in both tangent axes


def face_corner_ids(face: str) -> list[int]:
    """The 4 canonical corner ids of ``face`` in tl, tr, br, bl order (as
    seen from outside the face, matching the row-major sticker layout)."""
    spec = _F[face]
    start = np.array(spec["start"], dtype=np.float64)
    u = np.array(spec["u"], dtype=np.float64)
    v = np.array(spec["v"], dtype=np.float64)
    out = []
    for du, dv in ((0, 0), (2, 0), (2, 2), (0, 2)):
        p = start + u * du + v * dv
        bits = int(p[0] > 0) | int(p[1] > 0) << 1 | int(p[2] > 0) << 2
        out.append(bits)
    return out


def sticker_quads(face: str, sticker_frac: float = 0.88, chamfer: float = 0.12):
    """3D outlines of the 9 stickers of ``face``, row-major.

    Each sticker is a corner-chamfered square (octagon) lying on the face
    plane; ``sticker_frac`` is the sticker side as a fraction of the cell
    pitch, ``chamfer`` the corner cut as a fraction of the sticker side.

    Returns a list of 9 (n_pts, 3) arrays.
    """
    spec = _F[face]
    start = np.array(spec["start"], dtype=np.float64)
    u = np.array(spec["u"], dtype=np.float64)
    v = np.array(spec["v"], dtype=np.float64)
    half = CELL_PITCH * sticker_frac / 2.0
    c = chamfer * 2 * half  # chamfer length along each edge

    quads = []
    for r in range(3):
        for col in range(3):
            centre = start + u * (col + 0.5) * CELL_PITCH + v * (r + 0.5) * CELL_PITCH
            # Octagon: walk the square tl -> tr -> br -> bl, cutting corners.
            pts = []
            sq = [(-half, -half), (half, -half), (half, half), (-half, half)]
            for k in range(4):
                ax, ay = sq[k]
                bx, by = sq[(k + 1) % 4]
                ex, ey = bx - ax, by - ay
                elen = abs(ex) + abs(ey)  # axis-aligned edges
                t0, t1 = c / elen, 1 - c / elen
                pts.append((ax + ex * t0, ay + ey * t0))
                pts.append((ax + ex * t1, ay + ey * t1))
            quads.append(np.array([centre + u * px + v * py for px, py in pts]))
    return quads


def face_outline(face: str) -> np.ndarray:
    """(4, 3) outline of a face in tl, tr, br, bl order (outside view)."""
    spec = _F[face]
    start = np.array(spec["start"], dtype=np.float64)
    u = np.array(spec["u"], dtype=np.float64)
    v = np.array(spec["v"], dtype=np.float64)
    return np.array([start, start + 2 * u, start + 2 * u + 2 * v, start + 2 * v])


# --- states ------------------------------------------------------------------


def solved_state() -> str:
    return "".join(SOLVED_SCHEME[f] * 9 for f in FACE_ORDER)


def scrambled_state(rng: np.random.Generator) -> str:
    """Permutation of the solved stickers with centres kept fixed -- each
    colour appears exactly 9 times (valid-ish; parity not enforced, which is
    fine for a content-agnostic detector)."""
    s = list(solved_state())
    movable = [i for i in range(54) if i % 9 != 4]
    vals = [s[i] for i in movable]
    rng.shuffle(vals)
    for i, val in zip(movable, vals):
        s[i] = val
    return "".join(s)


def random_state(rng: np.random.Generator) -> str:
    """Each sticker independently uniform -- maximally content-agnostic."""
    return "".join(rng.choice(list(COLOR_LETTERS)) for _ in range(54))


def sample_state(rng: np.random.Generator) -> str:
    """Mix used by the generator: 15% solved (solid faces), 45% permutation
    scramble, 40% independent random."""
    r = rng.random()
    if r < 0.15:
        return solved_state()
    if r < 0.60:
        return scrambled_state(rng)
    return random_state(rng)


def state_face(state: str, face: str) -> str:
    """The 9 colour letters of ``face`` from a 54-char state string."""
    i = FACE_ORDER.index(face) * 9
    return state[i : i + 9]
