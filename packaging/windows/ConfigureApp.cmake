# Windows: makes <target> Cubeulator.exe with its version resource and icon,
# and declares the install rules the NSIS installer packages.
#
# Included by cube_platform_configure_desktop_app() in
# src/app/platform/windows/platform.cmake, inside that function, so `target`
# is the app and the helpers of cmake/DesktopPackaging.cmake are available.
include(GNUInstallDirs)

# The project declares LANGUAGES CXX only; resource files need RC.
enable_language(RC)

set_target_properties(${target} PROPERTIES OUTPUT_NAME "Cubeulator")

# The window icon, as a Qt resource (platform/windows/desktop_shell_windows.cpp
# says the window sets its own).
cube_embed_window_icon(${target})

# --- the version resource and icon ------------------------------------------
# The version and git description come from the same script that stamps
# cube/version.hpp, run again each build so the resource tracks new commits.
# configure_file only rewrites it when the text changes, so the resource is not
# recompiled needlessly.
find_package(Git QUIET)
set(_rc_template "${PROJECT_SOURCE_DIR}/packaging/windows/Cubeulator.rc.in")
set(_rc_file "${CUBEULATOR_PACKAGING_DIR}/Cubeulator.rc")
set(_ico_file "${CUBEULATOR_ICON_DIR}/Cubeulator.ico")
file(MAKE_DIRECTORY "${CUBEULATOR_PACKAGING_DIR}")

# Once at configure time so the file exists for the first build.
function(_cube_stamp_rc)
    set(CUBEULATOR_VERSION "${PROJECT_VERSION}")
    set(SRC "${_rc_template}")
    set(DST "${_rc_file}")
    set(WORKDIR "${PROJECT_SOURCE_DIR}")
    set(CUBEULATOR_ICON_FILE "${_ico_file}")
    set(CUBEULATOR_PUBLISHER "${CUBEULATOR_PUBLISHER}")
    set(CUBEULATOR_COPYRIGHT "${CUBEULATOR_COPYRIGHT}")
    include("${PROJECT_SOURCE_DIR}/cmake/GenerateVersion.cmake")
endfunction()
_cube_stamp_rc()

add_custom_target(cube_app_resource ALL
    BYPRODUCTS "${_rc_file}"
    COMMAND "${CMAKE_COMMAND}"
        "-DCUBEULATOR_VERSION=${PROJECT_VERSION}"
        "-DSRC=${_rc_template}"
        "-DDST=${_rc_file}"
        "-DGIT_EXECUTABLE=${GIT_EXECUTABLE}"
        "-DWORKDIR=${PROJECT_SOURCE_DIR}"
        "-DCUBEULATOR_ICON_FILE=${_ico_file}"
        "-DCUBEULATOR_PUBLISHER=${CUBEULATOR_PUBLISHER}"
        "-DCUBEULATOR_COPYRIGHT=${CUBEULATOR_COPYRIGHT}"
        -P "${PROJECT_SOURCE_DIR}/cmake/GenerateVersion.cmake"
    COMMENT "Stamping the Windows version resource"
    VERBATIM)
add_dependencies(${target} cube_app_resource)
if(CUBEULATOR_ICONS_AVAILABLE)
    add_dependencies(cube_app_resource cube_icons)
endif()

target_sources(${target} PRIVATE "${_rc_file}")
set_source_files_properties("${_rc_file}" PROPERTIES
    GENERATED TRUE
    OBJECT_DEPENDS "${_ico_file}")

# --- install rules ----------------------------------------------------------
set(_bindir "${CMAKE_INSTALL_BINDIR}")
cube_install_desktop_executable(${target} "${_bindir}")

# Dependencies that came from vcpkg: OpenCV, ONNX Runtime and, under them,
# everything they load. Resolved transitively at install time
# (cmake/InstallRuntimeDependencies.cmake); Qt's own are deployed by windeployqt
# in the script below.
set(_dep_dirs "$<TARGET_FILE_DIR:${target}>")
if(DEFINED _VCPKG_INSTALLED_DIR AND DEFINED VCPKG_TARGET_TRIPLET)
    list(INSERT _dep_dirs 0 "${_VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin")
endif()
install(CODE "
    set(CUBE_EXE \"$<TARGET_FILE:${target}>\")
    set(CUBE_DEP_DIRS \"${_dep_dirs}\")
    set(CUBE_DEST_DIR \"${_bindir}\")
")
install(SCRIPT "${PROJECT_SOURCE_DIR}/cmake/InstallRuntimeDependencies.cmake")

# The C++ runtime goes beside the executable (app-local), rather than
# windeployqt's default of bundling the 25 MB vc_redist installer, which
# nothing in the installer would run.
set(CMAKE_INSTALL_SYSTEM_RUNTIME_DESTINATION "${_bindir}")
set(CMAKE_INSTALL_UCRT_LIBRARIES FALSE)
include(InstallRequiredSystemLibraries)

# Qt's runtime, plugins and the QML modules the app imports: windeployqt.
cube_deploy_qt_on_install(${target} NO_COMPILER_RUNTIME)

# The licence and the third-party notices are installed by
# cube_add_third_party_notices() (cmake/ThirdPartyNotices.cmake), to the root of
# the install.
