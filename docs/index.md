# Cubeulator

Cubeulator is a camera-driven, on-device Rubik's Cube solver: hold each side of
a scrambled cube up to the camera, and it works out the cube, solves it, and
shows you the solution turn by turn on a 3D cube.

!!! note "Status: pre-release"
    For Windows, macOS and Linux; phones are not a target of this release. The
    scan has been checked on recorded sessions and a webcam, not yet across
    cameras, rooms and cubes, and there is no review screen yet. The
    [changelog](https://github.com/iainchesworthlabs/cubeulator/blob/main/CHANGELOG.md)
    says what each release does and what is not yet proven.

## How it works

1. **Scan.** The camera reads one side at a time, square on, and recognises each
   side by its own centre. Six sides that make exactly one cube that can exist
   are a scan; anything else asks for the sides again. See the
   [Vision pipeline](vision-pipeline.md).
2. **Solve.** Three methods, picked on the solve screen: *Beginner* (layer by
   layer, about 100 moves), *Intermediate* (CFOP, about 52) and *Advanced*
   (Kociemba's two-phase search, about 20). See [Solve methods](solver-methods.md).
3. **Show.** The solution is drawn on a 3D cube: the layer the next move turns
   nudges the way it has to go. The camera stays on, so a side you turn is seen
   and the solution steps on by itself.

Nothing is uploaded: there is no account, no telemetry and no update check.

## Get it

Packages for each desktop are on the
[Releases page](https://github.com/iainchesworthlabs/cubeulator/releases); the
[README](https://github.com/iainchesworthlabs/cubeulator#install) has the
first-run notes for each. To build it yourself, see
[Getting started](getting-started.md).

## For contributors

| | |
| --- | --- |
| [Architecture](architecture.md) | What is implemented, and how it fits together |
| [Dependency strategy](dependency-strategy.md) | vcpkg on desktop, and the mobile gap |
| [Testing & coverage](testing.md) | Test layers, and the 80% coverage gate |
| [Contributing](CONTRIBUTING.md) | The branching model, code style, the PR process |
| [Cutting a release](releasing.md) | How a version becomes packages on a GitHub Release |
