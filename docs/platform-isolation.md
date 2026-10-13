# Platform isolation

> **Principle.** The operating system is a CMake decision, not a source-code
> one. Everything that differs between platforms lives in
> `src/app/platform/<name>/`, behind a header in `src/app/interfaces/`, and the
> platform's own `platform.cmake` says which of those files are compiled and
> what they link against. Shared code - everything else - does not know which
> platforms exist: no `#ifdef`, no OS header, no runtime "which OS am I on".

Adding a platform is therefore two edits: a new `src/app/platform/<name>/`
directory, and one branch in the selector, `cmake/Platform.cmake`. Nothing in
`src/lib`, the Qt shell, the QML, the tests or any other CMake file needs to
learn the platform's name.

This is a rule for human contributors and for AI agents alike, and it is
enforced by CI (see [Enforcement](#enforcement)): a new `#ifdef` in shared code
turns the `platform-isolation` check red.

## The layout

```
src/app/
  interfaces/        what the shell needs from a platform: one header each
    camera.hpp  gpu_surface.hpp  inference_env.hpp  paths.hpp
    platform_info.hpp  crash_handler.hpp
  platform/
    windows/ macos/ linux/ android/ ios/
                     one implementation per interface, plus platform.cmake
    posix/           implementations shared by every non-Windows platform
    detail/          helpers every platform links (an OpenCV capture loop, ...)
tests/platform/<name>/   test cases only that platform can run
```

`posix/` is not a platform: it holds an implementation that more than one
platform would otherwise copy (a handler that has nothing to install, for
instance), and each platform's `platform.cmake` names the file. Reach for a
platform's own directory when the implementations genuinely differ.

## How CMake selects

`cmake/Platform.cmake` is the **only** file that tests `WIN32`, `APPLE`,
`ANDROID`, `IOS` or `UNIX`. It maps them to a platform name and includes that
platform's fragment. Every other CMake file reads what the fragment declared:

| What | Where it is declared | Who reads it |
|---|---|---|
| Source files compiled into `cube_platform` | `CUBEULATOR_PLATFORM_SOURCES` | `src/app/CMakeLists.txt` |
| System libraries (`strmiids`, `dbghelp`, ...) | `CUBEULATOR_PLATFORM_LINK_LIBRARIES` | `src/app/CMakeLists.txt` |
| Qt libraries a platform file needs | `CUBEULATOR_PLATFORM_QT_LINK_LIBRARIES` | `src/app/CMakeLists.txt` |
| C-runtime defines (`_CRT_SECURE_NO_WARNINGS`) | `CUBEULATOR_PLATFORM_COMPILE_DEFINITIONS` | `cube_platform`, every test target |
| The live tracker implementation | `CUBEULATOR_PLATFORM_CONTROLLER_SOURCES` / `_LINK_LIBRARIES` | `cube_app_controllers` |
| `cube_app` target properties (Android's manifest) | `CUBEULATOR_PLATFORM_APP_PROPERTIES` | `cube_app` |
| Is this a desktop (M3T tracker, dev tools, CPack) | `CUBEULATOR_PLATFORM_DESKTOP` | `third_party`, `tools`, `src/track_m3t`, packaging |
| Where an installed Qt lives | `CUBEULATOR_PLATFORM_QT_SEARCH_ROOTS` / `_QT_ARCH_DIRS` | `cmake/FindQt6.cmake` |
| The name in the version block | `CUBEULATOR_PLATFORM_DISPLAY_NAME` | `src/lib` (`build_target`) |
| Default Qt plugin for the test binaries | `CUBEULATOR_PLATFORM_TEST_QPA_PLATFORM` | `tests/` |
| Work before packages are found | `cube_platform_prepare_dependencies()` | `cmake/MobileDependencies.cmake` |
| Qt runtime next to the app (windeployqt) | `cube_platform_deploy_qt_runtime()` | `src/app/CMakeLists.txt` |
| Installers (NSIS, DragNDrop, DEB/RPM) | `cube_platform_configure_packaging()` | `cmake/Packaging.cmake` |

The full contract is the header comment of `cmake/Platform.cmake`; a fragment
that forgets part of it fails configure by name, not three files away.

Compiler concerns (`/W4` versus `-Wall`, sanitizer flags) are a separate
axis and stay in `cmake/CompilerWarnings.cmake`, `Sanitizers.cmake` and
`Coverage.cmake`, keyed on the compiler, not the OS.

## Adding a platform

1. Create `src/app/platform/<name>/` with one implementation of each
   interface (`camera_<name>.cpp`, `gpu_surface_<name>.cpp`,
   `inference_env_<name>.cpp`, `paths_<name>.cpp`, `platform_info_<name>.cpp`)
   and `platform.cmake`. Copy the closest existing platform's and change what
   differs; use `posix/` files where the implementation is the same.
2. Add a branch for it in `cmake/Platform.cmake`.
3. If it can run the test suites, add `tests/platform/<name>/` for any test
   case only it can run (optional: a platform with none simply has no
   directory).

That is the source tree. Three things outside it still carry platform names,
because their formats cannot read a CMake variable: the `platform` expressions
on dependencies in `vcpkg.json` (they mirror `CUBEULATOR_PLATFORM_DESKTOP`),
the configure/build/test presets in `CMakePresets.json`, and the CI legs in
`.github/workflows/ci.yml`. Add the platform to those as a build target would.

`tools/checks/check_platform_isolation.ps1` will tell you if a file in the
directory is not named by a `platform.cmake`, and the
`platform_fragments_satisfy_contract` test will tell you if the fragment is
incomplete.

## What to do instead of an `#ifdef`

Ask *"could a source file per platform, or a value CMake passes in, express
this?"* The cases already in the tree:

| Used to be | Is now |
|---|---|
| `#ifdef _WIN32` minidump filter in `main.cpp` | `interfaces/crash_handler.hpp`; `windows/crash_handler_windows.cpp` installs it, `posix/crash_handler_posix.cpp` has nothing to install |
| `#if defined(_WIN32)` COM guard in the capture loop | `platform/detail/capture_thread_scope.hpp`; one definition per platform |
| `#ifdef _WIN32` / `__APPLE__` / `__linux__` and compiler/arch macros building the version line | CMake works the line out (`cube_describe_build_target`) and stamps it into `version.hpp` |
| `#ifdef CUBE_HAVE_M3T` around the live tracker in `scan_session.cpp` | `controllers/live_track.hpp`; `live_track_m3t.cpp` on the desktops, `live_track_none.cpp` on phones, chosen in the fragment |
| `Qt.platform.os` ladders in QML | `interfaces/platform_info.hpp`, exposed to QML as the `PlatformFacts` singleton |
| `#ifndef Q_OS_WIN` choosing the test display | a value the fragment supplies, generated into `cube_test_platform_facts.hpp` (`tests/platform_facts.hpp.in`) |
| `#if defined(Q_OS_LINUX)` "needs a display" check in the system tests | the same header: which session variables the platform uses, and the advice when none is set |
| `#ifdef __linux__` XDG test cases | `tests/platform/linux/` |
| `#if defined(_WIN32)` backend choice in the hardware probes | the probes ask the app which backend it opens (`detail::capture_backend()`) |
| `#pragma warning(disable : 4996)` behind `_MSC_VER` | `_CRT_SECURE_NO_WARNINGS` from the fragment, on every test target |

## Rules

**Do**

- Declare what the shell needs from the OS in `src/app/interfaces/`, in
  `namespace cube::platform::<name>`, with a comment saying that there is one
  definition per platform.
- Implement it once per platform, and list each file in the `platform.cmake`
  of every platform that compiles it.
- Put OS-tied defines and link libraries in the fragment, never in a shared
  `CMakeLists.txt`.
- Pass a value in with `target_compile_definitions` when all that differs is a
  constant, rather than testing for the platform.

**Don't**

- Add `#if`, `#ifdef`, `#ifndef`, `#elif`, `#else` or `#endif` anywhere in
  first-party source - `src/`, `tests/` and `tools/`, platform directories
  included. They are already selected by CMake.
- Include an OS header (`<windows.h>`, `<unistd.h>`, `<sys/...>`, Android or
  Apple frameworks) outside `src/app/platform/` and `tests/platform/`.
- Ask the OS what it is at run time (`QSysInfo`, `QOperatingSystemVersion`,
  `Q_OS_*`, `Qt.platform.os`) outside the platform section.
- Test `WIN32`/`APPLE`/`ANDROID`/`IOS`/`UNIX` in any CMake file but
  `cmake/Platform.cmake`.

## Enforcement

`tools/checks/check_platform_isolation.ps1` runs as the `platform-isolation`
job in `ci.yml` (fast, ungated by the build legs, so a violation is reported
even while a porting leg is red). It fails on:

1. any preprocessor conditional in `src/`, `tests/` or `tools/`;
2. an OS header, or an OS-querying API (`QSysInfo`, `Q_OS_*`, `platformName()`,
   `Qt.platform`, ...), outside the platform section;
3. a platform directory without a `platform.cmake`, or a translation unit in
   one that no `platform.cmake` names (outside a comment);
4. a test of the OS (`WIN32`, `APPLE`, `ANDROID`, `IOS`, `UNIX`,
   `CMAKE_SYSTEM_NAME`, `$<PLATFORM_ID:...>`) in any CMake file but
   `cmake/Platform.cmake` and the fragments.

Run it locally from the repository root:

```bash
pwsh ./tools/checks/check_platform_isolation.ps1
```

It exits 0 when clean, 1 with `file:line` for each finding, and prints GitHub
annotations so a finding lands on the line in the pull request.

The CMake side has its own check, the `platform_fragments_satisfy_contract`
test (`tests/platform/check_fragments.cmake`). A build only includes the one
`platform.cmake` for the platform it targets, so the other four would otherwise
be exercised only by their own CI legs. This test includes every fragment it
finds, each in a process of its own, and holds it to the contract
(`cmake/PlatformContract.cmake`): every variable and hook defined, every file it
compiles present, and a branch in the selector for it. It runs on every leg, so
a typo in the macOS fragment fails the Linux build in seconds rather than the
macOS one in an hour.

Deliberately not scanned: `third_party/` and `deps/` (vendored code with its
own conventions), anything git ignores, and shader sources (GLSL has no
operating system to branch on). The same rule is held by ICL Forge
(`tools/checks/check_platform_macros.ps1`) and, for OS macros, by Aqualink
Automate (`scripts/check-os-macros.ps1`).
