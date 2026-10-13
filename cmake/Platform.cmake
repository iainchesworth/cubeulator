# ---------------------------------------------------------------------------
# Platform.cmake - the ONE place the target operating system is decided.
#
# This file maps the toolchain's own flags (ANDROID, IOS, WIN32, APPLE, UNIX)
# to a platform name, then includes that platform's fragment:
#
#     src/app/platform/<name>/platform.cmake
#
# Everything the platform contributes - its source files, system libraries,
# defines, deploy and packaging steps - is declared in that fragment, and every
# other CMake file reads it through the variables and hooks below. No other
# CMake file tests the OS, and no source file tests it either (see
# docs/platform-isolation.md; tools/checks/check_platform_isolation.ps1 holds
# the source side of that in CI).
#
# Adding a platform is therefore two edits: a src/app/platform/<name>/
# directory (its implementations plus a platform.cmake that fills in the
# contract below), and one branch in the selector here.
#
# The contract a fragment fills in
# --------------------------------
# Facts:
#   CUBEULATOR_PLATFORM_DISPLAY_NAME   How the platform is written in the
#                                      version block ("Windows", "macOS").
#   CUBEULATOR_PLATFORM_DESKTOP        ON where the developer tools, CPack
#                                      packaging and the installed notices
#                                      exist; OFF on phones.
#   CUBEULATOR_PLATFORM_TEST_QPA_PLATFORM
#                                      The Qt platform plugin the test
#                                      binaries default to when
#                                      QT_QPA_PLATFORM is unset ("" keeps
#                                      Qt's own default).
#   CUBEULATOR_PLATFORM_TEST_DISPLAY_VARIABLES
#                                      Space-separated environment variables
#                                      through which the platform's desktop
#                                      session reaches a program; one of them
#                                      must be set for a test that draws the
#                                      real app. "" where a session is always
#                                      there.
#   CUBEULATOR_PLATFORM_TEST_DISPLAY_HELP
#                                      One line of advice for when none is
#                                      set ("" when the above is). The word
#                                      PROGRAM stands for the test binary.
#   CUBEULATOR_PLATFORM_QT_SEARCH_ROOTS, CUBEULATOR_PLATFORM_QT_ARCH_DIRS
#                                      Where cmake/FindQt6.cmake looks for an
#                                      aqtinstall-style Qt when none is on
#                                      CMAKE_PREFIX_PATH.
#   CUBEULATOR_PLATFORM_NOTICES_DIR    Where the licence and the third-party
#                                      notices are installed, relative to the
#                                      install prefix: "." where the prefix is
#                                      the root of an installer or a disk image,
#                                      a share/doc directory where it is /usr.
#                                      Unused on phones.
#
# Build inputs (paths are relative to src/app):
#   CUBEULATOR_PLATFORM_SOURCES        Compiled into cube_platform: one
#                                      implementation of every interfaces/
#                                      header (and of the detail/ seams).
#   CUBEULATOR_PLATFORM_LINK_LIBRARIES System libraries cube_platform needs.
#   CUBEULATOR_PLATFORM_QT_LINK_LIBRARIES
#                                      Qt libraries cube_platform needs, linked
#                                      only when the Qt app is built.
#   CUBEULATOR_PLATFORM_COMPILE_DEFINITIONS
#                                      Defines for first-party targets that
#                                      call into the platform's C runtime.
#   CUBEULATOR_PLATFORM_APP_PROPERTIES Target properties for cube_app, as
#                                      <property> <value> pairs.
#
# Hooks (every platform defines all of them, even as no-ops):
#   cube_platform_prepare_dependencies()
#                                      Runs before vcpkg packages are imported.
#   cube_platform_deploy_qt_runtime(<target> <qml_dir>)
#                                      Puts Qt's runtime next to the built app.
#   cube_platform_deploy_qt_library(<target> <qt_target>)
#                                      Puts one more Qt library (QtTest, for
#                                      the system tests) next to a binary the
#                                      runtime deployment above did not cover.
#   cube_platform_configure_packaging()
#                                      A macro: adds this platform's CPack
#                                      generators to CPACK_GENERATOR.
#   cube_platform_configure_desktop_shell(<target>)
#                                      Adds the platform's implementation of
#                                      interfaces/desktop_shell.hpp (keep
#                                      awake, process identity, window traits)
#                                      and what it links to <target>, the
#                                      cube_app_desktop library. Unlike
#                                      CUBEULATOR_PLATFORM_SOURCES it may use
#                                      Qt: the library has it.
#   cube_platform_configure_desktop_app(<target>)
#                                      What makes <target>, the app, an
#                                      installable program on this desktop:
#                                      its file name, icon, version resource or
#                                      bundle, install rules and Qt deployment
#                                      (packaging/<name>/ConfigureApp.cmake). A
#                                      no-op on phones.
# ---------------------------------------------------------------------------
include_guard(GLOBAL)

