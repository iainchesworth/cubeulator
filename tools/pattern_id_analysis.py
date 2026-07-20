#!/usr/bin/env python3
"""Does a face's ORDERED nine-sticker pattern identify it from one frame?

Consumes the CSV written by tests/unit/pattern_id_test.cpp ("[.pattern-id]"):

    set CUBE_PATTERN_ID_DIR=D:/cubeulator-build/vision-test-frames/recordings
    cube_unit_tests.exe "[.pattern-id]" > readings.csv
    python tools/pattern_id_analysis.py readings.csv

Measurement only - nothing here runs in, or feeds, production. The question
is whether the ordered pattern separates "same physical face" from
"different physical face" WITHOUT any tracking continuity, well enough to
re-establish identity after a re-anchor. Today identity comes only from
continuous tracking, and colour may verify but never route.

GROUND TRUTH, and why it is trustworthy.
The recordings carry no face labels, so the labels are reconstructed from
POSE CONTINUITY - and pose continuity is used ONLY here, never as an input
to any distance being measured.

The detector's pose R(t) relates its own model frame to the camera, and the
model frame is re-chosen every frame up to the cube's 24-fold symmetry:
R(t) = R_true(t) . S(t) with unknown S(t) in the octahedral group. The
near face's body axis is therefore u(t) = S(t) . m, where m = R^T . (0,0,-1)
is the model-frame camera direction and is CONSTANT for face-path poses.

Between two passes a few tens of ms apart the cube barely moves, so
    D = R_b^T . R_a = S_b^T . Q . S_a  with  Q ~ I,
which means D lands close to a single group element g, and S_b = S_a . g^T.
Integrating that along the pass sequence recovers S(t) - hence the near-face
axis - for every reading in a chain. A link is accepted only when the
residual to the nearest element is small AND the runner-up element is far
enough away that the choice is unambiguous; otherwise the chain BREAKS and a
new one starts. Labels are therefore only comparable within a chain, which
is exactly what makes them safe: no claim is ever carried across a gap where
the geometry could not support it.

Expected error. A link mis-assigns g only if the true inter-frame rotation
approaches the 45 deg midpoint between group elements while still clearing
the margin gate - at the accepted gap and residual bounds that needs roughly
a 500 deg/s hand roll landing inside the acceptance window, which the margin
gate is there to refuse. Residual quantiles for accepted links are reported
so the margin actually achieved is visible rather than asserted.
"""

import csv
import itertools
import math
import sys
from collections import Counter, defaultdict

# --- geometry -------------------------------------------------------------

# The 24 rotation matrices of the cube (signed permutation matrices with
# determinant +1). Generated rather than transcribed - the C++ side proves
# the ONE table this script actually relies on (the four in-plane rolls)
# equals production's kBodyRelabel; see PatternIdRollsMatchBodyRelabel.
def octahedral():
    out = []
    for perm in itertools.permutations(range(3)):
        for signs in itertools.product((1, -1), repeat=3):
            m = [[0.0] * 3 for _ in range(3)]
            for row in range(3):
                m[row][perm[row]] = float(signs[row])
            det = (
                m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1])
                - m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0])
                + m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0])
            )
            if det > 0:
                out.append(m)
    assert len(out) == 24
    return out


LATTICE = octahedral()


