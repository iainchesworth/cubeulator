# Licensing: inventory, the Qt finding, options

Written 2026-10-13 for workstream E0b of the
[app experience plan](app-experience-plan.md). Status: **reviewed; one decision
taken, see below.**

> **Decision (Iain, 2026-10-13): skip the iOS App Store and the Mac App Store
> for now and deal with them later.** Cubeulator stays GPL-3.0-or-later and
> ships on the desktop (direct download, Linux packages) and, when mobile
> capture exists, Google Play and F-Droid. This is Option A below. Nothing in
> the build or the notices changes: the Apple-store question stays open in
> [`LICENSE-NOTE.md`](../LICENSE-NOTE.md) and returns when an Apple store
> becomes a goal. It is the only reason a Qt commercial licence (Option B)
> would be needed.

This is an engineering inventory and a decision paper. It is not legal advice,
it does not change Cubeulator's licence (GPL-3.0-or-later), and it does not
settle the question [`LICENSE-NOTE.md`](../LICENSE-NOTE.md) leaves open. That
decision is Iain's.

How to read the evidence marks:

- **[V]** verified against a primary source, cited inline.
- **[I]** inferred - from the verified facts, from practice, or from a
  source that is not authoritative. Treat as a lead, not a finding.
- **[L]** a lawyer would need to confirm before anyone relies on it.

## 1. Summary

