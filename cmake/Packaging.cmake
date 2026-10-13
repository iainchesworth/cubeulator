# ---------------------------------------------------------------------------
# CPack packaging. Included once, from the top-level CMakeLists.txt, after
# every target's install() rules have been declared.
#
# A plain ZIP archive is always offered (needs no external tool). Platform-
# native formats are layered on top when the GUI app is being built and the
# packaging tool for that format is actually available:
#
#   Windows  NSIS installer: Start menu entry, uninstaller, version, icon
#   macOS    .dmg holding Cubeulator.app
#   Linux    .deb and .rpm, plus a .tar.gz
#
# What goes into the packages - the executable, Qt's runtime, the icon,
# the desktop entry - is declared in cmake/DesktopPackaging.cmake, called
# from src/app/CMakeLists.txt. The specification is docs/design/platforms.md;
# how to build and install each package is docs/packaging.md.
#
# None of this applies to mobile: real packaging there is androiddeployqt's
# generated APK/AAB and Xcode's archive+export, not CPack.
#
# SIGNING is deliberately not configured (Iain has not decided; no
# certificate is bought, requested or referenced). It plugs in at:
#   * Windows - sign the installer after CPack writes it: a
#     CPACK_POST_BUILD_SCRIPTS script that runs signtool on
#     ${CPACK_PACKAGE_FILE_NAME}.exe. SmartScreen warns until it is signed.
#   * macOS - sign and notarise the app inside the .dmg: macdeployqt's
#     -codesign=<identity> (DEPLOY_TOOL_OPTIONS in DesktopPackaging.cmake),
#     then notarytool on the .dmg. An unsigned app needs the user to
#     override Gatekeeper.
# ---------------------------------------------------------------------------
if(ANDROID OR IOS)
    message(STATUS
        "Skipping CPack packaging for mobile target (ANDROID/IOS); "
        "use androiddeployqt/Xcode archive+export instead.")
    return()
endif()

include("${CMAKE_CURRENT_LIST_DIR}/AppIdentity.cmake")

set(CPACK_PACKAGE_NAME "${CUBEULATOR_APP_DISPLAY_NAME}")
set(CPACK_PACKAGE_VENDOR "${CUBEULATOR_PUBLISHER}")
set(CPACK_PACKAGE_CONTACT "${CUBEULATOR_PUBLISHER}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "${PROJECT_DESCRIPTION}")
set(CPACK_PACKAGE_HOMEPAGE_URL "${PROJECT_HOMEPAGE_URL}")
set(CPACK_PACKAGE_VERSION_MAJOR "${PROJECT_VERSION_MAJOR}")
set(CPACK_PACKAGE_VERSION_MINOR "${PROJECT_VERSION_MINOR}")
set(CPACK_PACKAGE_VERSION_PATCH "${PROJECT_VERSION_PATCH}")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "Cubeulator")
set(CPACK_RESOURCE_FILE_LICENSE "${PROJECT_SOURCE_DIR}/LICENSE")
set(CPACK_VERBATIM_VARIABLES ON)

# Cubeulator-0.1.0-windows-x64.exe, ...-macos-arm64.dmg, ...-linux-x64.deb
if(WIN32)
    set(_cube_pkg_os "windows")
elseif(APPLE)
    set(_cube_pkg_os "macos")
else()
    set(_cube_pkg_os "linux")
endif()
string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" _cube_pkg_arch)
if(_cube_pkg_arch MATCHES "^(amd64|x86_64|x64)$")
    set(_cube_pkg_arch "x64")
elseif(_cube_pkg_arch MATCHES "^(arm64|aarch64)$")
    set(_cube_pkg_arch "arm64")
endif()
set(CPACK_PACKAGE_FILE_NAME "Cubeulator-${PROJECT_VERSION}-${_cube_pkg_os}-${_cube_pkg_arch}")

set(CPACK_GENERATOR "ZIP")

