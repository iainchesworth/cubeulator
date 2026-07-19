# Dependency strategy: vcpkg on every platform

Cubeulator vendors [vcpkg](https://github.com/microsoft/vcpkg) as a pinned git
submodule (manifest mode, `vcpkg.json`) for its C++ dependencies: Catch2,
fmt, OpenCV, ONNX Runtime, and bgfx. Qt is a separate prebuilt install
(aqtinstall / `jurplel/install-qt-action`), not a vcpkg dependency, for the
same reasons CountdownSolver keeps it out of vcpkg (build size, Windows
`MAX_PATH`).

All five vcpkg dependencies - including OpenCV, ONNX Runtime, and bgfx - are
unscoped entries in `vcpkg.json`: they resolve the same way on every triplet
vcpkg supports, `arm64-android`/`arm64-ios` included (`opencv4` pins an
explicit feature list rather than taking the port's defaults, and `onnx`
is overridden via a `vcpkg-overlay-ports/` entry - see "onnxruntime + onnx:
a static-registration collision, and the overlay-port fix" below - but
neither carries any platform restriction of its own). The
`android-clang-*`/`ios-clang-*` presets chainload vcpkg's toolchain on top of
the NDK/iOS toolchain (`VCPKG_CHAINLOAD_TOOLCHAIN_FILE`) so every dependency
resolves the same way everywhere.
`cube::lib`'s `version.cpp` uses `fmt::format` rather than `std::format`
because the Android NDK's `libc++` doesn't implement `<format>` (it needs
locale support the NDK's minimal libc doesn't provide) — this was discovered
via a real CI failure, not anticipated in advance.

## Earlier plan and why it changed

The original bootstrap spec scoped `opencv4`, `onnxruntime`, and `bgfx` to
desktop-only triplets (`"platform": "!ios & !android"`), based on upstream
evidence that each library's mobile vcpkg support looked unverified or gappy:

| Library | Concern raised | Evidence |
|---|---|---|
| `bgfx` | No verified iOS/Android build via vcpkg's own port | [bgfx#905](https://github.com/bkaradzic/bgfx/issues/905) — upstream tracking issue for iOS arm64 |
| `onnxruntime` | Open, maintainer-acknowledged gap for iOS | [onnxruntime#23158](https://github.com/microsoft/onnxruntime/issues/23158) |
| `opencv4` | Android is an officially tested vcpkg triplet; iOS is unverified | [vcpkg blog: Android tested triplets](https://devblogs.microsoft.com/cppblog/vcpkg-2023-06-20-and-2023-07-21-releases-github-dependency-graph-support-android-tested-triplets-xbox-triplet-improvements-and-more/) |

Re-checking the actual vcpkg port manifests (`deps/vcpkg/ports/*/vcpkg.json`)
rather than just those linked issues showed none of the three are actually
blocked by vcpkg on mobile triplets:

- **bgfx**'s port declares `"supports": "!bsd"` — no ios/android exclusion.
  It's built via the [bgfx.cmake](https://github.com/widberg/bgfx.cmake)
  community wrapper, which has real, dedicated iOS support
  (`cmake/bgfx.cmake`: links `OpenGLES`/`Metal`/`UIKit`/`CoreGraphics`/
  `QuartzCore` frameworks when `CMAKE_SYSTEM_NAME MATCHES iOS`) and
  explicitly excludes `ANDROID` from its X11 lookup
  (`if(UNIX AND NOT APPLE AND NOT EMSCRIPTEN AND NOT ANDROID)`), so the X11
  dev packages CI installs for the Linux desktop build are never needed on
  either mobile platform.
- **onnxruntime**'s port declares `"supports": "!uwp"` — no ios/android
  exclusion; one optional feature is even scoped `"osx | ios"` specifically.
- **opencv4**'s port carries no `supports` restriction at all, and pulls in
  an Android-specific `cpu-features` dependency — a real signal of Android
  work already in the port, matching the officially-tested-triplet claim
  above.

So the original exclusion was a defensive call based on adjacent evidence,
not something vcpkg itself enforces. The strategy is now to let vcpkg build
all three uniformly across every platform and see what actually happens,
rather than pre-committing to a heavier, platform-specific fallback before
testing the simpler path.

## What this means for CI

`ci.yml`'s `android-build` and `ios-build` jobs are marked
`continue-on-error: true`, matching CountdownSolver's own treatment of
experimental matrix legs. They now attempt real vcpkg builds of
opencv4/onnxruntime/bgfx for `arm64-android`/`arm64-ios`, which was
previously untested territory for two of the three libraries on both
platforms. Both jobs cap `VCPKG_MAX_CONCURRENCY` at 2, mirroring the desktop
jobs' existing mitigation - onnxruntime alone already OOM'd runners at
default parallelism once its build reached compilation.

Because these jobs are `continue-on-error: true`, a build failure won't turn
CI red; check the job logs directly rather than relying on the checkmark.

## onnxruntime + onnx: a static-registration collision, and the overlay-port fix

The first `Ort::Session` construction in the hand-presence detector
(`cube::vision::OnnxHandPresenceDetector`) failed with "invalid model"
errors against a well-formed `.onnx` file, preceded by a wall of
`Schema error: Trying to register schema ... already registered` and a
`SchemasRegisterer` assertion ("4 schema were exposed ... 741 were
expected") — not a build failure, so it only surfaced at runtime, the
first time anything actually called into `Ort::Session`.

The initial hypothesis was OpenCV: `opencv4`'s `dnn` module (plus the
`directml` feature that only accelerates it) and its `gapi` module can
both import/run `.onnx` models and vendor their own ONNX operator-schema
registration to do it - `gapi` even auto-enables an ONNX backend via
`find_package(ONNX)` with no explicit vcpkg feature gating it, whenever a
standalone `onnx` install is visible in the same vcpkg tree (which it
always is here, since `cube::onnxruntime` depends on it too). Both were
disabled (`vcpkg.json` pins `opencv4`'s feature list explicitly - default
features minus `dnn`/`directml`/`gapi` - since nothing in this codebase
uses `cv::dnn` or `cv::gapi` anyway; `cube::vision`'s edge-AI code goes
through `cube::onnxruntime` directly). **That trim was worth keeping as
dependency hygiene, but it didn't fix the collision** - confirmed with a
minimal repro (`Ort::Env` + `Ort::Session` in a standalone .cpp linking
only `onnxruntime.lib`, no OpenCV, no `cube_lib`, nothing else from this
project) that reproduced the identical schema errors on its own.

The real cause: `onnxruntime`'s own vcpkg port
(`deps/vcpkg/ports/onnxruntime/portfile.cmake`) prints
`message(WARNING "The port requires 'onnx' port build with CMake option
ONNX_DISABLE_STATIC_REGISTRATION=ON")` - but the vendored `onnx` port
(`deps/vcpkg/ports/onnx/portfile.cmake`) doesn't set it, and exposes no
feature to opt in. Without that flag, ONNX's own static initializers
register its operator schemas automatically at library-load time; then
onnxruntime registers the *same* schemas itself, explicitly, the first
time an `Ort::Env`/`Ort::Session` is created - a warned-about, known gap
between these two specific port versions
(`onnx` 1.19.0 / `onnxruntime` 1.23.2 at this repo's pinned vcpkg
baseline), not anything specific to how this project links or uses
either library.

Fixed via a vcpkg overlay port rather than editing the pinned
`deps/vcpkg` submodule directly (which would leave it in a dirty,
unreproducible state): `vcpkg-configuration.json` registers
`vcpkg-overlay-ports/` as an overlay-ports root, and
`vcpkg-overlay-ports/onnx/` is a copy of the vendored `onnx` port (same
patches, same version, `port-version` bumped to 1) with
`-DONNX_DISABLE_STATIC_REGISTRATION=ON` added to its
`vcpkg_cmake_configure()` call. This is the standard, version-controlled
way to override a single port's build flags without touching the pinned
submodule - reproducible for CI and every worktree, and easy to drop once
(if) a future vcpkg baseline bump pulls in `onnx`/`onnxruntime` port
versions where this is already fixed upstream.

## Fallback: native SDKs per platform

If vcpkg's mobile build of any one of these three libraries turns out not to
actually work, the fallback is to consume that library's own official mobile
distribution instead, wrapped as a CMake `IMPORTED` target behind the same
interface name (`cube::opencv`, `cube::onnxruntime`, `cube::bgfx`) so
vision/render code still never branches on how the dependency was acquired:

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

This fallback is not currently pursued for any of the three libraries -
`cmake/MobileDependencies.cmake` sources all three via vcpkg on every
platform. It stays documented here so it isn't lost if CI shows one of them
genuinely doesn't work out.
