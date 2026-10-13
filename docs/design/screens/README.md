# Screen specs

One file per screen. Each follows the same order:

1. **What it is for**, its lane, and where it is entered from.
2. **Layout** in each size class (`../system.md`, "Size classes"): compact,
   short and expanded. Medium is the compact layout in a 560-wide centred
   column unless a spec says otherwise.
3. **Parts**: every control and region, the kit component it is, the
   strings it shows, and its `objectName`. Tests find controls by
   `objectName` and by accessible name (plan, lane contracts).
4. **States**: one heading per state, and the heading is the state's id
   from `../journey.md`. `tools/check_design.py` fails if the two lists
   differ, or if a spec names a string that `../strings.json` does not
   have.
5. **Needs** from the scan or solve lane, where the screen is theirs.

Conventions:

- `objectName` is `<screen>.<part>` in camelCase, and never changes once a
  test uses it.
- An accessible name is the visible label unless the table gives another.
- "Leading" and "trailing" are the start and end of a line of text: left
  and right in English, swapped in Hebrew and Arabic.
- Sizes are in device-independent pixels at a text scale of 1.0.
- Where a screen belongs to another lane, the spec is a proposal for its
  chrome and states. How the cube is read and what a turn means are not
  specified here.

The prototype (`../prototype/cubeulator-prototype.html`) draws every state
in every size class. Where the prototype and a spec disagree, the spec
wins and the prototype is wrong.

| screen | file | lane |
|---|---|---|
| Home | [home.md](home.md) | experience |
| First run | [first-run.md](first-run.md) | experience |
| Permission | [permission.md](permission.md) | experience |
| Scan | [scan.md](scan.md) | scan (proposal) |
| Review and hand entry | [review.md](review.md) | scan (proposal) |
| Solve | [solve.md](solve.md) | solve (proposal) |
| Complete | [complete.md](complete.md) | solve; chrome by experience |
| The solution | [solution.md](solution.md) | experience |
| History | [history.md](history.md) | experience |
| Settings | [settings.md](settings.md) | experience |
| About | [about.md](about.md) | experience |
| Licences | [licences.md](licences.md) | experience |
| Privacy | [privacy.md](privacy.md) | experience |
| Help | [help.md](help.md) | experience |