1. **The plan's Qt reading holds for Qt Quick 3D, and is wrong for Qt Shader
   Tools.** Qt Quick 3D is offered only as *Commercial or GPL-3.0-only*;
   there is no LGPL option [V]. Qt Shader Tools' *runtime library* is offered
   under LGPL-3.0 (or GPL-2.0/GPL-3.0, or commercial); only its `qsb`
   command-line tool is GPL-3.0-only, and that tool is a build-time program
   that does not ship [V]. See [section 3](#3-the-qt-question).
2. **Cubeulator's own code is not the obstacle.** Iain is the only author in
   the history (three identities, one person) [V: `git shortlog -sne`], so he
   can add a store exception, dual-license, or re-license it. The obstacle is
   that one Qt module the app links, Quick 3D, reaches the user under
   GPL-3.0-only and its copyright is The Qt Company's, not Iain's. A store
   exception on Cubeulator's own code does not change the terms Quick 3D's
   code arrives under [I][L].
3. **Qt is the only copyleft dependency that matters for the stores.** Every
   other shipped third-party component is permissive (MIT, BSD, Apache-2.0,
   Zlib, ISC, libpng, IJG), file-level weak copyleft (MPL-2.0: Eigen), or
   notice-only [V: inventory below]. On Linux an OpenCV feature pulls in
   vcpkg's GTK3 chain, which is LGPL (see
   [section 2.4](#24-vcpkg-ports)); that is avoidable.
4. **Routes for the App Store**, in the order this paper recommends them (full
   analysis in [section 5](#5-options)): (a) do not put iOS on the first
   release, and take the decision with a quote in hand; (b) a Qt commercial
   licence - Qt for Small Business is EUR 546 per developer per year while
   revenue stays under 1 MEUR/USD [V]; (c) draw the cube without Quick 3D,
   which moves the question from GPL to LGPL but does not remove it, because
   The Qt Company's own page says app stores may have rules that conflict
   with the LGPL [V]. Desktop and Google Play are not blocked by anything
   found here [I][L].
5. **The notices are generated, not written.** `THIRD-PARTY-NOTICES.txt` and a
   structured JSON are produced by the build from the app's resolved link
   line, vcpkg's installed tree and two checked-in manifests; a CTest case
   fails when something that ships has no notice
   ([section 6](#6-how-the-notices-are-produced)).
6. **Two things in the plan to correct** ([section 8](#8-things-in-the-plan-that-were-wrong-or-missing)):
   Qt Shader Tools, and the claim that iOS Qt linking is static (not
   confirmed by the Qt documentation I could read).

## 2. Inventory

Version numbers are from the Windows vcpkg install plan at the pinned baseline
(`5b698638`, 2026-10-13) and Qt 6.8.3. "Ships" means "is part of an installed
or distributed app on that platform"; the generated notices file is the
authoritative per-build answer, this table explains it.

Platform key: **W** Windows, **M** macOS, **L** Linux, **A** Android,
**I** iOS. Today the app target does not build for iOS at all (the
`ios-clang-*` presets set `CUBEULATOR_BUILD_APP=OFF`) [V:
`CMakePresets.json`], so an **I** below describes what *would* ship.

### 2.1 Cubeulator itself

| component | licence | notes |
|---|---|---|
| Cubeulator | GPL-3.0-or-later [V: `LICENSE`, `vcpkg.json`] | Sole author Iain Chesworth [V: `git shortlog`]. First commit 2026-07-16, so the copyright line is "Copyright (C) 2026 Iain Chesworth". Once Quick 3D is linked the *combined binary* can only be conveyed under GPL-3.0 (Quick 3D is `-only`); the source files remain "or later". |
| `src/app/qt/shaders/*.frag` | first-party | Written against Qt's custom-material model; no Qt code copied [V: file headers]. |

### 2.2 Qt 6.8.3

Per-module SPDX expressions below are from the SBOMs Qt ships in its own
install (`C:/Qt/6.8.3/msvc2022_64/sbom/*.spdx.json`) and agree with the
`SPDX-License-Identifier` lines in the installed headers [V]. The Qt Company's
documentation says the same for Quick 3D and Shader Tools (see section 3).

| module (as linked or imported) | linked by | offered under | ships |
|---|---|---|---|
| Qt Core, GUI, Concurrent, Network, OpenGL | `cube_app*`; Network and OpenGL are dependencies of Qml and Quick | Commercial OR LGPL-3.0 OR GPL-2.0 OR GPL-3.0 | W M L A I |
| Qt Qml, Quick, Quick Controls 2 (+ Templates 2), Labs Settings | `cube_app`, QML imports | same | W M L A I |
| **Qt Quick 3D** (Quick3D, Quick3DRuntimeRender, Quick3DUtils, Quick3DHelpers, Quick3DAssetImport) | `cube_app`; `import QtQuick3D`, `QtQuick3D.Helpers` in `Cube3DView.qml` | **Commercial OR GPL-3.0-only** | W M L A I |
| Qt Shader Tools (runtime library) | pulled in by Quick 3D (`Quick3DRuntimeRender` DEPENDS_ON `ShaderTools`) [V: SBOM]; the custom materials bake `*.frag` at run time | Commercial OR LGPL-3.0 OR GPL-2.0 OR GPL-3.0 | W M L A I |
| `qsb` (the shader baker), `moc`, `rcc`, `uic`, `windeployqt`, `androiddeployqt` | build tools | Commercial OR GPL-3.0 **with the Qt GPL exception 1.0** | no - build time only; the exception covers their output |
| Qt EntryPoint (`Qt6EntryPoint.lib`, Windows `WinMain` shim) | `WIN32_EXECUTABLE` | Commercial OR BSD-3-Clause | W |
| Qt Svg (+ `qsvg`, `qsvgicon` plugins) | deployed by `windeployqt`, not linked or imported [V: found by the notices test on the real deploy directory] | Commercial OR LGPL-3.0 OR GPL-2.0 OR GPL-3.0 | W (and wherever `windeployqt`/`androiddeployqt` add it) |
| Quick Controls 2 styles (Basic, Fusion, Imagine, Material, Universal, FluentWinUI3, Windows) and the Qt platform, image-format, TLS and network-information plugins | deployed by `windeployqt` because `QtQuick.Controls` is imported | same LGPL set; the Material style carries an MIT attribution (Angular Material shadows); the Windows platform plugin carries wintab's `LicenseRef-Lcs-Telegraphics` | W |

The Qt Company's bundled third-party code (zlib, libpng, harfbuzz, freetype,
PCRE2, double-conversion, md4c, tinycbor, Unicode CLDR, glslang, SPIRV-Cross,
Embree, tinyexr, xatlas, meshoptimizer, and several smaller attributions) is
listed per module in the SBOMs. The build reads a condensed snapshot
(`third_party/notices/qt-modules.json`) and emits one notice per component
reached from the modules above. Platform differences seen when comparing the
Windows and Android SBOMs (both read here): Embree, D3D12 memory allocator,
the Windows zlib wrappers and wintab are Windows-only; `forkfd` is
Android-only [V]. The macOS, Linux and iOS Qt builds were **not** available
to read, so on those platforms the snapshot over-includes rather than
guesses; `tools/licences/qt_sbom_manifest.py` re-derives it from any Qt
install's `sbom/` directory [I].

Obligation summary for the Qt set, *as distributed under GPL-3.0-only*: the
combined work is conveyed under GPL-3.0 with its Corresponding Source (which
includes the Qt libraries shipped with it, since they are not System
Libraries) or a written offer, the licence text, and no further restrictions
(GPLv3 sections 5(c), 6, 10 [V: `LICENSE` lines 208-260, 463]).

### 2.3 Vendored, models, design assets

| component | licence | where the text lives | ships | obligation |
|---|---|---|---|---|
| MediaPipe palm detection model (`palm_detection_mediapipe_2023feb_int8.onnx`) | Apache-2.0 [V: OpenCV Zoo README: "All files in this directory are licensed under Apache 2.0 License"] | `assets/models/palm_detection_mediapipe_LICENSE.txt` (an **unfilled** Apache template: `Copyright [yyyy] [name of copyright owner]`) | W M L A I (embedded as `:/models/`) | licence copy; state changes (none); the repo does not record the copyright holder or the download URL - add them |
| Lucide icon shapes (`Icon.qml`: camera, globe, menu, sun, refresh) | ISC, (c) Lucide Icons and Contributors [V: Lucide `LICENSE`] | `licences/ISC.txt` + the copyright line | all | notice must appear in all copies |
| Feather-derived shapes (chevron-left, chevron-right, check, alert-triangle, help-circle) | MIT, (c) 2013-present Cole Bemis [V: same file lists these as Feather-derived] | `licences/MIT.txt` + the copyright line | all | notice |
| Archivo, IBM Plex Mono, Noto Sans Hebrew, Noto Sans Arabic (bundled by the kit under `src/app/qt/fonts/` on `feature/ax-kit`) | OFL-1.1 [V: each family's OFL text, vendored by the kit] | the kit's `OFL-<family>.txt` beside the fonts; the manifest's `licenseFile` points at it | yes, once the kit merges (the entries are dormant until the files exist) | OFL: licence text travels with the fonts, no sale of the fonts alone, reserved font names ("Plex"). Copyright lines are copied from the OFL texts: Archivo (c) 2020 The Archivo Project Authors; IBM Plex Mono (c) 2017 IBM Corp.; Noto Sans Hebrew and Arabic (c) 2022 The Noto Project Authors. **Any font file under `assets/` or `src/app/qt/` that no emitted manifest component claims is an unresolved error**, so a new font cannot ship unnoticed |

The vendored M3T tracker (MIT, with `filesystem.h`, nanoflann and
tiny_obj_loader inside it) and the corner-detector weights
`cube_corner_pose.onnx` (AGPL-3.0, derived from Ultralytics YOLO11n-pose, never
distributed) were listed here until recovery plan R6 removed the tracking
pipeline. Nothing embeds or links either now. The weights stay in `.gitignore`;
a copy left in an old checkout is a file under `assets/` that no component
claims, which the generator reports as unresolved.

### 2.4 vcpkg ports

Windows list at the baseline; licences from each port's `vcpkg.json`, with the
text from its installed `share/<port>/copyright` [V]. Ports marked **[T]** are
tools/tests only.

| port | version | SPDX | ships | why / obligation |
|---|---|---|---|---|
| opencv4 | 4.12.0#5 | Apache-2.0 (port says `null`; the copyright file is Apache-2.0 plus SoftFloat and a CHI-table notice) | W M L A | Apache: licence copy; keep the bundled notices in its copyright file. Features: calib3d, core, fs, highgui, intrinsics, jpeg, png, quirc, thread, tiff, webp, + dshow/msmf/win32ui (W), v4l/gtk (L) |
| libjpeg-turbo | 3.2.0 | BSD-3-Clause (port); the file also carries the IJG licence | all | notice in docs |
| libpng | 1.6.58 | libpng-2.0 | all | notice; no misrepresentation |
| tiff | 4.7.2 | libtiff | all | notice |
| libwebp | 1.6.0#2 | BSD-3-Clause | all | notice |
| liblzma | 5.8.3 | 0BSD (liblzma) | all | none (0BSD); the copyright file notes LGPL getopt only for the CLI tools, which are not built |
| zlib | 1.3.2#1 | Zlib | all | none for binaries beyond not misrepresenting |
| quirc | 1.2 | ISC | all | notice |
| onnxruntime | 1.23.2#1 | MIT | W M L A I (removed by R6) | notice |
| onnx (overlay port) | 1.19.0#1 | Apache-2.0 | with onnxruntime | licence copy |
| abseil, flatbuffers, dlpack | 20260107.1#3, 25.12.19, 1.3 | Apache-2.0 | with onnxruntime | licence copy |
| protobuf, utf8-range | 6.33.4 | BSD-3-Clause, MIT | with onnxruntime | notice |
| re2, cpuinfo | 2025-11-05, 2026-04-13 | BSD-3-Clause, BSD-2-Clause | with onnxruntime | notice |
| eigen3 | 5.0.1 | MPL-2.0 (with BSD-3 and others in the file) | with onnxruntime | MPL file-level copyleft: unmodified, so the duty is to say where the source is |
| boost-config, boost-mp11, boost-headers | 1.91.0 | BSL-1.0 | with onnxruntime | none for machine-executable object code (BSL-1.0 text) |
| date, cxxopts, nlohmann-json, ms-gsl, safeint, optional-lite, wil (W only) | various | MIT / MIT / MIT / MIT / MIT / BSL-1.0 / MIT | with onnxruntime | notice |
| fmt | 12.2.0 | MIT | all (`cube_lib`) | notice |
| bgfx (+ libsquish, lodepng, mgnlibs, miniz, stb, tinyexr) | 1.129.8940-496#1 | BSD-2-Clause AND CC0-1.0 | **no - nothing links it** [V: the file API's link line of `cube_app` has no bgfx; `src/render` is a stub] | none until the render pipeline links it; the generator will start emitting it the day it does |
| catch2 **[T]** | 3.15.2 | BSL-1.0 | no | tests only |
| vcpkg-cmake, vcpkg-cmake-config, vcpkg-boost, boost-cmake, boost-uninstall, vcpkg-get-python-packages | - | - | no | install nothing outside `share/`; the generator skips ports with no `include/`, `lib/` or `bin/` content |

**Linux-specific finding [V: port manifests].** `vcpkg.json` asks for
`opencv4[highgui]` and, on Linux, `opencv4[gtk]`; the `gtk` feature makes vcpkg
build `gtk3` and a chain of about 45 ports: GTK3, GLib, Pango, ATK, gdk-pixbuf,
fribidi, gettext (all LGPL-2.0/2.1+), Cairo (LGPL-2.1 or MPL-1.1), D-Bus
(AFL-2.1 or GPL-2.0+), plus the X11 libraries. The only code in the repository
that included `highgui` was the vendored M3T, which R6 has removed; the app and
`cube::lib` do not use it. **Recommendation: drop the `highgui` and `gtk` features
from `vcpkg.json` now that M3T has gone** (not done yet). That deletes an LGPL
toolkit and ~45 ports from every Linux package. The generator will list
whatever the build does include on Linux; I could not run a Linux build here.

### 2.5 Per-platform differences that change obligations

| platform | what differs |
|---|---|
| Windows | Qt DLLs and vcpkg DLLs ship beside the exe (`windeployqt`, vcpkg applocal). On the 2026-10-13 build the deployment directory held 41 Qt modules and 45 Qt plugins and QML plugins, all attributed, **plus four binaries that are neither Qt's nor vcpkg's**: `d3dcompiler_47.dll` and `dxcompiler.dll` + `dxil.dll` (Microsoft's DirectX shader compilers, redistributable under the Windows SDK terms [I]) and `opengl32sw.dll` (Mesa llvmpipe software OpenGL, MIT-family [I]). `windeployqt` copies them by default. They are reported as a generator warning, not an error, because they are not linked dependencies. **Decision for E4a/Iain:** drop them with `windeployqt --no-opengl-sw --no-system-d3d-compiler --no-system-dxc-compiler` (the app does not need a software GL fallback or the DirectX compilers if the graphics backend is Direct3D 11/OpenGL on a real GPU), or keep them and add notices - `opengl32sw.dll` needs a Mesa notice, which I could not source locally. The `windeployqt` call is not in my lane to change. |
| macOS | Not built or verified here. Mac App Store adds Apple's terms (section 4). |
| Linux | GTK3 chain via OpenCV (above). Qt is installed from `jurplel/install-qt-action` in CI; the DEB/RPM packages set `SHLIBDEPS`/`AUTOREQPROV`, so Qt may be a dependency of the package rather than part of it - then Qt's source/notice duty moves to the distribution that provides it [I][L]. |
| Android | Qt is shared (`.so` inside the APK/AAB) so LGPL relinking is mechanically possible [I]. The camera backend is a stub today. |
| iOS | The app does not build today. Qt for iOS is normally linked statically [I - **not stated** in the Qt iOS documentation pages I read; Qt's own tracker has an open request to allow dynamic builds, QTBUG-85974]. |

## 3. The Qt question

**Claim under test (plan, decision 5):** Cubeulator links Qt Quick 3D and Qt
Shader Tools, which Qt publishes under GPLv3 or a commercial licence, not
LGPL.

### 3.1 Qt Quick 3D - holds

Four independent sources agree:

1. **Qt's licensing page, Qt 6.12 documentation.** The section "Modules
   available under GNU General Public License v3" lists Qt Quick 3D (and Qt
   Canvas Painter, CoAP, Graphs, GRPC, HTTP Server, Lottie Animation, MQTT,
   Network Authorization, Qml Compiler, Quick 3D Physics, Quick Timeline,
   Virtual Keyboard, Wayland Compositor). <https://doc.qt.io/qt-6/licensing.html> [V]
2. **The module's own page, Qt 6.12 and 6.8.** "Qt Quick 3D is available under
   the commercial license from The Qt Company. In addition, it is available
   under the GNU General Public License, version 3." LGPL is not mentioned.
   <https://doc.qt.io/qt-6/qtquick3d-index.html>,
   <https://doc.qt.io/qt-6.8/qtquick3d-index.html> [V]
3. **The 6.8.3 source headers.** 59 of the 60 headers in `include/QtQuick3D`
   and every header of `QtQuick3DRuntimeRender`, `QtQuick3DUtils`,
   `QtQuick3DAssetUtils` and `QtQuick3DHelpers` carry
   `SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only`; the one
   exception in each is the generated `*exports.h` (for example
   `qtquick3dexports.h`), which carries the LGPL-set identifier because it is
   boilerplate. Checked with `grep -rho` over
   `C:/Qt/6.8.3/msvc2022_64/include/*` [V].
4. **The 6.8.3 SBOM** (`sbom/qtquick3d-6.8.3.spdx.json`): `Quick3D`,
   `Quick3DRuntimeRender`, `Quick3DUtils`, `Quick3DAssetImport`,
   `Quick3DHelpers`, `Quick3DParticles`, `Quick3DEffects`, `Quick3DXr` and all
   their QML plugins have `licenseDeclared` = `LicenseRef-Qt-Commercial OR
   GPL-3.0-only`; the import tools carry `... GPL-3.0-only WITH
   Qt-GPL-exception-1.0` [V].

A search result suggested a distribution package for Qt 6.11 lists LGPL-3.0
for `qt6-quick3d`. I read that as the union of licences found in the whole
repository (the same header is LGPL-licensed in 6.8.3), not as a licence
change, because the Qt 6.12 pages above still say GPL-or-commercial [I].

**Also noted:** the licence is `GPL-3.0-only`, not "or later". Cubeulator's
source files stay "or later", but the combined binary can be conveyed only
under GPL-3.0 [V: SPDX id].

### 3.2 Qt Shader Tools - does not hold

- **Runtime library `ShaderTools`:** *Commercial OR LGPL-3.0-only OR
  GPL-2.0-only OR GPL-3.0-only* [V: SBOM `qtshadertools-6.8.3.spdx.json`; the
  6.8 documentation: "Since Qt 6.3, these free software licenses are the GNU
  Lesser General Public License, version 3, or the GNU General Public License,
  version 2" <https://doc.qt.io/qt-6.8/qtshadertools-index.html>].
- **`qsb` command-line tool:** *Commercial OR GPL-3.0-only WITH
  Qt-GPL-exception-1.0* [V: same two sources]. It is run during the build
  (`qt_add_shaders`), not shipped; the Qt GPL exception allows the output of
  the tool to be distributed under terms of one's choice, so long as the work
  is not otherwise derived from the tool [I: from my recollection of the
  exception's text; the Qt page lists "Qt GPL exception 1.0" for its tools,
  but the exception file itself was not available locally] [L].
- It is **not** on Qt's list of GPL-only modules [V: page above].

So Shader Tools adds no copyleft beyond what Quick 3D already forces. Its
bundled **glslang** carries a licence set that includes GPL-3.0-or-later *with
the Bison exception 2.2* (for generated parser files), which lets the output
be distributed under any terms; its notice must still be shipped [V: SBOM].

### 3.3 What else in Qt is GPL-only for this app

Nothing. Of the modules the app links or imports, Quick 3D and its helper
modules are the only ones without an LGPL option [V: section 2.2].

### 3.4 What it implies, combined with `LICENSE-NOTE.md`

`LICENSE-NOTE.md` frames the open question as GPLv3 against Apple's store
terms, for Cubeulator's own code. The Qt finding adds a second, separate
copyright holder to that question.

- **Cubeulator's own code** is Iain's alone. GPLv3 lets a copyright holder add
  additional permissions to material they own (section 7: "You may place
  additional permissions on material, added by you to a covered work, for
  which you have or can give appropriate copyright permission" [V:
  `LICENSE` line 360]), and a copyright holder is not bound by their own
  licence when distributing their own work. He can add a store exception,
  dual-license, or re-license it [I][L].
- **Qt Quick 3D's code** is The Qt Company's. It reaches every recipient
  under GPL-3.0-only unless they hold a commercial licence. Section 10 says
  the distributor "may not impose any further restrictions on the exercise of
  the rights granted" [V: `LICENSE` line 463], and the FSF's published
  position is that Apple's store terms impose such restrictions on any GPL
  code distributed through the store (the "Usage Rules" limit installation to
  a fixed set of devices, and apply "in addition to" any licence the developer
  supplies) [V: FSF, B. Smith, 26 May 2010, written about GPLv2 section 6;
  the FSF says the analysis applies to all versions]. Apple removed GNU Go in
  2010 after the FSF complained; VideoLAN's VLC for iOS went the same way and
  returned in 2013 as a rewrite under MPL-2.0 plus GPL-2.0-or-later, with the
  copyright holders' agreement [V: FSF article; VideoLAN press release,
  reported by Softpedia/Slashdot].
- **GPLv3 added a second hook:** section 6 requires "Installation
  Information" - "any methods, procedures, authorization keys, or other
  information required to install and execute modified versions of a covered
  work" - when object code is conveyed in a "User Product" [V: `LICENSE`
  lines 297-322]. A phone is a consumer product. iOS only runs code signed
  by a key the user does not hold unless they enrol in Apple's programme; the
  extent to which that satisfies the clause is a legal question [L].

**Is a store exception on Cubeulator's own code enough? No** [I][L]. The
exception can only grant permissions over Iain's own material (section 7).
The binary Apple would distribute also contains Quick 3D, whose licence
terms Iain cannot vary. The recipient receives Quick 3D's code under
GPL-3.0-only, so the conflict the FSF identifies is present for those
parts whatever Iain says about his. The exception would make Apple's
distribution of *his* code unobjectionable to *him*; it would not stop The
Qt Company (or any Qt contributor) from objecting about *theirs*. The same
reasoning is why a commercial Qt licence solves it: it is the instrument by
which the owner of that code grants different terms.

Whether The Qt Company would ever object is unknowable; this paper does not
rely on forbearance. The commercial licence page is also clear that GPL-only
parts "require your application or device to be licensed under GPL" [V: Qt
LGPL-obligations page, "Modules Not Available Under LGPL"].

## 4. The distribution channels

| channel | what stands in the way, if anything | status |
|---|---|---|
| **Direct download** (GitHub releases; ZIP/NSIS/DMG/TGZ/DEB/RPM) | Nothing beyond GPL-3.0 itself: ship licence, notices, source or an offer | clear [I] |
| **Microsoft Store** | Not examined in a primary source here. GPL apps appear in it [I]. MSIX packaging replaces the NSIS installer. | [L], research needed |
| **Mac App Store** | Same Apple terms as iOS (below) plus the sandbox; a notarised DMG outside the store avoids Apple's *store* terms entirely | blocked by the same question as iOS [I][L] |
| **Linux** (tarball, DEB, RPM, Flathub, Snap) | Nothing found. Distros that package it supply Qt themselves | clear [I] |
| **Google Play** | No contradiction found. The FSF's objections were to Apple's Usage Rules and DRM, not to Play; Play App Signing means Google holds the signing key, but Android lets a user install their own modified build (sideload), which is what the Installation Information clause asks for [I]. I found no official Google statement on GPLv3. | probably clear [I][L] |
| **F-Droid** | Builds from source and signs with its own key; designed for GPL apps [I] | clear [I] |
| **Apple App Store (iOS)** | The FSF analysis above; Qt's own page warns that stores "may have rules that are in conflict with LGPL" [V]; Quick 3D is GPL-3.0-only [V]; Installation Information on a locked device [L] | **open - needs a decision** |

## 5. Options

Costs are 2026-10-13 list figures from The Qt Company's own pages [V] and may
have moved; get a written quote.

### Option A - take no decision now; leave iOS off the first release

The plan already orders Windows, macOS and Linux first, Android next, iOS last
(decision 4), and the iOS app does not build today. The first store submission
the plan needs is desktop and, later, Google Play.

- **Cost:** nothing. **Consequence:** keeps GPL-3.0-or-later and Quick 3D
  as they are; no iOS until a route below is chosen.
- **Risk:** none new. Revisit when mobile capture exists (decision 8).
- **Does not cover:** the Mac App Store (the same Apple terms).

### Option B - a Qt commercial licence

- **What it is:** Qt for Application Development, or Qt for Small Business.
  Small Business is **EUR 546 (about USD 618) per developer licence per
  year**, 12-month terms renewed by hand, "max 1 MEUR/USD" annual revenue or
  funding, at most three licences per company bought online, standard
  support limited to five tickets a year, "apps for desktop and mobile"
  included (the Device Distribution licences that embedded products need are
  not required while eligible); eligibility is audited [V: <https://www.qt.io/pricing/qt-for-small-business>].
  Larger-company pricing is a quote.
- **What it covers:** all Qt modules, Quick 3D included, "under a single,
  commercial Qt license" [V: licensing page]. The cube stays in Quick 3D;
  nothing in `src/app/qt` changes.
- **What it asks of the code:** nothing technical. Cubeulator's own code can
  then be distributed under whatever terms Iain chooses for the store build:
  GPL-3.0-or-later publicly, plus a store exception or a proprietary licence
  for the store binary (he owns it). Third-party permissive components still
  need their notices (the generator covers them).
- **Catches to confirm [L]:** (1) The Qt Company's FAQ states that "moving
  code developed under an open source edition to a commercial license
  requires written consent from The Qt Company" [V: <https://www.qt.io/faq>];
  Cubeulator appears to have been developed on the open-source edition (Qt is
  installed with aqtinstall [V: `C:/Qt/aqtinstall.log`]). (2) A commercial
  licence for the store *binary* does not cure GPL obligations the *public*
  GPL release already carries, and the two releases need separate build
  paths. (3) Whether Qt Small Business covers a project whose source is
  public under GPL and that is distributed for free.
- **Consequence for LICENSE-NOTE.md:** it becomes the "dual-licensing"
  option the note lists, with Qt as the part that costs money.
- **Cost:** about USD 600 a year while eligible; a quote above that.

### Option C - draw the cube without Quick 3D

`src/render` is a bgfx stub; the cube is drawn today by `Cube3DView.qml`
(Quick 3D) and `CubeView.qml` (Canvas 2D). Without Quick 3D the app would link
only modules that have an LGPL option [V: section 2.2].

- **What it does for the licence:** the Qt set becomes available under
  LGPL-3.0 (or still GPL). Cubeulator's own GPL does not need to change.
- **What it does *not* do:** it does not make Apple's store terms compatible
  with Qt's LGPL. The Qt Company's own LGPL page says "online application
  stores, may have rules that are in conflict with LGPL" and that those
  stores "cannot be used with the LGPL option" [V]; the same page says that
  with static linking the application "may no longer be 'work that uses the
  library'" and may become subject to the LGPL, recommending that one "link
  dynamically, or provide the application source code to the user under LGPL"
  [V]. The LGPL still requires that a user can relink with a modified Qt:
  LGPLv3 4(d) offers (0) the application's object files so the user can
  recombine, or (1) a shared-library mechanism, and 4(e) the Installation
  Information where the GPL would require it [V: gnu.org LGPL-3.0, via
  summary]. On iOS the library is normally static [I], so (0) applies, and
  the Installation Information question is the same one as for GPL [L].
  Cubeulator's source being public makes the object-file duty easy to meet;
  the signing-key question does not go away.
- **Rendering alternatives, none verified here:** keep `CubeView.qml` (2D
  Canvas, already exists); a custom scene-graph item; the stub's bgfx via
  `QQuickRenderControl` (bgfx is BSD-2-Clause [V]); or Qt 3D, which is not on
  Qt's GPL-only list but whose maintenance status I did not check [I].
- **Cost:** engineering. The solve lane owns `Cube3DView.qml` and the cel
  shaders, so this is theirs to size; it throws away the shader work.
- **Consequence:** unblocks nothing on iOS by itself, and costs more than
  Option B. It does remove the GPL-only dependency, which matters if the
  project ever wants a non-GPL desktop licence without paying Qt.

### Option D - do not ship on the App Store

The cleanest on licence terms. GPL apps are distributable to iOS users as
source for self-build and sideload, or via TestFlight to named testers, and
on Android via Play and F-Droid [I][L]. The cost is audience.

- **Consequence for LICENSE-NOTE.md:** its question disappears for iOS and
  macOS stores; "intended to ship to app stores" is narrowed to Google Play.

### Option E - other routes found

1. **Dual-license only Cubeulator's own code, ship it with Option B for the
   store build.** This is Option B stated from the code's side; the repository
   needs a contributor policy (a CLA or an explicit "all contributions are
   under a licence permitting this") before anyone else contributes [I][L].
2. **Re-implement the 3D view in a native iOS layer (SwiftUI/Metal) with Qt
   only for the non-3D shell.** Does not remove Qt from the binary; not
   recommended.
3. **A notarised DMG and a Windows installer** cover the desktop audience
   without any store [I].

### Recommendation

**A now, B when iOS or the Mac App Store becomes a goal, C only if B's cost or
the consent point ([L] above) blocks it.** Concretely:

1. Do not let the licensing question block E0-E5. Nothing in the plan's
   desktop or Android work is affected.
2. Before E6 (release readiness), ask The Qt Company's sales team for a
   written answer to two questions: whether the open-source edition's code
   may move to a commercial licence (the consent clause) and whether Qt for
   Small Business covers a free app whose source is public. Their answer
   settles Option B.
3. If B is yes, record it in `LICENSE-NOTE.md` as the resolution (dual
   licence: GPL-3.0-or-later publicly, with a store exception, plus a Qt
   commercial licence for store builds). If B is no, take Option D for Apple
   and revisit C.

This is a recommendation about engineering order and cost, not a legal
conclusion.

## 6. How the notices are produced

```
 vcpkg installed tree  ──┐                  third_party/notices/
 (status, info/*.list,   │                    manifest.json      hand-kept: the model,
  share/<port>/copyright,│                                       icons, fonts, elections
  ports/<port>/vcpkg.json)│                   qt-modules.json   generated from Qt's SBOMs
                          ├─► cube_third_party_notices ──►      licences/<SPDX>.txt
 CMake file API: cube_app │      (cmake -P, before cube_app links)
 resolved link line      ─┤                       │
 QML `import`s            │                       ▼
 (src/app/qt/*.qml)      ─┘     <build>/third_party_notices/third-party-notices.json
                                <build>/third_party_notices/THIRD-PARTY-NOTICES.txt
                                       │                       │
                       qt_add_resources(cube_app)         install(FILES ...) → CPack
                       :/licences/*.json,*.txt            (refuses while unresolved)
```

- **What counts as shipped** is derived, not listed: the app's *resolved link
  line* from the CMake file API (so every static or shared library on the
  real command line, per platform and configuration), its QML imports, and the
  closure of each over vcpkg's `Depends` and Qt's SBOM edges. A vcpkg port
  that installs nothing outside `share/` is a build helper and is skipped.
  Nothing is added by hand for a vcpkg port: the day `bgfx` is linked, it
  appears; the day R6 removes onnxruntime, it and its dependency chain
  disappear (M3T, GLEW and GLFW already have).
- **What is checked by hand** is only what the build cannot know:
  `manifest.json` lists vendored code, models, icons and fonts, the licence
  elections (Qt taken under GPL-3.0-only; FreeType under FTL), and a table of
  licences for ports whose manifest says `license: null`.
- **The test** (`third_party_notices_complete`, label `licensing`) re-runs the
  generator with the deployment directory added - every Qt module, plugin and
  vcpkg DLL beside the executable - and fails if anything is unresolved, if a
  component has no licence or text, if a licence text the build needs is not
  in `third_party/notices/licences/`, or if the deployment holds a component
  the embedded notices do not list. It also fails for: a file under `assets/`
  or a directory under `third_party/` that no manifest entry claims; a QML
  `import` with no Qt module mapped; a linked Qt library missing from
  `qt-modules.json`; and a Qt version outside the one the manifests were
  written for.
- **Installing** (`cmake --install`, therefore CPack) runs a check and
  refuses to package while the JSON's `unresolved` list is non-empty, so an
  incomplete notice cannot ship even if the test was not run.

Maintaining it:

| change | what to do |
|---|---|
| add a vcpkg dependency | nothing; it is picked up. A port with `license: null` needs a `portLicences` line. |
| vendor code under `third_party/` or add a file under `assets/` | add a `components` entry (or `ignoredFiles`) in `manifest.json` |
| add a QML `import` of a new Qt module | add it to `qt.qmlImports`; the module may need to be added to `qt-modules.json` |
| upgrade Qt | `python -I tools/licences/qt_sbom_manifest.py --sbom <platform>=<qt>/sbom --out third_party/notices/qt-modules.json`, review the diff, update `qt.expectedQtVersion` |
| add the bundled fonts | add the files under `assets/fonts/`; the planned manifest entries start emitting; verify their copyright lines against the fonts |
| add a platform (Linux, macOS) | run the Qt SBOM tool on that platform's Qt install; its components are over-included until then (the JSON says `qtPlatformVerified:false`) |

Known gaps, so nobody mistakes them for done:

1. **Licence texts are complete for the Windows closure**, but three kinds
   of file were *typed* rather than copied (MIT, BSD-2-Clause, BSD-3-Clause,
   ISC, Zlib - the standard SPDX wording without a copyright line), so a
   reviewer should diff them against <https://spdx.org/licenses/> once. The
   provenance of every text is in `third_party/notices/licences/README.md`.
   Nine texts had to be fetched (FTL, Unicode-3.0, AML-glslang,
   Bison-exception-2.2, X11, HPND, Imlib2, MIT-open-group, HPND-sell-variant);
   they were downloaded from the SPDX licence-list repository with Iain's
   approval on 2026-10-13. `IJG` is the "LEGAL ISSUES" section of
   libjpeg-turbo's `README.ijg`, from the source vcpkg unpacked locally.
2. **Only Windows was exercised against a real build here.** The Windows
   result (2026-10-13, Qt 6.8.3, `windows-msvc-fastdebug-d`): 102 components -
   34 vcpkg ports, 6 Qt entries (4 repositories + Qt SVG + EntryPoint),
   55 Qt-bundled third-party components, M3T and its three bundled headers,
   the palm model, and the two icon sets - with 25 licence texts, nothing
   unresolved, one generator warning (the four files above). The notices
   file is 343 KB and the JSON 356 KB. Linux, macOS and Android use the same
   mechanism, but `qt.deployedRoots` has no list for them yet (the generator
   says so in a warning) and their Qt components are over-included until
   `qt-modules.json` is verified there; CI is where they get checked.
   The Windows build used a copy of an existing vcpkg installed tree, merged
   with five ports built in this session, configured with
   `VCPKG_MANIFEST_INSTALL=OFF`: another session held the shared
   `D:/vcpkg-buildtrees` lock while building onnxruntime. The libraries are
   the ones the manifest resolves to at the pinned baseline, but a clean
   vcpkg install was not run end to end here.
3. **The deployment scan covers shared-library files** (`.dll`, `.so`,
   `.dylib`). It does not look inside an Android APK/AAB or a macOS bundle's
   `Frameworks/`; E4a/E4b extend it when they define those layouts.
4. **Assets drawn in code are covered only by their manifest entry.** The
   Lucide and Feather icon shapes live in `Icon.qml`; nothing stops someone
   deleting the `lucide-icons` entry. Files under `assets/` and directories
   under `third_party/` are enforced; shapes copied into QML are not. A rule
   of review: any new vector or glyph data in QML gets a manifest entry.
5. **QML-only dependencies are by import name.** A QML module that is only
   loaded by string at run time would be missed; there are none today.

## 7. About and Licences screens: the requirement

The screens are built in E3b. This section is what they must show, with the
reason for each. Object names follow the plan's contract (every interactive
item has an `objectName` and an accessible name).

### 7.1 The About screen

GPL-3.0 section 0 defines **Appropriate Legal Notices**: an interactive
interface must include "a convenient and prominently visible feature that (1)
displays an appropriate copyright notice, and (2) tells the user that there
is no warranty for the work (except to the extent that warranties are
provided), that licensees may convey the work under this License, and how to
view a copy of this License. If the interface presents a list of user
commands or options, such as a menu, a prominent item in the list meets this
criterion" [V: `LICENSE` lines 103-110]. Section 5(d) requires each
interactive interface to display them [V: line 230].

The screen reachable from the menu, in this order:

| # | content | text (English source; translatable) | why |
|---|---|---|---|
| 1 | Name and version | `Cubeulator <version>` and the build's commit hash (`cube::version`) | the source offer below must name *this* version |
| 2 | Copyright | `Copyright © 2026 Iain Chesworth` | s.0 (1) |
| 3 | No warranty | `This program comes with ABSOLUTELY NO WARRANTY; for details see the licence.` | s.0 (2); s.15-16 |
| 4 | Licence grant | `This is free software, and you are welcome to redistribute it under the terms of the GNU General Public License, version 3 or (at your option) any later version.` | s.0 (2) |
| 5 | View the licence | button opening the full GPL-3.0 text, offline (a Qt resource), with the gnu.org URL beside it | s.0 (2) "how to view a copy" |
| 6 | Source | `Source code: https://github.com/iainchesworthlabs/cubeulator` plus, when the build is a tagged release, the tag; selectable and openable | s.6 Corresponding Source / offer |
| 7 | Third-party licences | button to the Licences screen; one line: `Cubeulator uses Qt and other open-source software; see Licences.` | the other licences' notice duties |
| 8 | Qt statement | `This application uses Qt <version> under the GNU General Public License v3. Qt source: https://download.qt.io/official_releases/qt/` | Qt is conveyed in the package; its Corresponding Source must be offered [L] |

Rules:

- The **publisher string** (store listing, installer metadata, an optional
  "Published by" line) is "Iain Chesworth Labs"; the repository is
  `github.com/iainchesworthlabs/cubeulator`. The **copyright line** in item 2
  is a separate matter: it names the copyright holder, it is not a rename,
  and the repository's `LICENSE` is the plain GPL text with no holder line, so
  who is named there (Iain Chesworth, or the Labs) is Iain's decision. Until he
  decides, the generated notices keep "Copyright (C) 2026 Iain Chesworth".
- The text of items 3 and 4 is **not** to be paraphrased: it is the GPL's own
  recommended notice; changing it is a legal decision.
- Items 1-6 must be reachable in **no more than two taps/clicks from any
  screen** and offline.
- The same copyright, warranty, licence and source lines go in the store
  listing's description and the website, because that is where the user
  decides to install.
- If a store build is ever made under another licence (Option B), the About
  text is whatever that licence requires, and this table is revisited with
  the lawyer.

### 7.2 The Licences screen

Reads `:/licences/third-party-notices.json` (schema 1). Per component:

| field | shown as |
|---|---|
| `name`, `version` | row title |
| `license`, `distributedUnder` | licence id; when they differ, "offered under X, used under Y" |
| `copyright` | under the title, verbatim |
| `homepage` | link |
| `via` | "part of ..." |
| `text`, or `licenseRefs` resolved through top-level `licenseTexts` | on tap: the full text, scrollable |
| `notes` | small print |

Also: a header line stating the platform and Qt version the list was generated
for (`platform`, `qtVersion`); `THIRD-PARTY-NOTICES.txt` is shown or exported
as a whole on request; and nothing in the list is hidden by default.

The resource is, in outline (all strings UTF-8; `unresolved` is empty in any
build that can be packaged):

```json
{
  "schema": 1,
  "project": { "name": "...", "license": "GPL-3.0-or-later", "copyright": "...", "homepage": "...", "source": "..." },
  "platform": "windows", "qtVersion": "6.8.3", "qtPlatformVerified": true,
  "components": [
    { "id": "vcpkg/fmt", "name": "fmt", "version": "12.2.0",
      "license": "MIT", "distributedUnder": "MIT",
      "copyright": "", "homepage": "https://github.com/fmtlib/fmt",
      "kind": "vcpkg | qt | qt-bundled | vendored | model | design | font",
      "via": [], "licenseRefs": [], "exemptTexts": [], "missingTexts": [],
      "notes": "", "text": "<the port's copyright file, verbatim>" }
  ],
  "licenseTexts": { "GPL-3.0-only": "...", "MIT": "..." },
  "linked": [ { "input": "...", "kind": "vcpkg|qt|first-party|system|manifest|unresolved", "component": "..." } ],
  "unresolved": [], "warnings": []
}
```

A component's terms are its own `text` when present, otherwise the entries of
`licenseTexts` named by `licenseRefs`; the Licences screen shows the copyright
line first, then those texts.

### 7.3 Per-licence wording the notices must carry

The generator emits these automatically as each component's copyright line and
licence reference. This list exists so a reviewer knows what to look for.

| licence | what it requires in a binary distribution |
|---|---|
| GPL-3.0 (Cubeulator, Qt Quick 3D and the Qt set as elected) | licence text; Corresponding Source or a written offer valid for three years; Appropriate Legal Notices; no further restrictions; Installation Information for User Products |
| LGPL-3.0 (only if elected; not today) | licence text, a prominent notice that the library is used, ability to relink, source or offer |
| MIT, BSD-2/3-Clause, ISC, libpng, libtiff, IJG | the copyright line and licence text in the documentation |
| Apache-2.0 | licence copy, any NOTICE file, state significant changes |
| MPL-2.0 (Eigen headers, Qt's psl-data) | notice, and say where the source of those files is |
| FTL (FreeType, if elected) | credit line in the documentation: "Portions of this software are copyright (c) <year> The FreeType Project (www.freetype.org). All rights reserved." |
| Unicode-3.0 | the copyright and permission notice with the data |
| Zlib, BSL-1.0, 0BSD, CC0-1.0 | nothing beyond not misrepresenting authorship (BSL-1.0: no notice in object code) |
| OFL-1.1 (fonts, planned) | licence copy with the fonts; reserved font names |

## 8. Things in the plan that were wrong or missing

1. **Qt Shader Tools** is not GPL-only; its runtime library has an LGPL
   option, and only the `qsb` tool is GPL-3.0-only (with a Qt GPL exception
   for its output) [V]. Quick 3D is the only GPL-only module the app links.
   The plan's three routes are otherwise right, with this correction: Shader
   Tools is not a reason to avoid a route.
2. **"Where linking is static" for iOS** is stated in the brief as a fact; the
   Qt iOS pages I read do not say it. It is the usual configuration of the
   prebuilt Qt for iOS and the 2014 Qt developer-list threads assume it, but a
   shared build is possible and tracked as an open request [I]. It changes
   which LGPL 4(d) option applies, not whether the store question exists.
3. **`cube_corner_pose.onnx` was embedded whenever the file existed**
   (`src/app/CMakeLists.txt`). A developer's local copy of the AGPL-derived
   weights would have been compiled into a package without any other check.
   Closed by R6: the corner detector is gone and nothing embeds the file.
4. **The CPack packages contain nothing today**: no `install()` rule exists
   for `cube_app`. The notices install rule is the only one. E4a owns the
   rest; until then a ZIP/NSIS package is just the notices and the licence.
5. **The licence file beside the palm model is an unfilled Apache template**
   (`Copyright [yyyy] [name of copyright owner]`) and the repository records
   neither the holder nor the download URL for the model. Apache-2.0 asks
   for the licence and any NOTICE; supplying the real holder is a one-line
   fix once someone checks the OpenCV Zoo.
6. **GTK3 and about 45 LGPL/X11 ports reach Linux** through
   `opencv4[gtk]`, which exists to back `highgui`, which only the vendored M3T
   used (section 2.4). M3T is gone; the two features are still asked for.
7. **bgfx is a declared dependency nothing links**; it would otherwise have
   looked like a shipped component. The inventory distinguishes declared from
   linked because the generator reads the link line.
8. **The plan's "Qt Quick 3D reading" is a known unknown**, listed under "What
   is not known" - this paper resolves it for Quick 3D (holds) and Shader
   Tools (does not).

## 9. What a lawyer should confirm

Not exhaustive, ordered by how much rides on it.

1. Whether Apple's App Store terms, as they stand today (not the 2010 text the
   FSF quoted), conflict with GPL-3.0 for Qt Quick 3D's code, and whether a
   copyright holder's own agreement (VideoLAN's route) is the only cure.
2. Whether Section 6's Installation Information duty is met on iOS by a user's
   ability to build and sideload with a personal Apple developer account.
3. Whether The Qt Company will agree to move open-source-edition code to a
   commercial licence for the store build (the FAQ's consent clause), and
   whether Qt for Small Business covers a free, publicly-sourced app.
4. Whether shipping Qt's official binaries inside a GPL-3.0 app requires the
   Qt source in the package, or whether a written offer pointing at
   `download.qt.io` meets GPL section 6.
5. Whether the Qt GPL exception 1.0 on `qsb` covers the baked shaders
   embedded in the app (I expect yes; the exception file was not available
   locally).
6. Whether the Linux DEB/RPM, which depend on system Qt rather than ship it,
   change who owes the Qt source and notices.
7. Whether the OpenCV Zoo model's licence chain (MediaPipe, converted and
   quantised) permits redistribution as the Apache-2.0 text says, and what
   copyright line to print.
8. Microsoft Store: policy terms for GPL software (not examined here).
9. Whether a contributor licensing policy is needed before accepting any
   outside contribution, if Option B is chosen.

## 10. Sources

Primary, read in this work:

- Qt Licensing (Qt 6.12), <https://doc.qt.io/qt-6/licensing.html>
- Qt Quick 3D (Qt 6.12 and 6.8), <https://doc.qt.io/qt-6/qtquick3d-index.html>,
  <https://doc.qt.io/qt-6.8/qtquick3d-index.html>
- Qt Shader Tools (Qt 6.8), <https://doc.qt.io/qt-6.8/qtshadertools-index.html>
- Qt LGPL obligations, <https://www.qt.io/development/open-source-lgpl-obligations>
- Qt for Small Business, <https://www.qt.io/pricing/qt-for-small-business>
- Qt FAQ (commercial licence consent), <https://www.qt.io/faq>
- Qt 6.8.3 install: `include/*` headers' SPDX lines and
  `sbom/*.spdx.json` (local, `C:/Qt/6.8.3/msvc2022_64`, and Android)
- GPL-3.0 text: this repository's `LICENSE`; LGPL-3.0, <https://www.gnu.org/licenses/lgpl-3.0.html>
- FSF, "More about the App Store GPL Enforcement" (B. Smith, 2010-05-26),
  <https://www.fsf.org/blogs/licensing/more-about-the-app-store-gpl-enforcement>
- VideoLAN, "VLC for iOS returns", <https://images.videolan.org/press/ios2out.html>
- Lucide `LICENSE`, <https://github.com/lucide-icons/lucide/blob/main/LICENSE>
- OpenCV Zoo `palm_detection_mediapipe`,
  <https://github.com/opencv/opencv_zoo/tree/main/models/palm_detection_mediapipe>
- vcpkg port manifests and installed `share/<port>/copyright` at baseline
  `5b698638c97b4610044a254347d571f823e56557`

Secondary or not authoritative (marked [I] where used): Qt wiki "Licensing
talk about mobile platforms" and the Qt development mailing list, July 2014
(iOS static linking and relinking); Qt forum threads on iOS and Google Play;
Qt bug QTBUG-85974; Arch Linux package metadata for `qt6-quick3d`.
