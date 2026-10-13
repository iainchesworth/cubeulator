#!/usr/bin/env python3
"""Build the click-through prototype and the generated parts of the docs.

    python docs/design/tools/build.py [--artifact PATH] [--unused]

Reads tokens.json, strings.json and journey.md, and writes:

  prototype/cubeulator-prototype.html   one self-contained page
  content.md                            the string inventory, between markers

Fails if the prototype uses a string key that strings.json does not have.
The prototype therefore cannot show wording that is not in the inventory.
"""
from __future__ import annotations

import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SRC = ROOT / "prototype" / "src" / "prototype.html"
OUT = ROOT / "prototype" / "cubeulator-prototype.html"
CONTENT = ROOT / "content.md"
BEGIN, END = "<!-- strings:begin -->", "<!-- strings:end -->"

# The repository copy is a whole document, so it opens from disk. The copy
# published as an Artifact is the same page without this wrapper, which the
# publisher adds itself.
WRAP_HEAD = """<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1, viewport-fit=cover">
<style>:root{color-scheme:light;padding:env(safe-area-inset-top,0px) 0 env(safe-area-inset-bottom,0px)}body{margin:0}[hidden]{display:none!important}</style>
</head>
<body>
"""
WRAP_TAIL = """
</body>
</html>
"""

KEY_LITERAL = re.compile(r"""['"]([A-Za-z][A-Za-z0-9]*(?:\.[A-Za-z0-9_]*)+)['"]""")
T_CALL = re.compile(r"(?<![A-Za-z0-9_$.])t\(")


def load_strings():
    raw = json.loads((ROOT / "strings.json").read_text(encoding="utf-8"))
    return {k: v for k, v in raw.items() if not k.startswith("$")}


def css_vars(tokens):
    color = tokens["color"]

    def block(theme):
        return "".join(f"--{name}:{value[theme]};" for name, value in color.items())

    light, dark = block("light"), block("dark")
    fixed = "".join(f"--ov-{name}:{v['value']};" for name, v in tokens["overlay"].items() if not name.startswith("$"))
    fixed += f"--ov-panel-a:{tokens['overlay']['panel']['alpha']};"
    for name, v in tokens["cube"].items():
        if name.startswith("$"):
            continue
        fixed += f"--cube-{name}:{v['value']};--mark-{name}:{v['markInk']};"
    return (
        f":root{{{light}{fixed}}}\n"
        f'@media (prefers-color-scheme: dark){{:root:not([data-theme="light"]){{{dark}}}}}\n'
        f':root[data-theme="dark"]{{{dark}}}\n'
        f'.device[data-app-theme="light"]{{{light}}}\n'
        f'.device[data-app-theme="dark"]{{{dark}}}\n'
    )


def parse_journey():
    """Screens, lanes and state ids, in the order journey.md gives them."""
    text = (ROOT / "journey.md").read_text(encoding="utf-8")
    body = text.split("## Screens and states", 1)[1].split("## Interruptions", 1)[0]
    screens, lane = [], "experience"
    by_name = {}
    for line in body.splitlines():
        m = re.match(r"Lane: \*{0,2}(\w+)", line)
        if m:
            lane = m.group(1)
        m = re.match(r"\| `([a-z-]+)\.([a-z-]+)` \| (.*?) \|", line)
        if not m:
            continue
        screen, state, when = m.groups()
        if screen not in by_name:
            by_name[screen] = {"screen": screen, "lane": lane, "states": []}
            screens.append(by_name[screen])
        by_name[screen]["lane"] = lane
        by_name[screen]["states"].append({"id": state, "when": re.sub(r"[`*]", "", when)})
    return screens


def keys_used(html, strings):
    """Every string literal inside a t( ... ) call is a key, or a key prefix
    that the script completes at run time (it then ends with a dot)."""
    used, missing = set(), set()
    namespaces = {k.split(".", 1)[0] for k in strings}
    for m in T_CALL.finditer(html):
        depth, i = 1, m.end()
        while depth and i < len(html):
            depth += {"(": 1, ")": -1}.get(html[i], 0)
            i += 1
        for lit in KEY_LITERAL.findall(html[m.end():i]):
            if lit.endswith("."):
                used |= {k for k in strings if k.startswith(lit)}
            elif lit in strings:
                used.add(lit)
            elif lit.split(".", 1)[0] in namespaces:
                missing.add(lit)
    return used, missing


def inventory_markdown(strings):
    groups = {}
    for key in strings:
        groups.setdefault(key.split(".", 1)[0], []).append(key)
    out = [f"{len(strings)} strings. Generated from `strings.json` by `tools/build.py`; edit that file, not this table.\n"]
    for group, keys in groups.items():
        out.append(f"\n#### `{group}`\n")
        out.append("| key | English | note for translators |")
        out.append("|---|---|---|")
        for key in keys:
            text, note = strings[key]
            if isinstance(text, dict):
                text = "<br>".join(f"*{form}:* {value}" for form, value in text.items())
            text = text.replace("\n", " / ").replace("|", "\\|")
            note = note.replace("|", "\\|")
            out.append(f"| `{key}` | {text} | {note} |")
    return "\n".join(out) + "\n"


def main():
    tokens = json.loads((ROOT / "tokens.json").read_text(encoding="utf-8"))
    strings = load_strings()
    journey = parse_journey()
    html = SRC.read_text(encoding="utf-8")

    used, missing = keys_used(html, strings)
    if missing:
        print("Prototype uses strings that are not in strings.json:", file=sys.stderr)
        for k in sorted(missing):
            print("  " + k, file=sys.stderr)
        return 1

    marks = {n: v["mark"] for n, v in tokens["cube"].items() if not n.startswith("$")}
    data = {
        "STR": {k: v[0] for k, v in strings.items()},
        "JOURNEY": journey,
        "MARKS": marks,
        "IDENTITY": {f.stem: f.read_text(encoding="utf-8") for f in sorted((ROOT / "identity").glob("*.svg"))},
    }
    html = html.replace("/*__TOKENS__*/", css_vars(tokens))
    html = html.replace("/*__DATA__*/", "const DATA = " + json.dumps(data, ensure_ascii=False) + ";")
    OUT.write_text(WRAP_HEAD + html + WRAP_TAIL, encoding="utf-8", newline="\n")
    if "--artifact" in sys.argv:
        Path(sys.argv[sys.argv.index("--artifact") + 1]).write_text(html, encoding="utf-8", newline="\n")

    if CONTENT.exists():
        text = CONTENT.read_text(encoding="utf-8")
        if BEGIN in text and END in text:
            head, rest = text.split(BEGIN, 1)
            tail = rest.split(END, 1)[1]
            CONTENT.write_text(head + BEGIN + "\n" + inventory_markdown(strings) + END + tail,
                               encoding="utf-8", newline="\n")

    states = sum(len(s["states"]) for s in journey)
    print(f"built {OUT.relative_to(ROOT.parent.parent).as_posix()}: {len(html) // 1024} KB, "
          f"{len(strings)} strings ({len(used)} used by the prototype), "
          f"{len(journey)} screens, {states} states")
    if "--unused" in sys.argv:
        for k in sorted(k for k in strings if k not in used):
            print("  not in prototype: " + k)
    return 0


if __name__ == "__main__":
    sys.exit(main())
