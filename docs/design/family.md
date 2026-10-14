# The family

Written 2026-10-13. Status: **decisions D1 to D9 taken the same day**
*(Iain)*, recorded in section 5; everything else here is survey or
proposal. It is not legal advice.

Iain builds more than one app. This page records what the others do, so
that Cubeulator's experience plan lines up with them where that is cheap and
right, and so that the places it should *not* copy them are written down.

Read-only surveys, 2026-10-13. A path is given wherever a statement matters
to a decision; check it before building on it.

| short name | repository | read at | UI |
|---|---|---|---|
| **ICL Forge** (Forge, Hearth, Crucible) | `iainchesworthlabs/iclforge` | `main` at `5bf94769d` | Qt Quick; Hearth for Android is Kotlin Views |
| **Countdown Solver** | `iainchesworth/CountdownSolver` | `develop`, 0.3.0 | Qt Quick |
| **Automate** | `iainchesworthlabs/aqualink-automate` | `develop` | Web: Alpine.js, vanilla CSS, no build step |

Paths below are relative to the repository named at the start of the
sentence.

## 1. The short version

**1. Cubeulator already is an ICL Forge app in look.** Not similar: the same
values.

| | ICL Forge, palette `signal`, light | Cubeulator, `tokens.json`, light |
|---|---|---|
| background | `bg #f3f2f2` | `paper #F3F2F2` |
| second pane | `surface #eae9e9` | `surface #EAE9E9` |
| text | `text #201e1d` | `ink #201E1D` |
| brand red | `accent #ec3013` | `accent #EC3013` |
| corners | radius 0 everywhere ("do not round anything") | radius 0 |
| Latin face | Archivo | Archivo |
| rules | 1 px on controls, 2 px between regions, 2 px dialog edge | 1 px, 2 px, 3 px |
| small label | uppercase kicker, tracking 1.2 px | eyebrow, capitals, tracking 0.1 em |
| focus ring | 2 px, offset 2 px, in `accentInk` | 2 px, offset 2 px, in `ink` |

The source is `apps/shared/theme/assets/qml/Theme.qml`, whose own comment says
its tokens "follow the Modernist design system". The design record is
`planning/hearth-design.md` (signed off 2026-09-22; the canvas is
published as `claude.ai/artifact/C4izzSr755SyBzTtExt8Qg`, which this review
could not open). So the plan's open question, *whether the original design
handoff still exists*, is half answered: its tokens survive in a shipping
file, and `tokens.json` can be checked against it instead of standing on its
own.

Where they differ, Cubeulator is the more careful one (section 3, F9): the
light-theme label on ICL Forge's red button measures **3.95:1**, red as text
**3.76:1**; Cubeulator's `accentFill`/`accentText` pair fixes both (6.46:1).

**2. The suite has two looks, not one.**

| | **Signal**: ICL Forge, Cubeulator | **Calm**: Countdown Solver, Automate |
|---|---|---|
| shape | square, 2 px rules, no depth | rounded: 12/9 px (Countdown), 22/16/11 px (Automate) |
| accent | red; ICL Forge also offers Ink blue, Console amber, the desktop's own | teal: `#2f7d7b` / `#54b8b0` (Countdown); teal, azure, aqua or violet by choice (Automate) |
| neutrals | warm grey `#f3f2f2` / `#151313` | warm paper `#f7f6f3` / `#232220` (Countdown); cool slate, dark first (Automate) |
| Latin face | Archivo | IBM Plex Sans (Countdown); Bricolage Grotesque and Hanken Grotesk (Automate) |
| depth | none | card borders (Countdown); shadow, glow and blur (Automate) |
| navigation | header and page switch (ICL Forge); screens (Cubeulator) | 214 px sidebar or bottom tabs (Countdown); top bar, drawer or bottom tabs (Automate) |

All four share these cues: a small tracked-capitals label above each
section; a monospace face for figures and status; sentence-case plain
wording; state never carried by colour alone; light, dark and follow-the-
device themes; a 2 px focus ring where there is one (three of four).

