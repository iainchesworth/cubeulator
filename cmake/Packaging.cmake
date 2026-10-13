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
# the desktop entry - is declared by each platform, in
# packaging/<name>/ConfigureApp.cmake, through the platform fragment's
# cube_platform_configure_desktop_app() hook (helpers in
# cmake/DesktopPackaging.cmake, called from src/app/CMakeLists.txt). Which
# generators a platform adds, and how they are set, is packaging/<name>/
# Packaging.cmake, reached through cube_platform_configure_packaging() below.
# The specification is docs/design/platforms.md; how to build and install each
# package is docs/packaging.md.
#
# None of this applies to mobile: real packaging there is androiddeployqt's
# generated APK/AAB and Xcode's archive+export, not CPack.
#
# SIGNING is deliberately not configured (Iain has not decided; no
# certificate is bought, requested or referenced). It plugs in at:
#   * Windows - packaging/windows/Packaging.cmake: sign the installer after
#     CPack writes it.
#   * macOS - packaging/macos/ConfigureApp.cmake: the bundle is signed ad hoc,
#     which Apple Silicon needs to run at all; set
#     CUBEULATOR_MACOS_CODESIGN_IDENTITY for a real signature.
# Both are described in docs/packaging.md.
# ---------------------------------------------------------------------------
if(NOT CUBEULATOR_PLATFORM_DESKTOP)
    message(STATUS
        "Skipping CPack packaging for the ${CUBEULATOR_PLATFORM} target; "
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
# 0.1.0, or 0.1.0-beta.1: what the installer shows. The Linux formats cannot
# take the hyphen as it is and say what they do instead in
# packaging/linux/Packaging.cmake.
set(CPACK_PACKAGE_VERSION "${CUBEULATOR_VERSION_FULL}")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "Cubeulator")
set(CPACK_RESOURCE_FILE_LICENSE "${PROJECT_SOURCE_DIR}/LICENSE")
set(CPACK_VERBATIM_VARIABLES ON)

# Cubeulator-0.1.0-windows-x64.exe, ...-macos-arm64.dmg, ...-linux-x64.deb;
# a pre-release carries its label (Cubeulator-0.1.0-beta.1-windows-x64.exe), so
# that it is never mistaken for the final release that follows it.
set(_cube_pkg_os "${CUBEULATOR_PLATFORM}")
string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" _cube_pkg_arch)
if(_cube_pkg_arch MATCHES "^(amd64|x86_64|x64)$")
    set(_cube_pkg_arch "x64")
elseif(_cube_pkg_arch MATCHES "^(arm64|aarch64)$")
    set(_cube_pkg_arch "arm64")
endif()
set(CPACK_PACKAGE_FILE_NAME "Cubeulator-${CUBEULATOR_VERSION_FULL}-${_cube_pkg_os}-${_cube_pkg_arch}")

set(CPACK_GENERATOR "ZIP")

# Where the generated icons are (cmake/DesktopPackaging.cmake); the platforms'
# packaging files name the one they use.
set(_cube_icon_dir "${CMAKE_BINARY_DIR}/packaging/icons")

if(CUBEULATOR_BUILD_APP)
    # The installers this platform can make (NSIS, DragNDrop, DEB/RPM) are the
    # platform fragment's to add: src/app/platform/<name>/platform.cmake.
    cube_platform_configure_packaging()
endif()

include(CPack)
