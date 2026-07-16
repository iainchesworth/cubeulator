# Cubeulator

Cubeulator is a camera-driven, on-device Rubik's Cube solver: point a camera
at a scrambled cube, capture its six faces, and get a step-by-step solution
rendered as a technical drawing.

!!! note "Current status: repository skeleton"
    This documentation describes the project as it will exist once the
    planned specs land. Right now (Spec 00), the repository is a build/CI
    skeleton with stub targets — no solver, computer vision, edge-AI, or
    rendering logic is implemented yet. See
    [Architecture](architecture.md) for what's real today versus planned,
    and [Dependency strategy](dependency-strategy.md) for an open risk this
    skeleton already surfaced.

## Planned pipeline

1. **Cube state model + Kociemba solving** from a hardcoded scramble.
2. **OpenCV capture-to-state pipeline** with manual per-face confirmation.
3. **Edge-AI live tracking** (coverage detection, auto-advance).
4. **bgfx technical-drawing render** (progress overlay, step animation).

Each stage is a separate, later spec, built in the order above once the
skeleton (this spec) is green.