**3. Under the look, the chassis is mostly shared already**, and Cubeulator
is mostly in line with it.

- `cmake/GenerateVersion.cmake` and `version_details()` stamp git into the
  binary and print the same block (`release`, `commit`, `branch`, `target`,
  `state: dirty`) in Countdown, ICL Forge and Cubeulator
  (`src/lib/src/version.cpp`).
- Qt Linguist `.ts` files and a `LanguageManager`. ICL Forge's header says
  it is "modelled directly on CountdownSolver's" and builds "to the set
  already shipped there rather than invent a second one".
- The language list is written in each language's own name, with "same as
  the system" first.
- Settings in `QSettings`. Theme: system, light, dark.
- No telemetry and no update check, in all four.
- GPL. Keep a Changelog. MkDocs Material. British source spelling in ICL
  Forge and in Cubeulator.

**4. Cubeulator is ahead in these places** (section 4 lists them as
back-ports): touch targets and size classes; safe areas; reduced motion
(only Automate also has it); contrast measured by a script and gated; the
rule that state is never colour alone, tested; remembered window geometry;
platform menus and back; text size in relative units with the operating
system's own setting multiplied in; text that wraps instead of eliding.

**5. Cubeulator is behind in these** (section 3 has the recipes):

| | family does | Cubeulator planned | decided |
|---|---|---|---|
| build details in About | commit, target, dirty flag, in a block, copyable | "Version 1.0.0" only | the compact line and **Copy** in About (F5) |
| diagnostics | save or copy a text file, secrets withheld | nothing | **Copy details** in V1.0 (D6, F7) |
| Hebrew and Arabic faces | Noto Sans Hebrew, Noto Sans Arabic, already vendored three times | IBM Plex Sans Hebrew and Arabic, none bundled yet | Noto (D3, F4) |
| language set | en fr de es ar he **yi** | en de fr es ja zh he ar (no yi) | Yiddish in V1.0: nine languages (D4, F4) |
| setting keys | `appearance/theme`, `language/code`, `firstRun/acknowledgedVersion` | "a stable key", unnamed | named in F2 |
| machine-translation note | an in-app sentence under the language list | in `brief.md`, not in the app | in E5 |
| pictures in the docs | generated from a manifest and checked | a screenshot tour is planned | manifest and capture entry (F10) |

**6. The publisher follows the account** *(Iain, 2026-10-13, D1)*. Iain
Chesworth Labs, ICL, is the umbrella for his personal and shared work; its
names are `iainchesworth`, `iainchesworthlabs` and `icl` (as in ICL Forge,
`iclforge`). Cubeulator moved to `iainchesworthlabs` on 2026-10-13, so its
publisher is **Iain Chesworth Labs** *(Iain, 2026-10-13, asked again after
the move)*. While it sat under `iainchesworth` the answer was Iain
Chesworth, by the same rule. No app shows a publisher or a copyright line
in its window today.

## 2. Side by side

