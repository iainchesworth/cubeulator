#!/usr/bin/env python3
"""Make the Qt Linguist catalogues from docs/design/strings.json, and hold them to it.

strings.json is the only place a string's English, its id and its note for
translators are written. Everything else is made from it:

    docs/design/strings.json        ids, British English, notes, plural forms
    src/app/i18n/languages.json     the languages, and how many plural forms each has
        |
        v   this script
    src/app/i18n/translations/cubeulator_<file>.ts      one per language, id-based
        |
        v   lrelease -idbased -nounfinished (the build)
    :/i18n/cubeulator_<file>.qm                          embedded in the app

What goes into each catalogue:

  * every id in strings.json, in its order, with the English as <source> and
    the note as <extracomment> (what lupdate writes for a //: comment);
  * English (en): the English as the translation too, plural forms included,
    so that the English .qm is complete. It is installed under every other
    language: that is what makes a string with no translation yet show its
    English and never its id (docs/i18n.md);
  * the pseudo-locale (xx): the English put through pseudo_localize() below;
  * every other language: the translation that is already in the file, by id,
    or an empty unfinished entry with the right number of plural forms.
    Nothing here writes a translation. E5 does, in Linguist or by hand;
    this script then keeps it.

An id that is no longer in strings.json is dropped from every catalogue (git
keeps what a translator wrote). A string's English may be reworded without
touching its translations - the id is what they hang on; a string whose
meaning changes gets a new id (docs/design/content.md, "For translators").

`%n` is written `%Ln` in the catalogues, so that Qt formats the count with the
app's language (Arabic digits where the language has them) and not with plain
digits. strings.json keeps `%n`, which is what its readers know.

    strings_to_ts.py --write    rewrite the catalogues
    strings_to_ts.py --check    exit 1, naming the ids, if --write would change a file (the CI gate)
    strings_to_ts.py --status   how much of each language is translated

Standard library only.
"""

from __future__ import annotations

import argparse
import json
import re
import sys
import xml.etree.ElementTree as ET
from dataclasses import dataclass, field
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
STRINGS = ROOT / "docs" / "design" / "strings.json"
LANGUAGES = ROOT / "src" / "app" / "i18n" / "languages.json"
OUT_DIR = ROOT / "src" / "app" / "i18n" / "translations"

SOURCE_LANGUAGE_TAG = "en_GB"

# --- the pseudo-locale -------------------------------------------------------
#
# The family's transform (ICL Forge's tools/generators/gen_pseudo_locale.py),
# kept in step with it so that the suite's pseudo-locales read alike:
#   1. every vowel accented, so a string reads as foreign at a glance without
#      changing its length or its meaning;
#   2. the end padded with ~40% of its length again, the standard way to make
#      truncation and overflow show before a real, longer translation does
#      (content.md allows a translation 40% more than the English);
#   3. the whole wrapped in [ ], so that a string cut short loses its closing
#      bracket and a string that never went through an id lookup stands out
#      because it has neither.
# Placeholders (%1, %L1, %n, %Ln) and markup are left alone: damaging one
# would break the very string the fixture exists to test.

_VOWELS = str.maketrans("aeiouAEIOU", "àéîõûÀÉÎÕÛ")
_PROTECTED = re.compile(r"%L?\d+|%L?n|<[^>]*>")
PAD_RATIO = 0.4
PAD_CHAR = "~"


def pseudo_localize(text: str) -> str:
    if not text.strip():
        return text
    pieces = _PROTECTED.split(text)
    holes = _PROTECTED.findall(text)
    out = pieces[0].translate(_VOWELS)
    for hole, piece in zip(holes, pieces[1:], strict=True):
        out += hole + piece.translate(_VOWELS)
    # Padding counts the visible length, not the placeholders or the markup.
    visible = len("".join(pieces))
    pad = max(2, round(visible * PAD_RATIO))
    # A string of paragraphs is padded at the end of the last one: the line
    # breaks it carries are the author's and are kept where they are.
    return f"[{out} {PAD_CHAR * pad}]"


# --- strings.json ------------------------------------------------------------


@dataclass
class Message:
    id: str
    note: str
    plural: bool
    # One English string, or the {"one": ..., "other": ...} forms, with %n
    # already written %Ln.
    forms: dict[str, str] = field(default_factory=dict)
    text: str = ""

    @property
    def source(self) -> str:
        """What goes in <source>: the plural form for a plural message."""
        return self.forms["other"] if self.plural else self.text


def _locale_count(s: str) -> str:
    return re.sub(r"%n", "%Ln", s)


def load_messages() -> list[Message]:
    raw = json.loads(STRINGS.read_text(encoding="utf-8"))
    out: list[Message] = []
    for key, value in raw.items():
        if key.startswith("$"):
            continue
        if not (isinstance(value, list) and len(value) == 2):
            raise SystemExit(f"{STRINGS}: {key}: expected [text, note]")
        text, note = value
        if isinstance(text, dict):
            if set(text) != {"one", "other"}:
                raise SystemExit(f"{STRINGS}: {key}: plural forms must be exactly one and other")
            out.append(Message(key, note, True, {k: _locale_count(v) for k, v in text.items()}))
        else:
            out.append(Message(key, note, False, text=_locale_count(text)))
    ids = [m.id for m in out]
    if len(set(ids)) != len(ids):
        raise SystemExit(f"{STRINGS}: duplicate ids")
    return out


# --- languages.json ----------------------------------------------------------


@dataclass
class Language:
    code: str
    file: str
    tag: str
    plurals: int
    pseudo: bool = False
    translated: bool = False

    @property
    def is_source(self) -> bool:
        return not self.pseudo and self.file == "en"

    @property
    def path(self) -> Path:
        return OUT_DIR / f"cubeulator_{self.file}.ts"


