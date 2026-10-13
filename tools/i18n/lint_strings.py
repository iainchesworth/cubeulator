#!/usr/bin/env python3
"""Fail on a user-visible string literal that is not an id lookup.

Everything a person reads in Cubeulator is `qsTrId("some.id")` in QML, and the
id is in docs/design/strings.json. A literal that reaches the screen does not
get translated, does not appear in the pseudo-locale and cannot be fallen back
from: it is the one way a string escapes every gate in docs/i18n.md. This is
the gate that closes it.

What it looks at, in every .qml file under src/app/qt:

  * a property that is shown or spoken - text, title, label, description,
    placeholderText, toolTip, anything ending in Text, Label, Title, Message,
    Caption or Hint, and Accessible.name and Accessible.description - given a
    string literal, alone or inside an expression;
  * a string built from a literal and something else (`"Camera " + name`):
    that is a sentence made of fragments, which no translator can reorder;
  * qsTr(...), which keys a string by its English: use qsTrId.

A literal with no letter in it ("·", "%1", "×") is not text and is left alone.
`objectName`, ids, colours, icon names and the like are not on the list and
are never looked at.

And in the C++ under src/app/qt, as a heuristic: a QStringLiteral, QLatin1String
or QString::fromUtf8 of two or more words that is not a log line. Controllers
expose codes and states; QML turns them into text (docs/i18n.md).

Files that belong to another lane are listed in tools/i18n/lint_allowlist.json,
literal by literal, each with the lane that owns it. A new literal in such a
file fails like any other; an entry whose literal has gone fails too, so the
list can only get shorter. `--report` writes the table of what each lane has
to change.

Standard library only.
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from dataclasses import dataclass
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
QT_DIR = ROOT / "src" / "app" / "qt"
STRINGS = ROOT / "docs" / "design" / "strings.json"
ALLOWLIST = Path(__file__).resolve().parent / "lint_allowlist.json"

# Properties that put text in front of a person (or a screen reader).
VISIBLE_EXACT = {
    "text", "title", "label", "description", "placeholderText", "toolTip",
    "Accessible.name", "Accessible.description", "ToolTip.text", "windowTitle",
    "subtitle", "summary", "helpText", "statusText", "headline", "heading",
    "guidance", "prompt", "hint", "caption", "status",
}
# A value that is a name or a path, never shown, whatever words it has.
NOT_TEXT_PROPS = re.compile(
    r"(?:^|[\s{,(])(?:objectName|source|family|fontHeading|fontMono|headingFamily|"
    r"font\.family|fontFamily|shortcut|key|state|type|name|iconName|variant|"
    r"target|property|from|to)\s*:\s*$"
)
# A canvas font ("bold 14px sans-serif") is a specification, not words.
FONT_SPEC = re.compile(r"(?:^|\s)\d+(?:\.\d+)?px(?:\s|$)|sans-serif|monospace", re.IGNORECASE)
# Two words of letters: a phrase rather than an identifier or a name.
PHRASE = re.compile(r"[^\W\d_]{2,}[\s ]+[^\W\d_]{2,}", re.UNICODE)
VISIBLE_SUFFIX = re.compile(r"(Text|Label|Title|Message|Caption|Hint)$")
# Calls whose first argument is shown or announced.
VISIBLE_CALLS = re.compile(r"\b(announce|showToast|showMessage|speak)\s*\(\s*(?=[\"'])")

HAS_LETTER = re.compile(r"[^\W\d_]", re.UNICODE)


@dataclass(frozen=True)
class Hit:
    file: str
    line: int
    literal: str
    kind: str  # literal | fragment | qsTr | cpp

    def key(self) -> tuple[str, str]:
        return (self.file, self.literal)


# --- QML ---------------------------------------------------------------------


def _strip_comments(src: str) -> str:
    """Blank out comments, keeping every offset and line break."""
    out = []
    i, n = 0, len(src)
    quote = ""
    while i < n:
        c = src[i]
        if quote:
            out.append(c)
            if c == "\\" and i + 1 < n:
                out.append(src[i + 1])
                i += 2
                continue
            if c == quote:
                quote = ""
            i += 1
            continue
        if c in "\"'`":
            quote = c
            out.append(c)
            i += 1
            continue
        if c == "/" and i + 1 < n and src[i + 1] == "/":
            j = src.find("\n", i)
            j = n if j < 0 else j
            out.append(" " * (j - i))
            i = j
            continue
        if c == "/" and i + 1 < n and src[i + 1] == "*":
            j = src.find("*/", i + 2)
            j = n if j < 0 else j + 2
            out.append(re.sub(r"[^\n]", " ", src[i:j]))
            i = j
            continue
        out.append(c)
        i += 1
    return "".join(out)


def _string_spans(expr: str) -> list[tuple[int, int, str]]:
    """(start, end, text) of each string literal in `expr`."""
    spans = []
    i, n = 0, len(expr)
    while i < n:
        c = expr[i]
        if c in "\"'":
            j = i + 1
            while j < n and expr[j] != c:
                j += 2 if expr[j] == "\\" else 1
            spans.append((i, j + 1, expr[i + 1 : j]))
            i = j + 1
            continue
        i += 1
    return spans


def _statement_end(src: str, start: int) -> int:
    """Where the expression that begins at `start` ends."""
    depth = 0
    i, n = start, len(src)
    quote = ""
    while i < n:
        c = src[i]
        if quote:
            if c == "\\":
                i += 2
                continue
            if c == quote:
                quote = ""
            i += 1
            continue
        if c in "\"'":
            quote = c
        elif c in "([{":
            depth += 1
        elif c in ")]}":
            if depth == 0:
                return i
            depth -= 1
        elif c == ";" and depth == 0:
            return i
        elif c == "\n" and depth == 0:
            # A ternary or a concatenation continues on the next line.
            m = re.match(r"\s*([?:+|&])", src[i + 1 :])
            if not m:
                return i
        i += 1
    return n


def _is_visible(prop: str) -> bool:
    return prop in VISIBLE_EXACT or bool(VISIBLE_SUFFIX.search(prop.split(".")[-1]))


_ASSIGN = re.compile(r"(?<![\w.])([A-Za-z_][\w]*(?:\.[A-Za-z_]\w*)*)\s*(:|=(?!=)|\+=)\s*")


def lint_qml(path: Path, rel: str | None = None) -> list[Hit]:
    rel = rel or path.relative_to(ROOT).as_posix()
    text = _strip_comments(path.read_text(encoding="utf-8"))
    hits: list[Hit] = []

    def line_of(offset: int) -> int:
        return text.count("\n", 0, offset) + 1

    consumed_until = -1
    for m in _ASSIGN.finditer(text):
        if m.start() < consumed_until:
            continue
        prop = m.group(1)
        if not _is_visible(prop):
            continue
        # `import`/`property string text: ...` declarations carry a type before
        # the name; the assignment regex starts at the name, which is right.
        end = _statement_end(text, m.end())
        expr = text[m.end() : end]
        consumed_until = end
        hits.extend(_literals_in(rel, expr, line_of(m.end())))

    for m in VISIBLE_CALLS.finditer(text):
        end = _statement_end(text, m.end())
        hits.extend(_literals_in(rel, text[m.end() : end], line_of(m.end())))

    # A phrase anywhere else: text that flows to the screen through a property
    # of the screen's own (`readonly property string guidance`, a JS block that
    # returns it) is not on any list of names, but it reads as a sentence.
    seen = {(h.line, h.literal) for h in hits}
    for a, b, content in _string_spans(text):
        line = line_of(a)
        if (line, content) in seen or not PHRASE.search(content) or FONT_SPEC.search(content):
            continue
        before = text[text.rfind("\n", 0, a) + 1 : a]
        if re.match(r"\s*(import|pragma)", before):
            continue
        if re.search(r"(console\.\w+|qsTrId|qtTrId|qsTr|QT_TRID_NOOP|Qt\.createQmlObject)\s*\(\s*$", before):
            continue
        if NOT_TEXT_PROPS.search(before):
            continue
        # A comparison or a switch label is code, not text.
        if re.search(r"(===?|!==?|case)\s*$", before):
            continue
        hits.append(Hit(rel, line, content, "literal"))
    return hits


def _literals_in(rel: str, expr: str, first_line: int) -> list[Hit]:
    """The literals of one expression that are shown rather than looked up."""
    hits: list[Hit] = []
    spans = _string_spans(expr)
    if not spans:
        return hits

    # Literals inside qsTrId(...)/qtTrId(...) are the id, not shown text; the
    # argument of qsTr(...) is shown text and a different failure.
    skip: set[int] = set()
    for call in re.finditer(r"\b(qsTrId|qtTrId|QT_TRID_NOOP)\s*\(", expr):
        end = _statement_end(expr, call.end())
        for idx, (a, b, _) in enumerate(spans):
            if call.end() <= a < end:
                skip.add(idx)
    qstr: set[int] = set()
    for call in re.finditer(r"\b(qsTr|QT_TR_NOOP)\s*\(", expr):
        end = _statement_end(expr, call.end())
        for idx, (a, b, _) in enumerate(spans):
            if call.end() <= a < end:
                qstr.add(idx)

    shown = [i for i in range(len(spans)) if i not in skip]
    letters = [i for i in shown if HAS_LETTER.search(spans[i][2])]
    # A literal joined to anything but another literal is a fragment.
    joined = ("+" in re.sub(r"\"[^\"]*\"|'[^']*'", "", expr)) and len(shown) >= 1

    for i in letters:
        a, b, content = spans[i]
        if FONT_SPEC.search(content):
            continue
        kind = "qsTr" if i in qstr else ("fragment" if joined else "literal")
        hits.append(Hit(rel, first_line + expr.count("\n", 0, a), content, kind))
    return hits


# --- C++ ---------------------------------------------------------------------

_CPP_LITERAL = re.compile(
    r"(?:QStringLiteral|QLatin1String|QString::fromUtf8|QString::fromLatin1|u8|tr)\s*\(\s*\"((?:[^\"\\]|\\.)*)\""
)
_CPP_LOG = re.compile(r"\b(qDebug|qInfo|qWarning|qCritical|qFatal|qCDebug|qCWarning|Q_ASSERT\w*|qPrintable|std::fprintf|fprintf|printf)\b")
_CPP_NOT_TEXT = re.compile(r"^[\w./:#@%\-\[\]{}<>=|*+?^$\\,;'\"!()&~]*$")


def lint_cpp(path: Path, rel: str | None = None) -> list[Hit]:
    rel = rel or path.relative_to(ROOT).as_posix()
    hits: list[Hit] = []
    for lineno, line in enumerate(path.read_text(encoding="utf-8", errors="replace").splitlines(), start=1):
        stripped = line.strip()
        if stripped.startswith(("//", "*", "/*", "#include", "#define")):
            continue
        if _CPP_LOG.search(line):
            continue
        for m in _CPP_LITERAL.finditer(line):
            lit = m.group(1)
            words = [w for w in re.split(r"\s+", lit.strip()) if HAS_LETTER.search(w)]
            if len(words) < 2:
                continue
            if _CPP_NOT_TEXT.match(lit) and " " not in lit:
                continue
            hits.append(Hit(rel, lineno, lit, "cpp"))
    return hits


# --- the allowlist and the report --------------------------------------------


def load_allowlist() -> dict[str, dict]:
    raw = json.loads(ALLOWLIST.read_text(encoding="utf-8"))
    return {e["file"]: e for e in raw["entries"]}


def english_index() -> dict[str, str]:
    raw = json.loads(STRINGS.read_text(encoding="utf-8"))
    index: dict[str, str] = {}
    for key, value in raw.items():
        if key.startswith("$"):
            continue
        text = value[0]
        texts = list(text.values()) if isinstance(text, dict) else [text]
        for t in texts:
            index.setdefault(_norm(t), key)
    return index


def _norm(s: str) -> str:
    return re.sub(r"\s+", " ", s.replace("\\n", " ")).strip().rstrip(".…").lower()


def collect() -> list[Hit]:
    hits: list[Hit] = []
    for path in sorted(QT_DIR.rglob("*.qml")):
        hits.extend(lint_qml(path))
    for path in sorted(QT_DIR.rglob("*.cpp")) + sorted(QT_DIR.rglob("*.hpp")):
        hits.extend(lint_cpp(path))
    return hits


def main(argv: list[str]) -> int:
    # Literals are Unicode; a Windows console or a redirect is not, by default.
    sys.stdout.reconfigure(encoding="utf-8")  # type: ignore[attr-defined]
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--report", action="store_true", help="print what each lane has to change, as Markdown")
    ap.add_argument("--init-allowlist", action="store_true", help="print an allowlist for today's hits (to review, not to commit blind)")
    args = ap.parse_args(argv)

    hits = collect()
    allow = load_allowlist() if ALLOWLIST.exists() else {}

    if args.init_allowlist:
        entries: dict[str, list[str]] = {}
        for h in hits:
            entries.setdefault(h.file, []).append(h.literal)
        print(json.dumps({f: sorted(set(v)) for f, v in entries.items()}, indent=2, ensure_ascii=False))
        return 0

    allowed_literals = {f: set(e.get("literals", [])) for f, e in allow.items()}
    used: set[tuple[str, str]] = set()
    failures: list[Hit] = []
    for h in hits:
        if h.literal in allowed_literals.get(h.file, ()):
            used.add(h.key())
        else:
            failures.append(h)

    if args.report:
        index = english_index()
        by_owner: dict[str, list[tuple[Hit, str]]] = {}
        for h in hits:
            entry = allow.get(h.file)
            if entry is None:
                continue
            by_owner.setdefault(entry["owner"], []).append((h, index.get(_norm(h.literal), "")))
        for owner, rows in sorted(by_owner.items()):
            print(f"### {owner}\n")
            print("| file | line | literal | id in strings.json |")
            print("|---|---|---|---|")
            for h, sid in rows:
                lit = h.literal.replace("|", "\\|")
                print(f"| `{h.file.removeprefix('src/app/qt/')}` | {h.line} | {lit} | {('`' + sid + '`') if sid else 'none: needs a string'} |")
            print()
        return 0

    stale = [
        (f, lit)
        for f, lits in allowed_literals.items()
        for lit in sorted(lits)
        if (f, lit) not in used
    ]

    ok = True
    if failures:
        ok = False
        print("User-visible text that is not an id lookup:\n")
        for h in failures:
            hint = {
                "literal": "use qsTrId(\"<id>\"), with the id in docs/design/strings.json",
                "fragment": "a sentence built from pieces cannot be translated: one string, with %1 for the part that varies",
                "qsTr": "qsTr keys a string by its English: use qsTrId",
                "cpp": "controllers expose a code or a state; the wording belongs in QML",
            }[h.kind]
            print(f"  {h.file}:{h.line}: {h.literal!r}\n      {hint}")
        print(
            f"\n{len(failures)} found. Add the string to docs/design/strings.json (id, English, note), run "
            "tools/i18n/strings_to_ts.py --write, and use the id. See docs/i18n.md."
        )
    if stale:
        ok = False
        print("\nAllowlist entries for text that is no longer there (remove them from tools/i18n/lint_allowlist.json):\n")
        for f, lit in stale:
            print(f"  {f}: {lit!r}")
    if ok:
        total = sum(len(v) for v in allowed_literals.values())
        print(f"No unwrapped user-visible string ({len(hits)} found, all {len(used)} of them allowlisted for other lanes; {total} entries).")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
