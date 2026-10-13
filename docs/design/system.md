# Design system

The existing look made into rules. Machine-readable values are in
[`tokens.json`](tokens.json); this page says what they are for. E2a ports
them into `Theme.qml` and builds the components listed here. After that, a
colour or a pixel size written into a screen is a defect (plan, E3 gate).

## The direction

Kept from the app as it stands *(Iain, 2026-10-13)*. The original design
handoff is gone, so this is reconstructed from the QML.

- **Flat.** Fills and rules. No shadows, gradients, blurs or glows.
- **Square.** Every corner radius is zero: buttons, panels, chips, sheets,
  stickers. Circles appear only where a circle is the meaning: the dots on a
  side being scanned, the ring mark on a yellow sticker.
- **Ruled like a drawing.** Regions are separated by a 2 px rule, not by
  cards. The one boxed object on a screen is the thing that needs deciding.
- **One accent.** Red marks the single most important action on a screen
  and the progress of the solve. It is never used for an error on its own.
- **The cube is the colour.** The chrome is near-black on near-white, or
  the reverse. The six cube colours are the only other colours on screen,
  so they read as the subject.
- **Type does the work.** Archivo ExtraBold for what to do, Archivo Regular
  for why, IBM Plex Mono for numbers and notation.

Everything below can be built from Qt Quick `Rectangle`, `Text`, `Shape`
and `Item` with property animations. Nothing needs a shader, an effect
layer or a blur.

## Colour

Semantic tokens with a light and a dark value. Screens use the token name,
never the value.

| token | light | dark | use |
|---|---|---|---|
| `paper` | `#F3F2F2` | `#1A1918` | Window background |
| `surface` | `#EAE9E9` | `#242220` | A second pane; a notice |
| `ink` | `#201E1D` | `#F3F2F2` | Text, icons, heavy rules |
| `inkSecondary` | `#5E5B5A` | `#B0ACAB` | Secondary text. Replaces `Theme.inkMuted(opacity)` |
| `inkDisabled` | `#858281` | `#7A7776` | Disabled labels |
| `outline` | `#858281` | `#7A7776` | Edges of controls |
| `divider` | `#C9C7C6` | `#3A3836` | Rules between regions. Decorative |
| `accent` | `#EC3013` | `#EC3013` | The brand red. Shapes only: progress fill, the underline on "Solved", the icon. **Never text** |
| `accentText` | `#B82009` | `#FF7359` | Red text and icons: eyebrows, ghost buttons, links |
| `accentFill` | `#B82009` | `#FF5A3D` | Primary button |
| `accentFillHover` | `#A51903` | `#FF6B50` | Primary button, hovered |
| `accentFillPress` | `#9E1702` | `#FF7D65` | Primary button, pressed |
| `onAccent` | `#FFFFFF` | `#1A1918` | Label on a primary button |
| `accentTint` | `#FFF2EF` | `#3A1A14` | The current row; a hint |
| `ok` | `#286F6D` | `#5ABDBA` | Positive. Teal, so it is never mistaken for the cube's green |
| `caution` | `#8A5800` | `#E0A030` | Caution |
| `hoverWash` | `#E4E2E2` | `#2E2C2A` | Hover fill of secondary and ghost buttons and rows |
| `pressWash` | `#DCDAD9` | `#343230` | Pressed fill of the same |
| `cubeBody` | `#1B1A1A` | `#0E0D0D` | Plastic between stickers |
| `cubeEdge` | `#201E1D` | `#8C8988` | Outline of a 2D sticker against the page |

Why the reds changed: the existing `#EC3013` measures 3.76:1 on `paper` and
under its own button label, and `inkMuted(0.55)` measures 3.66:1. Both miss
the 4.5:1 gate. The brand red is kept for shapes, where 3:1 is the rule and
it passes.

There is no error colour. An error is a `caution` icon, a plain heading
that says what happened, and a button that fixes it.

