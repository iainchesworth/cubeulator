# Bundled fonts

Compiled into the app under `:/fonts` and registered at start-up by
`kit::installBundledFonts()` (`../kit/bundled_fonts.cpp`), so that the design's
type (`docs/design/system.md`, "Type") is what is shown whether or not the
machine has these families installed. Every `.ttf` and `.otf` in this folder is
picked up by `src/app/CMakeLists.txt`; adding one needs no code change.

| file | family | weight | used for | licence |
|---|---|---|---|---|
| `Archivo-Regular.ttf` | Archivo | 400 | running text | `OFL-Archivo.txt` |
| `Archivo-Bold.ttf` | Archivo | 700 | headline, direction, bold body | `OFL-Archivo.txt` |
| `Archivo-ExtraBold.ttf` | Archivo | 800 | display, title, instruction, labels | `OFL-Archivo.txt` |
| `IBMPlexMono-Medium.ttf` | IBM Plex Mono | 500 | numerals, notation, key names | `OFL-IBMPlexMono.txt` |
| `NotoSansHebrew.ttf` | Noto Sans Hebrew | 100 to 900 (variable) | Hebrew and Yiddish | `OFL-NotoSansHebrew.txt` |
| `NotoSansArabic.ttf` | Noto Sans Arabic | 100 to 900 (variable) | Arabic | `OFL-NotoSansArabic.txt` |

Japanese and Simplified Chinese use the system's own font, chosen by language
(`Theme.headingFamily`); nothing for them is bundled.

All are under the SIL Open Font Licence 1.1, which allows bundling and
redistribution with the app and asks that the licence travel with the fonts.
**The generated third-party notices (E0b) must list all four families**, with
a font manifest entry for each file here. The files are unmodified.

## Where they came from

Fetched or copied 2026-10-13, unmodified, with the licence text each project
ships. Hebrew and Arabic were first bundled as IBM Plex Sans and replaced by
Noto the same day (D3 in `docs/design/family.md`: Noto is the family's, and
its variable files have an ExtraBold the Plex faces do not).

| family | source | version |
|---|---|---|
| Archivo | https://github.com/Omnibus-Type/Archivo, `fonts/ttf/` and `OFL.txt` | commit `211127690e8ff106c36c935f7e5e697114cff103` (2026-09-02) |
| IBM Plex Mono | https://github.com/IBM/plex release `@ibm/plex-mono@2.5.0`, `ibm-plex-mono.zip`, `fonts/complete/ttf/` | 2.5.0 |
| Noto Sans Hebrew | ICL Forge's shared copy, `apps/shared/theme/assets/fonts/NotoSansHebrew.ttf` (upstream https://github.com/notofonts/hebrew); the same file the family's other apps ship (`docs/design/family.md`, D3) | Version 3.001 |
| Noto Sans Arabic | the same folder, `NotoSansArabic.ttf` (upstream https://github.com/notofonts/arabic) | Version 2.012 |

SHA-256 of what is in this folder:

```
951a0ebab63b1bb0d90a26c27625bda803d570dace3851fa2f1eea65852d8983  Archivo-Bold.ttf
9fff67922ae2bc376a9e5c6d644eebf8c5b17f62a0f38e5f016e4b7e3903662f  Archivo-ExtraBold.ttf
59fbfd8a9bb41301bfe950e7b097fd42d52af224a07761e2c1b2c959a4c9cf9a  Archivo-Regular.ttf
98fbd727aae340b236955879dabed4d991aac9e8e90b3b2a67ce4a59221cc97c  IBMPlexMono-Medium.ttf
63111b5b2e074dd48cc67692e0a2726d86ee94c1c37fe8598257b7b4e87e869e  NotoSansArabic.ttf
7ef36a2c3593758cdb622e1bdef4f84523e92fbc3ccc667438dd80ff54c2de88  NotoSansHebrew.ttf
```

## Notes for whoever uses them

- Archivo ExtraBold and IBM Plex Mono Medium carry legacy family names
  ("Archivo ExtraBold", "IBM Plex Mono Medm"), and the Hebrew file's is "Noto
  Sans Hebrew Thin" (its default instance), alongside the typographic ones.
  `tests/kit/fonts_test.cpp` checks that Qt resolves weight 800 and weight 500
  to the right faces, and that the variable files expose a real ExtraBold
  style, on the platform the suite runs on; run it on each platform before
  trusting it there.
- The two Noto files are variable fonts. Qt on Windows exposes their named
  instances as separate styles (tested); macOS and Linux were not run.
- An update is: replace the file, update this table and the hashes, run
  `ctest -L fonts`.
