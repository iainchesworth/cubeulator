# FAQ

Something not working? [Troubleshooting](guide/troubleshooting.md) goes through
what the screen says and what to do about it.

**Does Cubeulator actually solve cubes?**
Yes, on Windows, macOS and Linux, and in a browser: hold each side of a cube up
to the camera, and it solves it and shows you the turns. It is a pre-release;
the [user guide](guide/index.md#what-this-release-is) and the
[changelog](https://github.com/iainchesworthlabs/cubeulator/blob/main/CHANGELOG.md)
say what is and is not yet proven.

**Is anything uploaded?**
No. The camera picture is read on your device and not saved; there is no
account, no telemetry, no crash upload and no update check. See
[Privacy](guide/privacy.md).

**Which cubes does it read?**
A standard 3x3 with dark (black) plastic between the stickers, in even light.
A cube with white or light plastic, or flush tiles and no visible gap, is not
read yet.

**Can I use it on a phone?**
In the phone's browser, yes: [open it](web.md) and use the rear camera. There
is no native Android or iOS app in this release. The Android and iOS builds in
CI compile the Qt/QML app but both camera backends are placeholders, so neither
can scan yet; see [Dependency strategy](dependency-strategy.md).

**Can I type the colours in by hand?**
Not yet. If a side will not read, show it again or improve the light; a review
and hand-entry screen is planned. See [Troubleshooting](guide/troubleshooting.md).

**Why does it say "Windows protected your PC", or macOS will not open it?**
The packages are not yet signed with a certificate from Microsoft or Apple. On
Windows choose *More info*, then *Run anyway*. On macOS right-click the app and
choose *Open*, or use *Open Anyway* in System Settings > Privacy & Security.
Whether a file really came from this project is checked with the GPG signature
and the attestation: [Verifying build
authenticity](SECURITY.md#verifying-build-authenticity).

**It does not read my cube.**
It reads one side at a time, square on, in even light. Strong backlight (a
window behind the cube), a cube with very light plastic and flush tiles, and a
side shown at an angle are the cases it is not yet proven on. See
[Scanning a cube](guide/scan.md) and the [Vision pipeline](vision-pipeline.md)
for its known limits.

**How many moves is the solution?**
That depends on the method: about 100 for *Beginner* (layer by layer, in steps
you can learn), about 50 for *Intermediate* (CFOP, the way fast solvers do it)
and about 20 for *Advanced* (the fewest, with no stages to learn). You can
change method at any time and it carries on from where the cube stands. See
[Solve methods](solver-methods.md).

**Why GPL-3.0-or-later, if this is meant to reach app stores?**
That tension is written down, not resolved, in
[LICENSE-NOTE.md](https://github.com/iainchesworthlabs/cubeulator/blob/main/LICENSE-NOTE.md):
the Apple stores are deferred, and the question returns before any Apple
submission. The inventory and the options are in [Licensing](licensing.md).
