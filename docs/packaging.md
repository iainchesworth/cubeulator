# Packaging the desktop app

How Cubeulator becomes something a person installs on Windows, macOS or
Linux. This is workstream E4a of the [app experience plan](app-experience-plan.md);
what each platform is supposed to do is specified in
[design/platforms.md](design/platforms.md). Mobile is E4b and is not here.

## What a build produces

| | package | built by | installs to |
|---|---|---|---|
| Windows | `Cubeulator-<version>-windows-x64.exe`, an NSIS installer | CPack `NSIS` | `C:\Program Files\Cubeulator` (`bin\Cubeulator.exe`), with a Start menu entry and an uninstaller |
| macOS | `Cubeulator-<version>-macos-arm64.dmg` | CPack `DragNDrop` | `Cubeulator.app`, dragged to Applications |
| Linux | `Cubeulator-<version>-linux-x64.deb` and `.rpm` | CPack `DEB`, `RPM` | `/usr/lib/cubeulator` (the app and the Qt it carries), `/usr/bin/cubeulator`, and the desktop entry, AppStream metadata and icons under `/usr/share` |

A `.zip` (and, on Linux, a `.tar.gz`) is always produced as well.

CI builds each one on its own runner and uploads it as a build artifact
(`.github/workflows/ci.yml`, job `build-and-test`). Nothing is released or
published.

### Building one locally

Configure and build as usual, then package:

```
cmake --preset windows-msvc-release
cmake --build --preset windows-msvc-release
cpack --config build/windows-msvc-release/CPackConfig.cmake -G NSIS
```

Needs, besides the build's own tools:

| | needs |
|---|---|
| everywhere | Python 3 (the icon generator; a build without it still works, with no icon) |
| Windows | [NSIS](https://nsis.sourceforge.io/) on `PATH` or in `Program Files\NSIS`; the MSVC environment, so that `windeployqt` can find the C++ runtime |
| macOS | nothing beyond Xcode's tools (`macdeployqt` comes with Qt) |
| Linux | `dpkg-deb` for `.deb`, `rpmbuild` (package `rpm`) for `.rpm` |

## The pieces

| file | does |
|---|---|
| `cmake/AppIdentity.cmake` | The name, the identifier and the Windows AppUserModelID, once |
| `cmake/DesktopPackaging.cmake` | Icons, the executable's identity per platform, the install rules and Qt deployment |
| `cmake/Packaging.cmake` | CPack: generators, file names, installer and package fields |
| `packaging/make_platform_icons.py` | SVG to `.ico`, `.icns`, PNG and `hicolor` files |
| `packaging/windows/Cubeulator.rc.in` | Icon and version resource |
| `packaging/windows/StampShortcut.ps1` | Puts the AppUserModelID on the Start menu shortcut |
| `packaging/macos/Info.plist.in` | `Info.plist`, including the camera usage string |
| `packaging/linux/*.in` | Desktop entry and AppStream metadata |
| `src/app/desktop/` | Window geometry, `KeepAwake`, process identity |
| `src/app/qt/DesktopShell.qml` | Shortcuts, the macOS menu bar, the placeholder About |

### Identity

| | value |
|---|---|
| Display name | Cubeulator |
| Identifier | `org.iainchesworth.cubeulator`, on every platform (Iain, 2026-10-13): the macOS bundle id, the Linux app id and desktop file name, later the Android package and iOS bundle id |
| Windows AppUserModelID | `IainChesworth.Cubeulator` |
| Executable | `Cubeulator.exe`, `Cubeulator.app`, `cubeulator` |
| Version | `project(VERSION)` in the top-level `CMakeLists.txt`, through `cmake/GenerateVersion.cmake`: in `cube/version.hpp`, the Windows version resource, `Info.plist`, the AppStream release and every package's file name |

### Icons

`packaging/make_platform_icons.py` reads `docs/design/identity/icon.svg` and
`icon-small.svg` and writes, at build time, into `<build>/packaging/icons`:

| file | for |
|---|---|
| `Cubeulator.ico` | Windows: 16, 20, 24, 32, 40, 48, 64, 256 px. 16 to 32 are drawn from `icon-small.svg` |
| `Cubeulator.icns` | macOS: 16 to 1024 px, the artwork at 80% on a rounded square of the paper colour |
| `png/icon-<n>.png` | The window icon (Windows and Linux), embedded as a Qt resource |
| `hicolor/...` | Linux: the scalable SVG and 48, 64, 128, 256 px PNGs |

Nothing is drawn by hand. The rasteriser is stdlib-only Python and handles
exactly the vocabulary `docs/design/tools/make_icon.py` emits (rect, polygon,
line, solid fills, round joins and caps); it stops with an error on anything
else rather than render it wrongly. Re-run `make_icon.py` and rebuild, and the
icons follow.

### Windows

- **Version resource** (`Cubeulator.rc.in`): company, product name, file
  description, version, copyright. Properties > Details shows them.
- **AppUserModelID**: the process sets `IainChesworth.Cubeulator` for itself
  at start-up (`desktop_integration.cpp`) and the installer stamps the same
  value on the Start menu shortcut (`StampShortcut.ps1`), so that the
  taskbar groups the pinned shortcut with the running window. If stamping
  fails the installer carries on; the cost is a second taskbar button for a
  shortcut pinned from the Start menu.
- **Installer**: NSIS from CPack, with the icon on the installer, the
  uninstaller and Settings > Apps, a licence page, one Start menu shortcut and
  an uninstaller. It installs for all users and asks for elevation.
- **C++ runtime**: installed beside the executable (`InstallRequiredSystemLibraries`),
  not as windeployqt's 25 MB `vc_redist.x64.exe`, which nothing would run.
- **Short paths**: the staged Qt tree is deep. Run `cpack -B <short dir>` if the
  build directory's own path is long; past 260 characters NSIS cannot open
  the files.

### macOS

- `Info.plist`: identifier, version, category (Utilities), icon,
  `NSCameraUsageDescription`. The camera string is `permission.system.camera`
  in `docs/design/strings.json`, read at configure time, so the wording is
  in one place.
- The menu bar is the one in `platforms.md`, built with Qt Quick Controls'
  native `MenuBar` (`DesktopShell.qml`). About, Settings and Quit are placed in
  the application menu by Qt from their wording; Services, Hide, Hide Others and
  Show All are the system's.

### Linux

- The desktop entry and AppStream metadata are named for the identifier. The
  summary and description come from `strings.json` (`store.summary`,
  `about.what`).
- The `.deb` recommends `fonts-noto-cjk`, and the `.rpm` suggests
  `google-noto-sans-cjk-fonts`, for Japanese and Chinese text on a minimal
  install (`platforms.md`, "Fonts").
- Qt, OpenCV and ONNX Runtime are inside the package, so its dependencies
  are the system libraries Qt's xcb and OpenGL plumbing loads. The list is in
  `cmake/Packaging.cmake`; it is written down, not discovered, because
  dpkg-shlibdeps and rpm's scan would try to resolve the bundled libraries.

## Window manners

All three desktops, from `src/app/desktop/window_geometry.*`:

- Opens at 960 x 680, centred, with a 360 x 560 minimum.
- Remembers size, position and maximised state (QSettings group `window`).
- Ignores a remembered position that is off every screen: unless 100 x 24
  logical pixels of the title bar are on one, the position is dropped and the
  window system places the window. The size is kept, lowered to fit the screen
  and raised to the minimum.
- The geometry is applied as initial properties of the root window, so it is
  created in place and never appears at one size and jumps to another.
- macOS: the window carries the full-screen hint, so the green button offers
  full screen.

## Keyboard shortcuts

`DesktopShell.qml`, from `accessibility.md` "Everywhere". Back (Esc,
Alt+Left, Cmd+[) belongs to the navigation stack.

| | Windows, Linux | macOS | Available |
|---|---|---|---|
| Settings | Ctrl+, | Cmd+, | Except on Settings |
| Help | F1 | Cmd+? | When a Help screen exists (disabled until E3a) |
| New scan | Ctrl+N | Cmd+N | From Home and Complete |
| Full screen | F11 | Ctrl+Cmd+F | Always |
| Quit | Ctrl+Q | Cmd+Q | Always |

Menu items and shortcuts call the navigation functions `AppState.qml` has
today; E2a repoints them. An action with nothing to call yet is disabled, not
absent.

## Keep awake

`KeepAwake` (`src/app/desktop/keep_awake.*`) is a QML singleton with one
read-write property, `active`, and one read-only one, `held`.

```qml
KeepAwake.active = true     // the scan or solve screen is showing
```

| | mechanism |
|---|---|
| Windows | `SetThreadExecutionState(ES_CONTINUOUS \| ES_DISPLAY_REQUIRED)` |
| macOS | `IOPMAssertionCreateWithName(kIOPMAssertionTypeNoDisplaySleep)` |
| Linux | `org.freedesktop.portal.Inhibit` (idle), falling back to `org.freedesktop.ScreenSaver.Inhibit` |

None asks the person for anything. The request is dropped while the app is
not in front and taken up again when it returns, so a forgotten `active =
false` cannot keep a laptop awake from the background. It is wired to nothing
yet: the screens turn it on in E3.

## Signing

Not configured, and no certificate is referenced anywhere (Iain has not
decided). It plugs in at two places:

- **Windows**: after CPack writes the installer, sign it with `signtool`
  from a `CPACK_POST_BUILD_SCRIPTS` script. Until then SmartScreen warns on
  first run.
- **macOS**: sign as `macdeployqt` deploys (`DEPLOY_TOOL_OPTIONS
  -codesign=<identity>` in `cmake/DesktopPackaging.cmake`), then notarise the
  `.dmg` with `notarytool`. Until then Gatekeeper refuses the app without a
  manual override.

Linux packages are unsigned; repository signing is the repository's.

## Not yet

- **Screenshots in the AppStream metadata** come from the system tests'
  screenshot tour (E0a, E6).
- **Translated desktop-entry comments and `InfoPlist.strings`** come with the
  languages (E5).
- **A real About screen** is E3b. Until then About is a placeholder dialog with
  the name and version, reached from the macOS menu and from a row at the end
  of Settings.
- **Flatpak** is later (`platforms.md`).