**State is never colour alone.** Every state below also changes a shape,
an icon, a border weight or a word.

### Over the camera

The scan screen draws on a live picture, which no theme controls. Its
panels are the same in both themes: `#141312` at 86% opacity, with
`#F3F2F2` text. Contrast is measured against the worst case, a pure white
picture behind the panel.

### Cube colours

| colour | value | mark | why this mark |
|---|---|---|---|
| white | `#F2F0EC` | none | The lightest; plain is its mark |
| yellow | `#F6CA3F` | ring | A hollow circle |
| orange | `#F87E0F` | slash | One diagonal line |
| red | `#CD3B37` | plus | Two crossed lines |
| green | `#4EB584` | dot | A solid circle |
| blue | `#0D5792` | bar | One straight line |

- The swatches are retuned from the app's current six so that the closest
  pair under protanopia is 11.7 apart instead of 5.4 (table below). They
  still cannot be told apart with no colour at all, which is why the mark
  exists.
- Yellow, orange, red and green are the four that colour-vision deficiency
  brings together. They carry the four most different marks.
- No mark has a direction. A sticker is seen at any rotation, and a
  triangle pointing sideways reads as "play".
- A mark is drawn in `#201E1D` on white, yellow, orange and green and in
  `#FFFFFF` on red and blue, at 3:1 or better against its sticker.
- Marks are **on by default** and can be switched off (Settings >
  Appearance > Marks on cube colours).
- A mark occupies the middle 40% of its sticker. Below 12 px a sticker is
  too small to carry one and must not be the only way a colour is shown.
- **The camera picture never carries marks.** It shows the real cube. The
  chips beneath it do.
- Wherever one colour is *meant* ("yellow on top"), the word is there too.
- Each mark has a name (`mark.*`: ring, slash, plus, dot, bar, no mark).
  The names appear in one place, the legend under the switch in Settings,
  where they tell a screen-reader user which shape goes with which colour.
- These are tidy swatches for drawn cubes. The scan chips show the colours
  the camera measured; the mark on each chip sticker says what the reader
  took it for.

### Measured

Generated by `python docs/design/tools/check_tokens.py --write`. The script
exits non-zero if any pairing misses its target, so the table cannot say
something the tokens do not.

<!-- measured:begin -->
### Text and control pairings

