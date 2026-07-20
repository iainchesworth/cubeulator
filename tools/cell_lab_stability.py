#!/usr/bin/env python3
"""Per-cell photometric stability over a "[.pattern-id]" CSV.

WHY THIS EXISTS. Session-relative colour classification
(SessionColorModel) was built on the diagnosis that per-sticker naming
was too noisy to name a face, and it barely moved the metric: 788
distinct nine-sticker strings became 766, modal share 23.2% -> 24.2%.
This script is what explained the null result, and it should be run
BEFORE any further work on colour naming, because it measures whether
the INPUT to naming is stable enough for naming to be the problem.

WHAT IT MEASURES. Readings are grouped by centre colour, roll-aligned to
the group's per-cell median (two screen readings of one physical face
differ by a pure in-plane grid rotation - see
pattern_id_test.cpp's PatternIdRollsMatchBodyRelabel), and each cell's
Lab deviation from that median is reported by CELL POSITION.

WHAT IT FOUND, on live1..live5:

    centre  median  7.1 Lab   p90 24.8   17% beyond 18 Lab
    edge    median 19.0 Lab   p90 37.6   52% beyond 18 Lab
    corner  median 17.6 Lab   p90 39.5   49% beyond 18 Lab

The centre sticker - the SAME physical sticker in every reading of a
face - is roughly 2.5x more stable than any off-centre sticker, and half
of all off-centre samples move further between readings than the closest
two colours a real cube can show are apart (kClosestCentrePairLab = 27,
red against orange). A sticker whose Lab wanders 18-38 units between
readings cannot be named consistently by ANY classifier, session-relative
or absolute, because the wander spans several colours.

That gradient is radial and roughly uniform off-centre rather than
concentrated in the corners, which is what a pure grid-SCALE error would
produce. It points at cell sampling geometry - where the nine sample
patches are placed and how much sticker versus bezel each one integrates
- one layer BELOW colour naming.

    python tools/cell_lab_stability.py <csv> [<csv> ...]
"""

import argparse
import collections
import csv
import math
import statistics
import sys

# Clockwise 90-degree rotation of a 3x3 grid as a cell permutation:
# out[ROT90[k]] = in[k]. Identical to the roll set production searches.
ROT90 = [2, 5, 8, 1, 4, 7, 0, 3, 6]

CELL_CLASS = {4: "centre", 1: "edge", 3: "edge", 5: "edge", 7: "edge"}


def rolls(cells):
    out = [list(cells)]
    current = list(cells)
    for _ in range(3):
        nxt = [None] * 9
        for k in range(9):
            nxt[ROT90[k]] = current[k]
        current = nxt
        out.append(list(current))
    return out


def csv_rows(handle):
    """Dict rows, skipping the Catch2 banner the harness writes first."""
    lines = handle.read().splitlines()
    for index, line in enumerate(lines):
        if line.startswith("clip,pass,"):
            return csv.DictReader(lines[index:])
    return iter(())


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("csv", nargs="+")
    args = parser.parse_args()

    groups = collections.defaultdict(list)
    for path in args.csv:
        with open(path, newline="") as handle:
            for row in csv_rows(handle):
                if row.get("face_good") != "1" or not row.get("abs_pattern"):
                    continue
                groups[(row["clip"], row["centre_class"])].append(
                    [(float(row["L%d" % k]), float(row["a%d" % k]), float(row["b%d" % k]))
                     for k in range(9)])

    by_class = collections.defaultdict(list)
    for readings in groups.values():
        # Per-cell median is the reference: robust to the minority of
        # readings that are badly misfit, which a mean would not be.
        reference = [[statistics.median([r[c][i] for r in readings]) for i in range(3)]
                     for c in range(9)]
        for reading in readings:
            aligned = min(rolls(reading),
                          key=lambda cand: sum(math.dist(cand[c], reference[c]) for c in range(9)))
            for c in range(9):
                by_class[CELL_CLASS.get(c, "corner")].append(math.dist(aligned[c], reference[c]))

    print("%-7s %8s  %7s %7s %7s  %s" % ("cell", "samples", "median", "p75", "p90", ">18 Lab"))
    for name in ("centre", "edge", "corner"):
        deviations = sorted(by_class[name])
        if not deviations:
            continue
        beyond = 100.0 * sum(1 for d in deviations if d > 18.0) / len(deviations)
        print("%-7s %8d  %7.1f %7.1f %7.1f  %6.1f%%"
              % (name, len(deviations), statistics.median(deviations),
                 deviations[int(0.75 * len(deviations))], deviations[int(0.90 * len(deviations))],
                 beyond))
    return 0


if __name__ == "__main__":
    sys.exit(main())
