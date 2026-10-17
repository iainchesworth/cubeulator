# Privacy

Cubeulator uses your camera to read the colours on a cube. This page says what
it does with the picture, and what it keeps.

## The short version

- The camera picture is used **on your device, and nowhere else**.
- **No picture is saved.** Nothing is uploaded or shared.
- There is **no account, no telemetry, no crash upload and no update check.**
  The application's own code contains no network calls at all.
- What it remembers is a handful of settings and the list of cubes you have
  solved, kept on your device.

## When the camera is on

The camera is on only on two screens:

- **The scan screen**, to read the six sides.
- **The solve screen**, to see how you are holding the cube and to notice the
  move you make (the small picture in the corner).

It is off on the start screen, in Settings and on the solved screen. Your
computer's own camera light is the proof: if it is lit, Cubeulator is looking.

Each camera frame is shrunk to 640 pixels across, analysed in memory for the
colours of nine stickers, and then dropped. The next frame replaces it. The
cube, as its colours, is held in memory for the solve and is gone when you scan
another cube or close the app.

## What is kept

On your device, in the system's settings store:

- the solve method, the appearance and the language you chose;
- the graphics system you chose, if you changed it;
- the window's size, position and whether it was maximised.

[Settings and shortcuts](settings.md#where-the-settings-are-kept) says where
that is on each system. Uninstalling Cubeulator does not remove it.

And, in a file of its own in the app's data folder, the history of your
solves: for each, when it started, the cube's colours, the turn reached,
how many turns and how long. Never a picture. It is
`%APPDATA%\Cubeulator\solve-history.json` on Windows,
`~/Library/Application Support/Cubeulator/solve-history.json` on macOS and
`~/.config/cubeulator/solve-history.json` on Linux, the newest 100 are kept,
and in a browser it is the page's own storage. **Settings > History** turns
keeping it off and clears it. See [Past solves](history.md).

## Developer recording and diagnostics

Two things can write more than the above to disk. Both are for people building
and testing Cubeulator, both are off unless someone switches them on, and
neither sends anything anywhere.

**Recording a scan.** If the environment variable `CUBE_SCAN_RECORD_DIR` is
set to a folder when the app starts, each scan's frames (at the 640-pixel
working size) are saved under it, in the format described in
[Recording format](../recording-format.md). While it is on, the scan screen
shows a red dot and *Recording this scan to* and the folder, so a recording is
never silent. Only a scan is recorded, never the solve screen's camera.

**Diagnostics.** **Settings > Write diagnostics to a file** (or
<kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>D</kbd>, or `CUBE_DIAGNOSTICS=1`) writes
what the scan reads and decides (each side's colours, how they were named, how
the cube was put together) as lines of text to a file in a per-user folder on
your device. It is off every time the app starts, and while it is on every
screen shows a strip saying so and where the file is. It holds numbers and
names and no picture, unless a second switch, **Also keep camera frames**, is
also on. See [Live diagnostics](../diagnostics.md).

Frames can show whoever was holding the cube, so keep any recording private.

## In a browser

[The browser version](../web.md) is a set of static files with no server behind
it. The page asks your browser for the camera, and the browser shows its own
indicator and lets you withdraw permission at any time. Settings are kept in
the browser's local storage for that site.

## Permissions

Only the camera. The macOS permission prompt reads: *Cubeulator uses the camera
to read the colours on your cube. Pictures stay on this device.* You can take
the permission away at any time in your system's privacy settings, and
scanning will then say *Camera unavailable*.

## Third-party code

Cubeulator is built from open-source components, which are credited in the
installed package and in [Licensing](../licensing.md). Each release file
carries a GPG signature and a build attestation, so you can
[check](../SECURITY.md#verifying-build-authenticity) that a download is the one
this project built.