| foreground | background | use | target | light | dark |
|---|---|---|---|---|---|
| `ink` | `paper` | Body text | 4.5:1 | 14.86 | 15.71 |
| `ink` | `surface` | Body text on a pane | 4.5:1 | 13.70 | 14.19 |
| `ink` | `accentTint` | Text in the current row | 4.5:1 | 15.18 | 14.05 |
| `ink` | `hoverWash` | Label on a hovered secondary button or row | 4.5:1 | 12.87 | 12.45 |
| `ink` | `pressWash` | Label on a pressed secondary button or row | 4.5:1 | 11.92 | 11.43 |
| `inkSecondary` | `paper` | Secondary text | 4.5:1 | 6.02 | 7.80 |
| `inkSecondary` | `surface` | Secondary text on a pane | 4.5:1 | 5.55 | 7.05 |
| `inkSecondary` | `accentTint` | Secondary text in the current row | 4.5:1 | 6.15 | 6.98 |
| `inkSecondary` | `hoverWash` | Secondary text in a hovered row | 4.5:1 | 5.22 | 6.18 |
| `accentText` | `paper` | Eyebrow, link, ghost button label | 4.5:1 | 5.79 | 6.56 |
| `accentText` | `surface` | Eyebrow, link on a pane | 4.5:1 | 5.33 | 5.92 |
| `accentText` | `accentTint` | Accent text in the current row | 4.5:1 | 5.91 | 5.87 |
| `accentText` | `hoverWash` | Ghost button label, hovered | 4.5:1 | 5.01 | 5.20 |
| `accentText` | `pressWash` | Ghost button label, pressed | 4.5:1 | 4.64 | 4.77 |
| `onAccent` | `accentFill` | Primary button label | 4.5:1 | 6.46 | 5.67 |
| `onAccent` | `accentFillHover` | Primary button label, hovered | 4.5:1 | 7.66 | 6.25 |
| `onAccent` | `accentFillPress` | Primary button label, pressed | 4.5:1 | 8.15 | 7.00 |
| `ok` | `paper` | Positive text and icon | 4.5:1 | 5.24 | 7.88 |
| `ok` | `surface` | Positive text and icon on a pane | 4.5:1 | 4.83 | 7.11 |
| `caution` | `paper` | Caution text and icon | 4.5:1 | 5.41 | 7.73 |
| `caution` | `surface` | Caution text and icon on a pane | 4.5:1 | 4.99 | 6.98 |
| `accentFill` | `paper` | Primary button against the page | 3.0:1 | 5.79 | 5.67 |
| `accentFill` | `surface` | Primary button against a pane | 3.0:1 | 5.33 | 5.12 |
| `accent` | `paper` | Brand graphics, progress fill, turn arrow | 3.0:1 | 3.76 | 4.18 |
| `accent` | `surface` | Brand graphics on a pane | 3.0:1 | 3.47 | 3.77 |
| `outline` | `paper` | Control boundary | 3.0:1 | 3.41 | 3.95 |
| `outline` | `surface` | Control boundary on a pane | 3.0:1 | 3.15 | 3.57 |
| `outline` | `accentTint` | Control boundary in the current row | 3.0:1 | 3.49 | 3.53 |
| `inkDisabled` | `paper` | Disabled label (exempt; held at 3:1 anyway) | 3.0:1 | 3.41 | 3.95 |
| `ink` | `paper` | Focus ring outer band against the page | 3.0:1 | 14.86 | 15.71 |
| `cubeEdge` | `paper` | Sticker outline against the page | 3.0:1 | 14.86 | 5.06 |
| `cubeEdge` | `surface` | Sticker outline against a pane | 3.0:1 | 13.70 | 4.57 |

### Over the camera

Panel `#141312` at 0.86 opacity. Measured against the worst case, a pure white picture behind it, where the panel reads as `#353433`.

| foreground | use | target | ratio |
|---|---|---|---|
| `overlay.ink` | Text on a panel | 4.5:1 | 11.12 |
| `overlay.inkSecondary` | Guidance text on a panel | 4.5:1 | 8.59 |
| `overlay.ok` | Counting / confirmed, with a filled shape | 4.5:1 | 7.35 |
| `overlay.caution` | Conflict or camera trouble, with a triangle icon | 4.5:1 | 6.98 |
| `overlay.outline` | Empty chip border; secondary button border on a panel | 3.0:1 | 4.39 |

### Cube colours: mark against sticker, sticker against plastic

| colour | value | mark | mark ink on sticker (3:1) | sticker on `cubeBody` |
|---|---|---|---|---|
| white | `#F2F0EC` | none | - | 15.26 |
| yellow | `#F6CA3F` | ring | 10.62 | 11.11 |
| orange | `#F87E0F` | slash | 6.31 | 6.60 |
| red | `#CD3B37` | plus | 4.91 | 3.54 |
| green | `#4EB584` | dot | 6.54 | 6.84 |
| blue | `#0D5792` | bar | 7.51 | 2.31 |

### Cube colours: how far apart each pair is

OKLab distance x 100 between two swatches as seen with each kind of colour vision. Under 10 is not reliably tellable from the swatch alone; the mark has to carry it.

