# iOS. Included by cmake/Platform.cmake; the contract it fills in is described
# at the top of that file.

set(CUBEULATOR_PLATFORM_DISPLAY_NAME "iOS")
set(CUBEULATOR_PLATFORM_DESKTOP OFF)

# The test suites are not built for iOS (CUBEULATOR_BUILD_TESTS is OFF in the
# preset); this is the value they would use if they were.
set(CUBEULATOR_PLATFORM_TEST_QPA_PLATFORM "offscreen")
set(CUBEULATOR_PLATFORM_TEST_DISPLAY_VARIABLES "")
set(CUBEULATOR_PLATFORM_TEST_DISPLAY_HELP "")
set(CUBEULATOR_PLATFORM_TEST_HIDE_WINDOW OFF)

# Qt is a cross-compiled kit here and always comes from CMAKE_PREFIX_PATH;
# the host-side install roots the desktop platforms search are meaningless.
set(CUBEULATOR_PLATFORM_QT_SEARCH_ROOTS "")
set(CUBEULATOR_PLATFORM_QT_ARCH_DIRS "")

# Not installed by CPack: see the packaging note below.
set(CUBEULATOR_PLATFORM_NOTICES_DIR ".")

set(CUBEULATOR_PLATFORM_SOURCES
    platform/ios/camera_ios.cpp
    platform/ios/paths_ios.cpp
    platform/ios/platform_info_ios.cpp
    platform/posix/capture_thread_scope_posix.cpp
    platform/posix/crash_handler_posix.cpp)

set(CUBEULATOR_PLATFORM_LINK_LIBRARIES "")
set(CUBEULATOR_PLATFORM_QT_LINK_LIBRARIES "")
set(CUBEULATOR_PLATFORM_COMPILE_DEFINITIONS "")

set(CUBEULATOR_PLATFORM_APP_PROPERTIES "")

function(cube_platform_prepare_dependencies)
endfunction()

# Real packaging is Xcode's archive+export, not CMake.
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

# Nothing to install: Real packaging is Xcode's archive+export, not CMake.
function(cube_platform_configure_desktop_app target)
endfunction()
