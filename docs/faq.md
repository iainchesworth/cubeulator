# FAQ / troubleshooting

**Does Cubeulator actually solve cubes yet?**
No — see the note at the top of [Home](index.md). This repository is
currently the Spec 00 build/CI skeleton; the solver, capture pipeline,
edge-AI tracking, and render logic each land as separate, later specs.

**Why is iOS still build-only in CI, when Android now builds the app?**
Both jobs attempt real vcpkg builds of OpenCV/bgfx for their
mobile triplet, previously untested territory for both
libraries — see [Dependency strategy](dependency-strategy.md). Android also
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