| pair | normal | protanopia | deuteranopia | tritanopia | no colour |
|---|---|---|---|---|---|
| white / yellow | 18.1 | 20.2 | 17.6 | 13.4 | 10.2 |
| white / orange | 29.4 | 33.4 | 26.5 | 31.7 | 25.1 |
| white / red | 42.7 | 48.2 | 38.3 | 44.2 | 40.8 |
| white / green | 28.3 | 22.8 | 25.9 | 28.5 | 24.2 |
| white / blue | 52.3 | 49.3 | 53.6 | 50.0 | 50.8 |
| yellow / orange | 17.4 | 18.5 | 13.1 | 18.4 | 14.9 |
| yellow / red | 33.8 | 36.9 | 28.7 | 31.0 | 30.6 |
| yellow / green | 22.4 | 14.5 | 20.7 | 24.6 | 14.0 |
| yellow / blue | 48.8 | 44.1 | 50.8 | 41.8 | 40.6 |
| orange / red | 17.2 | 18.7 | 16.1 | 13.0 | 15.7 |
| orange / green | 24.5 | 11.7 | 12.4 | 30.4 | **0.9** |
| orange / blue | 40.0 | 28.9 | 40.0 | 36.0 | 25.6 |
| red / green | 31.0 | 25.7 | 13.1 | 36.5 | 16.6 |
| red / blue | 30.6 | 15.8 | 25.8 | 33.0 | **9.9** |
| green / blue | 30.2 | 30.2 | 30.4 | 23.9 | 26.6 |
| closest pair | 17.2 (orange/red) | 11.7 (orange/green) | 12.4 (orange/green) | 13.0 (orange/red) | 0.9 (orange/green) |
<!-- measured:end -->

## Type

| family | used for | bundled |
|---|---|---|
| **Archivo** (OFL) | Everything in Latin script | Regular 400, Bold 700, ExtraBold 800 |
| **IBM Plex Mono** (OFL) | Numbers that change (turn count, time), notation, key names | Medium 500 |
| **Noto Sans Hebrew**, **Noto Sans Arabic** (OFL) | Hebrew, Yiddish and Arabic | One variable file each, weight 100 to 900, so headings keep 800. The same files ICL Forge, Countdown Solver and Automate ship (`family.md` D3) |
| The system's own | Japanese, Simplified Chinese | Not bundled; see `platforms.md` |

The fonts are loaded from the app's resources at start-up. A machine
without Archivo installed still shows Archivo (plan, E2 gate).

Sizes are in **rem**. One rem is `16 px x Theme.textScale`.
`textScale` is the operating system's text-size setting multiplied by the
app's own (Settings > Appearance > Text size: 1, 1.25, 1.5), clamped to
0.85-2.0. Every layout must hold at 2.0 (WCAG 1.4.4).

| role | rem | px at 1.0 | weight | line | use |
|---|---|---|---|---|---|
| `display` | 2.75 (2.25 compact) | 44 / 36 | 800 | 1.0 | The home headline; "Solved" |
| `instruction` | 2.0 | 32 | 800 | 1.1 | The side to turn. Read from arm's length |
| `title` | 1.75 | 28 | 800 | 1.1 | Screen titles |
| `direction` | 1.5 | 24 | 700 | 1.15 | Which way to turn |
| `headline` | 1.375 | 22 | 700 | 1.2 | Scan status; dialog and sheet titles |
| `label` | 1.0 (1.25 on a giant button) | 16 / 20 | 800 | 1.2 | Buttons |
| `body` | 1.0 | 16 | 400 | 1.45 | Running text |
| `small` | 0.875 | 14 | 400 | 1.45 | Notes under a setting; the hold reminder |
| `eyebrow` | 0.8125 | 13 | 700 | 1.2 | Small capitals with 0.1 em tracking, in `accentText` |
| `numeral` | 1.0 | 16 | 500 mono | 1.2 | Turn count, time |
| `numeralLarge` | 2.0 | 32 | 500 mono | 1.1 | The figures on the finishing screen |

Rules:

- **Nothing is smaller than 13 px at scale 1.0.** Body text is 16 px.
- Eyebrows are the only capitals. They are set by a font capitalisation
  property, never typed in capitals, so that translators see normal text.
  Scripts without capitals (Hebrew, Yiddish, Arabic, Japanese, Chinese)
  drop the tracking and use weight 700.