def quat_to_matrix(q):
    w, x, y, z = q
    n = math.sqrt(w * w + x * x + y * y + z * z)
    if n == 0.0:
        return [[1.0, 0, 0], [0, 1.0, 0], [0, 0, 1.0]]
    w, x, y, z = w / n, x / n, y / n, z / n
    return [
        [1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
        [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
        [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)],
    ]


def mat_mul(a, b):
    return [[sum(a[i][k] * b[k][j] for k in range(3)) for j in range(3)] for i in range(3)]


def mat_t(a):
    return [[a[j][i] for j in range(3)] for i in range(3)]


def mat_vec(a, v):
    return tuple(sum(a[i][k] * v[k] for k in range(3)) for i in range(3))


def rot_angle_deg(a):
    trace = a[0][0] + a[1][1] + a[2][2]
    return math.degrees(math.acos(max(-1.0, min(1.0, (trace - 1.0) / 2.0))))


def nearest_lattice(d):
    """(index, residual deg, runner-up deg) of the group element closest to d."""
    scored = sorted((rot_angle_deg(mat_mul(mat_t(g), d)), i) for i, g in enumerate(LATTICE))
    return scored[0][1], scored[0][0], scored[1][0]


def axis_key(v):
    """Quantise a unit vector to the signed axis it points nearest."""
    best = max(range(3), key=lambda i: abs(v[i]))
    return (best, 1 if v[best] > 0 else -1)


# --- the four in-plane rolls of a 3x3 grid --------------------------------
# Proven equal to production's kBodyRelabel stabiliser set, for all six
# faces, by PatternIdRollsMatchBodyRelabel in pattern_id_test.cpp. Applying
# them to raw screen-order arrays is therefore the same cell-permutation
# search oriented_cell_lab_distance runs over solver-order arrays.
ROT90 = [2, 5, 8, 1, 4, 7, 0, 3, 6]


def build_rolls():
    rolls = []
    cur = list(range(9))
    for _ in range(4):
        rolls.append(list(cur))
        cur = [ROT90[c] for c in cur]
    return rolls


ROLLS = build_rolls()
NON_CENTRE = [k for k in range(9) if k != 4]

# FacetColor ordinals, for readable output only.
COLOR_NAMES = {0: "unknown", 1: "white", 2: "yellow", 3: "red", 4: "orange",
               5: "green", 6: "blue"}


def lab_dist(a, b):
    return math.sqrt(sum((a[i] - b[i]) ** 2 for i in range(3)))


def pattern_distance(a, b):
    """oriented_cell_lab_distance minimised over the four rolls.

    Mean Lab over the eight NON-CENTRE cells - the centre is excluded
    because it is rotation-invariant and carries no roll information, and
    the centre-colour baseline scores it separately.
    """
    best = None
    for perm in ROLLS:
        total = sum(lab_dist(a.lab[k], b.lab[perm[k]]) for k in NON_CENTRE)
        mean = total / len(NON_CENTRE)
        if best is None or mean < best:
            best = mean
    return best


def centre_distance(a, b):
    return lab_dist(a.lab[4], b.lab[4])


def census_distance(a, b):
    return sum(abs(x - y) for x, y in zip(a.census, b.census))


# --- records --------------------------------------------------------------


class Reading:
    __slots__ = ("clip", "pas", "t", "good", "R", "corner", "trk_state", "trk_front",
                 "trk_epoch", "lab", "census", "centre_class", "chain", "face", "idx")


def load(path):
    rows = []
    with open(path, newline="", encoding="utf-8") as handle:
        for row in csv.DictReader(handle):
            if row["clip"] is None or not row["clip"].startswith("live"):
                continue
            r = Reading()
            r.clip = row["clip"]
            r.pas = int(row["pass"])
            r.t = int(row["t_ms"])
            r.good = row["face_good"] == "1"
            r.corner = row["corner_view"] == "1"
            r.trk_state = row["trk_state"]
            r.trk_front = row["trk_front"]
            r.trk_epoch = int(row["trk_epoch"])
            r.R = (
                quat_to_matrix([float(row[k]) for k in ("qw", "qx", "qy", "qz")])
                if row["pose_valid"] == "1"
                else None
            )
            if r.good:
                r.lab = [
                    tuple(float(row[f"{ch}{k}"]) for ch in ("L", "a", "b")) for k in range(9)
                ]
                r.census = [int(row[f"cen{i}"]) for i in range(7)]
                r.centre_class = int(row["centre_class"])
            else:
                r.lab = None
                r.census = None
                r.centre_class = -1
            r.chain = None
            r.face = None
            rows.append(r)
    return rows


# --- ground truth ---------------------------------------------------------

# Pose-valid passes arrive at roughly 10 Hz (measured median gap ~100 ms,
# p90 ~250 ms), so a link spans a real hand rotation of tens of degrees,
# not a few. The residual gate is sized to admit that while staying well
# inside the 45 deg midpoint between group elements, and the margin gate
# refuses any link that gets close to the midpoint.
MAX_GAP_MS = 250      # a link older than this is not "the cube barely moved"
MAX_RESID_DEG = 30.0  # residual to the nearest group element
MIN_MARGIN_DEG = 20.0  # runner-up must be this much further away


def build_chains(rows):
    """Integrate S(t) along the pose sequence; return residual diagnostics."""
    chains = 0
    resid_samples = []
    breaks = Counter()
    by_clip = defaultdict(list)
    for r in rows:
        by_clip[r.clip].append(r)

    for clip, seq in by_clip.items():
        seq.sort(key=lambda r: r.t)
        prev = None
        S = None
        for r in seq:
            if r.R is None:
                prev, S = None, None
                breaks["no_pose"] += 1
                continue
            if prev is None:
                chains += 1
                S = [[1.0, 0, 0], [0, 1.0, 0], [0, 0, 1.0]]
            else:
                if r.t - prev.t > MAX_GAP_MS:
                    breaks["gap"] += 1
                    chains += 1
                    S = [[1.0, 0, 0], [0, 1.0, 0], [0, 0, 1.0]]
                else:
                    d = mat_mul(mat_t(r.R), prev.R)
                    gi, resid, runner = nearest_lattice(d)
                    if resid > MAX_RESID_DEG:
                        breaks["residual"] += 1
                        chains += 1
                        S = [[1.0, 0, 0], [0, 1.0, 0], [0, 0, 1.0]]
                    elif runner - resid < MIN_MARGIN_DEG:
                        breaks["margin"] += 1
                        chains += 1
                        S = [[1.0, 0, 0], [0, 1.0, 0], [0, 0, 1.0]]
                    else:
                        resid_samples.append(resid)
                        S = mat_mul(S, mat_t(LATTICE[gi]))
            r.chain = (clip, chains)
            # Near-face body axis. Only meaningful for a FACE-path pose:
            # the corner model's camera direction points at a vertex, not a
            # face normal, so corner-pose rows carry continuity but never a
            # label.
            if not r.corner:
                m = mat_vec(mat_t(r.R), (0.0, 0.0, -1.0))
                r.face = axis_key(mat_vec(S, m))
            prev = r
    return chains, resid_samples, breaks


# --- reporting helpers ----------------------------------------------------


def pct(values, p):
    if not values:
        return float("nan")
    s = sorted(values)
    i = min(len(s) - 1, max(0, int(round(p / 100.0 * (len(s) - 1)))))
    return s[i]


def describe(name, values):
    if not values:
        return f"  {name:<10} n=0"
    mean = sum(values) / len(values)
    return (
        f"  {name:<10} n={len(values):<7d} mean={mean:7.2f} "
        f"p1={pct(values,1):7.2f} p5={pct(values,5):7.2f} p50={pct(values,50):7.2f} "
        f"p95={pct(values,95):7.2f} p99={pct(values,99):7.2f} max={max(values):7.2f}"
    )


def separation(same, diff, label, lo, hi, step):
    """Best threshold by Youden's J, with the error rates it buys."""
    best = None
    t = lo
    while t <= hi:
        fm = sum(1 for d in diff if d <= t) / len(diff) if diff else 0.0
        fs = sum(1 for d in same if d > t) / len(same) if same else 0.0
        j = (1 - fm) + (1 - fs) - 1
        if best is None or j > best[0]:
            best = (j, t, fm, fs)
        t += step
    j, t, fm, fs = best
    overlap_lo = pct(diff, 1)
    overlap_hi = pct(same, 99)
    print(
        f"  {label:<16} best_threshold={t:7.2f}  false_match={fm*100:6.2f}%  "
        f"false_split={fs*100:6.2f}%  J={j:.3f}"
    )
    print(
        f"  {'':<16} same_p99={overlap_hi:7.2f}  diff_p1={overlap_lo:7.2f}  "
        f"{'SEPARATED' if overlap_hi < overlap_lo else 'OVERLAPPING'}"
    )
    return t, fm, fs


# --- main -----------------------------------------------------------------


def main(path):
    rows = load(path)
    good = [r for r in rows if r.good]
    print(f"rows={len(rows)}  capture-grade readings={len(good)}")
    for clip in sorted({r.clip for r in rows}):
        n = sum(1 for r in rows if r.clip == clip)
        g = sum(1 for r in rows if r.clip == clip and r.good)
        print(f"  {clip}: rows={n} good={g}")

    chains, resid, breaks = build_chains(rows)
    labelled = [r for r in good if r.face is not None]
    print()
    print("=== 1. GROUND TRUTH ===")
    print(f"chains={chains}  accepted links={len(resid)}  breaks={dict(breaks)}")
    print(
        f"link residual to nearest group element: "
        f"p50={pct(resid,50):.2f} p95={pct(resid,95):.2f} max={max(resid):.2f} deg "
        f"(gate {MAX_RESID_DEG}, margin gate {MIN_MARGIN_DEG})"
    )
    print(f"labelled capture-grade readings={len(labelled)} of {len(good)}")

    by_chain = defaultdict(list)
    for r in labelled:
        by_chain[r.chain].append(r)
    usable = {k: v for k, v in by_chain.items() if len(v) >= 2}
    print(f"chains with >=2 labelled readings={len(usable)}")

    same, diff = [], []
    same_c, diff_c = [], []
    same_x, diff_x = [], []
    same_by_dt = defaultdict(list)
    for members in usable.values():
        for a, b in itertools.combinations(members, 2):
            p = pattern_distance(a, b)
            c = centre_distance(a, b)
            x = census_distance(a, b)
            if a.face == b.face:
                same.append(p)
                same_c.append(c)
                same_x.append(x)
                dt = abs(a.t - b.t)
                bucket = "<0.5s" if dt < 500 else ("0.5-2s" if dt < 2000 else ">2s")
                same_by_dt[bucket].append(p)
            else:
                diff.append(p)
                diff_c.append(c)
                diff_x.append(x)
    print(f"pose-continuity pairs: same-face={len(same)}  different-face={len(diff)}")

    # SECOND, INDEPENDENT different-face criterion, and the load-bearing one:
    # a chain that survives a quarter turn is exactly what the tracker cannot
    # do either, so pose continuity yields almost no different-face pairs. The
    # cube's six centres are six DISTINCT colours, so two readings whose
    # centres classify differently AND sit further apart than the closest
    # legitimate centre pair (red/orange, ~27 Lab - kClosestCentrePairLab in
    # capture_routing.hpp) are different physical faces. This is the same
    # predicate production's verified_same_face treats as load-bearing.
    #
    # Circularity, stated plainly: these pairs are SELECTED on centre colour,
    # so the centre-colour baseline is not fairly measurable on them and its
    # numbers below are reported for completeness only. The ordered-pattern
    # distance uses ONLY the eight non-centre cells and is therefore measured
    # fairly; the census baseline is partly correlated (the centre is one of
    # its nine votes) and is flagged where it matters.
    print()
    print("=== 2b. COLOUR-DERIVED different-face pairs (within clip) ===")
    diff2, diff2_c, diff2_x = [], [], []
    for clip in sorted({r.clip for r in good}):
        members = [r for r in good if r.clip == clip]
        for a, b in itertools.combinations(members[::2], 2):
            if a.centre_class == b.centre_class:
                continue
            if centre_distance(a, b) <= 30.0:
                continue
            diff2.append(pattern_distance(a, b))
            diff2_c.append(centre_distance(a, b))
            diff2_x.append(census_distance(a, b))
    print(f"different-face pairs from distinct centre colour: {len(diff2)}")

    print()
    print("=== 3. SEPARATION: ordered pattern (min over 4 rolls, mean Lab, 8 cells) ===")
    print(describe("same-face", same))
    print(describe("diff-face", diff))
    print()
    print("=== 5. BASELINES on the SAME pairs ===")
    print(describe("centre same", same_c))
    print(describe("centre diff", diff_c))
    print(describe("census same", same_x))
    print(describe("census diff", diff_x))
    print()
    print("same-face ordered-pattern distance BY TIME SEPARATION:")
    for bucket in ("<0.5s", "0.5-2s", ">2s"):
        print(describe(bucket, same_by_dt.get(bucket, [])))

    print()
    print("=== 3b. SEPARATION against the colour-derived different-face pairs ===")
    print(describe("pattern", diff2))
    print(describe("centre*", diff2_c))
    print(describe("census~", diff2_x))
    print("  (* selection-circular, ~ partly correlated with the selection)")

    print()
    print("thresholds vs pose-continuity different-face pairs:")
    thr, fm, fs = separation(same, diff, "ordered pattern", 0.0, 60.0, 0.25)
    separation(same_c, diff_c, "centre Lab", 0.0, 90.0, 0.25)
    separation(same_x, diff_x, "census L1", 0.0, 18.0, 1.0)

    print()
    print("thresholds vs colour-derived different-face pairs:")
    thr, fm, fs = separation(same, diff2, "ordered pattern", 0.0, 60.0, 0.25)
    separation(same_c, diff2_c, "centre Lab*", 0.0, 90.0, 0.25)
    separation(same_x, diff2_x, "census L1~", 0.0, 18.0, 1.0)

    print()
    print("=== 4. DISTINCT FACES PER CLIP ===")
    print("(a) ground truth, from the reconstructed near-face axis within each chain;")
    print("    chains are independent, so this counts distinct faces a chain SAW,")
    print("    then takes the maximum over chains as the per-clip lower bound.")
    for clip in sorted({r.clip for r in rows}):
        per_chain = defaultdict(set)
        for r in labelled:
            if r.clip == clip:
                per_chain[r.chain].add(r.face)
        counts = sorted((len(v) for v in per_chain.values()), reverse=True)
        multi = [c for c in counts if c >= 2]
        print(
            f"  {clip}: max distinct faces in one chain={counts[0] if counts else 0}  "
            f"chains seeing >=2 faces={len(multi)}  chain face-count histogram="
            f"{dict(Counter(counts))}"
        )

    print()
    print("(b) single-link clustering of ALL capture-grade readings, per clip, with")
    print("    NO pose used at all - swept over threshold. A working single-frame")
    print("    identifier would show a stable plateau at six (or at however many")
    print("    faces the clip actually shows); a collapse straight from many")
    print("    clusters to one means no threshold separates the faces.")
    sweep = [4.0, 6.0, 8.0, 10.0, 12.0, 15.0, 20.0, 25.0, 30.0]
    print("        readings" + "".join(f"{t:>7.0f}" for t in sweep))
    for clip in sorted({r.clip for r in rows}):
        members = [r for r in good if r.clip == clip]
        counts = "".join(f"{cluster_count(members, t):>7d}" for t in sweep)
        print(f"  {clip:<6}{len(members):>8d}{counts}")

    print()
    print("=== CROSS-CLIP (live1-4 one scramble, live5 another) ===")
    print("Nearest-pattern distance from each clip's readings to each other clip's,")
    print("as the 5th percentile of per-reading nearest-neighbour distance:")
    clips = sorted({r.clip for r in rows})
    by_clip_good = {c: [r for r in good if r.clip == c] for c in clips}
    header = "        " + "".join(f"{c:>10}" for c in clips)
    print(header)
    for a in clips:
        cells = []
        for b in clips:
            if a == b:
                cells.append("       -")
                continue
            nn = []
            for ra in by_clip_good[a][::3]:
                nn.append(min(pattern_distance(ra, rb) for rb in by_clip_good[b][::3]))
            cells.append(f"{pct(nn,5):10.2f}")
        print(f"  {a:<6}" + "".join(cells))

    print()
    print("=== 4c. DISTINCT FACES PER CLIP, by centre colour class ===")
    print("The decisive count. A cube's six centres are six DISTINCT colours, so")
    print("the number of centre classes a clip produces capture-grade readings of")
    print("is the number of physical faces that clip actually SAW well. This is")
    print("independent of any tracking, routing or pattern matching.")
    for clip in sorted({r.clip for r in good}):
        members = [r for r in good if r.clip == clip]
        counts = Counter(r.centre_class for r in members)
        named = {
            COLOR_NAMES.get(k, str(k)): v
            for k, v in sorted(counts.items(), key=lambda kv: -kv[1])
        }
        distinct = len([k for k in counts if k != 0])
        print(f"  {clip}: readings={len(members)} distinct centres={distinct} {named}")

    print()
    print("=== 5b. THE DECISION TASK: leave-one-out nearest-neighbour ===")
    print("Pair statistics do not say whether a ROUTER would work; a nearest-")
    print("neighbour assignment does. Label = centre colour class, which is")
    print("independent of the eight non-centre cells the ordered pattern scores,")
    print("so the pattern's number is fair. The neighbour is drawn from the same")
    print("clip but a DIFFERENT chain, so no near-duplicate consecutive frame can")
    print("be its own answer.")
    print("CAVEAT: the centre-Lab baseline predicts the label from the label's own")
    print("channel and is circular - read its number as 'centre colour is a stable")
    print("per-face signature', not as a like-for-like score.")
    metrics = (("pattern", pattern_distance), ("centre*", centre_distance),
               ("census", census_distance))
    totals = Counter()
    for clip in sorted({r.clip for r in good}):
        members = [r for r in good if r.clip == clip and r.centre_class != 0]
        res = Counter()
        for a in members:
            cands = [b for b in members if b.chain != a.chain]
            if not cands:
                continue
            for name, fn in metrics:
                nn = min(cands, key=lambda b: fn(a, b))
                res[name + "_n"] += 1
                res[name + "_ok"] += int(nn.centre_class == a.centre_class)
        totals.update(res)
        line = f"  {clip}: n={res['pattern_n']}"
        for name, _ in metrics:
            line += f"  {name}={res[name + '_ok'] / max(1, res[name + '_n']) * 100:5.1f}%"
        print(line)
    line = f"  ALL  : n={totals['pattern_n']}"
    for name, _ in metrics:
        line += f"  {name}={totals[name + '_ok'] / max(1, totals[name + '_n']) * 100:5.1f}%"
    print(line)
    labelled_all = [r for r in good if r.centre_class != 0]
    chance = Counter(r.centre_class for r in labelled_all)
    print(f"  chance (majority class) = {max(chance.values()) / len(labelled_all) * 100:.1f}%")

    print()
    print("=== CROSS-CHECK: tracker agreement (never an input above) ===")
    agree = dis = 0
    for members in usable.values():
        for a, b in itertools.combinations(members, 2):
            if a.trk_front == "-" or b.trk_front == "-" or a.trk_epoch != b.trk_epoch:
                continue
            if (a.trk_front == b.trk_front) == (a.face == b.face):
                agree += 1
            else:
                dis += 1
    total = agree + dis
    print(
        f"  same-epoch pairs where the tracker also claims a front face: {total}  "
        f"agreement with the reconstructed labels: "
        f"{(agree / total * 100) if total else float('nan'):.2f}%"
    )


def cluster_count(members, threshold):
    """Single-link agglomeration under the pattern distance."""
    parent = list(range(len(members)))

    def find(i):
        while parent[i] != i:
            parent[i] = parent[parent[i]]
            i = parent[i]
        return i

    for i in range(len(members)):
        for j in range(i + 1, len(members)):
            if find(i) != find(j) and pattern_distance(members[i], members[j]) <= threshold:
                parent[find(i)] = find(j)
    return len({find(i) for i in range(len(members))})


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else "readings.csv")