# Where the generated icons are (cmake/DesktopPackaging.cmake).
set(_cube_icon_dir "${CMAKE_BINARY_DIR}/packaging/icons")

if(CUBEULATOR_BUILD_APP)
    if(APPLE)
        list(APPEND CPACK_GENERATOR "DragNDrop")
        set(CPACK_DMG_VOLUME_NAME "Cubeulator")
        set(CPACK_DMG_FORMAT "UDZO")
        set(CPACK_PACKAGE_ICON "${_cube_icon_dir}/Cubeulator.icns")

    elseif(WIN32)
        find_program(CUBEULATOR_MAKENSIS_EXECUTABLE makensis
            HINTS "$ENV{ProgramFiles}/NSIS" "$ENV{ProgramFiles\(x86\)}/NSIS")
        if(CUBEULATOR_MAKENSIS_EXECUTABLE)
            list(APPEND CPACK_GENERATOR "NSIS")

            file(TO_NATIVE_PATH "${_cube_icon_dir}/Cubeulator.ico" _cube_ico_native)
            file(TO_NATIVE_PATH "${PROJECT_SOURCE_DIR}/packaging/windows/StampShortcut.ps1" _cube_stamp_native)

            set(CPACK_NSIS_PACKAGE_NAME "${CPACK_PACKAGE_NAME}")
            set(CPACK_NSIS_DISPLAY_NAME "${CPACK_PACKAGE_NAME}")
            set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)
            set(CPACK_NSIS_MODIFY_PATH OFF)

            # The installer, the uninstaller and Settings > Apps all carry the icon.
            set(CPACK_NSIS_MUI_ICON "${_cube_ico_native}")
            set(CPACK_NSIS_MUI_UNIICON "${_cube_ico_native}")
            set(CPACK_NSIS_INSTALLED_ICON_NAME "bin\\Cubeulator.exe")
            set(CPACK_NSIS_URL_INFO_ABOUT "${PROJECT_HOMEPAGE_URL}")
            set(CPACK_NSIS_HELP_LINK "${PROJECT_HOMEPAGE_URL}")
            set(CPACK_NSIS_CONTACT "${CPACK_PACKAGE_CONTACT}")

            # One Start menu shortcut, to bin\Cubeulator.exe.
            set(CPACK_PACKAGE_EXECUTABLES "Cubeulator" "Cubeulator")

            # Stamp that shortcut with the AppUserModelID the app sets for
            # itself (desktop_integration.cpp), so that the taskbar groups a
            # pinned shortcut with the running window. A failure here only
            # costs that grouping; the installer carries on.
            set(CPACK_NSIS_EXTRA_INSTALL_COMMANDS "
                InitPluginsDir
                File \"/oname=$PLUGINSDIR\\StampShortcut.ps1\" \"${_cube_stamp_native}\"
                nsExec::Exec '\"$SYSDIR\\WindowsPowerShell\\v1.0\\powershell.exe\" -NoProfile -NonInteractive -ExecutionPolicy Bypass -File \"$PLUGINSDIR\\StampShortcut.ps1\" -Shortcut \"$SMPROGRAMS\\$STARTMENU_FOLDER\\Cubeulator.lnk\" -AppUserModelId \"${CUBEULATOR_WINDOWS_APP_USER_MODEL_ID}\"'
                Pop $0
            ")
        else()
            message(STATUS
                "makensis not found: no Windows installer will be built "
                "(install NSIS, or pass -DCUBEULATOR_MAKENSIS_EXECUTABLE=...).")
        endif()

    elseif(UNIX)
        list(APPEND CPACK_GENERATOR "TGZ")

        # The packages put the app under /usr: its own directory in
        # /usr/lib/cubeulator (with the Qt it carries), a launcher in
        # /usr/bin, and the desktop entry, AppStream metadata and icons in
        # /usr/share.
        set(CPACK_PACKAGING_INSTALL_PREFIX "/usr")

        # Qt, OpenCV and ONNX Runtime ship inside the package, so the
        # dependencies named here are only the system libraries Qt's xcb and
        # OpenGL plumbing loads. They are listed rather than discovered
        # because dpkg-shlibdeps and rpm's own scan would also try to resolve
        # the bundled libraries, and would either fail or ask the distribution
        # for libQt6*.so it does not have.
        set(CPACK_DEBIAN_PACKAGE_SECTION "games")
        set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")

        find_program(CUBEULATOR_DPKG_DEB_EXECUTABLE dpkg-deb)
        if(CUBEULATOR_DPKG_DEB_EXECUTABLE)
            list(APPEND CPACK_GENERATOR "DEB")
            set(CPACK_DEBIAN_PACKAGE_MAINTAINER "${CPACK_PACKAGE_VENDOR}")
            set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS OFF)
            set(CPACK_DEBIAN_PACKAGE_DEPENDS
                "libc6, libstdc++6, libgcc-s1, libgl1, libegl1, libfontconfig1, libfreetype6, libglib2.0-0, libdbus-1-3, libx11-6, libx11-xcb1, libxcb1, libxkbcommon0, libxkbcommon-x11-0, libxcb-cursor0, libxcb-icccm4, libxcb-image0, libxcb-keysyms1, libxcb-randr0, libxcb-render-util0, libxcb-render0, libxcb-shape0, libxcb-shm0, libxcb-sync1, libxcb-xfixes0, libxcb-xkb1, libxcb-xinerama0")
            # Japanese and Chinese text is drawn with the system's font
            # (docs/design/platforms.md, "Fonts"); a minimal install has none.
            set(CPACK_DEBIAN_PACKAGE_RECOMMENDS "fonts-noto-cjk")
        endif()

        find_program(CUBEULATOR_RPMBUILD_EXECUTABLE rpmbuild)
        if(CUBEULATOR_RPMBUILD_EXECUTABLE)
            list(APPEND CPACK_GENERATOR "RPM")
            set(CPACK_RPM_PACKAGE_LICENSE "GPL-3.0-or-later")
            set(CPACK_RPM_PACKAGE_GROUP "Amusements/Games")
            # Bundled libraries must not satisfy, or be required of, the
            # distribution: no automatic requires or provides, and an
            # explicit list instead.
            set(CPACK_RPM_PACKAGE_AUTOREQPROV OFF)
            set(CPACK_RPM_PACKAGE_REQUIRES
                "libxcb, libxkbcommon, libxkbcommon-x11, mesa-libGL, mesa-libEGL, fontconfig, freetype, dbus-libs, glib2, libX11, libX11-xcb, xcb-util-cursor, xcb-util-image, xcb-util-keysyms, xcb-util-renderutil, xcb-util-wm")
            # RPM has no "recommends" field in CPack; a soft suggestion.
            set(CPACK_RPM_PACKAGE_SUGGESTS "google-noto-sans-cjk-fonts")
            set(CPACK_RPM_DEBUGINFO_PACKAGE OFF)
            # The directories the desktop owns are not ours to claim.
            set(CPACK_RPM_EXCLUDE_FROM_AUTO_FILELIST_ADDITION
                "/usr/share/applications"
                "/usr/share/metainfo"
                "/usr/share/icons"
                "/usr/share/icons/hicolor"
                "/usr/share/icons/hicolor/scalable"
                "/usr/share/icons/hicolor/scalable/apps"
                "/usr/share/icons/hicolor/48x48"
                "/usr/share/icons/hicolor/48x48/apps"
                "/usr/share/icons/hicolor/64x64"
                "/usr/share/icons/hicolor/64x64/apps"
                "/usr/share/icons/hicolor/128x128"
                "/usr/share/icons/hicolor/128x128/apps"
                "/usr/share/icons/hicolor/256x256"
                "/usr/share/icons/hicolor/256x256/apps")
        endif()
    endif()
endif()

include(CPack)
