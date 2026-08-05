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
is U=Y(ellow) R=O(range) F=G(reen) D=W(hite) L=R(ed) B=B(lue) -- exactly the
solver's cube::solver::canonical_color scheme (the same physical Western
cube, zero rotation between generator and solver frames); state strings use
the colour letters W/Y/R/O/G/B (54 chars, face-major).
"""

from __future__ import annotations

import itertools

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

# --- viewer-canonical corner order -------------------------------------------
#
# The model-frame binary corner ids above are anchored to the cube's OWN axes
# (U=+y etc.), which are NOT observable in an image once stickers are
# scrambled/random: under a uniform SO(3) pose the 24 rotational symmetries
# make every model-frame id equally plausible, so per-id keypoint supervision
# is noise and a detector collapses all 8 keypoints onto the cube centroid
# (observed: smoke run 2026-07/08, pose mAP 0 with box mAP 0.995).
#
# Fix: relabel each sample into a VIEWER-canonical binary order -- apply the
# cube symmetry S that best aligns the model axes with the view axes
# (+x -> image right, +y -> image up, +z -> toward the camera along the
# viewing ray to the cube centre), then index corners by the same binary rule
# in that relabelled frame.  Ids become a deterministic function of the
# visible geometry (id 7 ~ the near top-right corner), hence learnable.
# Horizontal flips still map i -> i XOR 1 (FLIP_IDX unchanged); see
# _selftest_view_canonical below, which verifies that exactly.


def _signed_perm_matrices() -> list[np.ndarray]:
    """All 48 signed permutation matrices (the full cube symmetry group)."""
    mats = []
    for perm in itertools.permutations(range(3)):
        for signs in itertools.product((1.0, -1.0), repeat=3):
            m = np.zeros((3, 3))
            for row, (col, s) in enumerate(zip(perm, signs)):
                m[row, col] = s
            mats.append(m)
    return mats


#: 48 signed permutation matrices; det +1 half = the 24 proper rotations.
SYMMETRIES_48 = _signed_perm_matrices()


def corner_index(p) -> int:
    """Binary corner id (x fastest) of a +-1 corner coordinate triple."""
    return int(p[0] > 0) | int(p[1] > 0) << 1 | int(p[2] > 0) << 2


def view_canonical_perm(R: np.ndarray, tvec=None) -> list[int]:
    """Viewer-canonical relabelling of the 8 corners for pose ``R`` (+ ``tvec``).

    Returns ``perm`` with ``perm[j]`` = the model-frame corner id that occupies
    slot ``j`` of the viewer-canonical binary order, i.e. relabelled keypoint
    arrays are built as ``new[j] = old[perm[j]]``.

    Canonical frame: the cube symmetry ``S`` (proper composite ``R @ S``
    enforced, so improper ``R`` used by the flip selftest also works)
    maximising ``trace(T^T R S)`` where ``T``'s columns are the ideal view
    axes in OpenCV camera coords: x -> image right, y -> image up, z -> toward
    the camera along the viewing ray to the cube centre (``-tvec``; straight
    down the optical axis when ``tvec`` is None).
    """
    R = np.asarray(R, dtype=np.float64).reshape(3, 3)
    if tvec is None:
        z_t = np.array([0.0, 0.0, -1.0])
    else:
        t = np.asarray(tvec, dtype=np.float64).reshape(3)
        z_t = -t / np.linalg.norm(t)
    up = np.array([0.0, -1.0, 0.0])  # image up in OpenCV camera coords
    y_t = up - np.dot(up, z_t) * z_t
    y_t /= np.linalg.norm(y_t)
    x_t = np.cross(y_t, z_t)
    T = np.stack([x_t, y_t, z_t], axis=1)
    det_r = float(np.linalg.det(R))
    best, best_score = None, -np.inf
    for S in SYMMETRIES_48:
        if float(np.linalg.det(S)) * det_r < 0.0:
            continue  # composite would be an improper (impossible) cube pose
        score = float(np.trace(T.T @ R @ S))
        if score > best_score:
            best_score, best = score, S
    return [corner_index(best @ CORNERS[j]) for j in range(8)]


CORNER_ORDER_MODEL = "binary_xyz_x_fastest"
CORNER_ORDER_VIEW = "binary_xyz_x_fastest_view_canonical"


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

SOLVED_SCHEME = {"U": "Y", "R": "O", "F": "G", "D": "W", "L": "R", "B": "B"}
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


# --- selftests ---------------------------------------------------------------


def _quat_to_mat(q: np.ndarray) -> np.ndarray:
    w, x, y, z = q
    return np.array(
        [
            [1 - 2 * (y * y + z * z), 2 * (x * y - w * z), 2 * (x * z + w * y)],
            [2 * (x * y + w * z), 1 - 2 * (x * x + z * z), 2 * (y * z - w * x)],
            [2 * (x * z - w * y), 2 * (y * z + w * x), 1 - 2 * (x * x + y * y)],
        ]
    )


def _selftest_view_canonical(n: int = 20000, seed: int = 0) -> None:
    """Verify view_canonical_perm: valid permutation, identity view -> identity
    order, canonical +z toward the viewer, and exact FLIP_IDX consistency
    under an Ultralytics-style horizontal image flip (mirrored scene
    relabels to perm[i XOR 1])."""
    rng = np.random.default_rng(seed)
    mirror = np.diag([-1.0, 1.0, 1.0])  # camera-frame x flip = image mirror

    assert view_canonical_perm(np.diag([1.0, -1.0, -1.0])) == list(range(8))
    assert FLIP_IDX == [i ^ 1 for i in range(8)]

    min_zdot = np.inf
    for _ in range(n):
        q = rng.normal(size=4)
        q /= np.linalg.norm(q)
        R = _quat_to_mat(q)
        t = np.array([rng.uniform(-2, 2), rng.uniform(-2, 2), rng.uniform(2.5, 9.0)])
        perm = view_canonical_perm(R, t)
        assert sorted(perm) == list(range(8)), perm

        # canonical +z axis must point toward the viewer along the view ray
        S = None
        for cand in SYMMETRIES_48:
            if np.linalg.det(cand) > 0 and [corner_index(cand @ CORNERS[j]) for j in range(8)] == perm:
                S = cand
                break
        z_new = (R @ S)[:, 2]
        zdot = float(np.dot(z_new, -t / np.linalg.norm(t)))
        min_zdot = min(min_zdot, zdot)
        assert zdot > 0.0, (zdot, perm)

        # flip consistency: generator labels for the mirrored scene must equal
        # FLIP_IDX applied to the labels of the original scene
        perm_m = view_canonical_perm(mirror @ R, mirror @ t)
        assert perm_m == [perm[j ^ 1] for j in range(8)], (perm, perm_m)

    print(f"cube_model selftest: OK ({n} random poses, min canonical z-alignment {min_zdot:.3f})")


if __name__ == "__main__":
    _selftest_view_canonical()
