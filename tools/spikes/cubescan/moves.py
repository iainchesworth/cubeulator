"""Face turns on a 54-facelet URFDLB string, built from geometry rather than typed tables.

Each facelet is a sticker at a position on the 3x3x3 with an outward normal;
a turn rotates the stickers of one layer. Used to check that two scans of the
same cube are related by the moves made between them.
"""
import itertools

import numpy as np

# Outward normal, and the (right, down) directions of each side's row-major grid
# as an outside viewer sees it: U with B at the top, D with F at the top, the rest
# with U at the top.
FRAMES = {
    'U': ((0, 1, 0), (1, 0, 0), (0, 0, 1)),
    'R': ((1, 0, 0), (0, 0, -1), (0, -1, 0)),
    'F': ((0, 0, 1), (1, 0, 0), (0, -1, 0)),
    'D': ((0, -1, 0), (1, 0, 0), (0, 0, -1)),
    'L': ((-1, 0, 0), (0, 0, 1), (0, -1, 0)),
    'B': ((0, 0, -1), (-1, 0, 0), (0, -1, 0)),
}
ORDER = 'URFDLB'


def _stickers():
    out = []
    for face in ORDER:
        n, right, down = (np.array(v) for v in FRAMES[face])
        for r in range(3):
            for c in range(3):
                out.append((tuple(n + (c - 1) * right + (r - 1) * down), tuple(n)))
    return out


STICKERS = _stickers()
INDEX = {s: i for i, s in enumerate(STICKERS)}


def _clockwise(axis):
    """Rotation matrix for a clockwise quarter turn seen from outside along +axis."""
    a = np.array(axis)
    k = np.array([[0, -a[2], a[1]], [a[2], 0, -a[0]], [-a[1], a[0], 0]])
    return np.eye(3, dtype=int) - k + np.outer(a, a) - np.eye(3, dtype=int) * 0 + (k @ k) * 0 \
        if False else (np.outer(a, a) - k)          # Rodrigues at -90 degrees: aa^T - K


def _perm(face):
    n = np.array(FRAMES[face][0])
    rot = _clockwise(n)
    perm = list(range(54))
    for i, (pos, normal) in enumerate(STICKERS):
        if np.dot(pos, n) == 1:
            perm[INDEX[(tuple(rot @ np.array(pos)), tuple(rot @ np.array(normal)))]] = i
    return perm


PERMS = {f: _perm(f) for f in ORDER}


def turn(state, move):
    """Apply one move such as R, R', R2."""
    times = {'': 1, '2': 2, "'": 3}[move[1:]]
    for _ in range(times):
        state = ''.join(state[PERMS[move[0]][i]] for i in range(54))
    return state


def apply(state, moves):
    for m in moves.split():
        state = turn(state, m)
    return state


ALL_MOVES = [f + s for f in ORDER for s in ('', '2', "'")]
SOLVED = ''.join(f * 9 for f in ORDER)


def moves_between(a, b, depth=2):
    """Shortest move sequences (up to `depth`) taking state a to state b."""
    if a == b:
        return ['']
    for d in range(1, depth + 1):
        hits = [' '.join(seq) for seq in itertools.product(ALL_MOVES, repeat=d)
                if all(seq[i][0] != seq[i + 1][0] for i in range(d - 1)) and apply(a, ' '.join(seq)) == b]
        if hits:
            return hits
    return []


def _selftest():
    import cubescan as cs
    for m in ALL_MOVES:
        assert cs.valid(list(turn(SOLVED, m))), m
    for f in ORDER:
        assert apply(SOLVED, ' '.join([f] * 4)) == SOLVED and turn(SOLVED, f) != SOLVED
        assert apply(SOLVED, f + ' ' + f + "'") == SOLVED
    assert apply(SOLVED, ' '.join(["R U R' U'"] * 6)) == SOLVED
    assert apply(SOLVED, "R U R' U'") != SOLVED
    # clockwise U carries the front row to the left side
    assert turn(SOLVED, 'U')[36:39] == 'FFF', turn(SOLVED, 'U')[36:45]
    assert turn(SOLVED, 'R')[2] == 'F'              # clockwise R carries F's right column up onto U
    assert moves_between(SOLVED, turn(SOLVED, "F'")) == ["F'"]
    print('moves selftest ok')


if __name__ == '__main__':
    _selftest()
