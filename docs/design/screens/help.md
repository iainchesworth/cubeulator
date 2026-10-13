# Help

Lane: experience. New (E3a). In the first release by Iain's decision.

Short answers to the things that go wrong, written to be read with a cube
in the other hand. Each topic is one screen of plain text; none needs the
internet.

Entered from: **Help** on Home and in Settings; F1; the macOS Help menu;
and straight to one topic from the places that need it:

| from | opens |
|---|---|
| Scan: the More sheet, and the hint | `help.scanning` |
| Review: the Help button | `help.entering` |
| Solve: the menu, **How to hold the cube** | `help.holding` |

Back from a topic opened that way returns to the scan, the review or the
solve as it was, not to the list of topics.

## Layout

One scrolling column at every size, 24 margin, centred from medium up. A
top bar: back and the title, which is "Help" on the list and the topic's
name on a topic.

**The list**: list rows, 56 high, at most 640 wide.

**A topic**: paragraphs of `body`, at most 34 rem wide, 12 apart. The
holding topic has a picture of the cube above the text.

## Parts

| part | component | string | objectName |
|---|---|---|---|
| Back | icon button | `common.back` | `help.back` |
| Title | top bar title, heading level 1 | `help.title`, or the topic's name | `help.title` |
| Topic row | list row | `help.howItWorks`, `help.scanning`, `help.entering`, `help.holding`, `help.turns`, `help.lost`, `help.cubes`, `help.notation`, `help.keys` | `help.topic.howItWorks` ... `help.topic.keys` |
| Topic text | paragraphs | `help.scanning.body`, `help.entering.body`, `help.holding.body`, `help.turns.body`, `help.lost.body`, `help.cubes.body`, `help.notation.body`, `help.keys.body` | `help.text` |
| Cube picture (holding) | The solve lane's cube view, solved, as held: yellow on top, green in front. Role: graphic; its name is `solve.hold.body` | `solve.hold.body` | `help.cube` |
| Hold reminder (holding) | Two swatches and a line, as on the solve screen | `solve.holdLine` | `help.holdLine` |

Help text uses exactly the words of the glossary (`../content.md`) and of
the strings it describes. When a button is renamed, the topic that names
it is edited in the same change.

## States

### `help.topics`

The list, in this order: How it works; Scanning the cube; Entering colours
by hand; Holding the cube; What the words mean; If the cube does not
match; Which cubes work; Cube notation; Keyboard.

- **How it works**: to `first-run.revisit`.
- **Keyboard** is listed only where there is a keyboard: on desktop, and
  on a phone or tablet with one attached.
- Each other row: to its topic.
- Back: to where Help was opened from.

### `help.scanning`

`help.scanning.body`: how to hold a side to the camera; that order and
which way up do not matter; light and glare; and that entering by hand is
there if a side will not read.

### `help.entering`

`help.entering.body`: choose a colour, tap its stickers; one side at a
time; the centre is fixed; any way up; what happens when the colours do
not make a cube. The "any way up" paragraph stands only if the scan lane
confirms it (`review.md`).

### `help.holding`

The cube picture and the hold reminder, then `help.holding.body`: yellow
centre on top, green centre in front, for the whole solve; look at the
centres; do not turn the whole cube round; prop the device up.

### `help.turns`

`help.turns.body`: what a side and a turn are; the six directions and why
they are given from where the person is; follow the picture if in doubt.

### `help.lost`

`help.lost.body`: the three things to try, in the same order as the
solve screen's sheet, ending with scanning again.

### `help.cubes`

`help.cubes.body`: which cubes are read, which are not yet, and that the
ones that are not can still be entered by hand if their six colours can
be matched. It says "not yet" only of things the recovery plan names as
future work.

### `help.notation`

`help.notation.body`: the six letters, the apostrophe and the 2, and why
notation's "clockwise" can disagree with the app's own words for the
bottom and the back. The letters are not translated.

### `help.keys`

`help.keys.body`: the keys for the solve screen, then the keys that work
everywhere. Key names stay as printed on keyboards. Desktop only.
