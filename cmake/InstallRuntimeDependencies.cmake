# ---------------------------------------------------------------------------
# InstallRuntimeDependencies.cmake - run at install time (install(SCRIPT ...)
# from cmake/DesktopPackaging.cmake, Windows only).
#
# Copies every DLL cube_app needs, transitively, that is not Qt's and not the
# system's. $<TARGET_RUNTIME_DLLS:...> is not enough: it lists the DLLs of
# the imported targets cube_app links directly (OpenCV, ONNX Runtime) and
# misses what those load in turn - zlib, libpng, libjpeg, libwebp, protobuf,
# abseil, re2 - so the installed app would stop with STATUS_DLL_NOT_FOUND on
# any machine without the vcpkg tree on PATH.
#
# Inputs, set by install(CODE) just before this runs:
#   CUBE_EXE       the executable to start from
#   CUBE_DEP_DIRS  where to look for DLLs that are not on PATH
#   CUBE_DEST_DIR  install prefix-relative directory to copy into
#
# Qt's DLLs are left to windeployqt (which also gets its plugins and QML
# modules); the C++ runtime comes from InstallRequiredSystemLibraries. Both
# turn up here as "unresolved" or are copied twice, which is harmless.
# ---------------------------------------------------------------------------
file(GET_RUNTIME_DEPENDENCIES
    EXECUTABLES "${CUBE_EXE}"
    RESOLVED_DEPENDENCIES_VAR _cube_resolved
    UNRESOLVED_DEPENDENCIES_VAR _cube_unresolved
    DIRECTORIES ${CUBE_DEP_DIRS}
    PRE_EXCLUDE_REGEXES "api-ms-.*" "ext-ms-.*"
    POST_EXCLUDE_REGEXES ".*[/\\][Ww][Ii][Nn][Dd][Oo][Ww][Ss][/\\].*")

foreach(_dep IN LISTS _cube_resolved)
    message(STATUS "Runtime dependency: ${_dep}")
    file(INSTALL DESTINATION "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/${CUBE_DEST_DIR}"
        TYPE SHARED_LIBRARY FILES "${_dep}")
endforeach()

foreach(_dep IN LISTS _cube_unresolved)
    message(STATUS "Not copied (windeployqt's or the system's): ${_dep}")
endforeach()
