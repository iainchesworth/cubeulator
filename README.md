# Cubeulator

[![CI](https://github.com/iainchesworth/cubeulator/actions/workflows/ci.yml/badge.svg)](https://github.com/iainchesworth/cubeulator/actions/workflows/ci.yml)
[![CodeQL](https://github.com/iainchesworth/cubeulator/actions/workflows/codeql.yml/badge.svg)](https://github.com/iainchesworth/cubeulator/actions/workflows/codeql.yml)
[![OSV-Scanner](https://github.com/iainchesworth/cubeulator/actions/workflows/osv-scanner.yml/badge.svg)](https://github.com/iainchesworth/cubeulator/actions/workflows/osv-scanner.yml)
[![Zizmor](https://github.com/iainchesworth/cubeulator/actions/workflows/zizmor.yml/badge.svg)](https://github.com/iainchesworth/cubeulator/actions/workflows/zizmor.yml)
[![OpenSSF Scorecard](https://api.scorecard.dev/projects/github.com/iainchesworth/cubeulator/badge)](https://scorecard.dev/viewer/?uri=github.com/iainchesworth/cubeulator)
[![Docs](https://img.shields.io/badge/docs-published-2f7d7b)](https://iainchesworth.github.io/cubeulator/)
[![C++23](https://img.shields.io/badge/C%2B%2B-23-blue)](docs/architecture.md)
[![License: GPL v3+](https://img.shields.io/badge/license-GPL--3.0--or--later-blue)](LICENSE)

A camera-driven, on-device Rubik's Cube solver: point a camera at a
scrambled cube, capture its six faces, and get a step-by-step solution
rendered as a technical drawing.

> **Status: repository skeleton (Spec 00).** No solver, computer vision,
> edge-AI, or rendering logic is implemented yet — this repo is currently
> the build/CI foundation those specs land on. See
> [Architecture](https://iainchesworth.github.io/cubeulator/architecture/)
> for what's real today versus planned.

Full docs live at
**[iainchesworth.github.io/cubeulator](https://iainchesworth.github.io/cubeulator/)**.

## Quick start

```sh
git clone --recurse-submodules https://github.com/iainchesworth/cubeulator
cd cubeulator
./ci/install-qt.sh   # or ci/install-qt.ps1 on Windows
cmake --preset linux-gcc-debug -DCMAKE_PREFIX_PATH=$HOME/Qt/6.8.3/gcc_64
cmake --build --preset linux-gcc-debug
ctest --preset linux-gcc-debug
```

Just want the core library, no Qt/GUI?

```sh
cmake --preset linux-gcc-debug -DCUBEULATOR_BUILD_APP=OFF
cmake --build --preset linux-gcc-debug
ctest --preset linux-gcc-debug
```

See [Getting started](https://iainchesworth.github.io/cubeulator/getting-started/)
for every platform's preset, including Windows/macOS and the (currently
build-only) Android/iOS targets.

## Documentation

| | |
| --- | --- |
| [Getting started](https://iainchesworth.github.io/cubeulator/getting-started/) | Requirements, cloning, first build |
| [Building & packaging](https://iainchesworth.github.io/cubeulator/building/) | Every preset, sanitizers, `cpack` |
| [Testing & coverage](https://iainchesworth.github.io/cubeulator/testing/) | TDD, test layers, the 80% coverage gate |
| [Architecture](https://iainchesworth.github.io/cubeulator/architecture/) | C++23 highlights, source layout |
| [Dependency strategy](https://iainchesworth.github.io/cubeulator/dependency-strategy/) | vcpkg on desktop vs. the mobile gap |
| [Contributing](https://iainchesworth.github.io/cubeulator/CONTRIBUTING/) | Branching model, code style, PR process |

## Contributing

Contributions are welcome — see
[CONTRIBUTING.md](https://iainchesworth.github.io/cubeulator/CONTRIBUTING/)
for the branching model (this project follows
[gitflow](https://nvie.com/posts/a-successful-git-branching-model/):
`develop` for day-to-day work, `main` for tagged releases only) and how PRs
get reviewed. Please also read the
[Code of Conduct](https://iainchesworth.github.io/cubeulator/CODE_OF_CONDUCT/).

## License

GPL-3.0-or-later. See [LICENSE](LICENSE) — and
[LICENSE-NOTE.md](LICENSE-NOTE.md) for an open question about app store
distribution that's flagged, not yet resolved.
