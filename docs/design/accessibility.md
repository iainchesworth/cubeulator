# Accessibility

## The target

**WCAG 2.2 level AA is the reference.** It is written for the web;
Cubeulator is a native app, so each criterion is read the way WCAG2ICT
reads it for software. Where a platform asks for more (48 px targets on
Android), the stricter rule is the kit's rule everywhere.

The app today has no accessible names, no keyboard focus and state carried
by colour alone (plan, "What was found"). So this is the design for the
first accessible version, not a refinement.

What "done" means, and who checks it:

| check | by |
|---|---|
| Contrast of every token pairing | `tools/check_tokens.py`, on every change |
| Every interactive item has a name, a role and a big enough target | System tests (E0a), walking the journey |
| The whole journey by keyboard alone | System tests |
| Nothing truncated at 200% text or in the pseudo-locale | System tests, from the screenshot tour |
| A real screen reader on each OS | A person, in E6. Not automatable |
| Five people who cannot solve a cube, one of them with a colour vision deficiency if at all possible | E6 |

## The criteria that shape the design

| criterion | how it is met |
|---|---|
| 1.1.1 Non-text content | The 3D cube, the camera picture, every icon, chip and sticker has a text alternative (below) |
| 1.3.1 Info and relationships | Headings are headings; a group of choices is a group with a name; the stage list is a list |
| 1.3.4 Orientation | Nothing is locked to portrait or landscape (`platforms.md`) |
| 1.4.1 Use of colour | Cube colours carry marks and names. No chrome state is colour alone (`system.md`) |
| 1.4.3 Contrast, 1.4.11 Non-text contrast | 4.5:1 for all text, 3:1 for control edges, icons and marks. Measured |
| 1.4.4 Resize text | Layouts hold at a text scale of 2.0 |
| 1.4.10 Reflow | One column at 320 wide; no sideways scrolling |
| 1.4.12 Text spacing | Text boxes grow with their text; nothing has a fixed height |
| 2.1.1 Keyboard, 2.1.2 No trap | Everything is reachable and leavable by keyboard. Dialogs hold focus and release it on close |
| 2.2.1 Timing adjustable | The only timer is the optional paced solve. It is off by default, has three paces, and any key or tap pauses it. The 10-second scan hint is advice, not a limit |
| 2.2.2 Pause, stop, hide | No animation loops beyond five seconds except the lean of the next layer, which reduced motion stops |
| 2.3.3 Animation from interactions | Reduced motion (below) |
| 2.4.3 Focus order | Reading order: leading to trailing, top to bottom, with the primary action last in its group |
| 2.4.7 Focus visible, 2.4.11 Focus not obscured | The focus ring; sheets and dialogs never cover the focused item |
| 2.5.1 Pointer gestures, 2.5.7 Dragging | No gesture is required. Every action is a single tap or key. Dragging the home cube is decoration |
| 2.5.8 Target size | 48 px minimum; 56 on scan; 96 on solve |
| 3.1.1 Language, 3.1.2 Language of parts | The app's locale is set on the window. Notation is marked as not-language so it is spelt, not pronounced |
| 3.2.x Predictable | Focus never moves by itself except to a dialog. A setting applies when it is changed, and says so |
| 3.3.1 Error identification, 3.3.3 Suggestion | Every failure state names what happened and offers the next step (`journey.md`) |
| 4.1.2 Name, role, value | Every kit component sets `Accessible.role`, `Accessible.name` and, where it has one, its value or checked state |
| 4.1.3 Status messages | Live announcements, listed per screen below |

## Colour vision

About one man in twelve and one woman in two hundred has a colour vision
deficiency, nearly always on the red-green axis. A cube's red, orange and
green sit on it.

**Measured, not assumed.** `check_tokens.py` simulates protanopia,
deuteranopia and tritanopia and no colour at all (Machado, Oliveira and
Fernandes, 2009) and measures how far apart each pair of the six lands.
The table is in `system.md`. With the app's colours as they are today,
orange and green are 5.4 apart under protanopia and red and green 6.5
under deuteranopia, where about 10 is the least that can be relied on.

The treatment has three layers. Each works without the others.

1. **Retuned swatches.** The closest pair under any dichromacy is now 11.7.
2. **A mark on every colour**, on by default: ring, slash, plus, dot, bar,
   and plain for white. Shown on the drawn cube, the scan chips, the
   colour picker, the review grid and every in-line swatch. With no colour
   at all, the six are still six.
3. **The word.** Wherever one colour is meant, it is named: "Yellow centre
   on top", "Too many: red (10)".

What the app cannot fix is the cube in the person's hand. So no
instruction depends on recognising a colour on the real cube, with one
exception: the hold. "Yellow centre on top, green centre facing you" asks
for two colours. It is backed by the picture of *their* cube as scanned:
the pattern of light and dark on the top and front can be matched even
when the hues cannot. E6 should test this step with someone who has a
red-green deficiency.

