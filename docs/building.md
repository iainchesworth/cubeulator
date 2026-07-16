# Building & packaging

## Presets

Naming convention: `<os>-<compiler>-<config>`.

| Preset | Notes |
| --- | --- |
| `windows-msvc-{debug,release}` | Full build + test |
| `linux-gcc-{debug,release}` | Full build + test |
| `linux-gcc-coverage` | Debug + gcov instrumentation, 80% line/branch gate |
| `macos-clang-{debug,release}` | Full build + test |
| `android-clang-{debug,release}` | Build-only; `CUBEULATOR_BUILD_APP=OFF` (no Qt) |
| `ios-clang-{debug,release}` | Build-only, unsigned; `CUBEULATOR_BUILD_APP=OFF` (no Qt) |

Android/iOS are build-only for now: mobile shells are planned to be native
(SwiftUI/Jetpack Compose), not Qt/QML, and the OpenCV/ONNX Runtime/bgfx
mobile dependency wiring isn't implemented yet — see
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
