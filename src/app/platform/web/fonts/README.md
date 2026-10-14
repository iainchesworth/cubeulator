# Japanese and Simplified Chinese for the browser build

A browser has no system fonts for Qt to use, and the face Qt for WebAssembly
bundles has no Japanese or Chinese, so the web build compiles its own into the
app under `:/fonts` (`platform.cmake`, `qt_add_resources`), where
`kit::installBundledFonts()` registers every font it finds at start-up. The
desktops use the system's own faces, chosen by language
(`docs/design/platforms.md`, "Fonts"), and bundle nothing.

| file | family | what it holds | licence |
|---|---|---|---|
| `NotoSansJP-Subset.ttf` | Noto Sans JP, variable weight 100-900 | the characters of the Japanese catalogue and language name, all hiragana and katakana, CJK punctuation, printable ASCII | `OFL-NotoSansJP.txt` |
| `NotoSansSC-Subset.ttf` | Noto Sans SC, variable weight 100-900 | the characters of the Simplified Chinese catalogue and language name, CJK punctuation, printable ASCII | `OFL-NotoSansSC.txt` |

They are the Noto Sans faces cut down by `tools/fonts/subset_cjk.py` to exactly
the characters the app can show: a closed set, since every string is in a
catalogue. The full fonts are 9.6 MB and 17.8 MB; these are a few hundred
kilobytes. They are **modified** (subset, unhinted) under the SIL Open Font
Licence 1.1, which allows it and asks that the licence and copyright travel
with them; both do, in the files and in the generated notices. Neither name
contains the Reserved Font Name ("Source").

The two share code points and not shapes, so a character one language needs is
in that language's face only. Which family a screen uses is the app's language
(`Theme.headingFamily` on the component-kit branch), not left to fallback.

## When a translation adds a character

`ctest -R i18n_web_cjk_subsets_cover_catalogues` (or
`python tools/fonts/subset_cjk.py --check`, which needs only Python) names the
characters a face lacks. Regenerate the faces from the full fonts below:

```sh
python -I tools/fonts/subset_cjk.py --write \
    --jp "NotoSansJP[wght].ttf" --sc "NotoSansSC[wght].ttf"
```

`--write` needs `fonttools` (`pip install fonttools`). Run it on files you
fetched into a directory of their own; the `-I` keeps Python from loading
anything from the current directory.

## Where the full fonts came from

Fetched 2026-10-14 from <https://github.com/google/fonts> at commit
`66928797f271fed5ee428544e7229685e2d90127`, unmodified, with the OFL text the
project ships:

| file in `ofl/` | size | SHA-256 |
|---|---|---|
| `notosansjp/NotoSansJP[wght].ttf` | 9,589,900 | `c2f3b4d463500a2ddcd3849cded1fceeb9fd6d1c32e6cbecd568453ba50fc68f` |
| `notosanssc/NotoSansSC[wght].ttf` | 17,772,300 | `a3041811a78c361b1de50f953c805e0244951c21c5bd412f7232ef0d899af0da` |

(Both fonts are version 2.004; upstream is
<https://github.com/notofonts/noto-cjk>.)

SHA-256 of what is in this folder:

```
2593c9a83974bff08142a7e0aed147b65ecd4a102541c2d67d2d72b0e2e50023  NotoSansJP-Subset.ttf
56b2e9610d2f31d9b8b834f25a099e94f09d4123bd29f01464e4581781fa3937  NotoSansSC-Subset.ttf
```

## Notes

- The variable fonts' default instance is Thin (weight 100), as the Noto Hebrew
  and Arabic files' is. Text asks for the weight it wants (400, 700, 800); Qt
  picks the matching instance from the weight axis.
- Replacing a face, or running `--write` again, changes the hashes above:
  update them with the file.
