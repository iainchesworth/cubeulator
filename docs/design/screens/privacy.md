# Privacy

> **Status in 0.1.0-beta.1:** Not built; the only privacy wording in the app
> is the permission screen's reassurances, which phones show. One claim in the
> table below does not match the code: recording is not compiled out of
> release builds. `DirectScan` records a scan when `CUBE_SCAN_RECORD_DIR`
> names a folder, nothing records by default, and the scan screen shows a
> banner while it does.

Lane: experience. New (E3b).

What the app does with the camera, what it keeps, and what it sends:
nothing. Three short sections that can be read in twenty seconds, in the
app's own voice, with no legal language.

Entered from: **About > Privacy**.

## The promise, and what keeps it true

The permission screen says "No picture is saved, uploaded or shared."
The recovery plan's R4 wanted every failed scan recorded automatically,
which would have made that false.

**Decided by Iain, 2026-10-13: recording failed scans is for developer
builds only.** So there is one statement, and it holds in every released
build:

| claim | kept true by |
|---|---|
| The camera picture is used on this device only | There is no network code in the app (plan decision 7) |
| No picture is saved | Release builds have no recording path. A developer build that records shows `settings.dev.recording` on its scan screen and in Settings, and hides `permission.point2` |
| History stays on this device, and holds no picture | `history.md`, "What is kept" |
| It does not use the internet | No accounts, adverts, analytics or crash upload. The one hand-off is the **Source code** row in About, which opens the browser when pressed |

A test in the release configuration should assert that the recording code
is not compiled in. That is for the scan lane and E0a; it is noted here
because this screen is what it protects.

If a later release does want failed scans kept, with consent, this screen
gains a section and a switch that is off by default, and the permission
screen's second line changes. That is a new decision, not a setting to
slip in.

## Layout

One scrolling column at every size, 24 margin, at most 34 rem wide for
reading, centred from medium up. A top bar: back and the title.

Three sections, each a `headline` and one paragraph of `body`.

## Parts

| part | component | string | objectName |
|---|---|---|---|
| Back | icon button | `common.back` | `privacy.back` |
| Title | top bar title, heading level 1 | `privacy.title` | `privacy.title` |
| The camera | heading level 2 and a paragraph | `privacy.camera.title`, `privacy.camera.body` | `privacy.camera` |
| Solve history | heading level 2 and a paragraph | `privacy.history.title`, `privacy.history.body` | `privacy.history` |
| The internet | heading level 2 and a paragraph | `privacy.network.title`, `privacy.network.body` | `privacy.network` |

There are no controls beyond back. Clearing history and turning it off
are in Settings, and the history paragraph says so.

The stores ask for a privacy policy at a web address. It is this text,
published with the source; E6 writes the page. The two must say the same
thing.

## States

### `privacy.default`

The three sections. Back: to About.
