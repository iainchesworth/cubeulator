# Building & packaging

## Presets

Naming convention: `<os>-<compiler>-<config>`.

| Preset | Notes |
| --- | --- |
| `windows-msvc-{debug,release}` | CI builds+tests `release` only |
| `linux-gcc-{debug,release}` | CI builds+tests `release` only |
| `linux-gcc-coverage` | Debug + gcov instrumentation, 80% line/branch gate |
| `macos-clang-{debug,release}` | CI builds+tests `release` only |
| `android-clang-{debug,release}` | CI builds `release` only; build-only, `CUBEULATOR_BUILD_APP=OFF` (no Qt) |
| `ios-clang-{debug,release}` | CI builds `release` only; build-only, unsigned, `CUBEULATOR_BUILD_APP=OFF` (no Qt) |

**CI only builds the `release` preset per platform** — that's what would
actually ship, and vcpkg builds both debug/release variants of every
dependency together in one pass regardless of which config our own project
targets, so a routine Debug CI leg would mostly just re-verify the same
dependency build under a different `CMAKE_BUILD_TYPE`. `debug` presets stay
fully defined and buildable locally (`cmake --preset linux-gcc-debug`) for
whenever you actually need an unoptimized/more-debuggable build, or define
your own combination in a personal `CMakeUserPresets.json` — CI just doesn't
babysit them on every push.

Android/iOS are build-only for now: mobile shells are planned to be native
(SwiftUI/Jetpack Compose), not Qt/QML. vcpkg now builds opencv4/onnxruntime/
bgfx on the mobile triplets too, the same way as desktop — see
[Dependency strategy](dependency-strategy.md).

## Options

- `CUBEULATOR_BUILD_APP` (default `ON`) — builds the Qt6 GUI shell. Off by
  default on the mobile presets.
- `CUBEULATOR_BUILD_TESTS` (default `ON`) — builds the unit/integration test
  suites.
- `CUBEULATOR_ENABLE_SANITIZERS` (default `ON`) — ASan+UBSan (GCC/Clang Debug)
  or ASan (real MSVC Debug); disabled automatically under clang-cl.
- `CUBEULATOR_ENABLE_COVERAGE` (default `OFF`) — gcov instrumentation; only
  the `linux-gcc-coverage` preset turns this on.

## Packaging

`cmake --build --preset <preset> --target package` (desktop only) produces a
ZIP everywhere, plus a platform-native format when its packaging tool is
available: NSIS (Windows), DragNDrop (macOS), DEB/RPM (Linux). Mobile
packaging goes through androiddeployqt/Xcode archive+export instead, once
those shells exist.
