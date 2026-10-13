# CI & dependencies

## Workflows

| Workflow | Purpose | Blocking? | In the merge queue? |
| --- | --- | --- | --- |
| `ci.yml` | Build + test matrix (Linux/macOS/Windows, release only), Android build (Qt/QML app, release only), iOS build-only (release only), coverage gate | Yes on `main`; on `develop` as each check goes green | Not yet |
| `codeql.yml` | Static analysis (C++) | Yes on `main`; on `develop` once green | Not yet |
| `branch-name-check.yml` | Enforces gitflow branch naming | Yes | Yes (skipped, a pass) |
| `dependency-review.yml` | Fails on a moderate+ vulnerability newly introduced in a PR | Yes | Yes |
| `osv-scanner.yml` | Scans `vcpkg.json` against the OSV database | No (visible only) | No |
| `zizmor.yml` | Lints the workflow YAML itself | No (visible only) | No |
| `scorecard.yml` | OpenSSF Scorecard supply-chain health score | No (badge/score) | No |
| `docs.yml` | Builds and publishes the mkdocs site to GitHub Pages | N/A (push to `main` only) | No |

See [Branch protection and the merge queue](branch-protection.md) for which
checks are required on `develop` and `main`, how the queue is configured, and
the `merge_group` rule a workflow has to follow to be part of it.

### Running some legs by hand

A push, pull request or queue entry always runs every leg of `ci.yml`. A
manual dispatch can name just the ones it needs, which matters because the
whole matrix is hours of hosted runners and the pool is shared:

```sh
gh workflow run ci.yml --ref <branch> -f legs=macos
gh workflow run ci.yml --ref <branch> -f legs=clang-format,linux
```

`legs` is a comma-separated list (no spaces) of `clang-format`, `clang-tidy`,
`linux`, `macos`, `windows`, `coverage`, `android`, `ios`, or `all` (the
default). An unknown name fails the run rather than running nothing.

Jobs of a dispatched run are named `dispatch - <leg>`, deliberately not the
names of the required checks: a run that skips legs would otherwise report
"skipped" (which counts as a pass) under a required check's name on the
branch's head commit.

## Toolchain versions

What CI builds with, and why anything is not simply "the newest". The pins that
are bumped by hand are listed under [Dependabot](#pins-dependabot-cannot-see).

| Component | In use | Newer exists? |
| --- | --- | --- |
| Qt | 6.12.0 (LTS; every fourth minor, 6.8 before it) | No - 6.12.0 is the newest release. The Windows kit is `msvc2022_64`; Qt ships no MSVC 2026 x64 kit, and it works with the VS 2026 toolset. Installing the Windows kit needs an unreleased aqtinstall (the pinned commit in `ci.yml` and `ci/install-qt.ps1`): the last release predates Qt 6.11's per-architecture repository layout. |
| MSVC | Visual Studio 2026, via `windows-2025-vs2026` | No. |
| GCC | 15.2 (`g++-15`, `gcov-15`) | Yes: GCC 16 is released upstream, but ubuntu-26.04's `gcc-16` is a March 2026 snapshot (`16-20260322`), not a release. The `Linux (GCC 15)` check name is also required on `main`. |
| clang-format | 23.1.3, pinned PyPI wheel (`ci/requirements-lint.txt`) | No. |
| clang-tidy and its compiler | Clang 23.x from apt.llvm.org | No, but it is a snapshot of `release/23.x` rebuilt as the branch moves, not a frozen version; there is no clang-tidy 23 wheel on PyPI yet. |
| AppleClang | Xcode 26 on `macos-26` (desktop); `macos-15` (iOS) | iOS stays on `macos-15` for the open vcpkg pkg-config issue [microsoft/vcpkg#50681](https://github.com/microsoft/vcpkg/issues/50681). |
| Android | NDK r28c (28.2.13676358), JDK 21, platform 36, build-tools 36.0.0 | r30 exists. r28c is newer than the r27c Qt 6.12 names and builds its libraries with: Qt recommends matching it but does not rule a newer NDK out, so this is outside Qt's tested configuration on purpose (r27c is the fallback). r28c's libc++ has `std::expected::transform`/`and_then`, `std::format`/`std::print`, `ranges::contains`/`fold_left` and `mdspan`, which r26b lacks; it still lacks float `from_chars`, `views::enumerate` and `flat_map`. |
| CMake | newest 4.x (`lukka/get-cmake`, `cmakeVersion: latest`); policies to 4.4 | No. |
| mkdocs / mkdocs-material | 1.6.x / 9.7.x | mkdocs 2.0 is still only a development release and mkdocs-material has no series newer than 9, so the range caps stay. |
| vcpkg libraries | OpenCV 4.x, ONNX Runtime 1.23, bgfx, Catch2, fmt, GLFW | Upstream is ahead (OpenCV 5.0, ONNX Runtime 1.31) but vcpkg's registry has no newer major of either, so these cannot move without a port of our own. A newer baseline only brings minors. |
| iOS deployment target | 18.0 (`CMakePresets.json`) | No - 18 is Qt 6.12's minimum iOS (it was 16.3 for Qt 6.8), built with Xcode 16's iOS 18 SDK. iOS CI is build-only and does not build Qt, so this is the project's own floor, not something Qt exercises there. |

## Dependencies

vcpkg (vendored submodule, manifest mode) supplies Catch2, fmt, OpenCV, ONNX
Runtime, and bgfx on every triplet, desktop and mobile alike, via a
chainloaded toolchain on `arm64-ios`/`arm64-android` — see below. Qt is a
separate prebuilt install (`jurplel/install-qt-action` in CI,
`ci/install-qt.*` locally) — kept out of vcpkg for build size and Windows
`MAX_PATH` reasons.

Mobile (arm64-ios/arm64-android) vcpkg builds of opencv4/onnxruntime/bgfx are
new and not yet proven out in CI (the `android-build`/`ios-build` jobs stay
`continue-on-error: true` until they are) — see
[Dependency strategy](dependency-strategy.md) for the evidence and the
fallback plan if any of the three doesn't work out.
