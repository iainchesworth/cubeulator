# ---------------------------------------------------------------------------
# cube::lib - solver/vision/render/i18n placeholders, no GUI, no platform code.
# ---------------------------------------------------------------------------
add_library(cube_lib)
add_library(cube::lib ALIAS cube_lib)

# fmt fills the gap std::format leaves on Android: the NDK's libc++ doesn't
# implement <format> (it needs locale support the NDK's minimal libc doesn't
# provide). Resolved via vcpkg on every triplet, including mobile - unlike
# opencv4/onnxruntime/bgfx, fmt has no native/platform-specific code to make
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
        src/model/scan_progress.cpp
        src/model/color_scheme.cpp
        src/solver/cube.cpp
        src/solver/scan_conversion.cpp
        src/solver/scan_assembly.cpp
        src/solver/notation.cpp
        src/solver/outside_view.cpp
        src/solver/hold.cpp
        src/solver/cross.cpp
        src/solver/corners.cpp
        src/solver/middle_edges.cpp
        src/solver/oll.cpp
        src/solver/pll.cpp
        src/solver/solver.cpp
        src/solver/stepped_solution.cpp
        src/vision/vision.cpp
        src/vision/grid_detection.cpp
        src/vision/face_observation.cpp
        src/vision/face_grid_fit.cpp
        src/vision/edge_observation.cpp
        src/vision/temporal_fusion.cpp
        src/vision/track_fusion.cpp
        src/vision/color_resolution.cpp
        src/vision/corner_observation.cpp
        src/vision/orientation_lattice.cpp
        src/vision/orientation_tracker.cpp
        src/vision/capture_routing.cpp
        src/vision/pose_estimation.cpp
        src/vision/color_classification.cpp
        src/vision/stability.cpp
        src/vision/auto_advance.cpp
        src/vision/hand_presence.cpp
        src/vision/idle_gate.cpp
        src/vision/pipeline/trace.cpp
        src/vision/pipeline/legacy_pipeline.cpp
        src/vision/pipeline/detect_track_loop.cpp
        src/vision/pipeline/pose_refiner.cpp
        src/vision/pipeline/face_projection.cpp
        src/vision/recording_v2.cpp
        src/vision/groundtruth.cpp
        src/vision/corner_detector.cpp
        src/vision/pose_from_corners.cpp
        src/vision/camera_intrinsics.cpp
        src/vision/direct_scan.cpp
        src/vision/held_cube.cpp
        src/render/render.cpp
        src/i18n/i18n.cpp)

add_dependencies(cube_lib cube_version)

target_include_directories(cube_lib
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/generated>"
        "$<INSTALL_INTERFACE:include>")

target_compile_features(cube_lib PUBLIC cxx_std_23)

target_link_libraries(cube_lib
    PUBLIC
        cube::opencv
    PRIVATE
        cube::onnxruntime
        cube::warnings
        cube::sanitizers
        cube::coverage
        fmt::fmt)