- Running text is capped at 34 rem wide (about 65 characters).
- Numbers that change in place use tabular figures (the mono face).
- Text wraps. It is never elided on a control. Truncation in the
  pseudo-locale is a failed test.

## Space, rules, corners

| | values |
|---|---|
| Spacing | 4, 8, 12, 16, 24, 32, 48, 64. Nothing else |
| Rules | 1 px hairline inside a component; 2 px between regions and round a control; 3 px for the one emphasised thing |
| Corner radius | 0 |
| Page margin | 24 compact (16 under 360 wide), 32 medium, 40 expanded |

Horizontal padding and margins are written as *leading* and *trailing*,
never left and right, so that they mirror (see `accessibility.md`).

## Size classes

From the window's size in device-independent pixels. Replaces the single
720 breakpoint.

| class | window | typically | layout |
|---|---|---|---|
| **compact** | width under 600 | A phone held upright; a narrow desktop window | One column. Primary action in the bottom thumb zone, full width |
| **medium** | width 600-839 | A small tablet; a phone with a hinge | The compact layout with a 560-wide column, centred |
| **expanded** | width 840 or more | A desktop window; a tablet on its side | Two panes: what to do, and the cube |
| **short** | height under 480, at any width | A phone on its side | Two columns, cube leading, controls trailing. Takes priority over the width class |

Rules:

- A class change keeps state, focus and scroll position.
- Layouts are built from anchors and layout items that fill what is left.
  No size is found by subtracting a constant from the window height.
- Smallest supported window: 320 x 480. Below that the screen scrolls.
- When content is taller than the window, the screen scrolls as a whole,
  except the solve and scan screens, which shrink the cube and the camera
  picture first and scroll only as a last resort.
- Safe-area insets are added to the page margins on all four sides
  (`platforms.md`).

## Target sizes

| token | size | where |
|---|---|---|
| `target.min` | 48 x 48 | Every interactive item, on every platform. WCAG 2.2 asks for 24; Android asks for 48 and iOS for 44 |
| `target.thumb` | 56 high | Controls on the scan screen and in sheets and dialogs: used one-handed |
| `target.handsFull` | 96 high, at least a third of the width | The two controls on the solve screen. Also, the whole cube picture is a target |

Targets that touch share an edge; they never overlap. A target smaller than
its visual is a defect. A visual smaller than its target (a 26 px icon in a
56 px button) is normal.

## Motion

| token | full | reduced | use |
|---|---|---|---|
| `quick` | 120 ms | 0 | Hover and press feedback; a chip arriving |
| `base` | 200 ms | 0 | A sheet or dialog opening; a panel changing height |
| `screen` | 280 ms | 120 ms | Changing screen. Full: fade in. Reduced: a shorter fade, opacity only |
| `turn` | 380 ms | 0 | One layer turn on the cube (solve lane) |

Easing is `OutCubic` for things arriving and `InOutQuad` for the cube.

**Reduced motion** is on when the operating system asks for it, or when
Settings > Appearance > Motion is set to Reduced. Then:

| full | reduced |
|---|---|
| The home cube turns slowly | It is still |
| The layer for the next turn leans back and forth | It is drawn leaning, still |
| A turn animates | It snaps |
| The working indicator pulses | Three still squares and the words |
| A chip pops in; six chips ripple when the scan completes | They appear; the ticks appear |
| The underline sweeps under "Solved" | It is drawn |
| The finishing cube bobs | Removed for everyone. It adds nothing |

No animation loops for more than five seconds without the person being
able to stop it (WCAG 2.2.2), except the lean of the next layer, which is
the instruction itself and stops with reduced motion.

## Icons

Line icons on a 24 x 24 grid, 2 px stroke, round caps, no fill; the set the
app already draws is Lucide (ISC licence). E2a redraws them as `Shape`
paths instead of `Canvas`, so they scale cleanly and take a colour token.

