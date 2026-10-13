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
| `IBMPlexSansHebrew-Regular.ttf`, `-Bold.ttf` | IBM Plex Sans Hebrew | 400, 700 | Hebrew (no 800 exists) | `OFL-IBMPlexSansHebrew.txt` |
| `IBMPlexSansArabic-Regular.ttf`, `-Bold.ttf` | IBM Plex Sans Arabic | 400, 700 | Arabic (no 800 exists) | `OFL-IBMPlexSansArabic.txt` |

Japanese and Simplified Chinese use the system's own font, chosen by language
(`Theme.headingFamily`); nothing for them is bundled.

All are under the SIL Open Font Licence 1.1, which allows bundling and
redistribution with the app and asks that the licence travel with the fonts.
**The generated third-party notices (E0b) must list all four families.** The
files are unmodified.

## Where they came from

Fetched 2026-10-13, unmodified, with the licence text each project ships.

| family | source | version |
|---|---|---|
| Archivo | https://github.com/Omnibus-Type/Archivo, `fonts/ttf/` and `OFL.txt` | commit `211127690e8ff106c36c935f7e5e697114cff103` (2026-09-02) |
| IBM Plex Mono | https://github.com/IBM/plex release `@ibm/plex-mono@2.5.0`, `ibm-plex-mono.zip`, `fonts/complete/ttf/` | 2.5.0 |
| IBM Plex Sans Hebrew | release `@ibm/plex-sans-hebrew@1.1.0`, `ibm-plex-sans-hebrew.zip` | 1.1.0 |
| IBM Plex Sans Arabic | release `@ibm/plex-sans-arabic@1.1.0`, `ibm-plex-sans-arabic.zip` | 1.1.0 |

SHA-256 of what is in this folder:

```
951a0ebab63b1bb0d90a26c27625bda803d570dace3851fa2f1eea65852d8983  Archivo-Bold.ttf
9fff67922ae2bc376a9e5c6d644eebf8c5b17f62a0f38e5f016e4b7e3903662f  Archivo-ExtraBold.ttf
59fbfd8a9bb41301bfe950e7b097fd42d52af224a07761e2c1b2c959a4c9cf9a  Archivo-Regular.ttf
98fbd727aae340b236955879dabed4d991aac9e8e90b3b2a67ce4a59221cc97c  IBMPlexMono-Medium.ttf
b74f809dead12442ed56e02a12c3bcc02076c9ad4e32f17d0a9ca6fc1aafc89e  IBMPlexSansArabic-Bold.ttf
8e0f1046c736bf939d4939ee3ae0116acf61cbcd6592deae7656761627080981  IBMPlexSansArabic-Regular.ttf
628c5dac15f61b8c981629426cc75d2123cf25520926bf28fa1a060c2cd096fb  IBMPlexSansHebrew-Bold.ttf
98cd8ca13fef47fb57c20faed17a346639bb418b7abeb096fd9693ea3eecc445  IBMPlexSansHebrew-Regular.ttf
```

## Notes for whoever uses them

- Archivo ExtraBold and IBM Plex Mono Medium carry legacy family names
  ("Archivo ExtraBold", "IBM Plex Mono Medm") alongside the typographic ones
  ("Archivo", "IBM Plex Mono"). `tests/kit/fonts_test.cpp` checks that Qt
  resolves weight 800 and weight 500 to the right faces on the platform the
  suite runs on; run it on each platform before trusting it there.
- An update is: replace the file, update this table and the hashes, run
  `ctest -L fonts`.
