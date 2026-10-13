# Linux. Included by cmake/Platform.cmake; the contract it fills in is
# described at the top of that file.

set(CUBEULATOR_PLATFORM_DISPLAY_NAME "Linux")
set(CUBEULATOR_PLATFORM_DESKTOP ON)

# No display is needed for the test binaries' items that are never shown.
set(CUBEULATOR_PLATFORM_TEST_QPA_PLATFORM "offscreen")

# A desktop session reaches a program through one of these, and the system
# tests draw the real app, so they need one. Qt Quick 3D has no software
# fallback, hence the software-GL note for a headless box.
# One space-separated string, not a CMake list: it is written into a header as is.
set(CUBEULATOR_PLATFORM_TEST_DISPLAY_VARIABLES "DISPLAY WAYLAND_DISPLAY")
set(CUBEULATOR_PLATFORM_TEST_DISPLAY_HELP
    "run it under a virtual display: xvfb-run -a PROGRAM (software GL: LIBGL_ALWAYS_SOFTWARE=1)")

set(CUBEULATOR_PLATFORM_QT_SEARCH_ROOTS
    "$ENV{HOME}/Qt" "/opt/Qt" "/usr/lib/qt6" "/usr/lib/x86_64-linux-gnu/qt6")
set(CUBEULATOR_PLATFORM_QT_ARCH_DIRS gcc_64)

set(CUBEULATOR_PLATFORM_SOURCES
    platform/linux/camera_linux.cpp
    platform/linux/inference_env_linux.cpp
    platform/linux/paths_linux.cpp
    platform/linux/platform_info_linux.cpp
    platform/posix/capture_thread_scope_posix.cpp
    platform/posix/crash_handler_posix.cpp)

set(CUBEULATOR_PLATFORM_LINK_LIBRARIES "")
set(CUBEULATOR_PLATFORM_QT_LINK_LIBRARIES "")
set(CUBEULATOR_PLATFORM_COMPILE_DEFINITIONS "")

set(CUBEULATOR_PLATFORM_APP_PROPERTIES "")

function(cube_platform_prepare_dependencies)
endfunction()

# Nothing to deploy: Qt comes from the system or from CMAKE_PREFIX_PATH and
# is found at run time the same way.
function(cube_platform_deploy_qt_runtime target qml_dir)
endfunction()

# Qt is found through the system's library path or RPATH, so a binary that
# links a Qt library finds it without a copy.
function(cube_platform_deploy_qt_library target qt_target)
endfunction()

macro(cube_platform_configure_packaging)
    list(APPEND CPACK_GENERATOR "TGZ")
    find_program(CUBEULATOR_DPKG_DEB_EXECUTABLE dpkg-deb)
    if(CUBEULATOR_DPKG_DEB_EXECUTABLE)
        list(APPEND CPACK_GENERATOR "DEB")
        set(CPACK_DEBIAN_PACKAGE_MAINTAINER "${CPACK_PACKAGE_VENDOR}")
        set(CPACK_DEBIAN_PACKAGE_SECTION "graphics")
        set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
    endif()
    find_program(CUBEULATOR_RPMBUILD_EXECUTABLE rpmbuild)
    if(CUBEULATOR_RPMBUILD_EXECUTABLE)
        list(APPEND CPACK_GENERATOR "RPM")
        set(CPACK_RPM_PACKAGE_LICENSE "GPL-3.0-or-later")
        set(CPACK_RPM_PACKAGE_GROUP "Amusements/Graphics")
        set(CPACK_RPM_PACKAGE_AUTOREQPROV ON)
    endif()
endmacro()
