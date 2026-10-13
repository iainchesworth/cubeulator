# iOS. Included by cmake/Platform.cmake; the contract it fills in is described
# at the top of that file.

set(CUBEULATOR_PLATFORM_DISPLAY_NAME "iOS")
set(CUBEULATOR_PLATFORM_DESKTOP OFF)

# The test suites are not built for iOS (CUBEULATOR_BUILD_TESTS is OFF in the
# preset); this is the value they would use if they were.
set(CUBEULATOR_PLATFORM_TEST_QPA_PLATFORM "offscreen")
set(CUBEULATOR_PLATFORM_TEST_DISPLAY_VARIABLES "")
set(CUBEULATOR_PLATFORM_TEST_DISPLAY_HELP "")

# Qt is a cross-compiled kit here and always comes from CMAKE_PREFIX_PATH;
# the host-side install roots the desktop platforms search are meaningless.
set(CUBEULATOR_PLATFORM_QT_SEARCH_ROOTS "")
set(CUBEULATOR_PLATFORM_QT_ARCH_DIRS "")

set(CUBEULATOR_PLATFORM_SOURCES
    platform/ios/camera_ios.cpp
    platform/ios/inference_env_ios.cpp
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
