# Screen specs

> **Status in 0.1.0-beta.1:** These are specifications, not descriptions of
> the app. The app has six screens, all of them the legacy ones in
> `src/app/qt`, not the kit's: Home, Permission, Scan, Solve, Complete and
> Settings are each partly what their spec says, and First run, Review and
> hand entry, The solution, History, Licences, Privacy and Help are not built
> (About is a placeholder dialog). Each page opens with its own status, and
> the table at the foot says it at a glance.

One file per screen. Each follows the same order:

1. **What it is for**, its lane, and where it is entered from.
2. **Layout** in each size class (`../system.md`, "Size classes"): compact,
   short and expanded. Medium is the compact layout in a 560-wide centred
   column unless a spec says otherwise.
3. **Parts**: every control and region, the kit component it is, the
   strings it shows, and its `objectName`. Tests find controls by
   `objectName` and by accessible name (plan, lane contracts).
4. **States**: one heading per state, and the heading is the state's id
   from `../journey.md`. `docs/design/tools/check_design.py` fails if the
   two lists differ, or if a spec names a string that `../strings.json` does
   not have.
5. **Needs** from the scan or solve lane, where the screen is theirs.

Conventions:

- `objectName` is `<screen>.<part>` in camelCase, and never changes once a
  test uses it.
- An accessible name is the visible label unless the table gives another.
- "Leading" and "trailing" are the start and end of a line of text: left
  and right in English, swapped in Hebrew, Arabic and Yiddish.
- Sizes are in device-independent pixels at a text scale of 1.0.
- Where a screen belongs to another lane, the spec is a proposal for its
  chrome and states. How the cube is read and what a turn means are not
  specified here.

The prototype (`../prototype/cubeulator-prototype.html`) draws every state
in every size class. Where the prototype and a spec disagree, the spec
wins and the prototype is wrong.

| screen | file | lane | in 0.1.0-beta.1 |
|---|---|---|---|
| Home | [home.md](home.md) | experience | Partly: `LaunchScreen.qml` |
| First run | [first-run.md](first-run.md) | experience | Not built |
| Permission | [permission.md](permission.md) | experience | Partly: `PermissionScreen.qml`, phones only |
| Scan | [scan.md](scan.md) | scan (proposal) | Partly: `DirectScanScreen.qml` |
| Review and hand entry | [review.md](review.md) | scan (proposal) | Not built |
| Solve | [solve.md](solve.md) | solve (proposal) | Partly: `SolveScreen.qml` |
| Complete | [complete.md](complete.md) | solve; chrome by experience | Partly: `CompleteScreen.qml` |
| The solution | [solution.md](solution.md) | experience | Not built |
| History | [history.md](history.md) | experience | Not built |
| Settings | [settings.md](settings.md) | experience | Partly: `SettingsScreen.qml` |
| About | [about.md](about.md) | experience | A placeholder dialog |
| Licences | [licences.md](licences.md) | experience | Not built (the notices are generated and ship in the packages) |
| Privacy | [privacy.md](privacy.md) | experience | Not built |
| Help | [help.md](help.md) | experience | Not built |
