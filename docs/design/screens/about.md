# About

Lane: experience. New (E3b). The app has no about box today.

What the app is, which version, who made it, under what licence, and the
way to the notices and the privacy statement.

Entered from: **Settings > About Cubeulator** on every platform; the
application menu on macOS (`../platforms.md`).

## Layout

One scrolling column at every size, 24 margin, at most 640 wide, centred
from medium up. A top bar: back and the title.

1. The lock-up: the icon at 72 px, then the name in `title` with the
   version beneath it in the mono face, and under that the build line with
   its **Copy** button.
2. Three short paragraphs: what it is; who made it; the licence, in
   `inkSecondary`.
3. Four list rows: Licences, Privacy, Source code, Copy details.
4. One line of small print.

## Parts

| part | component | string | objectName |
|---|---|---|---|
| Back | icon button | `common.back` | `about.back` |
| Title | top bar title | `about.title` | `about.title` |
| Icon | The app icon (`../identity/`). Decorative | - | `about.icon` |
| Name | `title`, heading level 1. Never translated | `app.name` | `about.name` |
| Version | `numeral`, `inkSecondary`. From the build, the same string the installer and the store show | `about.version` | `about.version` |
| What | `body` | `about.what` | `about.what` |
| Author | `body` | `about.author` | `about.author` |
| Licence | `body`, `inkSecondary` | `about.licence` | `about.licence` |
| Licences | list row | `about.licences` | `about.licences` |
| Privacy | list row | `about.privacy` | `about.privacy` |
| Source code | list row; its value is the address, shown in full so that it can be read and typed. Role: link | `about.source` | `about.source` |
| Small print | `small`, `inkSecondary` | `about.trademark` | `about.trademark` |
| Build line | `numeral`, `inkSecondary`. Platform and the first eight characters of the source revision, from the build (`version_details()`); `+dirty` is appended in developer builds only | `about.build` | `about.build` |
| Copy | ghost button beside the build line. Copies the version and the build line as one line, `cubeulator 1.0.0 · windows-x64 · built from a1b2c3d4`. Its label becomes `about.copied` for two seconds | `about.copyBuild`, `about.copied` | `about.copyBuild` |
| Copy details | list row with a note beneath it. Copies the block below. Its note is shown under the row; the row's label becomes `about.copied` for two seconds | `about.copyDetails`, `about.copyDetails.note`, `about.copied` | `about.copyDetails` |

The version and the build line can be selected, and copied with **Copy**,
for bug reports: selecting text by touch is awkward on a phone.

**What Copy details holds** (`family.md` F7, decision D6). Text only:

- the version line, and the full `version_details()` block;
- the operating system and its version, and the CPU architecture;
- the Qt version the app runs on and was built against, and the graphics
  backend;
- whether a camera was found and which capture backend is in use;
- the app's language, theme, text size and motion setting;
- the window size and the display's pixel ratio.

It never holds a picture, a colour read from a cube, a camera's name, a
file path, or anything about the person. The note under the row says so,
and that sentence must stay true of every build.

Cubeulator is under the GPL, which obliges it to say where the source is.
The **Source code** row is that statement, so it is on this screen and
not a level down.

## States

### `about.default`

- **Licences**: to `licences.list`.
- **Privacy**: to `privacy.default`.
- **Source code**: opens the address in the system's browser. This is the
  only place the app hands off to the internet, and it does so only when
  pressed.
- Back: to where it was opened from.

There is one state. With no network the screen is the same; the browser
is the one that will say so.
