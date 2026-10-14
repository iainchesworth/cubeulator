# FAQ / troubleshooting

**Does Cubeulator actually solve cubes?**
Yes, on Windows, macOS and Linux: hold each side of a cube up to the camera, and
it solves it and shows you the turns. It is a pre-release; the
[home page](index.md) says what is and is not yet proven.

**Windows says "Windows protected your PC", or macOS will not open it.**
The packages are not yet signed by Microsoft or Apple. On Windows choose *More
info*, then *Run anyway*. On macOS right-click the app and choose *Open*, or use
*Open Anyway* in System Settings > Privacy & Security. Whether a file really
came from this project is checked with the GPG signature and the attestation:
[Verifying build authenticity](SECURITY.md#verifying-build-authenticity).

**It does not read my cube.**
It reads one side at a time, square on, in even light. Strong backlight (a
window behind the cube), a cube with very light plastic and flush tiles, and a
side shown at an angle are the cases it is not yet proven on; the
[Vision pipeline](vision-pipeline.md) lists its known limits.

**Why is iOS still build-only in CI, when Android now builds the app?**
Both jobs attempt real vcpkg builds of OpenCV for their
mobile triplet, previously untested territory — see
[Dependency strategy](dependency-strategy.md). Android also
now builds the actual Qt/QML shell (`android-build` installs Qt for Android
and drops `CUBEULATOR_BUILD_APP=OFF`), confirmed working end-to-end
including on a local emulator. iOS stays build-only, `CUBEULATOR_BUILD_APP=OFF`,
until Qt-for-iOS packaging (signing, Xcode integration) is wired up — nobody
has had a Mac available to validate it yet. Both jobs stay
`continue-on-error: true` until fully proven out; desktop (Windows/Linux/
macOS) remains the fully-supported target for now.

**Why GPL-3.0-or-later if this is meant to ship to app stores?**
That tension is flagged, not resolved, in
[LICENSE-NOTE.md](https://github.com/iainchesworthlabs/cubeulator/blob/develop/LICENSE-NOTE.md) —
worth reading before a first store submission.
