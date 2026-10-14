# First run

> **Status in 0.1.0-beta.1:** Not built. There is no first-run screen and no
> setting that remembers one was shown: the app opens on Home every time.

Lane: experience. New.

Shown once, on the first launch, before Home. It says what will happen in
three steps and what is needed, so that nobody starts a scan without a
cube and a light. One screen, not a carousel (brief, D9).

Entered from: the first launch; **Help > How it works**.

## Layout

**Compact.** One column, 24 margin, scrolling if it must:

1. A row: the eyebrow at the leading side, the language button at the
   trailing side.
2. The headline.
3. Three numbered steps. Each is a 32 px numbered box, a bold word and a
   sentence in `inkSecondary`.
4. "You will need", then two list items in `inkSecondary`.
5. The primary action, full width, pushed to the bottom of the screen.

**Short.** The same column; it scrolls.

**Expanded.** Two panes. Leading: the column above, at most 640 wide, with
the primary action directly under the list. Trailing: `surface` with the
same decorative cube as Home.

## Parts

| part | component | string | objectName |
|---|---|---|---|
| Eyebrow | eyebrow | `app.name` | `firstRun.eyebrow` |
| Language | button, ghost, trailing chevron. Label is the current language's own name. Accessible name is `firstRun.language` plus that name | `firstRun.language` | `firstRun.language` |
| Headline | `title`, heading level 1 | `firstRun.title` | `firstRun.headline` |
| Steps | An ordered list of three | `firstRun.step1.title`, `firstRun.step1.body`, `firstRun.step2.title`, `firstRun.step2.body`, `firstRun.step3.title`, `firstRun.step3.body` | `firstRun.steps` |
| Needs | A heading and a list of two | `firstRun.need.title`, `firstRun.need.cube`, `firstRun.need.light` | `firstRun.needs` |
| Get started | button, primary, thumb, block, trailing chevron | `firstRun.start` | `firstRun.start` |
| Done (revisit) | button, primary, thumb, block | `common.done` | `firstRun.done` |

The step numbers are read as list positions ("1 of 3"), not as separate
text.

The app starts in the device's language. The language button is here
because this is the one screen a person may not be able to read.

## States

### `first-run.welcome`

First launch, before Home has ever been shown.

- **Get started**: records that the screen has been seen, then to
  `home.default` (or whichever Home state applies). It is never shown
  again by itself.
- **Language**: opens `settings.language`. Choosing a language applies it
  at once and returns here.
- Back on Android leaves the app without recording anything, so the screen
  shows again next time. There is no back control on screen.

The camera is not asked for here. Permission is asked for when the person
first presses Scan, where the reason is obvious.

### `first-run.revisit`

Opened from **Help > How it works**.

- A top bar with back and the title `help.howItWorks`.
- No eyebrow and no language button.
- The primary action is **Done**. It and back both return to
  `help.topics`.
