# Android. Included by cmake/Platform.cmake; the contract it fills in is
# described at the top of that file.

set(CUBEULATOR_PLATFORM_DISPLAY_NAME "Android")
set(CUBEULATOR_PLATFORM_DESKTOP OFF)

# The test suites are not built for Android (CUBEULATOR_BUILD_TESTS is OFF in
# the preset); this is the value they would use if they were.
set(CUBEULATOR_PLATFORM_TEST_QPA_PLATFORM "offscreen")
set(CUBEULATOR_PLATFORM_TEST_DISPLAY_VARIABLES "")
set(CUBEULATOR_PLATFORM_TEST_DISPLAY_HELP "")
set(CUBEULATOR_PLATFORM_TEST_HIDE_WINDOW OFF)
set(CUBEULATOR_PLATFORM_TEST_LINK_OPTIONS "")

# Qt is a cross-compiled kit here and always comes from CMAKE_PREFIX_PATH;
# the host-side install roots the desktop platforms search are meaningless.
set(CUBEULATOR_PLATFORM_QT_SEARCH_ROOTS "")
set(CUBEULATOR_PLATFORM_QT_ARCH_DIRS "")

# Not installed by CPack: see the packaging note below.
set(CUBEULATOR_PLATFORM_NOTICES_DIR ".")

set(CUBEULATOR_PLATFORM_SOURCES
    platform/android/camera_android.cpp
    platform/android/paths_android.cpp
    platform/android/platform_info_android.cpp
    platform/posix/capture_thread_scope_posix.cpp
    platform/posix/crash_handler_posix.cpp)

set(CUBEULATOR_PLATFORM_LINK_LIBRARIES "")

# camera_android.cpp uses QJniObject/QNativeInterface for the
# ACTION_APPLICATION_DETAILS_SETTINGS permission-settings deep link (see
# interfaces/camera.hpp: open_permission_settings()). cube_platform is
# otherwise deliberately Qt-free - this holds only while Android also always
# builds the Qt app (which is the case for every current preset); a
# genuinely headless CUBEULATOR_BUILD_APP=OFF Android configure would need
# this revisited.
set(CUBEULATOR_PLATFORM_QT_LINK_LIBRARIES Qt6::Core)

set(CUBEULATOR_PLATFORM_COMPILE_DEFINITIONS "")

# Custom AndroidManifest.xml (package/) declaring the CAMERA permission
# androiddeployqt's own auto-detection doesn't know to add for a C++-side
# QCameraPermission request (confirmed: without this, the OS permission
# dialog still appears, but `pm revoke`/real-device permission tracking
# doesn't recognise the app as having requested it at all). A directory of
# its own, because androiddeployqt copies the whole of it into the Gradle
# project and the platform directory also holds sources.
set(CUBEULATOR_PLATFORM_APP_PROPERTIES
    QT_ANDROID_PACKAGE_SOURCE_DIR "${CUBEULATOR_PLATFORM_DIR}/package")

# CMake's FindThreads (transitively pulled in by abseil, itself pulled in by
# protobuf/opencv4) fails outright under this project's NDK-chainloaded-under-
# vcpkg toolchain: every detection strategy it tries fails in sequence -
# CMAKE_HAVE_LIBC_PTHREAD's try_compile, -lpthreads, -lpthread, and even
# "does the compiler accept -pthread" all report failure (confirmed in CI) -
# which points to try_compile()'s isolated sub-invocation not correctly
# inheriting the chainloaded NDK toolchain, not an actual absence of thread
# support. Android's Bionic libc has bundled pthreads since API 21 (this
# project targets API 28), so pre-seeding CMAKE_HAVE_LIBC_PTHREAD as a cached
# TRUE - the standard workaround for this class of NDK/CMake interaction bug
# - makes FindThreads skip its broken detection and use the (correct) libc
# answer directly, without needing to fix the underlying toolchain plumbing.
function(cube_platform_prepare_dependencies)
    set(CMAKE_HAVE_LIBC_PTHREAD 1 CACHE INTERNAL
        "Pre-seeded: Android's Bionic libc bundles pthreads (API 21+)")
endfunction()

# Real packaging is androiddeployqt's generated APK/AAB, not CMake.
function(cube_platform_deploy_qt_runtime target qml_dir)
endfunction()

function(cube_platform_deploy_qt_library target qt_target)
endfunction()

macro(cube_platform_configure_packaging)
endmacro()

# A phone has no desktop shell of its own: the OS owns the window and the
# process's identity. The shared library still builds, with the implementation
# that does nothing (the screens' keep-awake request is answered "not held"
# until E4b).
function(cube_platform_configure_desktop_shell target)
    target_sources(${target} PRIVATE
        "${PROJECT_SOURCE_DIR}/src/app/platform/posix/desktop_shell_mobile.cpp")
endfunction()

# Nothing to install: Real packaging is androiddeployqt's generated APK/AAB, not CMake.
function(cube_platform_configure_desktop_app target)
endfunction()
