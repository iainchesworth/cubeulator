# FAQ / troubleshooting

**Does Cubeulator actually solve cubes yet?**
No — see the note at the top of [Home](index.md). This repository is
currently the Spec 00 build/CI skeleton; the solver, capture pipeline,
edge-AI tracking, and render logic each land as separate, later specs.

**Why is Android/iOS build-only in CI?**
The mobile dependency story (OpenCV/ONNX Runtime/bgfx via vcpkg) has real
gaps on those triplets — see [Dependency strategy](dependency-strategy.md).
Desktop (Windows/Linux/macOS) is the fully-supported target for now.

**Why GPL-3.0-or-later if this is meant to ship to app stores?**
That tension is flagged, not resolved, in
[LICENSE-NOTE.md](https://github.com/iainchesworth/cubeulator/blob/develop/LICENSE-NOTE.md) —
worth reading before a first store submission.
