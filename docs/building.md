# Building & packaging

## Presets

Naming convention: `<os>-<compiler>-<config>`.

| Preset | Notes |
| --- | --- |
| `windows-msvc-{debug,release}` | CI builds+tests `release` only |
| `linux-gcc-{debug,release}` | CI builds+tests `release` only |
| `linux-gcc-coverage` | Debug + gcov instrumentation, 80% line/branch gate |
| `{windows-msvc,linux-gcc,macos-clang}-profiling` | RelWithDebInfo + [Tracy](profiling.md) (`CUBEULATOR_ENABLE_TRACY=ON`, sanitizers off); not built by CI |
| `macos-clang-{debug,release}` | CI builds+tests `release` only |
| `android-clang-{debug,release}` | CI builds `release` only; builds the Qt/QML app via Qt for Android |
| `ios-clang-{debug,release}` | CI builds `release` only; build-only, unsigned, `CUBEULATOR_BUILD_APP=OFF` (no Qt) — Qt for iOS packaging not yet wired up, no Mac available to validate it here |
| `web-emscripten-{debug,release}` | CI builds `release` only; the Qt/QML app as WebAssembly for a browser — see [WebAssembly (browser)](#webassembly-browser) |

**CI only builds the `release` preset per platform** — that's what would
actually ship, and vcpkg builds both debug/release variants of every
dependency together in one pass regardless of which config our own project
targets, so a routine Debug CI leg would mostly just re-verify the same
dependency build under a different `CMAKE_BUILD_TYPE`. `debug` presets stay
fully defined and buildable locally (`cmake --preset linux-gcc-debug`) for
whenever you actually need an unoptimized/more-debuggable build, or define
your own combination in a personal `CMakeUserPresets.json` — CI just doesn't
babysit them on every push.

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
# once: the SDK and the kit
git clone https://github.com/emscripten-core/emsdk && cd emsdk
./emsdk install 5.0.5 && ./emsdk activate 5.0.5 && source ./emsdk_env.sh
python -m aqt install-qt all_os wasm 6.12.0 wasm_multithread -m qtquick3d qtshadertools --outputdir ~/Qt

# configure and build (EMSDK comes from emsdk_env; QT_ROOT_DIR is the wasm kit)
export QT_ROOT_DIR=$HOME/Qt/6.12.0/wasm_multithread
cmake --preset web-emscripten-release   -DQT_HOST_PATH=$HOME/Qt/6.12.0/gcc_64   -DQT_HOST_PATH_CMAKE_DIR=$HOME/Qt/6.12.0/gcc_64/lib/cmake
cmake --build --preset web-emscripten-release
```

`QT_ROOT_DIR` is the variable `jurplel/install-qt-action` exports, which is
what the preset reads to chainload the kit's `qt.toolchain.cmake` (which in
turn chainloads Emscripten's). The tests are not built for the browser
(`CUBEULATOR_BUILD_TESTS=OFF`).

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

- `CUBEULATOR_BUILD_APP` (default `ON`) — builds the Qt6 GUI shell. Off by
  default on the mobile presets.
- `CUBEULATOR_BUILD_TESTS` (default `ON`) — builds the unit/integration test
  suites.
- `CUBEULATOR_ENABLE_SANITIZERS` (default `ON`) — ASan+UBSan (GCC/Clang Debug)
  or ASan (real MSVC Debug); disabled automatically under clang-cl.
- `CUBEULATOR_ENABLE_COVERAGE` (default `OFF`) — gcov instrumentation; only
  the `linux-gcc-coverage` preset turns this on.
- `CUBEULATOR_ENABLE_TRACY` (default `OFF`) — Tracy profiler instrumentation
  (`cmake/Tracy.cmake`). Adds the `profiling` vcpkg manifest feature, which
  supplies the Tracy client and its command-line tools; off is a no-op that
  needs none of it. The `*-profiling` presets turn it on. See
  [Profiling](profiling.md).

## Packaging

`cmake --build --preset <preset> --target package` (desktop only) produces a
ZIP everywhere, plus a platform-native format when its packaging tool is
available: NSIS (Windows), DragNDrop (macOS), DEB/RPM (Linux). Mobile
packaging goes through androiddeployqt/Xcode archive+export instead, once
those shells exist.
