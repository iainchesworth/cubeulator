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
| Linux | `Cubeulator-<version>-linux-x64.deb` and `.rpm` | CPack `DEB`, `RPM` | `/usr/lib/cubeulator` (the app, the Qt it carries, and Qt's translations), `/usr/bin/cubeulator`, and the desktop entry, AppStream metadata, icons and licence under `/usr/share` |

The browser build is not packaged by CPack: it is a directory of static files
(`build/web-emscripten-release/bin`), which `docs.yml` serves under `/app/` and
`release.yml` zips as `Cubeulator-<version>-web.zip` with `LICENSE.txt`,
`THIRD-PARTY-NOTICES.txt` and `HOSTING.txt` (`packaging/web/make-package.sh`).
Because there is no installer to refuse an unresolved notice, the script reads
the build's inventory and refuses one itself; `packaging/web/check-package.sh`
then holds what is in the zip: the files the page loads, a module that is
WebAssembly and threaded, the notices and the licence. See
[Try it in your browser](web.md#hosting-it-yourself).

A plain `cpack` with no `-G` also writes a `.zip` (and, on Linux, a `.tar.gz`);
CI and the release name the generators they want, and only the packages in the
table are published.

CI builds each one on its own runner and uploads it as a build artifact
(`.github/workflows/ci.yml`, job `build-and-test`). CI publishes nothing: a
release is cut by tagging, through `.github/workflows/release.yml` (see
[Cutting a release](releasing.md)).

### Publishing

The same packages, built from a version tag by the `Release` workflow, are
checksummed (`SHA512SUMS`), signed with the project's GPG key, attested and
attached to a draft GitHub Release with notes rendered from `CHANGELOG.md`. See
[Cutting a release](releasing.md). Both CI and the release run
`packaging/check-package.sh` on what CPack wrote, which looks inside each package
for Qt, the executable, the icon, the identifier and the notices.

The package names carry the pre-release label (`Cubeulator-0.1.0-beta.1-...`):
`cmake/Packaging.cmake` names them from `CUBEULATOR_VERSION_FULL`, and the Linux
formats say it their own way (`0.1.0~beta.1`, rpm release `0.beta.1`) so that a
beta sorts before its final release.

### Building one locally

Configure and build as usual, then package:

```
cmake --preset windows-msvc-release
cmake --build --preset windows-msvc-release
cpack --config build/windows-msvc-release/CPackConfig.cmake -G NSIS
```

CPack writes the package, and its staging tree, to the directory it is run
from (CI runs it from the repository root and looks for the package there).
Qt's QML tree is deep: if the staging path passes 260 characters NSIS cannot
read it, so on Windows run from a short directory or pass `-B C:\cpk`.

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
| `cmake/DesktopPackaging.cmake` | What the desktops share: the icons, the window-icon resource, the install and Qt-deployment helpers, wording from the string inventory |
| `cmake/Packaging.cmake` | CPack: the file name, vendor and licence; each platform adds its generators |
| `src/app/platform/<os>/platform.cmake` | The platform's hooks into all of the above: `cube_platform_configure_packaging()`, `cube_platform_configure_desktop_app()`, `cube_platform_configure_desktop_shell()` (see [Platform isolation](platform-isolation.md)) |
| `packaging/<os>/Packaging.cmake` | That platform's CPack generators and fields |
| `packaging/<os>/ConfigureApp.cmake` | That platform's executable name, version resource or bundle, install rules and Qt deployment |
| `packaging/make_platform_icons.py` | SVG to `.ico`, `.icns`, PNG and `hicolor` files |
| `packaging/windows/Cubeulator.rc.in` | Icon and version resource |
| `packaging/windows/StampShortcut.ps1` | Puts the AppUserModelID on the Start menu shortcut |
| `packaging/macos/Info.plist.in` | `Info.plist`, including the camera usage string |
| `packaging/linux/*.in` | Desktop entry and AppStream metadata |
| `src/app/interfaces/desktop_shell.hpp` | What a platform supplies the shell: keep awake, process identity, window traits |
| `src/app/platform/<os>/desktop_shell_<os>.cpp` | One implementation of it per platform |
| `src/app/desktop/` | Window geometry, `KeepAwake` and the identity step, all platform-neutral |
| `src/app/qt/DesktopShell.qml` | Shortcuts, the macOS menu bar, the placeholder About |

### Identity

| | value |
|---|---|
| Display name | Cubeulator |
| Identifier | `com.iainchesworthlabs.cubeulator`, on every platform (Iain, 2026-10-13, after the repository moved to `iainchesworthlabs`; it follows the account; until then it had the `org` prefix and the old account): the macOS bundle id, the Linux app id and desktop file name, later the Android package and iOS bundle id |
| Publisher | `CUBEULATOR_PUBLISHER` in `cmake/AppIdentity.cmake`: "Iain Chesworth Labs", following the repository's account. One string in the Windows version resource, the AppStream developer and CPack's vendor and contact. The copyright line (`CUBEULATOR_COPYRIGHT_HOLDER`) stays in Iain's own name: "Copyright (C) 2026 Iain Chesworth" |
| Windows AppUserModelID | `IainChesworthLabs.Cubeulator`, following the publisher |
| Executable | `Cubeulator.exe`, `Cubeulator.app`, `cubeulator` |
| Version | `project(VERSION)` and `CUBEULATOR_VERSION_PRERELEASE` in the top-level `CMakeLists.txt` (set together by `tools/release/prepare_release.py`, see [Cutting a release](releasing.md#versions)), through `cmake/GenerateVersion.cmake`: in `cube/version.hpp`, the Windows version resource, `Info.plist` (without the pre-release label), the AppStream release and every package's file name |

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
- **AppUserModelID**: the process sets `IainChesworthLabs.Cubeulator` for itself
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
- Qt and OpenCV are inside the package, so its dependencies are the system
  libraries Qt's xcb and OpenGL plumbing loads, and a few its own libraries
  link (GLVND's `libOpenGL`, GSSAPI through Qt6Network). The list is in
  `packaging/linux/Packaging.cmake`; it is written down, not discovered,
  because dpkg-shlibdeps and rpm's scan would try to resolve the bundled
  libraries. A list written by hand drifts, so
  `packaging/linux/check-depends.sh` (run by `check-package.sh`, so by CI and by
  the release) unpacks the `.deb`, runs `ldd` over every ELF in it and fails for
  any system library whose package the `Depends` neither names nor pulls in.
  It found two the first dry run's package lacked: `libopengl0` and
  `libgssapi-krb5-2`. The `.rpm` list is written to match, with Fedora's names
  (`libglvnd-opengl`, `krb5-libs`). **Only the `.deb`'s `Depends` is
  machine-checked**: nothing compares the `.rpm` list with the files (an
  `rpm -qp --requires` comparison in a Fedora container job would), and it is
  the list that drifts first. It has also only been named for Fedora: another
  RPM distribution calls these packages something else.
- **X11, and Wayland through XWayland.** The only platform plugin carried is
  `xcb`. A native Wayland session runs the app as an X client, which every
  mainstream desktop provides; a Wayland-only session without XWayland does not
  start it. Qt's Wayland client libraries and shell integrations, which
  deployment copies by default, are removed so that the package does not carry
  libraries nothing loads. Native Wayland is a plugin and its dependencies to
  add, not a switch.
- **Qt's translations are in `/usr/lib/cubeulator/translations`**, and the
  `qt.conf` beside the executable says so (Qt's deployment writes the file
  without a `Translations` entry, which would have put them in
  `/usr/translations`, a directory in `/usr` that is nobody's and that any other
  Qt application packaged the same way would collide with).
- **Left out on purpose**, by `packaging/linux/ConfigureApp.cmake`: the GTK 3
  platform theme (it links GTK, Cairo, Pango and ATK, which a KDE or minimal
  desktop lacks and the package should not require; Qt uses the desktop-portal
  theme, which is carried), and the QML debugger and profiler servers
  (`plugins/qmltooling`; also left out on Windows and macOS).
- The desktop entry has one main category, `Game`, with `LogicGame`; the
  AppStream metadata names the same, which AppStream requires, and marks a
  pre-release `type="development"`. `check-package.sh` validates both
  (`desktop-file-validate`, `appstreamcli validate`).

## Window manners

All three desktops, from `src/app/desktop/window_geometry.*`, and only where
the platform says the window is the person's to keep
(`Traits::remembers_window_geometry`; a phone's is not):

- Opens at 960 x 680, centred, with a 360 x 560 minimum.
- Remembers size, position and maximised state (QSettings group `window`).
- Ignores a remembered position that is off every screen: unless 100 x 24
  logical pixels of the title bar are on one, the position is dropped and the
  window system places the window. The size is kept, lowered to fit the screen
  and raised to the minimum.
- The geometry is applied as initial properties of the root window, so it is
  created in place and never appears at one size and jumps to another.
- macOS: the window carries the full-screen hint, so the green button offers
  full screen (`Traits::fullscreen_button_hint`, which only macOS sets).

## Keyboard shortcuts

`DesktopShell.qml`, from `accessibility.md` "Everywhere". Back (Esc,
Alt+Left, Cmd+[) belongs to the navigation stack.

| | Windows, Linux | macOS | Available |
|---|---|---|---|
| Settings | Ctrl+, | Cmd+, | Except on Settings |
| Help | F1 | Cmd+? | When a Help screen exists (disabled until E3a) |
| New scan | Ctrl+N | Cmd+N | From Home and Complete |
| Enter by hand | Shift+Ctrl+N | Shift+Cmd+N | From Home and Complete, when hand entry exists (disabled until R3) |
| Full screen | F11 | Ctrl+Cmd+F | Always |
| Quit | Ctrl+Q | Cmd+Q | Always |
| Close window, Minimise | - | Cmd+W, Cmd+M | macOS only, in its menu bar |

Menu items and shortcuts call the navigation functions `AppState.qml` has
today; E2a repoints them. An action with nothing to call yet is disabled, not
absent.

## Keep awake

`KeepAwake` (`src/app/desktop/keep_awake.*`) is a QML singleton with one
read-write property, `active`, and one read-only one, `held`. The mechanism is
the platform's own, behind `interfaces/desktop_shell.hpp`.

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

Two different things are called signing here.

**What the release does already** (`release.yml`, [Cutting a release](releasing.md)):
a SHA-512 manifest, a detached GPG signature on it and on every package from the
project's key, a build-provenance attestation for each package and an SBOM. They
prove where a file came from. The operating system does not look at them.

**What the operating system looks at is not configured**, and no certificate is
referenced anywhere (Iain has not decided; both cost an account and a yearly
fee, and neither can be tried without one). It plugs in at two places:

- **Windows**: after CPack writes the installer, sign it with `signtool`
  from a `CPACK_POST_BUILD_SCRIPTS` script. Until then SmartScreen warns on
  first run.
- **macOS**: the bundle is signed ad hoc by default (Apple Silicon kills an
  unsigned process, so this is the floor). Pass
  `-DCUBEULATOR_MACOS_CODESIGN_IDENTITY="Developer ID Application: ..."` to
  sign for real; the hardened runtime and timestamp it adds are what
  notarisation requires. Then notarise the `.dmg` with `notarytool`. Until
  then Gatekeeper wants the right-click > Open workaround.

Linux packages are unsigned; repository signing is the repository's.

## Not yet

- **An elevated Windows install.** The installer's contents are checked
  (`check-package.sh`, and the extracted tree was read for the version
  resource, the licence and the notices), and the extracted tree runs; the
  install itself, the Start menu shortcut with its AppUserModelID, and the
  uninstaller have not been run on a machine from a release's installer.
- **A macOS package opened on a Mac by a person.** The bundle's structure, its
  identifier, its camera usage string, its version and its ad-hoc signature are
  checked in CI on a Mac runner; nobody has launched the `.dmg`'s app on
  hardware.
- **A Linux package installed on a clean machine.** Its dependencies are checked
  (above) and the unpacked tree starts under Xvfb with Qt's xcb plugin; a real
  `apt install` on a minimal system has not been done.
- **Native Wayland**, as above.
- **RPM distributions other than Fedora.**

- **Screenshots in the AppStream metadata** come from the system tests'
  screenshot tour (E0a, E6).
- **Translated desktop-entry comments and `InfoPlist.strings`** come with the
  languages (E5).
- **A real About screen** is E3b. Until then About is a placeholder dialog with
  the name and version, reached from the macOS menu and from a row at the end
  of Settings.
- **Flatpak** is later (`platforms.md`).
