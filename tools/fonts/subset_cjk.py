#!/usr/bin/env python3
"""Japanese and Simplified Chinese faces for the browser build.

WebAssembly has no system fonts, so the web build (src/app/platform/web)
compiles its own Noto Sans JP and Noto Sans SC into the app. The full variable
fonts are 9.6 MB and 17.8 MB; what the app can show is a closed set - its
catalogues and the languages' own names - so each face is cut down to exactly
that set, a few hundred kilobytes at most.

    python tools/fonts/subset_cjk.py --check
        Fails (exit 1) when the committed subsets lack a character the
        catalogues or languages.json use. Needs nothing but Python: it reads
        the fonts' character maps itself. Run by CTest (cube_web_cjk_subset).

    python -I tools/fonts/subset_cjk.py --write --jp NotoSansJP[wght].ttf --sc NotoSansSC[wght].ttf
        Rebuilds the subsets from the full fonts (see the README next to them
        for where those come from). Needs fontTools. Run it again whenever a
        translation brings in a character --check says is missing.

What goes in each face (the catalogue is src/app/i18n/translations):
    every character of that language's translations,
    the Han and kana characters of EVERY language's own name (languages.json)
    that the source font has,
    printable ASCII (Latin and digits inside a CJK string),
    the CJK punctuation and, for Japanese, the whole of hiragana and katakana
    (cheap, and a translator should not meet a missing kana).

Why every language's name is in both faces: the language list shows all of
them whatever language the app is in, and Qt draws a run of characters from
one face. Measured in the browser build: with each name only in its own face
Qt used the Japanese face for 简体中文 and drew boxes. The faces are not
interchangeable (Noto Sans JP has no 简, which is not a Japanese character), so
--check wants a name character in at least one face and every character of a
catalogue in its own.

Why the faces' declared code pages are the source fonts': the subsetter
recomputes OS/2 from what is left, and a face of a few hundred ideographs
claims no Chinese code page, which makes Qt's font matching drop it from the
candidates for Han text altogether (measured: the Chinese face was never used).
The faces are Japanese and Simplified Chinese faces; they say so.

A screen in Japanese or Chinese sets its family by the app's language (the
component kit's Theme.headingFamily), so its own text is in its own forms; the
two share code points and not shapes (docs/design/platforms.md, "Fonts").
"""
from __future__ import annotations

import argparse
import json
import struct
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
I18N = ROOT / "src" / "app" / "i18n"
FONTS = ROOT / "src" / "app" / "platform" / "web" / "fonts"

# Language code in languages.json -> (catalogue file, subset file).
FACES = {
    "ja": ("cubeulator_ja.ts", "NotoSansJP-Subset.ttf"),
    "zh-CN": ("cubeulator_zh_CN.ts", "NotoSansSC-Subset.ttf"),
}

ASCII = set(range(0x20, 0x7F))
# CJK Symbols and Punctuation, fullwidth ASCII variants (！？（）：), and the
# general punctuation CJK text uses (dashes, ellipsis, curly quotes).
CJK_PUNCTUATION = set(range(0x3000, 0x3040)) | set(range(0xFF01, 0xFF5F)) | {
    0x2014, 0x2015, 0x2018, 0x2019, 0x201C, 0x201D, 0x2026, 0x00B7, 0x30FB}
# Hiragana (U+3041-3096, 3099-309F) and katakana (U+30A0-30FF); U+3040, 3097
# and 3098 are unassigned.
KANA = set(range(0x3041, 0x3097)) | set(range(0x3099, 0x3100))


def catalogue_text(path: Path) -> str:
    """Every translated string of a Qt Linguist .ts file, plural forms too."""
    tree = ET.parse(path)
    parts = []
    for node in tree.iter("translation"):
        if node.text:
            parts.append(node.text)
        for form in node.iter("numerusform"):
            if form.text:
                parts.append(form.text)
    return "".join(parts)


