#!/usr/bin/env python3
"""Per-face reading-consistency measurement over a "[.pattern-id]" CSV.

THE QUESTION. Production must decide, from ONE frame, what nine colours a
face is showing. If two readings of the SAME physical face produce
different nine-sticker strings, every downstream consumer - routing,
verification, consensus voting, whole-cube assembly - is being fed noise
rather than evidence. So: replay a clip, group its capture-grade readings
by which physical face they show, and count how many DISTINCT strings each
face produced.

THE BASELINE this was written to expose: classifying each sticker
independently with classify_lab's absolute hue bands, ONE physical face
read 67 times produced 57 different strings (live4, modal reading 6% of
readings). That is the measurement that identified per-sticker colour
classification, not tracking or routing, as the blocker.

BOTH columns are produced by PRODUCTION CODE inside the harness
(pattern_id_test.cpp) rather than re-derived here, so a difference between
them is a difference in the shipped classifiers and not in two Python
transcriptions of them:

    abs_pattern   classify_lab() per sticker            (baseline)
    sess_pattern  SessionColorModel over the same face  (session-relative)

GROUPING. Readings are grouped by `centre_class` - classify_lab's name for
the centre sticker - for BOTH columns. Holding the partition fixed is what
makes the two numbers comparable: if each column were grouped by its own
centre naming, a classifier that merged two faces into one group would
score better for merging them. The partition is the baseline's, and it is
imperfect, which makes this a CONSERVATIVE test of the session model - it
is scored on a grouping it did not choose.

    python tools/face_reading_consistency.py <csv> [<csv> ...]
"""

import argparse
import collections
import csv
import sys

CENTRE_NAMES = {
    "0": "unknown", "1": "white", "2": "yellow",
    "3": "red", "4": "orange", "5": "green", "6": "blue",
}


def csv_rows(handle):
    """Dict rows, skipping the harness preamble.

    The harness writes the CSV to stdout, which Catch2 has already put its
    own banner on ("Filters: ...", the run header). Rather than ask the
    caller to strip it - and silently mis-parse when they forget, which is
    how this returned an empty table the first time - seek forward to the
    line that starts the CSV.
    """
    lines = handle.read().splitlines()
    for index, line in enumerate(lines):
        if line.startswith("clip,pass,"):
            return csv.DictReader(lines[index:])
    return iter(())


def tabulate(path, column):
    """Rows of (clip, centre, readings, distinct, modal, modal_share)."""
    by_face = collections.defaultdict(list)
    with open(path, newline="") as handle:
        for row in csv_rows(handle):
            if row.get("face_good") not in ("1", "true", "True"):
                continue
            pattern = row.get(column)
            if not pattern:
                continue
            by_face[(row["clip"], row["centre_class"])].append(pattern)

    out = []
    for (clip, centre), patterns in by_face.items():
        counts = collections.Counter(patterns)
        modal, hits = counts.most_common(1)[0]
        out.append((clip, CENTRE_NAMES.get(centre, centre), len(patterns),
                    len(counts), modal, 100.0 * hits / len(patterns)))
    # Most-observed faces first: those are the ones carrying the evidence.
    out.sort(key=lambda r: (r[0], -r[2]))
    return out


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("csv", nargs="+")
    parser.add_argument("--columns", default="abs_pattern,sess_pattern")
    args = parser.parse_args()
    columns = args.columns.split(",")

    rows = {column: [] for column in columns}
    for path in args.csv:
        for column in columns:
            rows[column].extend(tabulate(path, column))

    index = {}
    for column in columns:
        for clip, centre, n, distinct, modal, share in rows[column]:
            index.setdefault((clip, centre), {"n": n})[column] = (distinct, share, modal)

    header = "| clip | centre | readings |"
    divider = "|---|---|---|"
    for column in columns:
        header += " distinct (%s) | modal %% (%s) |" % (column, column)
        divider += "---|---|"
    print(header)
    print(divider)

    totals = {column: [0, 0.0, 0] for column in columns}
    for (clip, centre), entry in sorted(index.items(), key=lambda kv: (kv[0][0], -kv[1]["n"])):
        line = "| %s | %s | %d |" % (clip, centre, entry["n"])
        for column in columns:
            distinct, share, _ = entry.get(column, (0, 0.0, ""))
            line += " %d | %.0f%% |" % (distinct, share)
            totals[column][0] += distinct
            totals[column][1] += share * entry["n"]
            totals[column][2] += entry["n"]
        print(line)

    print()
    for column in columns:
        distinct_sum, weighted, n = totals[column]
        print("%-14s total distinct strings %4d   reading-weighted modal share %.1f%%"
              % (column, distinct_sum, weighted / n if n else 0.0))
    return 0


if __name__ == "__main__":
    sys.exit(main())
