# Identity

A proposal for the app icon, the name treatment and the store screenshots.
E4a and E4b turn it into the per-platform files listed in
[`../platforms.md`](../platforms.md).

## The icon

The app's own drawing of a cube, caught with its top layer part-way
through a turn. Flat, square-cornered, ruled in ink on paper like the
screens. The turning layer is the one red thing, as the next action is the
one red thing on every screen.

| file | what | used for |
|---|---|---|
| [`icon.svg`](icon.svg) | The icon, light | Windows, Linux, the iOS and macOS default, the Android background-and-foreground pair flattened for stores |
| [`icon-dark.svg`](icon-dark.svg) | The same on the dark paper | iOS dark variant; anywhere the icon sits on a light surface and needs to stand off |
| [`icon-small.svg`](icon-small.svg) | No grid lines, heavier outline, slightly larger in its box | 16, 20, 24 and 32 px on Windows and Linux; the browser-tab size of anything |
| [`icon-mono.svg`](icon-mono.svg) | One colour, no background | Android themed icons; the iOS tinted variant; a notification or tray glyph if one is ever needed |
| [`icon-android-foreground.svg`](icon-android-foreground.svg) | The drawing at 72% on a transparent ground | The Android adaptive icon's foreground layer. Its background layer is flat `paper` |

All five are drawn by `tools/make_icon.py` from the colours in
`tokens.json` and the isometric projection `CubeView.qml` uses, so the
icon and the app cannot drift apart.

Why this and not something else:

- **It is the product's one idea.** A cube, and what to do to it next.
- **It survives 16 px.** At that size it is a dark block with a red slab
  askew on top. The grid lines are dropped, not shrunk.
- **It is two tones and one accent.** No six-colour cube: at icon size the
  six colours turn to noise, and a multicoloured 3x3 grid is what every
  other cube app uses.
- **It is not the Rubik's logo.** No brand name, no brand colours in the
  brand's arrangement. E6 should still have the icon and the name looked
  at before a store release; this is not legal advice.

Per platform:

- **macOS** wants the artwork on a rounded square with a margin. The
  rounded square is `paper`; the drawing sits inside at 80%.
- **iOS** masks the square itself. Supply the full-bleed square with no
  transparency.
- **Android** masks to a circle, a squircle or a rounded square. The
  foreground file keeps the drawing inside the safe middle.
- **Windows** shows the icon on the taskbar on any colour. The light icon
  has its own `paper` ground, so it always reads.

Alternatives considered and set aside:

| idea | why not |
|---|---|
| A flat 3x3 side with one red sticker and the five marks | Reads as a grid, a calendar or noughts and crosses. Good as a secondary pattern, not as the icon |
| The cube inside camera brackets | Two ideas in 16 pixels. The brackets turn to dust |
| A six-colour cube | See above |

## The name

**Cubeulator**: one word, capital C, never "CubeUlator" or "Cube-ulator".

| treatment | spec | where |
|---|---|---|
| **Wordmark** | Archivo ExtraBold, sentence case, tracking -0.01 em, in `ink` | The About screen; store graphics; the README |
| **Eyebrow** | Archivo Bold, capitals, tracking 0.1 em, 13 px, in `accentText` | The top of the Home screen. This is the only place the name appears in the running app |
| **Lock-up** | The icon, a gap of half its width, the wordmark with its x-height centred on the icon | Store feature graphics; the top of the About screen |

- The name is never translated or transliterated. In right-to-left text it
  stays left-to-right and is isolated so that punctuation round it does
  not jump.
- The tagline, "Scan. Solve. In your hands.", is a headline. It is never
  locked to the wordmark.
- No full stop, no trade-mark sign, no version in the wordmark.

## Store screenshots

Every screenshot comes from the system tests' screenshot tour (plan, E0a),
which drives the real app with a **drawn cube** as its camera source.
**No screenshot ever shows a camera frame of a real room or a person.**
That is a rule for the public repository and for the stores alike.

One tour per language, so captions and screens agree. Light theme unless
said.

| # | screen and state | caption (`store.caption.*`) | says |
|---|---|---|---|
| 1 | `home.default` | Scan. Solve. In your hands. | What it is |
| 2 | `scan.reading`, three sides read, the drawn cube in the picture | Show each side to the camera | How it starts; that it reads by itself |
| 3 | `solve.move`, a turn of the right side | Follow one turn at a time | The core of it; the plain wording |
| 4 | `solve.stage-done`, with the explanation | See what each stage is for | That you can learn from it |
| 5 | `solve.move`, dark theme, marks visible | Colours you can tell apart | Dark theme; the marks |
| 6 | `complete.default` | Solved | The ending |

Sizes to capture, from one tour by setting the window size:

| store | sizes |
|---|---|
| Google Play | Phone, upright: 1080 x 1920 or larger; two to eight shots. Optional 7 and 10 inch tablet sets from the expanded layout |
| App Store | The current largest iPhone and the 13 inch iPad, upright. Other sizes are scaled by the store |
| Microsoft Store | 1920 x 1080, the expanded layout |
| Mac App Store | 2880 x 1800, the expanded layout |
| Flathub, AppStream | The default window, 960 x 680, at 1x and 2x, light theme first, with nothing but the window |

Composition: the screenshot at full size on `paper`, the caption above it
in the `title` style, nothing else. No device frames, no gradients, no
hands.

The store summary is `store.summary`: "Scan your cube with the camera,
then follow one turn at a time until it is solved."

## Open

- The reverse-domain identifier (`platforms.md`).
- Whether the name and icon clear a trade-mark check.
- Sound: three short sounds are part of the identity once they exist
  (`platforms.md`, Sound and haptics).