def is_cjk(codepoint: int) -> bool:
    """Han and kana: what a CJK face is for (not Hebrew, Arabic or Latin)."""
    return (0x2E80 <= codepoint <= 0x9FFF or 0xF900 <= codepoint <= 0xFAFF
            or 0x3400 <= codepoint <= 0x4DBF or 0x20000 <= codepoint <= 0x2FFFF)


def language_names() -> list[str]:
    """Every language's own name, as languages.json writes it."""
    data = json.loads((I18N / "languages.json").read_text(encoding="utf-8"))
    languages = data["languages"] if isinstance(data, dict) else data
    return [entry["name"] for entry in languages if "name" in entry]


def name_characters() -> set[int]:
    """The Han and kana characters the language list shows, in any language."""
    return {ord(c) for name in language_names() for c in name if is_cjk(ord(c))}


def needed(code: str) -> set[int]:
    """What the face for `code` must have: its catalogue, Latin, punctuation."""
    catalogue, _ = FACES[code]
    chars = {ord(c) for c in catalogue_text(I18N / "translations" / catalogue)}
    chars |= ASCII | CJK_PUNCTUATION
    if code == "ja":
        chars |= KANA
    # Whitespace and control characters in the catalogue (newlines in a
    # two-line headline) are layout, not glyphs.
    return {c for c in chars if c >= 0x20 and c != 0x7F}


# --- reading a font's character map without fontTools ------------------------

def cmap_of(path: Path) -> set[int]:
    data = path.read_bytes()
    if data[:4] not in (b"\x00\x01\x00\x00", b"true", b"OTTO"):
        raise SystemExit(f"{path.name}: not a TrueType/OpenType font")
    num_tables = struct.unpack(">H", data[4:6])[0]
    cmap_offset = None
    for i in range(num_tables):
        tag, _checksum, offset, _length = struct.unpack(">4sIII", data[12 + 16 * i:28 + 16 * i])
        if tag == b"cmap":
            cmap_offset = offset
    if cmap_offset is None:
        raise SystemExit(f"{path.name}: no cmap table")

    _version, num_records = struct.unpack(">HH", data[cmap_offset:cmap_offset + 4])
    covered: set[int] = set()
    seen = set()
    for i in range(num_records):
        _platform, _encoding, sub_offset = struct.unpack(
            ">HHI", data[cmap_offset + 4 + 8 * i:cmap_offset + 12 + 8 * i])
        if sub_offset in seen:
            continue
        seen.add(sub_offset)
        start = cmap_offset + sub_offset
        fmt = struct.unpack(">H", data[start:start + 2])[0]
        if fmt == 4:
            seg_x2 = struct.unpack(">H", data[start + 6:start + 8])[0]
            segs = seg_x2 // 2
            ends = struct.unpack(f">{segs}H", data[start + 14:start + 14 + seg_x2])
            starts_at = start + 16 + seg_x2
            starts = struct.unpack(f">{segs}H", data[starts_at:starts_at + seg_x2])
            for lo, hi in zip(starts, ends):
                if lo != 0xFFFF:
                    covered.update(range(lo, hi + 1))
        elif fmt == 12:
            groups = struct.unpack(">I", data[start + 12:start + 16])[0]
            for g in range(groups):
                lo, hi, _glyph = struct.unpack(">III", data[start + 16 + 12 * g:start + 28 + 12 * g])
                covered.update(range(lo, hi + 1))
        # Other formats are not written by the subsetter and cover nothing here.
    return covered


