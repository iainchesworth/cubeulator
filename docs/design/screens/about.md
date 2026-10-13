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
   version beneath it in the mono face.
2. Three short paragraphs: what it is; who made it; the licence, in
   `inkSecondary`.
3. Three list rows: Licences, Privacy, Source code.
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

The version can be selected and copied, for bug reports.

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