Chrome states that used to be colour alone, and what they are now:

| where | was | is |
|---|---|---|
| Dots on the side in view | yellow, then green | A hollow ring, then a filled disc; and the headline changes |
| Chip for a side in view again | a lighter border | A 3 px border and a pointer above the chip |
| Chips when the scan completes | green borders | A 3 px border and a check in each corner; and "Cube read" |
| Conflict and camera trouble | orange text | A triangle icon, the heading, and a button that fixes it |
| Stage done, current, to come | opacity and a red box | A check; a filled box and a tinted row; an outlined box |
| A switch | - | Thumb position and a check |
| A chosen option | red fill | Inverted fill and a check |

## Screen readers

Narrator and NVDA on Windows, VoiceOver on macOS and iOS, TalkBack on
Android, Orca on Linux. All reach Qt Quick through `Accessible`.

Rules for every screen:

- **Entering a screen** moves screen-reader focus to its title, which is a
  heading. The title is what is announced.
- **Order** follows reading order. In a two-pane layout, the pane with the
  instruction comes before the pane with the picture.
- **A change the person did not cause** is announced through a live region:
  `polite` for progress, `assertive` only for something that stops them.
  Qt 6.8 adds `Accessible.announce`; E2a confirms it reaches each
  platform's reader and wraps it in one kit function.
- **Decorative things are ignored** (`Accessible.ignored`): rules, the
  home cube, the underline on "Solved".
- **Marks are not read as shapes.** A sticker's name is its colour.

### Things that are opaque, and what stands in for them

| thing | why it is opaque | text alternative |
|---|---|---|
| The 3D cube on the solve screen | One GPU surface | The instruction block is the alternative: it carries the same turn in words. The cube item's own name is "The cube as it should look now", with the turn sentence as its description. A **Describe the cube** action reads the nine colours of the top, front and right sides, row by row. Needs a list of stickers from the solve lane, which `SolveController.stickers` already gives |
| The camera picture | Live video | Name: "Camera picture". Everything the picture tells a sighted person is also in the status panel, which is live |
| The dots on the stickers in view | Drawn on the picture | Nothing of their own. "A side is in view" is announced once; "Reading this side" follows from the status panel |
| A side chip | Nine coloured squares | "Side 3: read. Red side." Activating it reads the nine colours row by row |
| A canvas-drawn icon | A painted item | Icons become `Shape` items and are ignored; the control they sit in carries the name |
| A direction arrow | An icon | Ignored; the direction is in the words beside it |

### What is announced, screen by screen

| screen | on entry | live announcements |
|---|---|---|
| home | "Cubeulator. Scan. Solve. In your hands." then the lede | "No camera found" when that notice appears |
| first-run | The headline | - |
| permission | "Camera access" | After the system prompt: the new title, "Camera access is off", if refused |
| scan | The status headline, then the guidance | Each change of headline or guidance, politely, with repeats within two seconds dropped. "A side is in view." "Side 3 of 6 read." "Cube read." A camera failure is assertive |
| review | The title, then the status line | The status line when validity changes: "This is a valid cube", "Too many: red (10)". Not on every sticker |
| solve | The hold headline and body; then, on the first turn, the turn sentence | Each new turn: "Turn the top a quarter turn to the left." Then, only if asked for (a second action on the instruction), "Turn 12 of 62. White corners, stage 2 of 5." Stage changes: "White cross done. Check your cube matches the picture. Next: White corners." In timer mode the turn is announced as it appears |
| complete | "Solved. 62 turns. 12 minutes 40 seconds." | - |
| solution | The title and summary | The turn sentence when the selected turn changes |
| history | The title, then how many entries | "Deleted." |
| settings | The title | The new value when a setting is changed by a key |
| about, licences, privacy, help | The title | - |

Durations are spoken as durations ("12 minutes 40 seconds"), not as
"twelve forty". Notation, when on, is spelt out letter by letter and
"prime" is said for the apostrophe; it follows the plain sentence, never
replaces it.

### The solve screen without sight

It is possible to follow the solve by ear: hold as told, then one sentence
a turn, advancing with Space or a double tap. Two things make it workable
and both are proposals:

- **Read turns aloud**, using the platform's own speech, so that a screen
  reader is not needed just for this and a sighted person with their eyes
  on the cube gets it too. **Out of the first release** *(Iain,
  2026-10-13)*. Until it exists, following by ear needs the platform's
  screen reader, which reads the same sentences.
- **Describe the cube**, for checking by touch-free comparison at a stage
  change. A person who cannot see the cube's colours cannot make that
  comparison at all; for them the stage check is skipped by one key.

