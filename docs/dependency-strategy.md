# Dependency strategy: vcpkg vs. mobile

Cubeulator vendors [vcpkg](https://github.com/microsoft/vcpkg) as a pinned git
submodule (manifest mode, `vcpkg.json`) for its C++ dependencies: Catch2,
fmt, OpenCV, ONNX Runtime, and bgfx. Qt is a separate prebuilt install
(aqtinstall / `jurplel/install-qt-action`), not a vcpkg dependency, for the
same reasons CountdownSolver keeps it out of vcpkg (build size, Windows
`MAX_PATH`).

`fmt` is resolved via vcpkg on **every** triplet, including
`arm64-android`/`arm64-ios` — unlike the three libraries below, it has no
native/platform-specific code that makes mobile triplets risky, so the
`android-clang-*`/`ios-clang-*` presets chainload vcpkg's toolchain on top of
the NDK/iOS toolchain (`VCPKG_CHAINLOAD_TOOLCHAIN_FILE`) specifically so
`fmt` (and `Catch2` on desktop) resolve the same way everywhere.
`cube::lib`'s `version.cpp` uses `fmt::format` rather than `std::format`
because the Android NDK's `libc++` doesn't implement `<format>` (it needs
locale support the NDK's minimal libc doesn't provide) — this was discovered
via a real CI failure, not anticipated in advance.

## The gap

Before writing any application code, the three heavier dependencies were
checked for support on the `arm64-ios` and `arm64-android` vcpkg triplets:

| Library | vcpkg triplet support (mobile) | Evidence |
|---|---|---|
| `bgfx` | No verified iOS/Android build via vcpkg's own port | [bgfx#905](https://github.com/bkaradzic/bgfx/issues/905) — upstream tracking issue for iOS arm64 |
| `onnxruntime` | Open, maintainer-acknowledged gap for iOS | [onnxruntime#23158](https://github.com/microsoft/onnxruntime/issues/23158) |
| `opencv4` | Android is an officially tested vcpkg triplet; iOS is unverified | [vcpkg blog: Android tested triplets](https://devblogs.microsoft.com/cppblog/vcpkg-2023-06-20-and-2023-07-21-releases-github-dependency-graph-support-android-tested-triplets-xbox-triplet-improvements-and-more/) |

None of the three vcpkg mobile CI wrappers (`bgfx.cmake` included) have a
reliably maintained iOS/Android path either — see
[widberg/bgfx.cmake#85](https://github.com/widberg/bgfx.cmake/issues/85).

## The strategy

`vcpkg.json` scopes `opencv4`, `onnxruntime`, and `bgfx` to desktop triplets
only (`"platform": "!ios & !android"`). For `arm64-ios` / `arm64-android`, the
plan is to consume each library's own official distribution instead, wrapped
as a CMake `IMPORTED` target behind the same interface name
(`cube::opencv`, `cube::onnxruntime`, `cube::bgfx`) so vision/render code
never branches on how a dependency was acquired:

- **OpenCV**: `opencv2.framework` (iOS, via `platforms/ios/build_framework.py`)
  / `OpenCV-android-sdk` (Android, `OpenCV_DIR=.../sdk/native/jni` +
  `find_package(OpenCV)`).
- **ONNX Runtime**: CocoaPods `onnxruntime-c`/`onnxruntime-objc` or a
  `build.sh --ios` `.xcframework` (iOS); the Maven
  `com.microsoft.onnxruntime:onnxruntime-android` `.aar`, or `build.sh
  --android` (Android) — both unpacked and `IMPORTED`-wrapped rather than
  pulled in via CocoaPods/Gradle directly.
- **bgfx**: built via its own GENie build (`make ios-arm64`, `make
  android-arm64`), not vcpkg or `bgfx.cmake`, then `IMPORTED`-wrapped.

`cmake/MobileDependencies.cmake` is where this wiring will live; it is
currently a documented placeholder (see its header comment) because no code
in this bootstrap spec links any of the three libraries yet — the vision,
edge-AI, and render specs are what will actually need them, and implementing
the real fetch/build/unpack logic is tracked as follow-up work at that point.

## What this means for CI

`ci.yml`'s `android-build` and `ios-build` jobs are marked
`continue-on-error: true`, matching CountdownSolver's own treatment of
experimental matrix legs. They are expected to build the (Qt-free, dependency-
free) `cube::lib` and `cube::platform` targets successfully now; they are not
expected to build anything that links OpenCV/ONNX Runtime/bgfx until the
mobile dependency wiring above is implemented.
