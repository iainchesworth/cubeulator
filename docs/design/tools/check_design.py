#!/usr/bin/env python3
"""Check that the design documents agree with each other.

    python docs/design/tools/check_design.py

This is the E1 gate made mechanical. It fails when:

  1. a state in journey.md has no spec, or a spec heading names a state
     that journey.md does not have;
  2. a spec's "string" column names a key that strings.json does not have;
  3. a string in strings.json is named by no document at all, so nothing
     says where it is shown;
  4. the prototype has no renderer for a screen in journey.md.

A key may be named exactly (`home.scan`) or as a family (`stage.*`).
"""
from __future__ import annotations

import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
TICKED = re.compile(r"`([^`]+)`")
KEYLIKE = re.compile(r"^[A-Za-z][A-Za-z0-9]*(\.[A-Za-z0-9_]+)*(\.\*)?$")


def journey_ids():
    text = (ROOT / "journey.md").read_text(encoding="utf-8")
    body = text.split("## Screens and states", 1)[1].split("## Interruptions", 1)[0]
    return re.findall(r"^\| `([a-z-]+\.[a-z-]+)` \|", body, flags=re.M)


def spec_headings():
    found = {}
    for path in sorted((ROOT / "screens").glob("*.md")):
        for m in re.finditer(r"^### `([a-z-]+\.[a-z-]+)`\s*$", path.read_text(encoding="utf-8"), flags=re.M):
            found.setdefault(m.group(1), []).append(path.name)
    return found


def string_columns(path):
    """Yield the ticked tokens in every table column headed 'string' or 'strings'."""
    column = None
    for line in path.read_text(encoding="utf-8").splitlines():
        if not line.startswith("|"):
            column = None
            continue
        cells = [c.strip() for c in line.strip().strip("|").split("|")]
        if column is None:
            heads = [c.lower() for c in cells]
            column = next((i for i, h in enumerate(heads) if h in ("string", "strings")), -1)
            continue
        if column >= 0 and column < len(cells) and not set(cells[column]) <= set("-: "):
            yield from TICKED.findall(cells[column])


def matches(token, keys):
    if token.endswith(".*"):
        return {k for k in keys if k.startswith(token[:-1])}
    return {token} if token in keys else set()


def main():
    problems = []
    raw = json.loads((ROOT / "strings.json").read_text(encoding="utf-8"))
    keys = {k for k in raw if not k.startswith("$")}

    # 1. journey and specs name the same states
    states = journey_ids()
    specs = spec_headings()
    if len(states) != len(set(states)):
        problems.append("journey.md lists a state twice: " + ", ".join(sorted({s for s in states if states.count(s) > 1})))
    for s in states:
        if s not in specs:
            problems.append(f"no spec for state {s}")
        elif specs[s] != [s.split(".")[0] + ".md"]:
            problems.append(f"state {s} is specified in {', '.join(specs[s])}; expected {s.split('.')[0]}.md only")
    for s in specs:
        if s not in states:
            problems.append(f"{specs[s][0]} specifies {s}, which journey.md does not have")

    # 2. every string a spec names exists
    named = set()
    for path in sorted((ROOT / "screens").glob("*.md")):
        for token in string_columns(path):
            if not KEYLIKE.match(token):
                continue
            hit = matches(token, keys)
            if not hit:
                problems.append(f"{path.name} names a string that does not exist: {token}")
            named |= hit

    # 3. every string is named somewhere
    docs = [p for p in ROOT.glob("*.md")] + list((ROOT / "screens").glob("*.md")) + list((ROOT / "identity").glob("*.md"))
    for path in docs:
        text = path.read_text(encoding="utf-8")
        if "<!-- strings:begin -->" in text:   # the generated inventory names every key; it does not count
            text = text.split("<!-- strings:begin -->")[0] + text.split("<!-- strings:end -->")[1]
        for token in TICKED.findall(text):
            if KEYLIKE.match(token):
                named |= matches(token, keys)
    for k in sorted(keys - named):
        problems.append(f"string {k} is not named by any document")

    # 4. the prototype draws every screen
    src = "".join(p.read_text(encoding="utf-8") for p in (ROOT / "prototype" / "src").glob("*") if p.suffix in (".html", ".js"))
    for screen in dict.fromkeys(s.split(".")[0] for s in states):
        if not re.search(r"screens(\.%s|\['%s'\])\s*=" % (re.escape(screen), re.escape(screen)), src):
            problems.append(f"the prototype has no renderer for {screen}")

    screens = len({s.split('.')[0] for s in states})
    print(f"{len(states)} states in {screens} screens; {len(keys)} strings; {len(problems)} problems")
    for p in problems:
        print("  " + p)
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
