"""How often does a recorded session show two or three sides at once?

Runs corner.py's oblique lattice finder over every frame of a session and
prints the frames with two or more lattices that share an edge.

  python corner_scan.py <session-dir> [stride]
"""
import os
import sys
import time

import cv2

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import corner  # noqa: E402


def main():
    session = sys.argv[1]
    stride = int(sys.argv[2]) if len(sys.argv) > 2 else 1
    names = sorted(f for f in os.listdir(session) if f.startswith("rec_") and f.endswith(".jpg"))
    started = time.time()
    counts = {}
    hits = []
    for n in range(0, len(names), stride):
        bgr = cv2.imread(os.path.join(session, names[n]))
        lattices = corner.find_lattices(corner.blobs_oblique(bgr))
        edges = corner.shared_edges(lattices)
        key = (len(lattices), len(edges))
        counts[key] = counts.get(key, 0) + 1
        if len(lattices) >= 2:
            hits.append((n, len(lattices), [lat["n"] for lat in lattices],
                         [(a, b, len(m)) for a, b, m in edges]))
    print("session %s: %d frames in %.0fs" % (os.path.basename(session), len(names) // stride,
                                              time.time() - started))
    print("  (lattices, shared edges): count ->", dict(sorted(counts.items())))
    print("  frames with two or more lattices:", len(hits))
    for h in hits[:40]:
        print("   ", h)


if __name__ == "__main__":
    main()
