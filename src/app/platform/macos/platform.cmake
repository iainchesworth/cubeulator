# macOS. Included by cmake/Platform.cmake; the contract it fills in is
# described at the top of that file.

set(CUBEULATOR_PLATFORM_DISPLAY_NAME "macOS")
set(CUBEULATOR_PLATFORM_DESKTOP ON)
# A native camera device, opened from the calling thread? (cmake/Platform.cmake)
set(CUBEULATOR_PLATFORM_TEST_NATIVE_CAMERA ON)

# No display is needed for the test binaries' items that are never shown.
set(CUBEULATOR_PLATFORM_TEST_QPA_PLATFORM "offscreen")

# A macOS session is always there.
set(CUBEULATOR_PLATFORM_TEST_DISPLAY_VARIABLES "")
set(CUBEULATOR_PLATFORM_TEST_DISPLAY_HELP "")

# Not hidden: whether a transparent, click-through window is exposed and
# rendered under AppKit has not been measured.
set(CUBEULATOR_PLATFORM_TEST_HIDE_WINDOW OFF)
set(CUBEULATOR_PLATFORM_TEST_LINK_OPTIONS "")

set(CUBEULATOR_PLATFORM_QT_SEARCH_ROOTS
    "$ENV{HOME}/Qt" "/opt/Qt" "/usr/local/opt/qt6" "/opt/homebrew/opt/qt6")
set(CUBEULATOR_PLATFORM_QT_ARCH_DIRS macos clang_64)

# The root of the disk image: the licence and the notices sit beside the .app.
set(CUBEULATOR_PLATFORM_NOTICES_DIR ".")

set(CUBEULATOR_PLATFORM_SOURCES
    platform/macos/camera_macos.cpp
    platform/macos/paths_macos.cpp
    platform/macos/platform_info_macos.cpp
    platform/posix/capture_thread_scope_posix.cpp
    platform/posix/crash_handler_posix.cpp)

set(CUBEULATOR_PLATFORM_LINK_LIBRARIES "")
set(CUBEULATOR_PLATFORM_QT_LINK_LIBRARIES "")
set(CUBEULATOR_PLATFORM_COMPILE_DEFINITIONS "")

set(CUBEULATOR_PLATFORM_APP_PROPERTIES "")

function(cube_platform_prepare_dependencies)
endfunction()

# macdeployqt is the equivalent of Windows' windeployqt and is not wired here:
# there was no Mac available to verify it against.
function(cube_platform_deploy_qt_runtime target qml_dir)
endfunction()

# Qt is found through RPATH, so a binary that links a Qt library finds it
# without a copy.
function(cube_platform_deploy_qt_library target qt_target)
endfunction()

# The .dmg holding Cubeulator.app. packaging/macos/Packaging.cmake.
macro(cube_platform_configure_packaging)
    include("${PROJECT_SOURCE_DIR}/packaging/macos/Packaging.cmake")
endmacro()

# The shell's macOS implementation: a no-display-sleep power assertion, and
# the window traits macOS has (its icon is the bundle's, a menu bar, a full
# screen button). IOKit has the power assertion.
function(cube_platform_configure_desktop_shell target)
    target_sources(${target} PRIVATE
        "${PROJECT_SOURCE_DIR}/src/app/platform/macos/desktop_shell_macos.cpp")
    target_link_libraries(${target} PRIVATE "-framework IOKit" "-framework CoreFoundation")
endfunction()

# The bundle, its Info.plist and icon, install rules and Qt deployment into
# it, and the code signature. packaging/macos/ConfigureApp.cmake.
function(cube_platform_configure_desktop_app target)
    include("${PROJECT_SOURCE_DIR}/packaging/macos/ConfigureApp.cmake")
endfunction()
