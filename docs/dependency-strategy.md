# Dependency strategy: vcpkg on every platform

Cubeulator vendors [vcpkg](https://github.com/microsoft/vcpkg) as a pinned git
submodule (manifest mode, `vcpkg.json`) for its C++ dependencies: Catch2,
fmt and OpenCV. Qt is a separate prebuilt install
(aqtinstall / `jurplel/install-qt-action`), not a vcpkg dependency, for the
same reasons CountdownSolver keeps it out of vcpkg (build size, Windows
`MAX_PATH`).

OpenCV has one entry in `vcpkg.json` for every triplet but the browser's
(`"platform": "!emscripten"`; the browser has an entry of its own, see
[WebAssembly](#webassembly)): it resolves the same way on every triplet vcpkg
supports, `arm64-android`/`arm64-ios` included (`opencv4` pins an explicit
feature list rather than taking the port's defaults, but carries no mobile
restriction of its own). The
`android-clang-*`/`ios-clang-*` presets chainload vcpkg's toolchain on top of
the NDK/iOS toolchain (`VCPKG_CHAINLOAD_TOOLCHAIN_FILE`) so every dependency
resolves the same way everywhere.
`cube::lib`'s `version.cpp` uses `fmt::format` rather than `std::format`
because the Android NDK's `libc++` didn't implement `<format>` when this was
written (NDK r26b; it needs locale support the NDK's minimal libc doesn't
provide) — this was discovered via a real CI failure, not anticipated in
advance. The NDK CI uses now (r28c) has `std::format`, so `fmt` is a choice
kept, not a constraint.

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

## Opt-in features

Tracy is the one dependency that is **not** in the base list: it is the
`profiling` manifest feature, selected by `-DCUBEULATOR_ENABLE_TRACY=ON` (the
top-level `CMakeLists.txt` adds it to `VCPKG_MANIFEST_FEATURES` before
`project()`, the way it already does for `tests`). A normal configure never
resolves it, so the binary cache and every shipped package are unaffected.
The feature asks for `tracy[on-demand]` for the target and
`tracy[cli-tools]` as a `host` dependency, so a cross-build for a phone
builds the capture/export tools for the machine that will run them. See
[Profiling](profiling.md).

## bgfx: removed

`bgfx` was declared in `vcpkg.json` from the bootstrap spec on, for a
"technical-drawing render pipeline" that was never written: `src/render` held
one function returning an empty `Result`, and nothing in the build linked
bgfx. The cube is drawn by Qt Quick 3D (`Cube3DView.qml`) and the QML Canvas
(`CubeView.qml`), and the graphics API underneath is Qt's RHI, so the
dependency bought nothing and cost seven vcpkg ports (bgfx, libsquish,
lodepng, mgnlibs, miniz, stb, tinyexr) on every CI leg, mobile included, plus
a `find_package(bgfx REQUIRED)` that made every configure depend on it.

It was removed together with its scaffolding: the `cube::gpu_render` and
lib-side `cube::render` stubs, the `gpu_surface` platform interface (a native
handle "resolved to a bgfx-compatible surface", with five per-OS
implementations nothing called) and its two error codes. See
[Architecture](architecture.md#gpu-rendering) for what draws what, and for
`QQuickRhiItem`, the route to take if custom GPU drawing inside the Qt Quick
scene is ever needed. If bgfx itself is wanted again it is one manifest line
and a `find_package`; the evidence trail below for its mobile support is kept
for that day.

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
not something vcpkg itself enforces. The strategy is to let vcpkg build
OpenCV the same way on every platform rather than pre-committing to a
heavier, platform-specific fallback before testing the simpler path.

## WebAssembly

The browser build ([Try it in your browser](web.md)) takes its two libraries
from vcpkg the same way, on the `wasm32-emscripten-threads` triplet
(`cmake/triplets/`): vcpkg's own `wasm32-emscripten` with `-pthread`, because
Qt's multithreaded WebAssembly kit is built with atomics and shared memory and
the linker refuses an object that was not. Where it differs from the rest:

- **OpenCV is its own, smaller, entry in `vcpkg.json`**, selected by
  `"platform": "emscripten"` (the desktop and mobile entry carries
  `"!emscripten"`): core, imgproc, imgcodecs and videoio only, with the
  `intrinsics` feature (OpenCV's WebAssembly SIMD path), and none of the image
  codecs, calib3d or QR code feature. Nothing the app ships calls any of them;
  they are minutes of WebAssembly compiling each. The one consequence is that
  `cv::imwrite` has no encoder in the browser, which only the developer scan
  recording uses, and that cannot be turned on there.
- **OpenCV runs single-threaded.** `WITH_PTHREADS_PF=OFF`, set in the triplet:
  with it on, the first `cv::cvtColor` or `cv::resize` on the GUI thread
  - the browser's main thread - would hand work to a pool of Web Workers that
  only the main thread's event loop can create, and wait for them.
- **vcpkg's wasm32-emscripten triplet is community tier**, not covered by
  vcpkg's own CI, and only recently applies `VCPKG_C_FLAGS`/`VCPKG_CXX_FLAGS`
  at all (the wrapper toolchain landed in July 2026, and the `builtin-baseline`
  here includes it). Moving the baseline back past it would silently drop
  `-pthread`.

## What this means for CI

`ci.yml`'s `android-build` and `ios-build` jobs build opencv4 for
`arm64-android`/`arm64-ios` through vcpkg. Both cap `VCPKG_MAX_CONCURRENCY`
at 2, mirroring the desktop jobs' mitigation - the dependency builds are
large, and the cap was set when ONNX Runtime, now gone, made them larger.
The `web-build` job does the same for `wasm32-emscripten-threads` (its cap
is 4). See [CI & dependencies](ci.md) for which legs are required.

## Fallback: native SDKs per platform

If vcpkg's mobile build of OpenCV turns out not to actually work, the
fallback is to consume its own official mobile distribution instead,
wrapped as a CMake `IMPORTED` target behind the same interface name
(`cube::opencv`) so vision code still never branches on how the dependency
was acquired:

- **OpenCV**: `opencv2.framework` (iOS, via `platforms/ios/build_framework.py`)
  / `OpenCV-android-sdk` (Android, `OpenCV_DIR=.../sdk/native/jni` +
  `find_package(OpenCV)`).

This fallback is not currently pursued - `cmake/MobileDependencies.cmake`
sources OpenCV via vcpkg on every platform. It stays documented here so it
isn't lost if CI shows it genuinely doesn't work out.