| kind | icons | in right-to-left |
|---|---|---|
| Navigation | back and forward chevrons, the chevron on a row | **Mirror** |
| Turn direction | left, right, up, down, clockwise, anticlockwise, either | **Never mirror.** They describe the cube |
| Objects | camera, clock, sliders, grid, pause, refresh, check, cross, alert | Do not mirror |
| Help | a question mark in a circle | Mirrors in Arabic only, where the question mark itself is mirrored |

An icon never stands alone on a control, except back, pause and close,
each of which has an accessible name and a tooltip on desktop.

## Components

Each is one QML type in the kit, with an `objectName`, an accessible role
and name, keyboard focus and the states listed. "Focused" means keyboard
focus; it is shown the same way everywhere:

> **Focus ring.** 2 px `ink`, 2 px outside the control, with the page
> showing between. Over the camera the ring is `overlay.ink`. It appears
> for keyboard focus only, never for a tap or click.

### Button

A label, with an optional leading icon, trailing chevron or key name.

| variant | rest | hover | pressed | disabled |
|---|---|---|---|---|
| **primary** | `accentFill`, label `onAccent` | `accentFillHover` | `accentFillPress` | fill `surface`, 2 px `divider` edge, label `inkDisabled` |
| **secondary** | transparent, 2 px `outline` edge, label `ink` | fill `hoverWash` | fill `pressWash` | edge `divider`, label `inkDisabled` |
| **ghost** | transparent, label `accentText` | fill `hoverWash` | fill `pressWash` | label `inkDisabled` |

| size | height | label | where |
|---|---|---|---|
| regular | 48 | 1 rem | Everywhere by default |
| thumb | 56 | 1 rem | Scan screen, sheets, dialogs, the main action of a screen |
| giant | 96 | 1.25 rem | Solve screen only |

- One primary button a screen. A dialog's safe choice is secondary and
  first; its other choice is ghost.
- `block`: fills the width; label at the leading edge, chevron at the
  trailing edge.
- Heights are minimums. A label that wraps makes the button taller.
- A disabled button always has the reason beside it in words. If nothing
  the person can do would enable it, it is not shown.
- Activates on click, tap, Space and Enter.

### Icon button

A 26 px icon in a 56 x 56 target. States as ghost. Has an accessible name;
on desktop, a tooltip with that name after 600 ms of hover or on focus.
Used for back, pause and close only.

### Switch

For a setting that is on or off. A row: label and note at the leading side,
the switch at the trailing side. The whole row is the target, 56 high.

| state | track (52 x 32) | thumb (24 x 24 square) |
|---|---|---|
| off | transparent, 2 px `outline` | `outline`, at the leading end |
| on | `accentFill` | `onAccent`, at the trailing end, with a check drawn in `accentFill` |
| hover | row fill `hoverWash` | - |
| pressed | row fill `pressWash` | - |
| disabled | 2 px `divider` | `divider`; label `inkDisabled`; the reason in the note |

On and off differ by position and by the check, not by colour. A screen
reader reads the state as `common.on` or `common.off`.

### Choice

Two to four options, one chosen. A row of joined secondary buttons; wraps
to a column when the labels do not fit.

| state | look |
|---|---|
| unchosen | secondary button |
| chosen | fill `ink`, label `paper`, leading check |
| hover, pressed, disabled | as secondary |

Arrow keys move the choice; Tab enters and leaves the group once.

### List row

A row that goes somewhere or shows a value: label, optional value in
`inkSecondary`, trailing chevron. 56 high, full width, hairline beneath.
States as ghost with an `ink` label. Used in Settings, Help, History,
Licences and sheets.

### Top bar

56 high. Back (icon button) at the leading edge, then the screen title in
`label` type; an optional action at the trailing edge. A 2 px `divider`
rule beneath. On expanded screens with two panes the bar spans both.

### Dialog

A question that must be answered before going on. Centred, at most 400
wide, 2 px `ink` edge, on a scrim of `#141312` at 60%.