def check() -> int:
    failed = False
    have: dict[str, set[int]] = {}
    for code, (_, subset) in FACES.items():
        path = FONTS / subset
        if not path.is_file():
            print(f"{code}: {path.relative_to(ROOT)} is missing", file=sys.stderr)
            failed = True
            continue
        have[code] = cmap_of(path)
        missing = sorted(needed(code) - have[code])
        if missing:
            failed = True
            shown = "".join(chr(c) for c in missing[:60])
            print(f"{code}: {path.name} lacks {len(missing)} character(s) its catalogue "
                  f"uses: {shown!r}" + (" ..." if len(missing) > 60 else ""), file=sys.stderr)
        else:
            print(f"{code}: {path.name} covers its catalogue ({len(needed(code))} characters "
                  f"needed, {len(have[code])} in the face)")
    if len(have) == len(FACES):
        unshown = sorted(name_characters() - set().union(*have.values()))
        if unshown:
            failed = True
            print("no face has these characters of the language list: "
                  f"{''.join(chr(c) for c in unshown)!r}", file=sys.stderr)
        else:
            print(f"the language list's {len(name_characters())} CJK characters are in a face")
    if failed:
        print("run: python -I tools/fonts/subset_cjk.py --write --jp <NotoSansJP[wght].ttf> "
              "--sc <NotoSansSC[wght].ttf>", file=sys.stderr)
    return 1 if failed else 0


# --- cutting the full fonts down ------------------------------------------------

def write(sources: dict[str, Path]) -> int:
    try:
        from fontTools import subset
        from fontTools.ttLib import TTFont
    except ImportError:
        print("fontTools is needed to --write: pip install fonttools", file=sys.stderr)
        return 2

    FONTS.mkdir(parents=True, exist_ok=True)
    names = name_characters()
    sources_have: set[int] = set()
    for code in FACES:
        sources_have |= set(TTFont(sources[code]).getBestCmap())
    for code, (_, name) in FACES.items():
        source = sources[code]
        font = TTFont(source)
        have = set(font.getBestCmap())
        must = needed(code)
        absent = sorted(must - have)
        if absent:
            # A character the full font does not have cannot be put in the
            # subset; say so rather than shipping a face that looks complete.
            print(f"{code}: {source.name} has no glyph for "
                  f"{''.join(chr(c) for c in absent)!r}; they will fall back", file=sys.stderr)

        # The code pages the source declares: see the module docstring.
        declared = (font["OS/2"].ulCodePageRange1, font["OS/2"].ulCodePageRange2)

        options = subset.Options()
        options.hinting = False             # unhinted at the sizes Qt renders at
        options.layout_features = ["kern", "ccmp", "locl", "mark", "mkmk", "vert", "vrt2",
                                   "palt", "halt", "calt"]
        options.name_IDs = ["*"]            # the licence and copyright stay
        options.name_legacy = True
        options.notdef_outline = True
        options.glyph_names = False
        options.drop_tables += ["DSIG"]
        subsetter = subset.Subsetter(options)
        subsetter.populate(unicodes=sorted((must | names) & have))
        subsetter.subset(font)
        font["OS/2"].ulCodePageRange1, font["OS/2"].ulCodePageRange2 = declared
        out = FONTS / name
        font.save(out)
        print(f"{code}: {out.relative_to(ROOT)}  {out.stat().st_size:,} bytes, "
              f"{len(font.getBestCmap())} characters, "
              f"axes {[a.axisTag for a in font['fvar'].axes] if 'fvar' in font else 'none'}")
    nowhere = sorted(names - sources_have)
    if nowhere:
        print(f"neither source font has {''.join(chr(c) for c in nowhere)!r}", file=sys.stderr)
    return check()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--check", action="store_true")
    mode.add_argument("--write", action="store_true")
    parser.add_argument("--jp", type=Path, help="NotoSansJP[wght].ttf (full font)")
    parser.add_argument("--sc", type=Path, help="NotoSansSC[wght].ttf (full font)")
    args = parser.parse_args()
    if args.check:
        return check()
    if not (args.jp and args.sc):
        parser.error("--write needs --jp and --sc")
    return write({"ja": args.jp, "zh-CN": args.sc})


if __name__ == "__main__":
    sys.exit(main())