The scan cannot be done without sight today: nothing says which way to
tilt the cube. That is out of scope for the first release and is said
here so that it is not claimed.

## Keyboard

Every screen works with a keyboard and nothing else. This is also how a
switch, a page-turner pedal or a presenter's clicker drives the app.

### Everywhere

| key | does |
|---|---|
| Tab, Shift+Tab | Next, previous control. Order is reading order |
| Space, Enter | Activate the focused control |
| Arrow keys | Move within a group: a choice, the colour picker, a list, the review grid |
| Esc | Back. In a dialog or sheet: close it, choosing the safe option |
| Alt+Left (Windows, Linux), Cmd+[ (macOS) | Back |
| F1 (Windows, Linux), Cmd+? (macOS) | Help |
| Ctrl+, / Cmd+, | Settings |
| Ctrl+N / Cmd+N | Scan a cube, from Home |
| F11 / Ctrl+Cmd+F | Full screen |
| Ctrl+Q / Cmd+Q | Quit |

In right-to-left layouts the arrow keys that mean "forward" and "back"
swap, as the layout does. Arrow keys that move across the review grid do
not: the grid is the cube.

### Scan

| key | does |
|---|---|
| Esc | Cancel (asks first if any side is read) |
| R | Start over (asks first) |
| M | More |
| H | Enter the rest by hand |

### Review and hand entry

| key | does |
|---|---|
| 1-6 | Choose the colour to paint with, in the order shown |
| Arrow keys | Move the cursor across the stickers of the side |
| Space, Enter | Paint the sticker under the cursor |
| Backspace, Delete | Unset it |
| Page Down, Page Up | Next, previous side |
| Ctrl+Enter / Cmd+Enter | Solve, when the cube is valid |

### Solve

Focus does not need to be on anything. These work from anywhere on the
screen, so that a knuckle on the space bar is enough.

| key | does |
|---|---|
| Space, Enter, Page Down, forward arrow | Next turn. On the hold step: start. At a stage change: carry on. In timer mode: pause, or carry on |
| Page Up, back arrow | Back one |
| Esc | Leave. The solve is kept |
| P | Pause: opens the menu |
| H | How to hold the cube |

Space and Enter act on a focused button instead when focus is on one. The
main button shows `Space` as a reminder on desktop.

### Solution

| key | does |
|---|---|
| Forward arrow, back arrow | Next, previous turn |
| Space | Play, pause |
| Home, End | First turn, solved |

## Text size

- The type scale is in rem. One rem is 16 px times `Theme.textScale`.
- `textScale` is the operating system's text size times the app's own
  setting (1, 1.25 or 1.5), clamped to 0.85-2.0. How each platform's
  setting is read is in `platforms.md`.
- Controls grow with their labels. Nothing has a fixed height.
- At large scales the solve screen gives up, in order: the stage
  explanation, the hold reminder (it stays in the menu), then cube size
  down to 140 px. The instruction and the two buttons are never dropped
  or shrunk.
- At 2.0 on a compact screen some screens scroll. None scrolls sideways.

## Reduced motion

Follows the operating system, with an override in Settings. The full
table of what changes is in `system.md` under Motion. In short: nothing
moves except by the person's own action, and a turn snaps instead of
sweeping. The direction of a turn is then carried by the still lean of
the layer, the arrow icon and the words.

## Right to left

Hebrew and Arabic ship in the first release, so this is tested, not
planned.

**Mirrors:** the order of things across the screen; leading and trailing
margins; which side the back control, the chevrons and the primary action
are on; progress bars and the countdown; the order of the six scan chips
(a sequence); the order of the solve screen's two buttons, and with them
which arrow key means "next".

**Never mirrors:**

- the 3D cube and anything it does;
- the camera picture;
- the nine stickers inside a chip, a side in the review screen, or the
  unfolded cube;
- direction arrows, and the words *left*, *right*, *clockwise*,
  *anticlockwise*: they describe the object in the hand;
- notation, key names, version numbers and times: always left to right;
- the six swatches of the colour picker keep their order and their 1-6
  keys.

The kit enforces the second list: the cube view, the chip and the
direction icon set `LayoutMirroring.enabled: false` on themselves and
their children. A screen cannot mirror them by accident.

Numbers are formatted by the locale. Arabic may show Arabic-Indic digits;
notation never does.

## What is not known

- Whether `Accessible` on custom Qt Quick items is read well by TalkBack
  and VoiceOver on phones. Desktop is the safer ground. E2a builds the
  button first and tries it on all five before building the rest.
- Whether `Accessible.announce` exists and behaves the same on all five
  in Qt 6.8.
- Whether the hold step works for someone who cannot tell green from
  orange on their own cube. E6.
- Whether the marks help or distract people with ordinary colour vision.
  They are a switch, so the default can change after E6.