- Title in `headline`, one sentence of body, two buttons.
- The safe choice is first, secondary, focused when the dialog opens, and
  what Esc and back choose.
- Focus stays inside until it closes, then returns to where it was.

### Sheet

A short list of actions that does not need answering. Slides up from the
bottom edge on compact; a centred panel at most 440 wide from medium up.
Same edge and scrim as a dialog. A tap on the scrim, Esc or back closes it.
Rows are block buttons, 56 high. The last row is **Close**.

### Notice

Something the person should know, in place. `surface` fill, 2 px `outline`
edge, a leading icon, a bold first line, a sentence, optional buttons.

| kind | icon | icon colour |
|---|---|---|
| information | circle with an "i" | `ink` |
| caution | triangle | `caution` |
| positive | check | `ok` |

Announced by screen readers when it appears (`accessibility.md`).

### Card

The one object on a screen that needs deciding: the unfinished solve on
Home. 2 px `ink` edge, no fill, content and up to two buttons.

### Progress

| part | look | states |
|---|---|---|
| **Solve progress** | 6 px bar, `divider` track, `accent` fill from the leading edge, a 2 px `ink` tick at each stage boundary | - |
| **Working** | Three 12 px `accent` squares pulsing in turn, with a `headline` saying what is being worked out | reduced: still |
| **Countdown** (timer mode) | 8 px bar draining to the leading edge over the pace | paused: full and still |
| **Capture** (scan) | 6 px bar along the bottom of the next chip, filling as the side is held | - |

### Sticker, swatch, side chip

- **Sticker**: a square in a cube colour with its mark. Edge `cubeEdge`
  when it sits on the page, none when it sits on `cubeBody`.
- **Swatch**: a 1 em sticker with a 2 px edge, in line with text that names
  a colour or a side.
- **Side chip**: nine stickers in a 3 x 3 grid with 2 px gaps on a
  `cubeBody` ground. The grid **never mirrors**.

| chip state | look |
|---|---|
| empty | 1 px `overlay.outline` edge |
| filling | empty, with the capture bar |
| read | the nine stickers |
| in view again | 3 px `overlay.ink` edge and a pointer above it |
| confirmed (scan complete) | 3 px `overlay.ok` edge and a check in the corner |

### Colour picker

Six buttons, each a 56 px sticker with its mark and the colour's name
beneath. One is chosen: 3 px `ink` edge and a check. Keys 1-6 choose, in
the order shown.

### Status panel

The scan screen's headline and guidance, on an overlay panel at the top
leading corner, at most 520 wide. `headline` then `body`. Caution and
complete states add a leading icon. It is a live region.

### Instruction block

The solve screen's instruction. Three lines:

1. a swatch of the side's centre colour and the side's name, in
   `instruction`;
2. a direction icon in `accentText` and the direction, in `direction`;
3. the amount in `body`, `inkSecondary`; then the notation in a key-name
   box if it is switched on.

Centred on expanded, at the leading edge on compact.

### Stage row

In the expanded solve screen's side panel. A 32 px numbered box, the
stage's name, and for the current stage its explanation.

| state | box | row |
|---|---|---|
| to come | 2 px `outline`, the number | - |
| current | `accentFill`, the number in `onAccent` | fill `accentTint`; explanation shown |
| done | 2 px `ok` edge, a check | - |

### Key name

A key's name (`Space`) in `numeral` type inside a 1 px box. Shown on the
solve screen's main button on desktop. Always left-to-right.

### Empty state

A `title`, one sentence, and the button that would fill the screen.

## What the prototype does that Qt will not

The prototype is a web page. These are review aids or conveniences in it,
not part of the design:

- The "See as" control (a colour-matrix filter over the whole app).
- Container queries: the kit derives `Theme.sizeClass` from the window.
- `text-wrap: balance` on headings: Qt wraps greedily. Accept it.
- The 40%-longer strings: E2b builds a real pseudo-locale.