| | Cubeulator (plan) | ICL Forge | Countdown Solver | Automate |
|---|---|---|---|---|
| Style | square, flat, red | square, flat, red; Forge uses Fusion, the others Basic | rounded cards, teal | rounded, glow, blur, teal |
| Theme setting | system, light, dark | system, light, dark; **palette** signal, ink, console, system | system, light, dark (resets on launch) | light, dark; **accent** of four |
| Latin faces | Archivo; IBM Plex Mono | Archivo; system mono (none bundled) | IBM Plex Sans; IBM Plex Mono | Bricolage Grotesque; Hanken Grotesk; system mono |
| Hebrew, Arabic | Noto Sans Hebrew, Noto Sans Arabic (D3; was IBM Plex Sans) | Noto Sans Hebrew, Noto Sans Arabic | the same Noto faces | the same Noto faces, by `unicode-range` |
| Japanese, Chinese | the system's, chosen by language | not shipped | not shipped | the system's, by `html[lang]` |
| Type sizes | rem, times `textScale` | px times `fontScale` | px | px and rem |
| Text size setting | device, 1.25, 1.5 (times the OS's) | 100, 125, 150, 175 %, system | none | none |
| Reduced motion | system, plus a setting | none | none | `prefers-reduced-motion` |
| Smallest target | 48 px; 96 on solve | 30 px buttons (desktop) | 44 px on phone | 44 px on phone |
| About is | a screen under Settings | a dialog, reached three ways | a card in Settings | the `#about` route |
| About shows | icon, name, version, what, author, licence, three rows, small print | icon, name, tagline, what, **version block**, licence and trademark, third-party note, repository link, Licences and Close | name and version, **version block**, GitHub link, Third-Party Licenses | name, tagline, version, commit, commit date, homepage, language chips |
| Copyright in the app | none ("Made by Iain Chesworth Labs") | none (README and macOS bundle only) | none | none |
| Licences | generated screen (E0b) | `NOTICES.txt` in a read-only dialog (Crucible, Hearth; not Forge) | a dialog of five hard-coded entries | none |
| Privacy in the app | a screen | prose in the docs | none | prose in the docs |
| Diagnostics | none | save, copy, live view | a rotating log file | a diagnostics page, capture downloads |
| Help in the app | Help screens, F1 | `?` and F1 open About | none | a link to the contributing guide |
| Settings store | `QSettings`, org and app `Cubeulator` | `QSettings`, org `iclforge`; a migration from old names | `QSettings`; only the language persists | `localStorage`, mirrored to the server |
| Language mechanism | Linguist, live switch (E2b) | Linguist, shared `LanguageManager`, live switch | Linguist, `LanguageManager` (the original) | catalogues in JS, `AquaI18n`, live switch |
| Languages | en de fr es ja zh he ar yi (D4; was eight, no yi) | en fr de es ar he yi | en fr de es ar he yi | en de es fr ar he ja zh yi |
| Source spelling | British | British ("Licences") | mixed ("Licenses") | mixed |
| Pseudo-locale | planned, real | `xx`, environment only | none | `⟦…⟧` scan in a test |
| Translation gates | planned lint | `update_translations`, no unfinished, `test_translations.cpp` | two grep jobs | key and placeholder parity |
| Window size | remembered (E4a) | not remembered | fixed, min = max | n/a |
| Identifier | `com.iainchesworthlabs.cubeulator` | `com.iainchesworthlabs.*` (macOS), `com.iclforge.hearth` (Android) | `org.iainchesworth.countdownsolver` | PWA "Aqualink Automate" |
| GitHub account | `iainchesworthlabs` (moved 2026-10-13) | `iainchesworthlabs` | `iainchesworth` | `iainchesworthlabs` |
| Docs pictures | system-test tour (E0a) | manifest of 84, `take_pictures.py`, `check_doc_pictures.py` | by hand | `scripts/capture-doc-screenshots.js` |

## 3. The common features, and how the suite does them

Each item is the recipe the family uses, what Cubeulator's design says
today, and the proposal. Lane and timing are in section 6.

### F1. Publisher line and identifiers

- **Family.** Copyright appears in the README ("Copyright (C) 2026 Iain
  Chesworth.") and the macOS bundle ("Copyright (c) Iain Chesworth"). The
  CPack vendor was "Iain Chesworth" in all of them when surveyed;
  Cubeulator's became "Iain Chesworth Labs" on `develop` with the move. No
  app shows it in a window.
- **Identifiers.** Countdown Solver: `org.iainchesworth.countdownsolver`.
  ICL Forge has three schemes: `com.iainchesworthlabs.hearth` and
  `.forge-gui` on macOS, `com.iclforge.hearth` on Android
  (`apps/hearth/android/app/build.gradle.kts`). Cubeulator is decided
  (`brief.md` D12) and takes the form of ICL Forge's macOS apps.
- **Decided (D1, D9).** One publisher string, used in the same five
  places: the Windows version resource, `Info.plist`, the AppStream
  `<developer>`, the store listing, and the About small print. The account
  decides it. Cubeulator moved to `iainchesworthlabs` on 2026-10-13, so it
  is **Iain Chesworth Labs**; `cmake/Packaging.cmake` on `develop` already
  says so. The copyright line stays in the person's name: "Copyright ©
  2026 Iain Chesworth". The identifier follows the account too:
  **`com.iainchesworthlabs.cubeulator`**, the form ICL Forge's macOS apps
  already use. Both were put to Iain again after the move. The outlier in
  the suite is still ICL Forge's Android id.

### F2. Settings store and keys

- **Family.** `QSettings`. ICL Forge: organisation `iclforge`, application
  `Hearth`, `Crucible` or `forge-gui`; keys `appearance/theme`,
  `appearance/palette`, `appearance/textScale`, `language/code`,
  `behaviour/*`, `firstRun/acknowledgedVersion`. Because the organisation
  and application names changed once, it carries
  `apps/shared/preferences/src/settings_migration.cpp`. Read before the
  first frame, so the wrong theme or language never flashes.
- **Cubeulator.** `main.cpp` already sets organisation and application to
  `Cubeulator`. `screens/settings.md` says "a stable key" and names none.
- **Proposal.** Name the keys now, in the family's `group/name` style, and
  list them in `screens/settings.md`:

  | key | setting |
  |---|---|
  | `language/code` | Language; absent means "same as this device" |
  | `appearance/theme` | Theme |
  | `appearance/textSize` | Text size |
  | `appearance/motion` | Motion |
  | `appearance/marks` | Marks on cube colours |
  | `solving/advance`, `solving/pace`, `solving/explain`, `solving/notation`, `solving/sound`, `solving/haptics` | Solving |
  | `camera/deviceId` | Remembered camera |
  | `history/keep` | Solve history |
  | `firstRun/acknowledgedVersion` | First run; a later version can ask again |

  Reason: a key renamed after the first release needs a migration. ICL
  Forge has already had to write one.

### F3. Language manager and catalogue

- **Family.** `LanguageManager`
  (`apps/shared/preferences/src/language_manager.{hpp,cpp}`; the original is
  `src/app/language_manager.*` in Countdown Solver):
  - start-up order: environment override, saved `language/code`, system
    locale, `en`;
  - `availableLanguages()` returns `{code, name}`, English first, names in
    their own language and never translated; the pseudo-locale is never in
    the list;
  - `setLanguage()` saves, swaps the app's translator and Qt's own
    `qt_<code>.qm`, sets the layout direction from `QLocale`, switches the
    font family for the script, and calls `engine.retranslate()`; no restart;
  - `useSystemLanguage()` and `hasOverride()` for the "same as this device"
    row; the constructor takes the `.qm` base name, "so a second Qt app
    reuses this class rather than copying it".
- **Gates.** `update_translations` rerun in CI and failing on a diff; no
  `type="unfinished"` (Countdown: two grep jobs); `test_translations.cpp`
  (ICL Forge: nothing empty, `%n` kept, brand terms kept, source sets
  match); a language-switch tripwire, `tests/qml/tst_LanguageSwitching.qml`;
  a pseudo-locale `xx` generated by `tools/generators/gen_pseudo_locale.py`
  to catch a `qsTr()` bypass.
- **Authoring.** Whole sentences, `%1` placeholders, `//:` extracomments on
  short strings, a glossary, brand terms never translated
  (`docs/crucible/localisation.md`, `docs/translations.md` in Countdown).
- **Cubeulator.** E2b is not started. `strings.json` has stable keys and a
  note per string.
- **Proposal.**
  1. E2b **ports** the family `LanguageManager` and its gates rather than
     writing a third. It adds what the family lacks and `system.md` already
     asks for: numbers and times that follow the *app's* language
     (Countdown never calls `QLocale::setDefault`, so they follow the
     operating system), plural coverage per language, the unwrapped-string
     lint.
  2. **Ids** *(Iain, 2026-10-13, D5)*. The family writes
     `qsTr("English text")`, so a reworded English string invalidates its
     translations; `strings.json` has ids, which keep them. Qt supports ids
     (`qsTrId`, `lupdate -idbased`). The mechanism, the manager and the
     gates are the family's either way; `test_translations`-style checks
     match source sets by id. Nine languages is where a reworded English
     string costs most.

### F4. Language set and faces

- **Set.** en fr de es ar he yi in Countdown and ICL Forge; Automate adds
  ja and zh. Cubeulator had de, fr, es, ar, he, ja and zh, and omitted yi.
  Five are in all four apps: de, es, fr, ar, he. With Yiddish (D4) it is
  nine: en de fr es ja zh he ar yi.
- **Faces.** Hebrew and Arabic are Noto Sans Hebrew and Noto Sans Arabic in
  all three (`apps/shared/theme/assets/fonts`;
  `src/app/resources/fonts` in Countdown; `assets/web/vendor/fonts` in
  Automate), selected per language by `font_family_for()`. ICL Forge uses
  Noto Sans Hebrew for Yiddish too. Cubeulator's `Theme.qml` names IBM Plex
  Sans Hebrew and Arabic in `headingFamily`, with a 700-weight ceiling
  (`maxWeight`) because Plex has no ExtraBold in those scripts; no font file
  is in the tree yet.
- **Decided (D3, D4).** Noto Sans Hebrew and Noto Sans Arabic: the same
  files and the same OFL notices as the other three.
  - Both ICL Forge files are variable fonts with a weight axis of 100 to 900
    (checked: `NotoSansHebrew.ttf`, 110 KB; `NotoSansArabic.ttf`, 825 KB).
    ExtraBold exists, so the 700 ceiling (`maxWeight`) in `Theme.qml` goes.
    E2a should check that Qt draws weight 800 from the variable axis on each
    desktop OS.
  - Arabic is 825 KB, not the "about 300 KB" `platforms.md` said for the
    Plex face.
  - Yiddish uses Noto Sans Hebrew, as in ICL Forge. Its name in the list is
    יידיש, as Countdown Solver's `kLanguages` writes it.
  - The change in `Theme.qml` is two `case` lines, made before E2a bundles
    the files. Japanese and Chinese stay the system's, chosen by language,
    as Automate does.
  - Yiddish is in V1.0: nine languages, three of them right to left.

### F5. About

- **Family.** Crucible and Hearth, in this order: icon at 40 px, name,
  tagline; **what it does**; **version**, in the monospace face; **licence**
  (GPL, and a trademark disclaimer where there is one); one general
  sentence on what it carries from others, naming no component, because
  "what this build carries differs by platform and by build, and the notices
  file is the one place that says"; the repository link; **Licences…** and
  **Close**, focus on Close, Esc closes. Hearth compacts the version to
  `iclforge <ver> · <target> · built from <commit8>`.
  Source: `apps/crucible/ui/assets/qml/AboutDialog.qml` for the layout;
  `apps/hearth/ui/assets/qml/AboutDialog.qml`, `compactVersion()`, for the
  compact line.
- **Cubeulator.** A screen under Settings, which is right for a phone.
  Content is the family's, less the build details.
- **Proposal.** Keep the screen. Add, under the version:
  - a second monospace line, the family's compact form:
    `cubeulator 1.0.0 · windows-x64 · built from a1b2c3d4`, with `+dirty`
    when the tree was dirty; the data is `version_details()`, already in
    `src/lib/src/version.cpp` in the same format as the others;
  - a **Copy** button on it. Selecting text by touch on a phone is awkward;
    a bug report needs the line.
  The trademark note, the GPL line and the source address are already there.

### F6. Licences

- **Family.** ICL Forge: `cmake/Notices.cmake` assembles
  `notices/fragments/*.txt` and `notices/licences/` into `NOTICES.txt`;
  configure fails on an unresolved `{{TOKEN}}`; the same text ships in the
  package and is embedded as `:/notices/NOTICES.txt` for a read-only monospace
  dialog. Countdown Solver's is a hand-written list of five, in English
  only.
- **Cubeulator.** E0b is stronger than both: generated from the build and
  from vcpkg's own `copyright` files, per platform
  (`cmake/ThirdPartyNotices*.cmake`, `docs/licensing.md`).
- **Proposal.** Nothing to change. Two checks: the file is
  `THIRD-PARTY-NOTICES.txt` here and `NOTICES.txt` there; keep it, it says
  more. And About names no component, as above.

### F7. Diagnostics ("Copy details")

- **Family.** Forge (`PreferencesDialog.qml`), Hearth (`Settings.qml`) and
  Crucible (`SettingsPage.qml`) each have **Save diagnostics…**; Hearth's
  Diagnostics card adds **View live…** and **Copy diagnostics**. Crucible
  writes `crucible-diagnostics-<date>-<time>.txt`: the version block, Qt runtime and
  build, the platform plugin, the operating system, a ring of the last 512
  log messages, the settings, and anything secret as `<withheld>`, both by
  structure and by scrubbing. Countdown Solver keeps a rotating log file.
- **Cubeulator.** Nothing is planned. The repository is public, the app
  runs on five platforms, and on a phone the camera backend does not work
  yet; a person filing "it doesn't scan" has nothing to paste.
- **Decided (D6): in V1.0.** **Copy details** in About (Help can point to it
  later): text only. Version line; operating system
  and version; Qt runtime; graphics backend; whether a camera was found and
  which backend; app language, theme, text size, motion; window size and
  pixel ratio. **Never** a picture, a colour read from the cube, or a
  device name. The screen says what it contains, in the plan's own voice:
  "No picture, and nothing about your cube."

### F8. Help, shortcuts and names

- **Family.** Not consistent. Crucible: `?` and F1 open About. Hearth: `?`
  opens Shortcuts, which opens About, which opens Licences. Forge: a text
  "About" button. Forge says "Preferences"; everything else says
  "Settings". Forge's About says "LICENSE" where the rest say "LICENCES".
- **Cubeulator.** F1 is Help; About is under Settings, and in the macOS
  application menu; Ctrl+, is Settings; Esc is back (`platforms.md`).
- **Proposal.** Record Cubeulator's as the family standard: **F1 and `?` are
  Help; About is in Settings (and the macOS application menu); the word is
  "Settings"; "Licences".** It is the cleanest of the four and is already
  designed.

### F9. Tokens

| Cubeulator | ICL Forge (`Theme.qml`) | note |
|---|---|---|
| `paper` | `bg` | same value |
| `surface` | `surface` | same |
| `ink` | `text` | same |
| `inkSecondary` | `textMuted` (text at 68 %) | ICL Forge's alpha form measures 5.45:1 light, 7.42:1 dark. Cubeulator's fixed colour measures 6.02:1; its old `inkMuted(0.55)` was the 3.66:1 miss |
| `divider` | `divider` (text at 50 %) | decorative in both (3.16:1 there) |
| `accent` | `accent` | same in light; dark is `#ff5c40` there, `#EC3013` here |
| `accentText` | `accentInk` | ICL derives it: the accent if it reaches 4.5:1, else `a700` |
| `onAccent` | `accentText` | the label on a fill |
| `accentFill` | none (the button is `accent`) | **this is the fix**; see below |
| `ok`, `caution` | `good` (neutral), `warn` (the accent) | ICL has no teal or amber; cube colours are Cubeulator's subject and it needs them |

Dark neutrals differ too:

| | ICL Forge dark | Cubeulator dark |
|---|---|---|
| background | `#151313` | `#1A1918` |
| surface | `#1d1a1a` | `#242220` |
| text | `#ece7e5` (15.1:1) | `#F3F2F2` (15.7:1) |
| accent as shape | `#ff5c40` | `#EC3013` |

Measured here: the label on ICL Forge's light red button is 3.95:1 (its
`accentText` takes the better of `bg` and `text`) or 4.20:1 in white; red
used as text is 3.76:1.

- **Proposal.**
  1. E2a records the mapping in `tokens.json` and `system.md`. No token
     is renamed: Cubeulator's names say what a thing is for.
  2. Values do not move before V1.0. Whether the dark neutrals converge, and
     in which direction, is a look-and-feel choice for Iain with the two
     side by side; the measured set is Cubeulator's.
  3. Offer `accentFill`/`accentText` to ICL Forge (section 4).
  4. No accent palettes (Ink, Console) for Cubeulator *(Iain, 2026-10-13,
     D7)*. Red is the product's one accent, and the cube is the colour.

### F10. Docs and pictures

- **Family.** ICL Forge: `tools/pictures/pictures.json` lists 84 pictures;
  `take_pictures.py` drives the running app with `--shot --page <page>
  --language <code>` on a hermetic scratch settings store;
  `check_doc_pictures.py` is the gate. Automate:
  `scripts/capture-doc-screenshots.js` boots the real binary on replay
  fixtures, and `CLAUDE.md` says to regenerate in the same change as a UI
  change. Automate's docs skin (`docs/stylesheets/aqualink.css`) repeats the
  app's tokens, so docs and app cannot drift. Countdown's are by hand.
- **Cubeulator.** E0a's tour. The docs site is stock Material teal; the
  `CHANGELOG.md` still says "repository skeleton".
- **Proposal.**
  1. Give the tour a **manifest** and a check that a documentation picture
     is in it (the ICL Forge pattern). Pictures are generated, never taken
     by hand.
  2. Give the app a capture entry with `--page` and `--language` and a
     scratch settings store. It is exactly what E5 needs to run the tour per
     language.
  3. At E6, skin the docs site from `tokens.json`, as Automate does; today
     the app is red and the site is teal.

## 4. What the others should take from Cubeulator

For Iain. Not scheduled in this plan, and not this repository's to change.

| to | from Cubeulator |
|---|---|
| ICL Forge | the `accentFill`/`accentText` pair (label 3.95:1 to 6.46:1); a reduced-motion setting; one way to reach About and Help (F8); "Settings" and "Licences" throughout; every change applies at once (Forge's Preferences still has Save and Cancel); one bundle identifier scheme; the app icon in the app's red (it is blue); remembered window geometry; Hearth's 911 untranslated messages |
| Countdown Solver | persist the theme (only the language survives a restart); the Licences dialog in `qsTr`; accessible names and a focus ring; "Licences"; numbers that follow the app's language |
| Automate | a Licences screen and a licence line in About; one spelling (British, with ICL Forge); the same Noto faces are already shared |

## 5. Decisions

All taken by Iain on 2026-10-13.

| # | decision | answer | changes |
|---|---|---|---|
| D1 | How the publisher is written in the app | **Iain Chesworth Labs**: the publisher follows the account, and the repository moved to `iainchesworthlabs` on 2026-10-13 *(Iain, asked again after the move; before it, "Iain Chesworth")*. Copyright line "Copyright © 2026 Iain Chesworth" | E4a: version resource, `Info.plist`, AppStream developer. E3b: About. `Packaging.cmake` on `develop` already says it |
| D2 | Is Signal the suite's look | **Two looks, shared chassis.** Cubeulator stays Signal with ICL Forge; Countdown Solver and Automate keep Calm. What is shared is behaviour and structure, not palette | no reskin of any other app |
| D3 | Hebrew and Arabic faces | **Noto Sans Hebrew and Noto Sans Arabic**, the family's | `Theme.qml`, `system.md`, `platforms.md`, the notices |
| D4 | Yiddish | **In V1.0.** Nine languages: en de fr es ja zh he ar yi | `brief.md` D-table, `settings.md`, `platforms.md`, `accessibility.md`, E5's set, the prototype's list; plan "decided since" amended |
| D5 | Catalogue by source text (the family) or by id (`strings.json`) | **Ids** | E2b's brief: `qsTrId`, `lupdate -idbased`, ids matched in the translation tests |
| D6 | Copy details | **In V1.0**, not V1.1 | E3b gains the build line, its **Copy** and the **Copy details** row; `screens/about.md` says exactly what is copied; `strings.json` gains five strings |
| D7 | Accent palettes for Cubeulator | **No** | - |
| D8 | A shared component kit across repositories | **Not before V1.0 ships.** Align by spec; revisit when two products would use one kit; a vcpkg overlay port is the route the repository already knows | - |
| D9 | Identifiers across the suite | **Follows D1.** An identifier follows its repository's account, so with the move Cubeulator's is `com.iainchesworthlabs.cubeulator`, matching ICL Forge's macOS `com.iainchesworthlabs.*` *(Iain, 2026-10-13, after the move, chosen over `org.iainchesworthlabs.` and over keeping the old id; D12 amended)*. ICL Forge's Android `com.iclforge.hearth` is the outlier, for that repository | E4a: bundle id, Linux app id and desktop file name, Android package |

## 6. Where this lands

"Before V1.0" is cheap now and costs a migration or a rework later. "V1.1"
waits for the sessions in flight to finish.

| item | session | when |
|---|---|---|
| F1 publisher line | E4a (resource, `Info.plist`, AppStream), E3b (About) | before V1.0; already what E4a writes |
| F2 setting keys | E2a ("settings that persist"), E3a | before V1.0 |
| F3 language manager, ids and gates | E2b, which has not started: its brief says "port" and "ids" | before V1.0 |
| F4 Noto faces | E2a, before it bundles fonts | before V1.0 |
| F4 Yiddish | E5, with E2b's list | before V1.0 |
| F5 build line and Copy | E3b | before V1.0 |
| F6 licences | E3b | nothing to do; two checks |
| F7 Copy details | E3b | before V1.0 |
| F8 Help, About, naming | E3a, E3b | already designed; recorded here |
| F9 token mapping | E2a, as a note | before V1.0; value changes V1.1 |
| F10 tour manifest and capture entry | E0a's successor, E5 | before E5; skin at E6 |
| machine-translation note under the language list | E5 | before V1.0 |
| a "reviewed by" line per language | E5, E6 | V1.1 |
| dark neutrals converge with ICL Forge's | Iain, side by side | V1.1, if wanted |
| back-ports to the other repositories (section 4) | their own plans | not scheduled |

## 7. The checklist

What an app in the suite has, so a new one starts from it:

1. `GenerateVersion.cmake` and `version_details()`; the same block in About.
2. About: name, what it is, version line with **Copy**, licence, trademark
   note where one applies, source link, Licences, Close or back.
3. Licences from the build, never typed; shipped and shown as the same text.
4. Settings: Language, Appearance (theme, text size, motion), the
   app's own, About, Help. Every change applies at once.
5. `QSettings` with `group/name` keys, read before the first frame.
6. Linguist, the family `LanguageManager`, endonyms, same-as-device first,
   live switch with direction and font, a pseudo-locale, CI gates.
7. British source spelling; sentence case; no exclamation marks.
8. Noto Sans Hebrew and Arabic; the system's CJK, chosen by language.
9. A 2 px focus ring, 2 px out; names and roles on every control.
10. No telemetry, no update check, no account; say so in one paragraph.
11. Diagnostics you can paste, with no picture and nothing secret.
12. Pictures in the docs from a manifest, checked in CI.

## 8. Not known

- Whether ICL Forge's published design canvas still opens.
- Which of the machine translations have had a fluent reader. ICL Forge and
  Countdown Solver say in the app that theirs have not. Cubeulator's German,
  French, Spanish, Japanese and Simplified Chinese (2026-10-14) are drafts that
  no one has read; they say so in `languages.json` (`draft`) and `i18n.md`, and
  not yet in the app.
- When the suite wants one component kit (D8: not before V1.0 ships).
- Who reads the Yiddish, Hebrew and Arabic drafts before release.
- Whether Qt draws weight 800 from Noto's variable axis on all five
  platforms (E2a checks; F4).
- How the publisher is written in the other apps. Cubeulator's is decided
  (D1).