def load_languages() -> list[Language]:
    raw = json.loads(LANGUAGES.read_text(encoding="utf-8"))
    langs = [
        Language(e["code"], e["file"], e["tag"], int(e["plurals"]), translated=bool(e.get("translated")))
        for e in raw["languages"]
    ]
    p = raw["pseudo"]
    langs.append(Language(p["code"], p["file"], p["tag"], int(p["plurals"]), pseudo=True, translated=True))
    return langs


# --- reading an existing catalogue -------------------------------------------


@dataclass
class Existing:
    forms: list[str]
    unfinished: bool


def read_existing(path: Path) -> dict[str, Existing]:
    if not path.exists():
        return {}
    root = ET.parse(path).getroot()
    found: dict[str, Existing] = {}
    for message in root.iter("message"):
        mid = message.get("id")
        tr = message.find("translation")
        if mid is None or tr is None:
            continue
        numerus = tr.findall("numerusform")
        forms = [(n.text or "") for n in numerus] if numerus else [(tr.text or "")]
        found[mid] = Existing(forms, tr.get("type") in ("unfinished", "vanished", "obsolete"))
    return found


# --- writing -----------------------------------------------------------------


def _esc(s: str) -> str:
    return s.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")


def _translation(message: Message, lang: Language, existing: dict[str, Existing]) -> tuple[list[str], bool]:
    """(forms, unfinished) for one message in one language."""
    n = lang.plurals if message.plural else 1
    if lang.is_source:
        if message.plural:
            return [message.forms["one"], message.forms["other"]], False
        return [message.text], False
    if lang.pseudo:
        if message.plural:
            return [pseudo_localize(message.forms["one"]), pseudo_localize(message.forms["other"])], False
        return [pseudo_localize(message.text)], False
    old = existing.get(message.id)
    if old is not None and len(old.forms) == n and any(f.strip() for f in old.forms):
        return old.forms, old.unfinished or not all(f.strip() for f in old.forms)
    return [""] * n, True


def render(messages: list[Message], lang: Language, existing: dict[str, Existing]) -> str:
    lines = [
        '<?xml version="1.0" encoding="utf-8"?>',
        "<!DOCTYPE TS>",
        f'<TS version="2.1" language="{lang.tag}" sourcelanguage="{SOURCE_LANGUAGE_TAG}">',
        "<context>",
        "    <name></name>",
    ]
    for m in messages:
        forms, unfinished = _translation(m, lang, existing)
        attrs = f' id="{_esc(m.id)}"' + (' numerus="yes"' if m.plural else "")
        lines.append(f"    <message{attrs}>")
        lines.append(f"        <source>{_esc(m.source)}</source>")
        if m.note:
            lines.append(f"        <extracomment>{_esc(m.note)}</extracomment>")
        type_attr = ' type="unfinished"' if unfinished else ""
        if m.plural:
            lines.append(f"        <translation{type_attr}>")
            for f in forms:
                lines.append(f"            <numerusform>{_esc(f)}</numerusform>")
            lines.append("        </translation>")
        else:
            lines.append(f"        <translation{type_attr}>{_esc(forms[0])}</translation>")
        lines.append("    </message>")
    lines += ["</context>", "</TS>", ""]
    return "\n".join(lines)


def build() -> dict[Path, str]:
    messages = load_messages()
    out: dict[Path, str] = {}
    for lang in load_languages():
        out[lang.path] = render(messages, lang, read_existing(lang.path))
    return out


def status() -> list[tuple[Language, int, int]]:
    messages = load_messages()
    rows = []
    for lang in load_languages():
        existing = read_existing(lang.path)
        done = 0
        for m in messages:
            forms, unfinished = _translation(m, lang, existing)
            if not unfinished and all(f.strip() for f in forms):
                done += 1
        rows.append((lang, done, len(messages)))
    return rows


def _ids_in(text: str) -> set[str]:
    return set(re.findall(r'<message id="([^"]+)"', text))


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    mode = ap.add_mutually_exclusive_group(required=True)
    mode.add_argument("--write", action="store_true", help="rewrite the catalogues")
    mode.add_argument("--check", action="store_true", help="fail if --write would change a file")
    mode.add_argument("--status", action="store_true", help="print how much of each language is translated")
    args = ap.parse_args(argv)

    if args.status:
        for lang, done, total in status():
            label = "pseudo" if lang.pseudo else ("source" if lang.is_source else lang.code)
            print(f"{label:>7}  {done:4d} / {total}  {'translated' if lang.translated else 'not yet translated'}")
        return 0

    expected = build()
    problems: list[str] = []
    for path, text in expected.items():
        current = path.read_text(encoding="utf-8") if path.exists() else None
        if current == text:
            continue
        if args.write:
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(text, encoding="utf-8", newline="\n")
            print(f"wrote {path.relative_to(ROOT)}")
            continue
        if current is None:
            problems.append(f"{path.relative_to(ROOT)}: missing")
            continue
        added = sorted(_ids_in(text) - _ids_in(current))
        dropped = sorted(_ids_in(current) - _ids_in(text))
        detail = []
        if added:
            detail.append(f"{len(added)} id(s) to add, first: {added[0]}")
        if dropped:
            detail.append(f"{len(dropped)} id(s) to drop, first: {dropped[0]}")
        if not detail:
            detail.append("text or notes differ from strings.json")
        problems.append(f"{path.relative_to(ROOT)}: " + "; ".join(detail))

    if args.check and problems:
        print("The translation catalogues are out of step with docs/design/strings.json:")
        for p in problems:
            print(f"  {p}")
        print("Run: python tools/i18n/strings_to_ts.py --write   and commit the result.")
        return 1
    if args.check:
        print(f"{len(expected)} catalogues match docs/design/strings.json.")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
