# Dependency strategy: vcpkg on every platform

Cubeulator vendors [vcpkg](https://github.com/microsoft/vcpkg) as a pinned git
submodule (manifest mode, `vcpkg.json`) for its C++ dependencies: Catch2,
fmt, OpenCV, and bgfx. Qt is a separate prebuilt install
(aqtinstall / `jurplel/install-qt-action`), not a vcpkg dependency, for the
same reasons CountdownSolver keeps it out of vcpkg (build size, Windows
`MAX_PATH`).

OpenCV and bgfx are unscoped entries in `vcpkg.json`: they resolve the same
way on every triplet vcpkg supports, `arm64-android`/`arm64-ios` included
(`opencv4` pins an explicit feature list rather than taking the port's
defaults, but neither carries any platform restriction of its own). The
`android-clang-*`/`ios-clang-*` presets chainload vcpkg's toolchain on top of
the NDK/iOS toolchain (`VCPKG_CHAINLOAD_TOOLCHAIN_FILE`) so every dependency
resolves the same way everywhere.
`cube::lib`'s `version.cpp` uses `fmt::format` rather than `std::format`
because the Android NDK's `libc++` doesn't implement `<format>` (it needs
locale support the NDK's minimal libc doesn't provide) — this was discovered
via a real CI failure, not anticipated in advance.

## What was removed, and why

ONNX Runtime (with its `onnx` overlay port and the MediaPipe palm model it
ran) and the vendored M3T tracker (with GLEW and GLFW) were dependencies of the
earlier tracking pipeline and its hand detector. Recovery plan step R6 removed
the code that used them (see [recovery-plan.md](recovery-plan.md)), and with
it the abseil / protobuf / re2 / flatbuffers chain ONNX Runtime pulled in.
Nothing in the tree links ONNX Runtime now.

The `onnx` overlay port existed because the pinned `onnx` and `onnxruntime`
port versions did not agree on static schema registration, which only showed at
runtime as an invalid-model error on the first `Ort::Session`. If a model
runtime is ever added again, check that first; the overlay port
(`vcpkg-overlay-ports/onnx/`) is in git history.

## Earlier plan and why it changed

The original bootstrap spec scoped `opencv4` and `bgfx` (and then-present
`onnxruntime`) to desktop-only triplets (`"platform": "!ios & !android"`),
based on upstream evidence that each library's mobile vcpkg support looked
unverified or gappy:

| Library | Concern raised | Evidence |
|---|---|---|
| `bgfx` | No verified iOS/Android build via vcpkg's own port | [bgfx#905](https://github.com/bkaradzic/bgfx/issues/905) — upstream tracking issue for iOS arm64 |
| `opencv4` | Android is an officially tested vcpkg triplet; iOS is unverified | [vcpkg blog: Android tested triplets](https://devblogs.microsoft.com/cppblog/vcpkg-2023-06-20-and-2023-07-21-releases-github-dependency-graph-support-android-tested-triplets-xbox-triplet-improvements-and-more/) |

Re-checking the actual vcpkg port manifests (`deps/vcpkg/ports/*/vcpkg.json`)
rather than just those linked issues showed neither is actually blocked by
vcpkg on mobile triplets:

- **bgfx**'s port declares `"supports": "!bsd"` — no ios/android exclusion.
  It's built via the [bgfx.cmake](https://github.com/widberg/bgfx.cmake)
  community wrapper, which has real, dedicated iOS support
  (`cmake/bgfx.cmake`: links `OpenGLES`/`Metal`/`UIKit`/`CoreGraphics`/
  `QuartzCore` frameworks when `CMAKE_SYSTEM_NAME MATCHES iOS`) and
  explicitly excludes `ANDROID` from its X11 lookup
  (`if(UNIX AND NOT APPLE AND NOT EMSCRIPTEN AND NOT ANDROID)`), so the X11
  dev packages CI installs for the Linux desktop build are never needed on
  either mobile platform.
- **opencv4**'s port carries no `supports` restriction at all, and pulls in
  an Android-specific `cpu-features` dependency — a real signal of Android
  work already in the port, matching the officially-tested-triplet claim
  above.

So the original exclusion was a defensive call based on adjacent evidence,
not something vcpkg itself enforces. The strategy is to let vcpkg build both
uniformly across every platform rather than pre-committing to a heavier,
platform-specific fallback before testing the simpler path.

## What this means for CI

`ci.yml`'s `android-build` and `ios-build` jobs build opencv4 and bgfx for
`arm64-android`/`arm64-ios` through vcpkg. Both cap `VCPKG_MAX_CONCURRENCY`
at 2, mirroring the desktop jobs' mitigation - the dependency builds are
large, and the cap was set when ONNX Runtime, now gone, made them larger.
See [CI & dependencies](ci.md) for which legs are required.

## Fallback: native SDKs per platform

If vcpkg's mobile build of either library turns out not to actually work, the
fallback is to consume that library's own official mobile distribution
instead, wrapped as a CMake `IMPORTED` target behind the same interface name
(`cube::opencv`, `cube::bgfx`) so vision/render code still never branches on
how the dependency was acquired:

- **OpenCV**: `opencv2.framework` (iOS, via `platforms/ios/build_framework.py`)
  / `OpenCV-android-sdk` (Android, `OpenCV_DIR=.../sdk/native/jni` +
  `find_package(OpenCV)`).
- **bgfx**: built via its own GENie build (`make ios-arm64`, `make
  android-arm64`), not vcpkg or `bgfx.cmake`, then `IMPORTED`-wrapped.

This fallback is not currently pursued for either library -
`cmake/MobileDependencies.cmake` sources both via vcpkg on every platform.
It stays documented here so it isn't lost if CI shows one of them genuinely
doesn't work out.
