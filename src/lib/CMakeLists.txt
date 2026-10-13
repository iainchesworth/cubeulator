# ---------------------------------------------------------------------------
# cube::lib - solver, vision and i18n, no GUI, no platform code.
# ---------------------------------------------------------------------------
add_library(cube_lib)
add_library(cube::lib ALIAS cube_lib)

# fmt fills the gap std::format leaves on Android: the NDK's libc++ doesn't
# implement <format> (it needs locale support the NDK's minimal libc doesn't
# provide). Resolved via vcpkg on every triplet, including mobile - unlike
# opencv4, fmt has no native/platform-specific code to make
# mobile triplets risky (see docs/dependency-strategy.md).
find_package(fmt CONFIG REQUIRED)

# --- Version + git stamp ---------------------------------------------------
find_package(Git QUIET)
set(CUBEULATOR_VERSION_TEMPLATE "${CMAKE_CURRENT_SOURCE_DIR}/include/cube/version.hpp.in")
set(CUBEULATOR_VERSION_HEADER "${CMAKE_CURRENT_BINARY_DIR}/generated/cube/version.hpp")

# What this build targets ("Windows x86_64 (MSVC 1951)"), worked out from
# the platform fragment and the toolchain here rather than by predefined
# macros inside a source file - this library holds no platform code.
cube_describe_build_target(CUBEULATOR_BUILD_TARGET)

# Generate once at configure time so the header exists for the first build.
set(CUBEULATOR_VERSION "${CUBEULATOR_VERSION_FULL}")
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
        "-DCUBEULATOR_VERSION=${CUBEULATOR_VERSION_FULL}"
        "-DCUBEULATOR_BUILD_TARGET=${CUBEULATOR_BUILD_TARGET}"
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
        src/solver/cube.cpp
        src/solver/scan_assembly.cpp
        src/solver/notation.cpp
        src/solver/outside_view.cpp
        src/solver/hold.cpp
        src/solver/cross.cpp
        src/solver/corners.cpp
        src/solver/middle_edges.cpp
        src/solver/oll.cpp
        src/solver/pll.cpp
        src/solver/cubie.cpp
        src/solver/two_phase.cpp
        src/solver/two_phase_tables.cpp
        src/solver/cfop_last_layer.cpp
        src/solver/cfop.cpp
        src/solver/solver.cpp
        src/solver/stepped_solution.cpp
        src/vision/color_resolution.cpp
        src/vision/color_classification.cpp
        src/vision/recording_v2.cpp
        src/vision/direct_scan.cpp
        src/vision/corner_view.cpp
        src/vision/held_cube.cpp
        src/i18n/i18n.cpp)

# Two files make the compiler build lookup tables from loops: the two-phase
# solver's phase 1 move tables (two_phase_tables.cpp, about 85 thousand
# entries) and the last-layer algorithms, parsed from their text
# (cfop_last_layer.cpp). They fit inside every compiler's default
# constant-evaluation budget today - by a factor of only 1.5 to 2 on Clang and
# MSVC - but a budget is a count of steps a vendor chose, so these two files
# are given plenty of room rather than being one added move table away from a
# build error. Only these files: the budget exists to stop runaway evaluation,
# and everything else should keep that protection.
#
# The flag differs by compiler (clang-cl accepts the Clang spelling only
# through /clang:).
if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang" AND CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
    set(CUBEULATOR_CONSTEXPR_BUDGET "/clang:-fconstexpr-steps=10000000")
elseif(MSVC)
    set(CUBEULATOR_CONSTEXPR_BUDGET "/constexpr:steps10000000")
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    set(CUBEULATOR_CONSTEXPR_BUDGET "-fconstexpr-ops-limit=200000000")
else()
    set(CUBEULATOR_CONSTEXPR_BUDGET "-fconstexpr-steps=10000000")
endif()
set_source_files_properties(
    src/solver/two_phase_tables.cpp
    src/solver/cfop_last_layer.cpp
    PROPERTIES COMPILE_OPTIONS "${CUBEULATOR_CONSTEXPR_BUDGET}")

add_dependencies(cube_lib cube_version)

# Which cube/profiling.hpp the zone macros resolve to: CMake chooses the
# directory, not an #ifdef in the header (see cmake/Tracy.cmake). PUBLIC
# because every layer above this one - the app's capture loop and controllers,
# the tracker wrapper, the tools - is instrumented with the same macros, and
# the real variant includes Tracy's own headers, which cube::tracy supplies.
if(CUBEULATOR_ENABLE_TRACY)
    set(CUBEULATOR_PROFILING_DIR "${CMAKE_CURRENT_SOURCE_DIR}/variants/profiling-tracy_enabled")
else()
    set(CUBEULATOR_PROFILING_DIR "${CMAKE_CURRENT_SOURCE_DIR}/variants/profiling-tracy_disabled")
endif()

target_include_directories(cube_lib
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/generated>"
        "$<BUILD_INTERFACE:${CUBEULATOR_PROFILING_DIR}>"
        "$<INSTALL_INTERFACE:include>")

target_compile_features(cube_lib PUBLIC cxx_std_23)

target_link_libraries(cube_lib
    PUBLIC
        cube::opencv
        cube::tracy
    PRIVATE
        cube::warnings
        cube::sanitizers
        cube::coverage
        fmt::fmt)
