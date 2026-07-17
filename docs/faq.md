# FAQ / troubleshooting

**Does Cubeulator actually solve cubes yet?**
No — see the note at the top of [Home](index.md). This repository is
currently the Spec 00 build/CI skeleton; the solver, capture pipeline,
edge-AI tracking, and render logic each land as separate, later specs.

**Why is Android/iOS build-only in CI?**
Those jobs now attempt real vcpkg builds of OpenCV/ONNX Runtime/bgfx for the
mobile triplets, previously untested territory for two of the three
libraries — see [Dependency strategy](dependency-strategy.md). They stay
`continue-on-error: true` until that's proven out; desktop (Windows/Linux/
macOS) remains the fully-supported target for now.

**Why GPL-3.0-or-later if this is meant to ship to app stores?**
That tension is flagged, not resolved, in
[LICENSE-NOTE.md](https://github.com/iainchesworth/cubeulator/blob/develop/LICENSE-NOTE.md) —
worth reading before a first store submission.
