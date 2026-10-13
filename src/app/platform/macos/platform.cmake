# macOS. Included by cmake/Platform.cmake; the contract it fills in is
# described at the top of that file.

set(CUBEULATOR_PLATFORM_DISPLAY_NAME "macOS")
set(CUBEULATOR_PLATFORM_DESKTOP ON)

# No display is needed for the test binaries' items that are never shown.
set(CUBEULATOR_PLATFORM_TEST_QPA_PLATFORM "offscreen")

# A macOS session is always there.
set(CUBEULATOR_PLATFORM_TEST_DISPLAY_VARIABLES "")
set(CUBEULATOR_PLATFORM_TEST_DISPLAY_HELP "")

set(CUBEULATOR_PLATFORM_QT_SEARCH_ROOTS
    "$ENV{HOME}/Qt" "/opt/Qt" "/usr/local/opt/qt6" "/opt/homebrew/opt/qt6")
set(CUBEULATOR_PLATFORM_QT_ARCH_DIRS macos clang_64)

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

macro(cube_platform_configure_packaging)
    list(APPEND CPACK_GENERATOR "DragNDrop")
endmacro()
