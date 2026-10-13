# Permission

Lane: experience. Replaces `PermissionScreen.qml`.

Explains why the camera is wanted before the operating system asks, and
gives a way on when the answer is no. Shown only where the system asks per
app: **Android, iOS and macOS**. Windows and Linux never show it; a
blocked camera there is `scan.camera-blocked`.

Entered from: **Scan a cube** on Home or **Scan another cube** on Complete
when access is not allowed; **Allow camera** in Settings.

## Layout

**Compact.** A top bar with back and no title. One column, 24 margin:

1. A 56 px outlined box with the camera icon (a caution triangle when
   refused).
2. The title.
3. One paragraph.
4. Three reassurances, each a check in `ok` and a sentence. Not shown when
   refused.
5. The actions, full width, pushed to the bottom: the primary first, then
   the others.

**Short.** The same column; it scrolls.

**Expanded.** Two panes. Leading: the column, at most 640 wide, actions
directly under the text. Trailing: `surface` with the decorative cube.

## Parts

| part | component | string | objectName |
|---|---|---|---|
| Back | icon button | `common.back` | `permission.back` |
| Title | `title`, heading level 1 | `permission.title`, `permission.refused.title` | `permission.title` |
| Body | `body` | `permission.body`, `permission.refused.body` | `permission.body` |
| Reassurances | A list of three; each check icon is ignored by screen readers | `permission.point1`, `permission.point2`, `permission.point3` | `permission.points` |
| Allow camera | button, primary, thumb, block | `permission.allow` | `permission.allow` |
| Open device settings | button, primary, thumb, block | `common.openDeviceSettings` | `permission.openSettings` |
| Ask again | button, secondary, block | `permission.askAgain` | `permission.askAgain` |
| Enter colours by hand | button, secondary, block | `common.enterByHand` | `permission.enterByHand` |
| Not now | button, ghost | `permission.notNow` | `permission.notNow` |

`permission.point2` says "No picture is saved, uploaded or shared." That
is true of every released build (brief, D7, confirmed 2026-10-13). A
developer build that records failed scans must not show this line; it
shows `settings.dev.recording` in its place.

The system's own prompt is drawn by the device. The only words of ours in
it are the reason, on iOS and macOS: `permission.system.camera`, supplied
through `Info.plist` in every language. Android's prompt takes no reason,
which is why this screen comes first.

## States

### `permission.ask`

Camera access has not been asked for, or was refused and the system will
still ask.

- **Allow camera**: the system prompt. If allowed, straight to
  `scan.starting` (or back to Settings if it was opened from there). If
  refused, `permission.refused`.
- **Enter colours by hand**: to `review.empty`.
- **Not now** and back: to where the screen was opened from.

While the system prompt is up the app is inactive beneath it. Nothing on
this screen changes until the answer comes.

### `permission.refused`

Access was refused, or the device does not allow it (a managed device, a
parental control). Qt reports the same thing for both, so the wording
covers both.

- Title `permission.refused.title`; body `permission.refused.body`. The
  box shows the caution triangle. No reassurances.
- **Open device settings** is primary: after a refusal it is the one
  thing sure to work. It opens the system's settings for this app. When
  the app comes back to the front with access allowed, it goes straight
  to `scan.starting`.
- **Ask again**: asks the system once more. Android stops showing its
  prompt after a second refusal and iOS after the first; if nothing
  appears, the screen stays as it is, with the settings button still
  there.
- **Enter colours by hand**: to `review.empty`.
- **Not now** and back: to where the screen was opened from.
- A screen reader is told the new title when the state changes.
