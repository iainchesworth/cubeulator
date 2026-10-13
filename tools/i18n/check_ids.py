#!/usr/bin/env python3
"""Every id the code asks for must be in docs/design/strings.json.

An id that is in the code and not in the catalogue is a string the English
catalogue cannot answer, so the one thing the fall-back cannot save is a raw
id on the screen. Qt's own extractor is used, so that what is checked is what
the translation tooling would see: lupdate over the QML and C++ of the app
(qsTrId, qtTrId, QT_TRID_NOOP).

    check_ids.py --lupdate <path to lupdate>

Also reports, without failing, the ids the catalogue holds that no code asks
for yet: most of the design is not built, and this is how that shrinks.

Standard library only.
"""

from __future__ import annotations

import argparse
import json
import re
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
STRINGS = ROOT / "docs" / "design" / "strings.json"
SOURCES = ROOT / "src" / "app" / "qt"


def catalogue_ids() -> set[str]:
    raw = json.loads(STRINGS.read_text(encoding="utf-8"))
    return {k for k in raw if not k.startswith("$")}


def code_ids(lupdate: str) -> dict[str, list[str]]:
    """id -> the files that ask for it, from lupdate."""
    with tempfile.TemporaryDirectory() as tmp:
        out = Path(tmp) / "extracted.ts"
        files = [str(p) for ext in ("qml", "cpp", "hpp") for p in sorted(SOURCES.rglob(f"*.{ext}"))]
        cmd = [lupdate, "-extensions", "qml,cpp,hpp", "-locations", "relative", "-noobsolete", *files, "-ts", str(out)]
        proc = subprocess.run(cmd, capture_output=True, text=True, encoding="utf-8", errors="replace")
        if proc.returncode != 0 or not out.exists():
            sys.stderr.write(proc.stdout + proc.stderr)
            raise SystemExit("lupdate failed")
        found: dict[str, list[str]] = {}
        for message in ET.parse(out).getroot().iter("message"):
            mid = message.get("id")
            if not mid:
                continue
            where = [loc.get("filename", "?") for loc in message.findall("location")]
            found[mid] = where
        return found


def main(argv: list[str]) -> int:
    sys.stdout.reconfigure(encoding="utf-8")  # type: ignore[attr-defined]
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--lupdate", required=True, help="path to Qt's lupdate")
    args = ap.parse_args(argv)

    wanted = code_ids(args.lupdate)
    have = catalogue_ids()
    unknown = sorted(set(wanted) - have)
    unused = sorted(have - set(wanted))

    # A call lupdate cannot read: the id is not a literal.
    dynamic: list[str] = []
    for path in sorted(SOURCES.rglob("*.qml")):
        text = path.read_text(encoding="utf-8")
        for m in re.finditer(r"\b(qsTrId|qtTrId)\s*\(\s*([^\s\"')])", text):
            dynamic.append(f"{path.relative_to(ROOT).as_posix()}: {m.group(0)}...")

    ok = True
    if unknown:
        ok = False
        print("Ids the code asks for that docs/design/strings.json does not have:\n")
        for mid in unknown:
            print(f"  {mid}   ({', '.join(sorted(set(wanted[mid]))) or 'unknown location'})")
        print("\nAdd each to strings.json, or fix the typo. Until then the screen would show the id.")
    if dynamic:
        ok = False
        print("\nAn id that is not a literal cannot be checked, extracted or found by the lint:\n")
        for d in dynamic:
            print(f"  {d}")
    if ok:
        print(f"{len(wanted)} ids in the code, all in strings.json; {len(unused)} of its {len(have)} not used by any code yet.")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
