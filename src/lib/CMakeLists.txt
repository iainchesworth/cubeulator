# ---------------------------------------------------------------------------
# cube::lib - solver/vision/render/i18n placeholders, no GUI, no platform code.
# ---------------------------------------------------------------------------
add_library(cube_lib)
add_library(cube::lib ALIAS cube_lib)

# --- Version + git stamp ---------------------------------------------------
find_package(Git QUIET)
set(CUBEULATOR_VERSION_TEMPLATE "${CMAKE_CURRENT_SOURCE_DIR}/include/cube/version.hpp.in")
set(CUBEULATOR_VERSION_HEADER "${CMAKE_CURRENT_BINARY_DIR}/generated/cube/version.hpp")

# Generate once at configure time so the header exists for the first build.
set(CUBEULATOR_VERSION "${PROJECT_VERSION}")
set(SRC "${CUBEULATOR_VERSION_TEMPLATE}")
set(DST "${CUBEULATOR_VERSION_HEADER}")
set(WORKDIR "${PROJECT_SOURCE_DIR}")
include("${PROJECT_SOURCE_DIR}/cmake/GenerateVersion.cmake")

# Re-stamp on every build so the git information tracks new commits without a
# reconfigure. configure_file only rewrites when the content changes, so this
# does not needlessly recompile dependants.
add_custom_target(cube_version ALL
    BYPRODUCTS "${CUBEULATOR_VERSION_HEADER}"
    COMMAND "${CMAKE_COMMAND}"
        "-DCUBEULATOR_VERSION=${PROJECT_VERSION}"
        "-DSRC=${CUBEULATOR_VERSION_TEMPLATE}"
        "-DDST=${CUBEULATOR_VERSION_HEADER}"
        "-DGIT_EXECUTABLE=${GIT_EXECUTABLE}"
        "-DWORKDIR=${PROJECT_SOURCE_DIR}"
        -P "${PROJECT_SOURCE_DIR}/cmake/GenerateVersion.cmake"
    COMMENT "Stamping version and git information"
    VERBATIM)

target_sources(cube_lib
    PRIVATE
        src/version.cpp
        src/solver/solver.cpp
        src/vision/vision.cpp
        src/render/render.cpp
        src/i18n/i18n.cpp)

add_dependencies(cube_lib cube_version)

target_include_directories(cube_lib
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/generated>"
        "$<INSTALL_INTERFACE:include>")

target_compile_features(cube_lib PUBLIC cxx_std_23)

target_link_libraries(cube_lib PRIVATE cube::warnings cube::sanitizers cube::coverage)