# ANDROID/IOS must be checked before WIN32/APPLE/UNIX: CMake sets ANDROID
# alongside UNIX, and IOS alongside APPLE, when cross-compiling.
if(ANDROID)
    set(CUBEULATOR_PLATFORM android)
elseif(IOS)
    set(CUBEULATOR_PLATFORM ios)
elseif(WIN32)
    set(CUBEULATOR_PLATFORM windows)
elseif(APPLE)
    set(CUBEULATOR_PLATFORM macos)
elseif(UNIX)
    set(CUBEULATOR_PLATFORM linux)
else()
    message(FATAL_ERROR
        "Unsupported target platform. Add src/app/platform/<name>/ with a "
        "platform.cmake (see the contract at the top of cmake/Platform.cmake) "
        "and a branch for it in this file.")
endif()

set(CUBEULATOR_PLATFORM_DIR "${PROJECT_SOURCE_DIR}/src/app/platform/${CUBEULATOR_PLATFORM}")
include("${CUBEULATOR_PLATFORM_DIR}/platform.cmake")

# A fragment that forgets part of the contract should fail here, by name, not
# as an empty source list or a missing-command error three files away.
include(PlatformContract)
cube_platform_check_contract("${CUBEULATOR_PLATFORM_DIR}/platform.cmake"
    "${PROJECT_SOURCE_DIR}/src/app")

# ---------------------------------------------------------------------------
# cube_describe_build_target(<out>)
#
# "<platform> <arch> (<compiler> <version>)", the target line of the version
# block. Decided here, from what CMake knows about the toolchain, rather than
# by the compiler's predefined macros inside a source file.
# ---------------------------------------------------------------------------
function(cube_describe_build_target out)
    # What the build is TARGETING. CMAKE_SYSTEM_PROCESSOR describes the host
    # when only the compiler's own target changed (MSVC cross-targeting, a
    # CMAKE_OSX_ARCHITECTURES that differs from the host) and is empty for
    # CMAKE_SYSTEM_NAME=iOS, so the explicit architecture settings come first.
    set(_processor "${CMAKE_SYSTEM_PROCESSOR}")
    if(CMAKE_CXX_COMPILER_ARCHITECTURE_ID)
        set(_processor "${CMAKE_CXX_COMPILER_ARCHITECTURE_ID}")
    endif()
    if(CMAKE_OSX_ARCHITECTURES)
        list(GET CMAKE_OSX_ARCHITECTURES 0 _processor)
    endif()
    string(TOLOWER "${_processor}" _processor)
    if(_processor MATCHES "^(amd64|x86_64|x64)$")
        set(_arch x86_64)
    elseif(_processor MATCHES "^(arm64|aarch64)$")
        set(_arch arm64)
    elseif(_processor MATCHES "^(x86|i[3-6]86)$")
        set(_arch x86)
    else()
        set(_arch unknown-arch)
    endif()

    # clang-cl reports itself as MSVC-compatible but is Clang; its
    # CMAKE_CXX_COMPILER_ID is already "Clang".
    set(_version "${CMAKE_CXX_COMPILER_VERSION}")
    if(_version MATCHES "^([0-9]+\\.[0-9]+\\.[0-9]+)")
        set(_version "${CMAKE_MATCH_1}")
    endif()
    if(CMAKE_CXX_COMPILER_ID MATCHES "^(Clang|AppleClang)$")
        set(_compiler "Clang ${_version}")
    elseif(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
        # The _MSC_VER number, as the version block has always shown it.
        set(_compiler "MSVC ${MSVC_VERSION}")
    elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        set(_compiler "GCC ${_version}")
    else()
        set(_compiler "unknown compiler")
    endif()

    set(${out} "${CUBEULATOR_PLATFORM_DISPLAY_NAME} ${_arch} (${_compiler})" PARENT_SCOPE)
endfunction()
