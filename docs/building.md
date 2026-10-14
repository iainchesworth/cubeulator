# Building & packaging

Toolchain requirements are in [Getting started](getting-started.md#requirements);
the exact versions CI builds with, and why, are in
[CI & dependencies](ci.md#toolchain-versions).

## Presets

Naming convention: `<os>-<compiler>-<config>`. A preset configures into
`build/<preset>/` and puts its executables in `build/<preset>/bin`.

| Preset | Notes |
| --- | --- |
| `windows-msvc-{debug,release}` | CI builds+tests `release` only |
| `linux-gcc-{debug,release}` | CI builds+tests `release` only |
| `linux-gcc-coverage` | Debug + gcov instrumentation, 80% line/branch gate |
| `{windows-msvc,linux-gcc,macos-clang}-fastdebug` | RelWithDebInfo, so optimised with no sanitizers: the quick build for local iteration (see [Testing](testing.md#debug-is-slow-use-fastdebug-for-local-iteration)); not built by CI |
| `{windows-msvc,linux-gcc,macos-clang}-profiling` | RelWithDebInfo + [Tracy](profiling.md) (`CUBEULATOR_ENABLE_TRACY=ON`, sanitizers off); not built by CI |
| `macos-clang-{debug,release}` | CI builds+tests `release` only |
| `android-clang-{debug,release}` | CI builds `release` only; builds the Qt/QML app via Qt for Android |
| `ios-clang-{debug,release}` | Xcode generator, so macOS only; CI builds `release` only; build-only, unsigned, `CUBEULATOR_BUILD_APP=OFF` (no Qt) — Qt for iOS packaging not yet wired up, no Mac available to validate it here |
| `web-emscripten-{debug,release}` | CI builds `release` only; the Qt/QML app as WebAssembly for a browser — see [WebAssembly (browser)](#webassembly-browser) |
| `web-emscripten-diagnostics` | Release + the in-process recorder + the browser tests' read-only driver (`window.cubeulatorProfile`, `window.cubeulatorDriver`); built by the `web-test` and `web-perf` legs. For measuring and for `tests/web`, not for publishing |
| `web-emscripten-tests` | The library and its unit, integration and performance binaries as WebAssembly, run under Node by `ctest` (`CUBEULATOR_BUILD_APP=OFF`); built by the `web-test` leg |

**CI only builds the `release` preset per platform** — that's what would
actually ship, and vcpkg builds both debug/release variants of every
dependency together in one pass regardless of which config our own project
targets, so a routine Debug CI leg would mostly just re-verify the same
dependency build under a different `CMAKE_BUILD_TYPE`. `debug` presets stay
fully defined and buildable locally (`cmake --preset linux-gcc-debug`) for
whenever you actually need an unoptimized/more-debuggable build, or define
your own combination in a personal `CMakeUserPresets.json` — CI just doesn't
babysit them on every push.

On Linux, CI compiles with GCC 15 (`-DCMAKE_CXX_COMPILER=g++-15`; the presets
say plain `g++`, so pass it if yours is older) and installs the X11 and OpenGL
development packages before the vcpkg build: `libx11-dev libgl1-mesa-dev
libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev libxext-dev`.

Android builds the Qt/QML shell like every other platform (see
[Architecture](architecture.md)); iOS remains build-only,
`CUBEULATOR_BUILD_APP=OFF`, until Qt-for-iOS packaging is wired up. vcpkg
builds opencv4 on the mobile triplets too, the same way as
desktop — see [Dependency strategy](dependency-strategy.md).

`android-clang-{debug,release}` set `CMAKE_CXX_SCAN_FOR_MODULES=OFF`: CMake's
Ninja generator auto-enables C++20 modules dependency scanning (needs
`clang-scan-deps`), which the Android NDK's bundled LLVM toolchain doesn't
expose in a way CMake can locate when chainloaded under vcpkg's toolchain
file — confirmed in CI (`CMAKE_CXX_COMPILER_CLANG_SCAN_DEPS-NOTFOUND`). The
project doesn't use C++ modules, so disabling the scan avoids the missing
tool entirely rather than chasing its exact path per NDK version.

Configuring `android-clang-*` locally (outside `ci.yml`, which passes these
via `jurplel/install-qt-action`'s env vars) needs four extra cache variables
that a plain desktop configure doesn't:

```
cmake --preset android-clang-debug \
  -DCMAKE_PREFIX_PATH=<path-to-Qt>/6.12.0/android_arm64_v8a \
  -DQT_HOST_PATH=<path-to-Qt>/6.12.0/<desktop-kit> \
  -DQT_HOST_PATH_CMAKE_DIR=<path-to-Qt>/6.12.0/<desktop-kit>/lib/cmake \
  -DCMAKE_FIND_ROOT_PATH_MODE_PACKAGE=BOTH \
  -DANDROID_SDK_ROOT=<path-to-Android-SDK>
```

`QT_HOST_PATH`/`QT_HOST_PATH_CMAKE_DIR` are normally supplied by Qt's own
`qt-cmake` wrapper, which this project doesn't use (it configures through
vcpkg's toolchain directly); without them, `qt_standard_project_setup()` and
other host-tool-dependent macros are simply undefined ("Unknown CMake
command"). `CMAKE_FIND_ROOT_PATH_MODE_PACKAGE` defaults to `ONLY` under the
NDK toolchain, which hides both Qt kits from `find_package()` since neither
lives under `CMAKE_FIND_ROOT_PATH` (vcpkg's own install tree is unaffected —
its toolchain file adds itself to `CMAKE_FIND_ROOT_PATH` already). All four
were confirmed necessary against a real local Android build, not just CI.

The preset also chainloads the NDK's own toolchain file from
`$ANDROID_NDK_HOME`, so that environment variable has to point at an NDK (CI
uses r28c, 28.2.13676358, with JDK 21 and Android platform 36; the versions
and why are in [Toolchain versions](ci.md#toolchain-versions)).

## WebAssembly (browser)

`web-emscripten-*` builds the same Qt/QML app as WebAssembly, which is what
[Try it in your browser](web.md) serves. The output is a directory of static
files - `index.html`, `cube_app.js`, `cube_app.wasm`, `qtloader.js`, the
isolation worker and the icon - and nothing else: there is no installer.

What it needs, all of it fixed by the Qt version:

| | |
| --- | --- |
| Qt | The `wasm_multithread` kit of the same version as the desktop one (6.12.0), plus a desktop kit of that version for its host tools (`moc`, `rcc`, `qsb`, `lrelease`) |
| Emscripten | **Exactly** the version Qt names for its release - 5.0.5 for Qt 6.12.0 (see [Qt for WebAssembly](https://doc.qt.io/qt-6/wasm.html)). Qt's toolchain file stops the configure on any other |
| Threads | The build is threaded throughout: Qt's kit, this project and every vcpkg port are built with `-pthread` (the triplet `cmake/triplets/wasm32-emscripten-threads.cmake`). The single-threaded kit cannot run the app, which uses `QtConcurrent` and `std::thread` |

```sh
# once: aqtinstall at the commit CI pins (the last release, 3.3.0, cannot read
# Qt 6.11+'s WebAssembly kits; see .github/actions/web-build/action.yml), the
# SDK, and both kits
python -m pip install "git+https://github.com/miurahr/aqtinstall.git@076e1659807d0b362a3ed684d54c2e9c775eb9c7"
git clone https://github.com/emscripten-core/emsdk ~/emsdk
~/emsdk/emsdk install 5.0.5 && ~/emsdk/emsdk activate 5.0.5 && source ~/emsdk/emsdk_env.sh
python -m aqt install-qt all_os wasm 6.12.0 wasm_multithread -m qtquick3d qtshadertools --outputdir ~/Qt
python -m aqt install-qt linux desktop 6.12.0 linux_gcc_64 -m qtquick3d qtshadertools --outputdir ~/Qt

# configure and build, from the Cubeulator checkout (EMSDK comes from
# emsdk_env; QT_ROOT_DIR is the wasm kit)
export QT_ROOT_DIR=$HOME/Qt/6.12.0/wasm_multithread
cmake --preset web-emscripten-release   -DQT_HOST_PATH=$HOME/Qt/6.12.0/gcc_64   -DQT_HOST_PATH_CMAKE_DIR=$HOME/Qt/6.12.0/gcc_64/lib/cmake
cmake --build --preset web-emscripten-release
```

`QT_ROOT_DIR` is the variable `jurplel/install-qt-action` exports, which is
what the preset reads to chainload the kit's `qt.toolchain.cmake` (which in
turn chainloads Emscripten's). The app presets do not build the test binaries
(`CUBEULATOR_BUILD_TESTS=OFF`): a Catch2 program has no page to run in. They
are the `web-emscripten-tests` preset's, run under Node, and the app itself is
tested in a browser by `tests/web`; both are described in
[Browser tests](testing.md#browser-tests).

Two properties of the whole build, because every object in it has to agree:

- **Threads** (`-pthread`): atomics and bulk memory on every object, or
  `wasm-ld` refuses the shared memory.
- **Exceptions** (`-fexceptions`): on for this project's code, the vcpkg ports
  and the link line, as they are in Qt's kit. Emscripten's default is to compile
  a `throw` to an abort and a `catch` to nothing, which would silently disable
  the handlers the app has (and fail every Catch2 check). It has to be the
  kit's mode: an object built with `-fwasm-exceptions` does not link against it.

Both are set in the triplet and in `src/app/platform/web/platform.cmake`; a
change to either rebuilds every port.

Serve the output directory with any static server - a `file://` URL will not
do, since the page needs `https://` or `localhost`:

```sh
python -m http.server 8000 --directory build/web-emscripten-release/bin
# then open http://localhost:8000/
```

The page needs cross-origin isolation for threads. A server that sends
`Cross-Origin-Opener-Policy: same-origin` and `Cross-Origin-Embedder-Policy:
require-corp` gives it; one that does not (`python -m http.server`, GitHub
Pages) is covered by `cubeulator-isolation.js`, which registers a service
worker that adds the two headers and reloads the page once.

## Options

- `CUBEULATOR_BUILD_APP` (default `ON`) — builds the Qt6 GUI shell. `OFF`
  needs no Qt at all; the library, the platform layer and the tests that need
  no Qt are still built. The `ios-clang-*` presets set it `OFF`.
- `CUBEULATOR_BUILD_TESTS` (default `ON`) — builds the unit/integration test
  suites. The Android, iOS and web presets set it `OFF`.
- `CUBEULATOR_ENABLE_SANITIZERS` (default `ON`) — ASan+UBSan (GCC/Clang Debug)
  or ASan (real MSVC Debug); disabled automatically under clang-cl.
- `CUBEULATOR_ENABLE_COVERAGE` (default `OFF`) — gcov instrumentation; only
  the `linux-gcc-coverage` preset turns this on.
- `CUBEULATOR_ENABLE_TRACY` (default `OFF`) — Tracy profiler instrumentation
  (`cmake/Tracy.cmake`). Adds the `profiling` vcpkg manifest feature, which
  supplies the Tracy client and its command-line tools; off is a no-op that
  needs none of it. The `*-profiling` presets turn it on. See
  [Profiling](profiling.md).
- `CUBEULATOR_ENABLE_PROFILE_RECORDER` (default `OFF`) — the same zone macros
  recorded in-process (`cmake/ProfileRecorder.cmake`) and exported as a Chrome
  trace, a CSV and a summary, for where Tracy's client cannot run: a browser.
  Exclusive with `CUBEULATOR_ENABLE_TRACY`. The `web-emscripten-diagnostics`
  and `web-emscripten-tests` presets turn it on. See
  [Profiling](profiling.md#in-the-browser).
- `CUBEULATOR_WEB_TEST_DRIVER` (default `OFF`, web only) — links the read-only
  driver (`window.cubeulatorDriver`) that `tests/web` reads the app through.
  The `web-emscripten-diagnostics` preset turns it on; the published build has
  none of it.

## Packaging

`cmake --build --preset <preset> --target package` (desktop only) produces a
ZIP everywhere (and a `.tar.gz` on Linux), plus a platform-native format when
its packaging tool is available: NSIS (Windows), DragNDrop (macOS), DEB/RPM
(Linux). What goes in each, and what the tools need, is
[Packaging the desktop app](packaging.md). On Android the APK is made by
`androiddeployqt`, which the build runs itself (CI uploads the debug-signed
one); iOS has no packaging yet, since its preset is build-only; the web build's
output is the directory above.
