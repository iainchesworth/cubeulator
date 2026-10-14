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

# Not hidden: CI's display is virtual, so there is nobody to bother. Whether a
# transparent window is composited and exposed on a real X11 or Wayland desktop
# has not been measured; if the walk is ever run on one and the flicker
# matters, this is where to try it.
set(CUBEULATOR_PLATFORM_TEST_HIDE_WINDOW OFF)

set(CUBEULATOR_PLATFORM_QT_SEARCH_ROOTS
    "$ENV{HOME}/Qt" "/opt/Qt" "/usr/lib/qt6" "/usr/lib/x86_64-linux-gnu/qt6")
set(CUBEULATOR_PLATFORM_QT_ARCH_DIRS gcc_64)

# A package installs under /usr, so the licence and the notices go where the
# distribution keeps documentation rather than into /usr itself.
set(CUBEULATOR_PLATFORM_NOTICES_DIR "share/doc/cubeulator")

set(CUBEULATOR_PLATFORM_SOURCES
    platform/linux/camera_linux.cpp
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

# A .tar.gz, and a .deb and an .rpm where their tools are installed.
# packaging/linux/Packaging.cmake.
macro(cube_platform_configure_packaging)
    include("${PROJECT_SOURCE_DIR}/packaging/linux/Packaging.cmake")
endmacro()

# The shell's Linux implementation: the session's inhibit interface over
# D-Bus (the desktop portal, then org.freedesktop.ScreenSaver). It needs
# QtDBus, which the library does not otherwise link.
function(cube_platform_configure_desktop_shell target)
    find_package(Qt6 6.5 REQUIRED COMPONENTS DBus)
    target_sources(${target} PRIVATE
        "${PROJECT_SOURCE_DIR}/src/app/platform/linux/desktop_shell_linux.cpp")
    target_link_libraries(${target} PRIVATE Qt6::DBus)
endfunction()

# The app in its own directory under /usr/lib with the Qt it carries, a
# launcher in /usr/bin, and the desktop entry, AppStream metadata and icons.
# packaging/linux/ConfigureApp.cmake.
function(cube_platform_configure_desktop_app target)
    include("${PROJECT_SOURCE_DIR}/packaging/linux/ConfigureApp.cmake")
endfunction()
