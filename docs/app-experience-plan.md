# App experience plan

> **Status in 0.1.0-beta.1:** This is the plan the app is being built to, not
> a description of it. On `develop` are the system tests (E0a), the generated
> licence notices (E0b), the translations plumbing (E2b), the desktop shell's
> shortcuts, window memory, icon and packaging (E4a) and the design documents
> (E1). Not there: the component kit (E2a), and with it the E3 screens (first
> run, About, Licences, Privacy, Help, History, review and hand entry), the
> mobile shell (E4b), reviewed translations (E5) and release readiness (E6);
> the screens the app has are the six legacy ones in `src/app/qt`.

Written 2026-10-13. Status: **in effect.** On 2026-10-13 E0a, E0b and E1 had
reported, E2a and E4a were running, `feature/app-experience` held the design
and the accepted solve work, and no `ax` code branch had landed on it. Since
then the solve work (pull request 4), E0a (18), E2b (20), E0b (27) and E4a
(3) have landed on `develop` by pull request; E2a, the kit, has not. See
[Integration](#integration) and ["Where the phases
stand"](#where-the-phases-stand).

This covers everything a person meets in Cubeulator apart from the two things
already in flight - reading the cube ([recovery-plan.md](recovery-plan.md))
and animating the solve (`feature/solve-guidance`). That is: the design
language, the journey between screens, wording, languages, accessibility,
the about box and licences, platform conventions, app identity and
packaging, and tests that drive the whole app from its UI.

## Verdict

*As written on 2026-10-13, about `feature/direct-scan` at `fa81b2e`. What has
changed since is listed under "What was found", below.*

The app has a look but not a system, and the journey is not closed.

Six screens share a palette, a button and a confident direction: flat,
square, red accent, the feel of a technical drawing. Under that:

- **The journey dead-ends.** Nothing ever navigates to the Complete screen,
  and the Solve screen has no way out - no back, no cancel, no "scan
  another". The last move leaves a disabled "Solved!" button.
- **On a phone it cannot scan.** The Android and iOS camera backends return
  `camera_not_found`, and iOS does not build the app at all. No plan covers
  this.
- **Nothing is translatable, nothing is accessible.** Zero `qsTr`, zero
  `Accessible`, no keyboard focus anywhere.
- **The design's fonts are not shipped.** `Theme.qml` names Archivo and IBM
  Plex Mono; no font file is bundled, so every machine without them
  installed silently shows something else.
- **There is no about box, no licence notice, no app icon**, and the Android
  package is still `org.qtproject.example`.

So the order is: a safety net (tests that drive the UI), then a design phase
that ends in something to click through and sign off, then a component kit,
then each screen rebuilt on the kit, then platforms, languages and release.

## What was found

Read from `feature/direct-scan` at `fa81b2e`.

| area | today | where |
|---|---|---|
| Journey | Complete screen unreachable; Solve screen has no exit | `AppState.qml` (no `screen = "complete"`), `SolveScreen.qml` |
| Journey | "Solve history", "Review solution" and the "English" button do nothing or the wrong thing | `LaunchScreen.qml:47,133`, `CompleteScreen.qml:87,156` |
| Journey | No back handling (Android back, Esc); one remembered screen, for Settings only | `Main.qml`, `AppState.qml:24` |
| Solve | On a phone the app never says how to hold the cube - "Hold yellow on top, green facing you" exists only in the wide layout, and no move means anything without it | `SolveScreen.qml:202` |
| Solve | A grey "AUTO" camera box that is not a camera; captions such as "orthographic · no perspective" | `SolveScreen.qml:157,233` |
| Settings | "Detect hands with AI" and "Auto-advance" drive `ScanSession`, which no screen starts | `SettingsScreen.qml:125-145`, `main.cpp:128` |
| Settings | Only theme and graphics backend survive a restart; "Rendering" and "Developer" sit beside user settings | `SettingsState.qml` |
| Languages | No `qsTr`; no Linguist in the build; the language list is `["English"]`; some user text is built in C++ | all QML; `DirectScan.cameraError`, `SettingsModel.handDetectionUnavailableReason` |
| Accessibility | No `Accessible` roles or names; buttons are `Item` + `MouseArea`, not focusable; checkbox target is 20 px | `AppButton.qml`, `SettingsScreen.qml:34` |
| Accessibility | State is carried by colour alone (yellow/green dots, chip borders), in an app whose subject is six colours | `DirectScanScreen.qml:69-80` |
| Accessibility | 77 hard-coded pixel sizes, 29 of them 11 px or smaller; nothing follows the OS text size; looping animations with no reduced-motion switch | all screens |
| Type | Archivo and IBM Plex Mono named, not bundled, not loaded | `Theme.qml:46` |
| Layout | One breakpoint (720); phone layouts sized by subtraction (`root.height - 360`); no safe-area insets | `LaunchScreen.qml:26`, `PermissionScreen.qml:69` |
| Theme | 54 colour literals outside `Theme.qml`, 39 of them in the legacy `ScanScreen.qml` | `DirectScanScreen.qml`, `SolveScreen.qml` |
| Mobile | Screen can sleep mid-solve; no haptics or sound; orientation unspecified | `AndroidManifest.xml` |
| Identity | No icon, no splash, no bundle id, no Windows version resource; package `org.qtproject.example` | `src/app/platform/android/package/`, `cmake/Packaging.cmake` |
| Legal | No about box; no third-party notices anywhere; the GPL and App Store question is open | `LICENSE-NOTE.md` |
| Privacy | The permission screen promises "Nothing is stored, uploaded, or shared"; R4 plans to record every failed scan | `PermissionScreen.qml:25`, `recovery-plan.md` R4 |
| Docs | README and CHANGELOG still say "repository skeleton, no solver" | `README.md:16` |
| Tests | The coverage job builds with the app off, so nothing under `src/app/qt` counts toward the 80% gate | `ci.yml:495` |

**Since then**, to `develop` at 1644b058 (2026-10-14):

- *Journey.* The solve screen's last step goes to Complete, and **Rescan**
  leaves the solve screen for the scan; Complete's **Scan another cube**
  works. The Home **History** button and Complete's **Review the solution**
  did nothing at that date; both are built since (`HistoryScreen.qml`,
  `SolutionScreen.qml`). There is still no back handling: Esc and the Android back
  key are not bound, and the desktop shell (`DesktopShell.qml`) adds the
  shortcuts for Settings, a new scan, full screen and quit.
- *Settings.* *Detect hands with AI* and *Auto-advance* went with
  `ScanSession` (R6). The theme, the language, the solve method and the
  graphics backend persist. *Rendering* and *Developer* rows still sit beside
  the user's settings, in every build.
- *Languages.* Linguist is in the build, strings are keyed by id, nine
  languages are listed and switch live, and 74 strings are asked for by id.
  78 literals remain (`i18n-lanes.md`), most of them on the scan and solve
  screens, and `DirectScan.cameraError` is still text made in C++.
- *Accessibility.* Unchanged but for the desktop shell: no `Accessible` names
  or roles on the screens, and the buttons are still not focusable.
- *Type.* Archivo and IBM Plex Mono are still named and not bundled. The
  browser build bundles Japanese and Chinese subsets.
- *Identity.* The app has an icon, a version resource and installers
  (E4a), and its identifier and publisher are `com.iainchesworthlabs.cubeulator`
  and Iain Chesworth Labs. The Android manifest's package is still
  `org.qtproject.example`, the Android and iOS camera backends are still
  stubs, and iOS builds in CI (device, build-only, unsigned).
- *Legal.* Third-party notices are generated and ship in the desktop
  packages. There is no About or Licences screen: only a placeholder About
  (name and version) in the desktop shell.
- *Docs.* README and CHANGELOG no longer say "repository skeleton". The 80%
  coverage gate still excludes `src/app/qt`; the system tests' leg reports its
  coverage without gating on it.

## The person using it

Four facts decide most of the design. They are assumptions until E6 tests
them with real people.

1. **Both hands are on the cube.** During the solve the device is propped up
   or lying down, read from arm's length, and touching it means putting the
   cube down and losing the grip. Nothing on the solve screen may need a
   small or precise tap, and the screen must not sleep.
2. **On a phone, scanning is one hand each** - phone in one, cube in the
   other, with a thumb at best.
3. **They cannot solve a cube**, or they would not need this. `R U R' U'`
   is jargon to them. The picture carries the instruction; notation is the
   secondary reading.
4. **Colour is the subject.** About one man in twelve has a red-green colour
   vision deficiency, and a cube's red, orange and green sit exactly there.
   Wherever the app shows a cube colour it must be tellable apart without
   hue.

Principles that follow:

- **Never stranded.** Every screen has a way back and a way on, in every
  state, including the failure ones.
- **Readable from where the cube is.** Size for arm's length on the solve
  screen, not for a hand-held phone.
- **Say what is happening and what to do next**, in plain words, one thing at
  a time. The scan screen already does this well; it is the model.
- **Honest.** No button that does nothing, no setting that changes nothing,
  no promise the build does not keep.
- **One system.** A screen is built from kit parts and tokens. A colour or a
  size written into a screen is a defect.
- **Each platform's manners.** Back, menus, about, insets and installers
  behave the way that platform's users expect, from one shared QML shell.

## Decisions for Iain

None of these blocks E0. The defaults are what the plan assumes until told
otherwise.

| # | decision | default | blocks |
|---|---|---|---|
| 1 | Who it is for | Someone who cannot solve a cube. Plain words first, notation second | E1 |
| 2 | Design direction | Keep the existing direction and make it a system, rather than start again. Wanted: the original design handoff (the code cites its `support.js`; it is not in the repo) | E1 |
| 3 | Languages at first release | English, German, French, Spanish, Japanese, Simplified Chinese. Right-to-left later, but layouts built mirror-safe now. Drafts by machine; each needs a fluent reader before release | E5 |
| 4 | Platform order | Windows, macOS, Linux first - they scan today. Android next. iOS last | E4 |
| 5 | Licence and stores | Wait for E0b's report. See below | iOS, E6 |
| 6 | First-release features | Remove the dead buttons now. "Review solution" in, help and how-to-hold in, solve history out | E1 |
| 7 | Telemetry, crash upload | None. It keeps the privacy promise simple | E3 |
| 8 | Who builds mobile capture | A separate workstream after R4, outside this plan. Desktop release does not wait for it | E4b |

**Decided since, all on 2026-10-13.** Where an answer differs from the
default above, the answer stands.

- 3: nine languages - the six above plus Hebrew, Arabic and Yiddish, so
  right-to-left is in the first release (`docs/design/brief.md`). Hebrew and
  Arabic are set in Noto Sans, the family's face, not IBM Plex
  (`docs/design/family.md` D3, D4).
- 5: the Apple stores are skipped for now; the question is deferred, not
  resolved (`LICENSE-NOTE.md` on `feature/ax-licences`).
- 6: solve history is in, kept on the device (`docs/design/brief.md`).
- A release build never saves a picture from the camera; recording failed
  scans is for developer builds (`brief.md` D7).
- Source strings are British English (`brief.md` D10).
- Hebrew and Arabic are set in Noto Sans Hebrew and Noto Sans Arabic, the
  files the other apps ship (`design/family.md` D3).
- Translations are keyed by id, as `strings.json` already is, not by the
  English text (`design/family.md` D5).
- The repository moved to `iainchesworthlabs/cubeulator` on 2026-10-13.
  Publisher and identifier follow the account, and Iain confirmed both
  after the move: the publisher is written "Iain Chesworth Labs" in About
  and the packages, the copyright line stays "Copyright © 2026 Iain
  Chesworth", and the identifier is `com.iainchesworthlabs.cubeulator`,
  the form ICL Forge's macOS apps use under the same account
  (`design/family.md` D1 and D9, `brief.md` D12). Before the move the
  answers were "Iain Chesworth" and `org.iainchesworth.cubeulator`. For
  about an hour on 2026-10-13 this plan said `org.iainchesworthlabs.`; that
  came from a question that did not offer the `com.` form, and is withdrawn.

**On 5.** Cubeulator is GPL-3.0-or-later and Iain is its only author, so its
own terms are his to change. But the app links Qt Quick 3D and Qt Shader
Tools, which Qt publishes under GPLv3 or a commercial licence, not LGPL. If
that reading holds, an App Store exception on Cubeulator's own code would
not settle the Apple question, because the same terms arrive through Qt. The
routes would be a Qt commercial licence, drawing the cube without Quick 3D
(`src/render`, a bgfx stub, held no rendering code and was removed on
2026-10-13), or not shipping on the App Store. Desktop
and Google Play are not affected. E0b verifies this before anyone relies on
it; it is not legal advice.

## Lanes

Experience work stays out of the solve and scan lanes' files until they
land. The table was corrected on 2026-10-13 from the solve lane's own
account of `feature/solve-guidance` at `9e6f243`, checked against the
branch.

| lane | owns | experience work may |
|---|---|---|
| **Solve** - `feature/solve-guidance` | `Cube3DView.qml`, `SolveScreen.qml`, `solve_controller.*`, the `HeldCube` controller, `controllers/app_controllers.*`, `src/lib` solver and `vision/held_cube`, `tests/qml_test_main.cpp`, `tests/qml_test_app.hpp`, `cube_qml_tests` | read. After it lands, E3d restyles the chrome around the cube; the cube view and what a move means stay theirs |
| **Scan** - `feature/direct-scan` | `DirectScanScreen.qml`, `direct_scan.*`, the scan flow in `AppState.qml`, `ScanSession` and `ScanScreen.qml` (removed in R6), the review and hand-entry screen (R3), `src/app/platform` | read. After R4, E3c restyles the chrome; reading behaviour stays theirs |
| **Experience** - `feature/app-experience` | `Theme.qml`, `AppButton.qml`, `Icon.qml`, `Main.qml`, Launch, Permission, Settings and Complete screens, new About and Licences screens, `SettingsState.qml`, translations, packaging, identity, `tests/system/`, `docs/design/` | - |

The solve lane crossed into the scan lane, because Iain asked for the
camera on the solve screen: `direct_scan.{hpp,cpp}` in both the app and the
library (watching a held cube, and `start()` closing a running camera
first), and `AppState.qml` (`solveCamera`, `rescan()`, the camera kept for
"solve"). Those changes travel with the solve branch.

Two branches are stacked on the solve branch at `a27a36e` and land after it:
`feature/solve-merge-adjacent-moves` and `bugfix/launch-cube-top-view`.

`CubeView.qml` is no longer used by the solve screen. `CompleteScreen.qml`
still draws its cube with it.

Shared files: `src/app/CMakeLists.txt` and `tests/CMakeLists.txt` are
append-only, each lane in its own block. `main.cpp` is shared by everyone
and has its own rule under [Integration](#integration). `AppState.qml` is
split in E2a into navigation (experience) and scan flow (scan), agreed with
the scan and solve lanes before it is touched.

Contracts every lane keeps:

- **Wording lives in QML, never in a controller.** Controllers expose states
  and codes; QML turns them into translatable text. `SolveController`
  already works this way.
- **Every interactive item has an `objectName` and an accessible name.**
  Tests find controls by them.
- **The app starts without a camera.** A test binary can construct the
  controllers and feed frames.
- **Cube colours come from `Theme`**, which will gain colour-vision
  variants.

Found in the audit, belonging to the active lanes:

- Solve: reaching Complete; leaving Solve; the hold instruction on phones;
  the mock "AUTO" box and developer captions. **All four are dealt with on
  `feature/solve-guidance`**: the last step is Finish and goes to Complete,
  "Rescan" leaves the screen, the hold line is shared by both layouts, and a
  real camera preview replaced the box. Its controls are named `solve.cube`,
  `solve.next`, `solve.previous`, `solve.rescan` and `solve.rescanFromNote`.
- Scan: the "nothing is stored" promise against R4's automatic recording
  (settled: developer builds only); the two settings that die with
  `ScanSession`; `cameraError` text built in C++.

## Integration

*Resolved, except for the kit, by 2026-10-14: the solve work landed as pull
request 4 (with the scan work, 8), E0a as 18, E2b as 20, E0b as 27 and E4a as
3. Start-up is `main.cpp` making one `AppRuntime` that owns `AppControllers`
and the engine, with `DesktopIntegration` and `installBundledFonts()` as the
single lines around it. `feature/ax-kit` has not landed. What follows is the
account as written on 2026-10-13.*

Added 2026-10-13. Five branches hold finished or running work. None of the
`ax` branches contains another.

| branch | lane | commits | state |
|---|---|---|---|
| `feature/solve-guidance` | solve | 9 on `fa81b2e` | pushed; **accepted by Iain 2026-10-13** and merged into `feature/app-experience` the same day |
| `feature/ax-system-tests` | E0a | 11 | pushed; reconciling start-up, below |
| `feature/ax-licences` | E0b | 7 | local |
| `feature/ax-desktop` | E4a | 5, running | pushed; draft pull request 3, against `develop` |
| `feature/ax-kit` | E2a | 7, running | local; has not yet merged this branch |

`feature/app-experience` is on `origin` from 2026-10-13.

**How work lands, from 2026-10-13.** `develop` takes no direct pushes.
Every topic branch opens a pull request against `develop` and goes through
a merge queue, which merges it onto the current tip, re-runs the required
checks and deletes the branch (`docs/branch-protection.md` on `develop`).
So the `ax` branches land on `develop`, not on this branch. This branch is
where they pick up the solve work and the plan until those are on
`develop`, and it lands there by pull request like any other.

**Start-up is contested.** Four of them changed `main.cpp` independently:

- solve: `cube::app::AppControllers` - the controllers, `registerWithQml()`
  and `addImageProviders(engine)` - plus a sixth controller, `HeldCube`,
  made from `DirectScan&` and `SolveController&`. The solve screen does not
  load without it. Also a `CUBE_SOLVE_DEMO` hook.
- E0a: `cube::app::AppRuntime` - five controllers, their registration and
  the engine that loads `Main.qml`.
- E4a: `DesktopIntegration`, three lines around the engine.
- E2a: `installBundledFonts()`, one line.

One shape, applied by whichever lands second:

- `AppControllers` is the only place a controller is made or registered.
  Adding a controller means adding it there.
- `AppRuntime` owns an `AppControllers` and the engine, and nothing else
  about controllers.
- `main.cpp` makes an `AppRuntime`. Fonts, desktop integration and any
  later shell concern are single lines around it, as E4a and E2a already
  wrote them.

**Where the lines meet.** `feature/app-experience` holds the solve work: it
merged `feature/solve-guidance` directly, because `feature/direct-scan` is
the scan lane's to move and has not moved. The merge was clean, since this
branch had only documents of its own. Start-up here is therefore the solve
lane's `AppControllers`, and every `ax` branch meets it on its next merge,
from this branch or from `develop` once the solve work is there.

The system-test session reconciles the shape, on its own branch, because
its tests are what prove the result. In the same change:

- `known_today_test` stops asserting that Complete is unreachable and that
  the solve screen has no exit.
- The journey test walks Finish to Complete and the rescan way out.
- The temporary CI trigger for the branch (`e554bcd`) is taken out. Nothing
  replaces it: pull requests into `develop` already run CI.

**Order into `develop`.** Each pull request shows everything its branch has
that `develop` lacks, and `develop` lacks the scan work, the solve work and
this plan. So:

1. The solve work, pull request 4, which carries the scan work with it.
2. This branch: after 1 it is the plan and the design documents alone.
3. E0a, the safety net, with start-up reconciled.
4. E0b, then E2a and E4a as they finish. Each merges `develop` first and
   re-applies its line or two of `main.cpp` around `AppRuntime`.

`develop`'s own CI is red at the time of writing, for reasons that predate
this work; a separate session owns making it green. Two checks are required
today: the gitflow branch name and the dependency diff.

**Two hazards, reported by the solve lane and not yet reproduced here.**

- A controller registered with `qmlRegisterSingletonInstance` serves one
  `QQmlEngine` per process. A second engine gets null singletons. Both test
  harnesses already keep to one engine each; the merged one must too.
- A second `QQuickWindow::grabWindow()` with `Cube3DView` in the scene
  crashed the process for the solve lane (Qt 6.8.3, Direct3D), so its
  gallery test takes one picture per process. The screenshot tour takes
  every picture in one process. In CI it runs on Linux under software GL,
  where this has not been seen. On Windows it is the first thing to check
  after the merge, on the solve screen first.

**To settle where the kit meets the others.** From the kit's report of
2026-10-13 (`docs/kit.md` on `feature/ax-kit`), checked against the
branches.

- **Font notices: fixed on the licences branch, to be proven at the
  merge.** The kit bundles six font files under `src/app/qt/fonts/`: Archivo
  in three weights, IBM Plex Mono Medium, Noto Sans Hebrew and Noto Sans
  Arabic. The licences manifest had entries for Archivo and IBM Plex Mono
  only, pointing at `assets/fonts/`, and emitted an entry only when files
  existed there, so the fonts would have shipped with no notice and nothing
  would have failed. From `60e74ab` on `feature/ax-licences` the manifest
  follows the files, the two Noto faces have entries, and a font file that
  no entry claims fails the notices test and the install check. The entries
  are dormant until the files exist: when the kit and the licences branch
  are both on this branch, run that test.
- **The `AppState.qml` split is proposed, not agreed.** The proposal is at
  the foot of `docs/kit.md` and was written before the solve lane added
  `solveCamera` and `rescan()`. It is re-read against the merged file, then
  put to the solve and scan lanes, before anything moves.

**Two findings for the design, also from the kit.**

- `outline` measures under 3:1 on the hover and press washes (2.96 and 2.74
  light, 3.13 and 2.87 dark). `tokens.json` has no such pairing, so
  `check_tokens.py` cannot see it. The kit already avoids the case: the one
  boundary that sits inside a washed row, the switch's track edge, turns
  `ink` while hovered or pressed. Either that becomes the written rule, or
  `outline` darkens (about `#7E7B7A` in light) and the pairs join the table.
- On Windows, Qt reports an `AlertMessage` as a window and does not put
  `Accessible.description` where a screen reader speaks it. So anything a
  person must hear is in the control's name or in text in the reading
  order, never in a description alone. `design/accessibility.md` should say
  so wherever it relies on a description.

## Phases

Each phase has one gate. Branches are `feature/ax-<topic>`, cut from
`feature/app-experience`, which was cut from `feature/direct-scan` and
follows it.

### Where the phases stand

As of 2026-10-14, `develop` at 7e764eef, from the code.

| phase | state |
|---|---|
| E0a System tests | Landed (18). `tests/system` boots the real `Main.qml`, walks the journey and takes the screenshot tour; `src/app/qt` coverage is reported, not gated |
| E0b Licences | Landed (27). Notices are generated from the build and ship with the desktop packages; no screen shows them yet |
| E1 Design | Landed: `docs/design/`. The prototype page is built from `strings.json` by `build.py`; run it after the strings change |
| E2a Kit | Not landed. `Theme.qml` is the legacy one; `AppButton` is still an item and a mouse area; only `installBundledFonts()` is there |
| E2b Translations | Landed (20), with ja and zh-CN machine drafts (96). See [i18n.md](i18n.md) |
| E3a-E3d Screens | Mostly not on `develop`: the legacy Launch, Permission, Scan, Solve, Settings and Complete screens remain, some converted to ids. The solution and History screens are built (`SolutionScreen.qml`, `HistoryScreen.qml`) |
| E4a Desktop | Landed (3): icon, version resource, installers, shortcuts, remembered window, macOS menu bar (`platforms.md` says it has not been run on a Mac). Nothing calls the keep-awake helper yet |
| E4b Mobile | Nothing on `develop`. No mobile capture, so a phone build cannot scan |
| E5 Languages | Not reviewed. ja and zh-CN are complete drafts; de, fr, es, he, ar and yi are empty |
| E6 Release readiness | Nothing on `develop`: no screen-reader pass, no sessions with people, no store listings |
| F Family alignment | Nothing on `develop`; the decisions D1-D9 are recorded in `design/family.md` |

**E0 - Safety net and inventory.** No visual change. Starts now.

- *E0a System tests.* Boot the real `Main.qml` with the real controllers and
  a test frame source (`tests/unit/drawn_cube.hpp` already draws cube sides
  with nobody in the picture). Drive it with synthesised input and walk the
  journey. Save a screenshot of every screen. Put `src/app/qt` into the
  coverage job.
- *E0b Licences.* Inventory every component that ships, per platform.
  Generate the notices from the build (vcpkg installs each port's
  `copyright` file) so they cannot drift when R6 removes M3T and
  onnxruntime. Verify the Qt Quick 3D reading. Write the options for
  `LICENSE-NOTE.md`.

Gate: one command walks launch to solve headless on Linux in CI and uploads
the screenshots; the coverage report includes `src/app/qt`; the notices are
generated; the licence options are on Iain's desk.

**E1 - Design.** Starts now, beside E0. Writes `docs/design/` and no app
code.

- `brief.md` - who it is for, where and how it is used, what the first
  release holds.
- `journey.md` - every screen and every state of it (empty, working, failed,
  refused, interrupted), how each is entered and left, and what "back" does
  on each platform.
- `system.md` - tokens (colour with measured contrast, a type scale in
  relative units, spacing, size classes, motion), every component and its
  states, minimum target sizes.
- `screens/` - one spec per screen and size class: first run, permission,
  scan, review and hand entry, solve, complete, settings, about, licences,
  privacy, help.
- `content.md` - voice, a glossary (side, turn, layer; the notation policy),
  and the full string inventory with keys and notes for translators.
- `accessibility.md` - the target (WCAG 2.2 AA as the reference), what a
  screen reader says on each screen, the keyboard map, how six cube colours
  are told apart without hue, text scaling, reduced motion.
- `platforms.md` - per platform: back, menus, where About lives, insets,
  orientation, keep-awake, haptics, window sizes, icons, installers.
- Identity - app icon, name treatment, store screenshot plan.
- A click-through prototype of the whole journey, phone and desktop, light
  and dark.

Gate: Iain has clicked through the prototype at both sizes and signed it
off; every state in `journey.md` has a spec; the string inventory is
complete.

**E2 - The kit.** After the E1 gate (E2b can start once E0a lands).

- *E2a Components.* Tokens in `Theme`; bundled fonts with their licences;
  focusable, accessible controls (button, toggle, choice, list row, dialog,
  banner, progress); layout primitives with size classes and safe areas; a
  navigation stack with back handling; settings that persist; a
  developer-only gallery screen.
- *E2b Translations plumbing.* Linguist in the build, keyed by id; the
  family's `LanguageManager` and its CI gates ported rather than written a
  third time (`design/family.md` F3); live language switching, a
  pseudo-locale (longer, accented strings), locale-aware numbers and times,
  plural forms, and a lint that fails on an unwrapped user-visible string.

Gate: every kit component is reachable by keyboard, has a name and role,
meets the target size, and holds 4.5:1 contrast in both themes; the
pseudo-locale truncates nothing; on a machine without Archivo installed the
app still shows Archivo.

**E3 - Screens on the kit.** After E2a.

- *E3a* Launch, first run, permission, settings, help.
- *E3b* About, licences, privacy.
- *E3c* Scan and review chrome - after R4, with the scan lane.
- *E3d* Solve and complete chrome - after `feature/solve-guidance` lands.

Gate: the system tests walk the whole journey finding controls by accessible
name only, by keyboard only, at both sizes, in both themes, in the
pseudo-locale; no colour or pixel-size literal remains outside `Theme`.

**E4 - Platforms.** Beside E3.

- *E4a Desktop.* Icons; Windows version resource and installer; macOS bundle,
  menu bar and About; Linux desktop entry and metadata; minimum window size,
  remembered geometry, keyboard shortcuts.
- *E4b Mobile shell.* Package id, icons, splash, back button, orientation,
  keep-awake, insets, haptics; build the Qt app for iOS. Usable only once
  mobile capture exists (decision 8).

Gate: an installable package per desktop OS with icon, version and About; an
APK with its own identity; the screen stays awake through a solve.

**E5 - Languages.** Translate the chosen set, add fallback fonts for scripts
Archivo does not cover, run the screenshot tour per language.
Gate: a fluent reader has reviewed each language's tour.

**E6 - Release readiness.** A pass with a real screen reader on each OS;
sessions with five people who cannot solve a cube; store listings and
privacy policy; the licence decision recorded; README, CHANGELOG and docs
brought up to date.
Gate: four of five people get from launch to a solved cube without help.

## Family alignment and V1.1

Added 2026-10-13, from a read of the other apps in the suite: ICL Forge,
Countdown Solver and Automate. The full account, with sources, is
[`design/family.md`](design/family.md). Two findings matter to this plan:

- Cubeulator's palette, type, corners and rules are already ICL Forge's
  `signal` palette, value for value. E1's `tokens.json` can be checked
  against `apps/shared/theme/assets/qml/Theme.qml` there.
- The suite's common machinery (version stamp, Linguist and a
  `LanguageManager`, endonym language list, `QSettings`, no telemetry) is
  mostly shared already. The gaps are small and are cheapest to close in
  the sessions that are open now.

**Decided on 2026-10-13** (`family.md` section 5), all by Iain:

- D1: the publisher follows the account. The repository moved to
  `iainchesworthlabs` the same day, so it is **Iain Chesworth Labs**, with
  the copyright line in Iain's own name. D9 follows: the identifier is
  `com.iainchesworthlabs.cubeulator`.
- D2: two looks, one shared chassis. Cubeulator stays Signal with ICL
  Forge; no other app is reskinned.
- D3: Hebrew and Arabic are Noto Sans, the family's faces.
- D4: Yiddish is in V1.0, so nine languages.
- D5: strings are keyed by id.
- D6: Copy details is in V1.0.
- D7: no accent palettes. D8: no shared kit before V1.0 ships.

**Before V1.0** - cheap now, a migration or a rework later. Each rides in a
session that is already planned; none is a new session.

| item | rides with | what changes |
|---|---|---|
| F1 publisher line and identifier | E4a, E3b | one string, "Iain Chesworth Labs", in the version resource, `Info.plist`, AppStream and About; `com.iainchesworthlabs.cubeulator` as the bundle id, Linux app id and Android package. E4a's branch still carries the earlier answers |
| F2 setting keys | E2a, E3a | the keys are named in `design/screens/settings.md`, in the family's `group/name` style |
| F3 language manager and ids | E2b | its brief says **port** the family `LanguageManager` and its CI gates, key by id (`qsTrId`), and add locale-aware numbers, plurals and the unwrapped-string lint |
| F4 Noto faces | E2a, before it bundles fonts | two `case` lines in `Theme.qml`, the 700 ceiling goes, and E2a checks that Qt draws weight 800 from the variable axis |
| F4 Yiddish | E2b, E5 | the ninth language: its name in the list, Noto Sans Hebrew, a machine draft, RTL in the tour |
| F5 build line | E3b | the build line and a **Copy** button on it |
| F7 Copy details | E3b | a text block for a bug report: no picture, nothing about the cube. Specified in `design/screens/about.md` |
| F9 token mapping | E2a | a note in `tokens.json` and `system.md`; no value moves |
| machine-translation note | E5 | one sentence under the language list, as ICL Forge and Countdown Solver have |

The design documents already carry D3, D4 and D6: `brief.md`, `system.md`,
`platforms.md`, `settings.md`, `about.md`, `accessibility.md`, `strings.json`
and the prototype.

**V1.1** - after E3 and E5 have landed.

| item | what |
|---|---|
| "Reviewed by" per language | said in the app, per language, once a fluent reader has looked |
| F10 tour manifest | the screenshot tour gets a manifest and a check that documentation pictures are in it; a capture entry with `--page` and `--language` |
| Docs skin | the documentation site skinned from `tokens.json` |
| Dark neutrals | converge with ICL Forge's, if Iain wants it, in the direction he chooses |
| Help points to Copy details | from **Trouble scanning** |
| Back-ports | the list in `family.md` section 4, for the other repositories |

Gate: every line of the checklist in `family.md` section 7 is true of
Cubeulator, or is recorded as a deliberate difference.

## Sessions

Every session spawned from this plan runs on **Sonnet 5.5** (Iain,
2026-10-13).

| id | session | branch | starts when | touches |
|---|---|---|---|---|
| E0a | System tests that drive the app from its UI | `feature/ax-system-tests` | now | `tests/system/`, `main.cpp` start-up, `ci.yml`, coverage |
| E0b | Licence inventory and generated notices | `feature/ax-licences` | now | `cmake/`, `docs/`, `LICENSE-NOTE.md` |
| E1 | Design the app experience | `feature/ax-design` | now | `docs/design/` only |
| E2b | Translations plumbing | `feature/ax-i18n` | E0a landed | build, `main.cpp`, `SettingsState.qml` |
| E2a | Component kit | `feature/ax-kit` | E1 gate | `Theme`, kit components, navigation |
| E3a | Launch, permission, settings, help | `feature/ax-screens-shell` | E2a | those screens |
| E3b | About, licences, privacy | `feature/ax-about` | E2a and E0b | new screens |
| E3c | Scan and review chrome | `feature/ax-scan-chrome` | E2a and R4 | scan lane's QML, with them |
| E3d | Solve and complete chrome | `feature/ax-solve-chrome` | E2a and solve landed | solve lane's QML, with them |
| E4a | Desktop packaging and manners | `feature/ax-desktop` | E1 gate | packaging, resources |
| E4b | Mobile shell | `feature/ax-mobile` | E2a and decision 8 | manifest, iOS build |
| E5 | Languages | `feature/ax-languages` | E3 gate | translation files |
| E6 | Release readiness | `feature/ax-release` | E4 and E5 | docs, listings |
| F | Family alignment (V1.1) | `feature/ax-family` | E3 and E5 landed | About, Settings, `docs/design/family.md` |

## Testing the app from its UI

Layers, the first two existing today:

1. **Unit** and 2. **integration** - Catch2, as now.
3. **Component** - kit parts loaded for real. Builds on the harness the solve
   lane added (`cube_qml_tests`: Catch2, `QGuiApplication`, offscreen).
4. **System** - the whole app, from `Main.qml`, driven by input events.

Journeys the system tests walk: first launch to solved and round again;
permission refused, then granted; no camera; cancel mid-scan; a scan that
does not make a valid cube; every setting changed and still set after a
restart; language and theme switched live; phone and desktop sizes; the
whole thing by keyboard alone; back at every screen.

Checks that ride on those walks: nothing truncated in the pseudo-locale;
every interactive item has a name, a role and a big enough target; contrast
computed from the tokens; the screenshot tour, uploaded from CI as the
review surface for design, translation and store listings.

**Coverage.** C++ is straightforward: build the app in the coverage job and
`gcovr` counts `src/app/qt` as it already counts the library. QML is not:
`gcov` cannot see QML or JavaScript, and Qt's own answer is Coco, a paid
product. The plan's measure for QML is reach - every screen, every state of
it, every control activated at least once, counted against the inventory in
`journey.md` and enforced by a test that fails when something new is never
visited. E0a measures first; a number is set after that, not before.

**Where it runs.** Linux under a virtual display with software GL is the
gate (Quick 3D needs a real render path; offscreen alone is enough only for
tests that never draw). Windows and macOS run the same suite if it proves
stable. Phones are not in CI at first.

## What is not known

- **QML line coverage without Coco.** Reach may be the honest ceiling.
- **Quick 3D on CI's software renderers**, on all three desktop runners.
- **The Qt Quick 3D licence reading.** E0b checks the source of truth.
- **How a move is advanced with both hands full** - a large tap, a timer,
  voice, or the camera seeing the turn. It needs trying with a real cube in
  hand. The solve lane owns the mechanism; E1 proposes.
- **The original design handoff.** Its tokens survive, value for value, in
  ICL Forge's `signal` palette (`design/family.md`). The handoff files
  themselves have not been found.
- **Who reads each translation.**
- **Screen readers over custom QML on Android and iOS.** The 3D cube and the
  canvas-drawn icons are opaque to them and need text alternatives.
- **Mobile capture.** Until it exists the phone experience can be designed
  and built but not used.
