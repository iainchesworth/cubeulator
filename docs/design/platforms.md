# Platforms

One shared QML shell; each platform's manners. This says what a person on
each platform expects and what the shell does about it. E4a (desktop) and
E4b (mobile) build it.

Statements about a specific Qt 6.8 API are marked **(verify)**: they are
from memory of the Qt documentation, not from a build, and the session
that builds them must check.

## Summary

### Desktop

| | Windows | macOS | Linux |
|---|---|---|---|
| **Back** | Esc, Alt+Left, mouse back button, on-screen control | Esc, Cmd+[, on-screen control | As Windows |
| **Menus** | None. Modern Windows apps of this size have no menu bar | The system menu bar, native | None |
| **About** | Settings > About Cubeulator | Application menu > About Cubeulator. Also in Settings | Settings > About Cubeulator |
| **Settings** | The Settings button; Ctrl+, | Application menu > Settings...; Cmd+, | The Settings button; Ctrl+, |
| **Help** | The Help button; F1 | Help menu > Cubeulator Help; Cmd+? | The Help button; F1 |
| **Quit** | Close the window; Alt+F4 | Cmd+Q. Closing the window also quits: there is nothing to keep running | Close the window; Ctrl+Q |
| **Camera permission** | No prompt. Windows can block camera access for desktop apps in its privacy settings: `scan.camera-blocked` | The system prompts on first use. `permission.ask` is shown first to explain | No prompt outside a sandbox. In Flatpak, the camera portal asks |
| **Insets** | None | None, except full screen on a notched display: keep the top bar below the notch | None |
| **Window** | Default 960 x 680. Minimum 360 x 560. Size, position and maximised state are remembered; a remembered position that is off every screen is ignored | The same. Full screen is offered (green button, Ctrl+Cmd+F) | The same |
| **Keep awake** | While the scan or solve screen is showing | The same | The same |
| **Sound** | Yes | Yes | Yes |
| **Haptics** | No | No | No |
| **Icon** | `.ico` with 16, 20, 24, 32, 40, 48, 64 and 256 px | `.icns`, 16 to 1024 px | `hicolor` theme: one scalable SVG and 48, 64, 128, 256 px PNGs |
| **Installer** | NSIS installer from CPack (already wired up). Start menu entry, uninstaller, version resource | `.dmg` from CPack. Signed and notarised | `.deb` and `.rpm` from CPack (already wired up), with a desktop entry and AppStream metadata. Flatpak later |

### Mobile

Usable only once mobile capture exists (plan decision 8). Until then a
phone build opens in `home.camera-unsupported` and works by hand entry.

| | Android | iOS |
|---|---|---|
| **Back** | The system back gesture or button; the on-screen control does the same. On `home`, back leaves the app | The on-screen control at the top leading corner; a swipe from the leading edge |
| **Menus** | None | None |
| **About** | Settings > About Cubeulator | Settings > About Cubeulator |
| **Camera permission** | Asked in context, from `permission.ask`. After two refusals Android stops showing its prompt: `permission.refused` offers the system settings | Asked once, from `permission.ask`. After a refusal only the Settings app can change it |
| **Insets** | Edge to edge. Status bar, navigation bar and display cut-outs are insets | Notch or Dynamic Island, home indicator, and the rounded corners in landscape |
| **Orientation** | Both. Follows the device | Both. Follows the device; not upside-down on a phone |
| **Keep awake** | While the scan or solve screen is showing | The same |
| **Sound** | Yes; follows the media volume | Yes; silent when the ring switch is off |
| **Haptics** | Yes | Yes |
| **Language** | Declares its languages to the system, so the per-app language setting (Android 13 and later) works. The in-app choice also works | Declares its languages, so Settings > Cubeulator > Language works. The in-app choice also works |
| **Icon** | Adaptive: a foreground and a background layer, plus a single-colour layer for themed icons | One 1024 px image, no transparency, no rounded corners; dark and tinted variants |
| **Launch** | The system splash screen: the icon on `paper` (light) or its dark value | A launch screen: the mark on `paper`. No text |
| **Package** | `.aab` for the store, `.apk` for testing | Xcode archive. The App Store question is open (plan decision 5) |

## Back

The shell keeps a stack of screens and exposes one `back()` action. The
rules for where it goes are in `journey.md`. Per platform it is bound to:

- the on-screen back control (all platforms; always present);
- Esc (desktop);
- Alt+Left on Windows and Linux, Cmd+[ on macOS; in right-to-left
  layouts Alt+Right;
- the mouse back button (desktop);
- the Android back key and gesture. Qt delivers it as a key event and as
  the window's close request **(verify)**; the shell accepts it unless the
  screen is `home`. Predictive back, where the screen beneath peeks
  through during the gesture, needs the manifest to opt in and Qt to
  support it **(verify)**; if it does not, the plain gesture is acceptable;
- the iOS edge swipe, implemented in the shell as a drag from the leading
  edge. It is an extra: the on-screen control is always there.

Back never quits the app except from `home` on Android, where that is
what Android does.

## Menus (macOS)

Built with Qt Quick Controls' `MenuBar`, whose menus are `Popup.Native`.
*Checked against Qt 6.8.3 (E4a): `Popup.popupType` is new in 6.8 and
`QQuickMenuBar` has a native path (`createNativeMenuBar`, logging category
`qt.quick.controls.nativemenus`). It has not been run on a Mac; if the bar
turns out to sit inside the window, `Qt.labs.platform`'s `MenuBar` is the
fallback.* About, Settings and Quit reach the application menu by their
wording (Qt's text heuristic), and Services, Hide, Hide Others and Show All
are the system's.

| menu | items |
|---|---|
| **Cubeulator** | About Cubeulator · Settings... (Cmd+,) · Services · Hide Cubeulator (Cmd+H) · Hide Others · Show All · Quit Cubeulator (Cmd+Q) |
| **File** | New Scan (Cmd+N) · Enter Colours by Hand (Shift+Cmd+N) · Close Window (Cmd+W) |
| **View** | Enter Full Screen (Ctrl+Cmd+F) |
| **Window** | Minimise (Cmd+M) · Zoom · Bring All to Front |
| **Help** | Cubeulator Help (Cmd+?) |

The wording is in `strings.json`: the menu titles and items are `menu.*`
(File, New Scan, Enter Colours by Hand, View, Window, Settings..., Quit
Cubeulator, Cubeulator Help), and About is `about.title`. The items macOS
supplies itself (Services, Hide, Minimise, Zoom, full screen) take the
system's own translations.

No Edit menu: there is no text to edit. An item that cannot act where the
person is (New Scan during a solve asks nothing; it is simply available
from `home`, `complete` and `history` only) is disabled, not hidden, as
macOS expects of menus.

## Safe areas

Every screen's content sits inside the safe area; backgrounds and the
camera picture run to the edges.

- Insets are added to the page margin, per edge. A 24 px margin beside a
  44 px notch becomes 68 px on that edge only.
- The scan screen's panels and the solve screen's two buttons clear the
  home indicator and the gesture bar by the inset **plus** 8 px. A swipe
  up to go home must not land on **Next turn**.
- Qt 6.9 adds a `SafeArea` type; in 6.8 the window's safe-area margins are
  read through a small helper **(verify)**. The kit hides the difference
  behind `Theme.insets`.

## Orientation

Nothing is locked. The Android manifest already says `unspecified`; keep
it. A phone propped against a mug is as often on its side as upright, and
that is the **short** size class (`system.md`).

Rotating keeps the state, the place in the solve, and focus. The cube
view is told the new size and nothing else.

## Keep awake

The screen must not dim or lock while the person's hands are on the cube.

- **On** while `scan` or `solve` is the current screen and the app is in
  front. **Off** everywhere else, when a sheet has been open for two
  minutes, and when the app goes to the background.
- There is no cross-platform Qt API for this *(checked against Qt 6.8.3's
  headers, E4a: there is no public one)*. A `KeepAwake`
  helper, one implementation per platform: a keep-screen-on window flag on
  Android; the idle-timer switch on iOS; a display-required execution
  state on Windows; a no-display-sleep power assertion on macOS; the
  session's inhibit interface on Linux.
- It asks for nothing the person must approve on any of the five.

## Sound and haptics

Both confirm something the person may not be looking at: during a scan
their eyes are on the cube and the camera, during a solve on the cube.

| event | sound | haptic (phones) |
|---|---|---|
| A side is read | A soft tick | A light tap |
| The cube is read | Two rising notes | A double tap |
| A turn is advanced | A softer tick. None in timer mode, where the turn arrives by itself: a low tick marks the new turn instead | A light tap |
| A stage is done | One note | A medium tap |
| Solved | Three rising notes | A success pattern |
| The scan is in conflict; the camera fails | Nothing. An error is not scolded | Nothing |

- Each is under 300 ms and quiet. No music, no voice.
- **Sound** and **Vibration** are separate switches in Settings > Solving,
  both on by default. Sound obeys the device's volume and silent switch.
- The sounds are made for the app and licensed with it. E4 commissions or
  synthesises them; until then the switch is not shown.
- Qt has no haptics API in Qt 6 **(verify)**; a small helper calls the
  platform's own.

## Text size, motion, theme, language: where the system's setting comes from

| setting | Windows | macOS | Linux | Android | iOS |
|---|---|---|---|---|---|
| **Text size** | Settings > Accessibility > Text size | None system-wide; the in-app setting is the only one | The desktop's text-scaling factor | Display > Font size | Dynamic Type |
| **Reduced motion** | Accessibility > Visual effects > Animation effects | Accessibility > Display > Reduce motion | The desktop's animations switch | Accessibility > Remove animations | Accessibility > Motion > Reduce Motion |
| **Dark theme** | Personalisation > Colours | Appearance | The desktop's colour scheme | Display > Dark theme | Display & Brightness |
| **Language** | Display language | Language & Region, and per app | The session's locale | System languages, and per app | Per app, in Settings |

Each is read by a small helper like the existing `SystemTheme`, exposes a
property that changes live, and has an override in the app's Settings.
Where a platform has no such setting, the helper reports the neutral
value.

## Fonts

| script | font | shipped |
|---|---|---|
| Latin | Archivo; IBM Plex Mono | In the app. About 400 KB |
| Hebrew | Noto Sans Hebrew (variable, weights 100-900) | In the app. About 110 KB |
| Yiddish | Noto Sans Hebrew, the same file | In the app |
| Arabic | Noto Sans Arabic (variable, weights 100-900) | In the app. About 825 KB |
| Japanese | The system's: Yu Gothic UI on Windows, Hiragino Sans on macOS and iOS, Noto Sans CJK JP on Android and Linux | No |
| Simplified Chinese | The system's: Microsoft YaHei UI, PingFang SC, Noto Sans CJK SC | No |

- A Japanese or Chinese font is 5-10 MB a weight. Every target ships a
  good one except a minimal Linux install, where the `.deb` and `.rpm`
  recommend the Noto CJK package and the Flatpak uses the host's fonts.
- Japanese and Chinese share code points but not shapes. The font family
  is chosen by the app's language, not left to fallback, so that Japanese
  text is not drawn with Chinese forms.
- All bundled fonts are under the SIL Open Font Licence and are listed in
  the generated notices (E0b).
- The two Noto files are the ones ICL Forge, Countdown Solver and Automate
  already bundle (`family.md` F4). A variable font gives weight 800 for
  headings; E2a checks that Qt draws it on every desktop OS.

## Identity each platform needs

| | value | status |
|---|---|---|
| Display name | Cubeulator | Fixed |
| Identifier (Android package, iOS and macOS bundle id, Linux app id and desktop file name) | `com.iainchesworthlabs.cubeulator` | *(Iain, 2026-10-13, after the repository moved to `iainchesworthlabs`.)* The GitHub account and the repository name in lower case, with `com.` as on ICL Forge's macOS apps. One id on every platform. It replaces `org.qtproject.example` and must not change after the first store release |
| Windows AppUserModelID | `IainChesworthLabs.Cubeulator` | Follows the publisher. Set by the app at start-up and stamped on the installer's Start menu shortcut (E4a), so that the taskbar groups the button with its shortcut |
| Version | From the build (`GenerateVersion.cmake`), shown in About and in every package | In the Windows resource, `Info.plist`, the AppStream release and every desktop package's name (E4a). Not yet in the Android manifest (E4b) |
| Publisher | Iain Chesworth Labs | From `Packaging.cmake`. The copyright line is "Copyright © 2026 Iain Chesworth" (`family.md` D1). One variable, `CUBEULATOR_PUBLISHER` in `cmake/AppIdentity.cmake`, read by the version resource, the AppStream developer and CPack |
| Category | Windows, macOS, iOS: Utilities. Android: Tools. Linux: `Utility;Game;LogicGame;` | Proposed |
| Camera usage string (iOS, macOS) | `permission.system.camera` in `strings.json`, in all nine languages | Written |
| Code signing | Windows: unsigned installers trip SmartScreen. macOS: unsigned apps will not open without a workaround. Both need a certificate | **Open**, for E4a and E6 |

## Per platform, the things that are easy to miss

**Windows**

- The title bar follows the app theme, not just the system's.
- High-contrast themes: the app keeps its own palette (it already meets
  AA) but honours the system's focus-visible and animation settings.
- The version resource carries the product name, version, company and
  copyright, so that Properties > Details and the installer agree.

**macOS**

- The camera prompt's text comes from `Info.plist`, translated through
  `InfoPlist.strings`.
- The Dock icon follows macOS's shape: the artwork sits on a rounded
  square with the standard margin (`identity/README.md`).

**Linux**

- A desktop entry with `Name`, translated `Comment`, `Icon`, `Categories`
  and `Keywords` (cube; rubik; solver; puzzle).
- AppStream metadata with a summary, description, screenshots from the
  system-test tour and a content rating. Software centres will not list
  it otherwise.
- X11, and Wayland through XWayland (the package carries Qt's `xcb` platform
  plugin only; native Wayland is not packaged yet, `packaging.md`). The window's
  app id matches the desktop file name.

**Android**

- Targets the current API level, which makes edge-to-edge mandatory.
- The camera permission is declared `required="false"` for the camera
  feature, so that devices without one can still install and use hand
  entry.
- Back, rotation, a split-screen resize and a language change must not
  restart the solve. The solve state lives outside the activity's view.

**iOS**

- Supports Dynamic Type through `Theme.textScale`.
- No exit control anywhere.
- The App Store and the GPL: unresolved (plan decision 5). Nothing in this
  design depends on the answer.
