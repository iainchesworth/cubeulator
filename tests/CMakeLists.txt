# ---------------------------------------------------------------------------
# Test suites. Catch2 provides both the framework and CTest integration.
# ---------------------------------------------------------------------------
find_package(Catch2 3 REQUIRED)
include(Catch)

# --- Unit tests: fast, isolated, one behaviour each -----------------------
add_executable(cube_unit_tests
    unit/error_test.cpp
    unit/version_test.cpp
    unit/scan_session_config_test.cpp
    unit/model_test.cpp
    unit/grid_detection_test.cpp
    unit/face_observation_test.cpp
    unit/temporal_fusion_test.cpp
    unit/color_resolution_test.cpp
    unit/corner_observation_test.cpp
    unit/edge_observation_test.cpp
    unit/orientation_lattice_test.cpp
    unit/orientation_tracker_test.cpp
    unit/tracker_sweep_test.cpp
    unit/capture_routing_test.cpp
    unit/pose_estimation_test.cpp
    unit/frame_debug_test.cpp
    unit/cross_session_test.cpp
    unit/union_assembly_test.cpp
    unit/frame_recording_test.cpp
    unit/color_classification_test.cpp
    unit/stability_test.cpp
    unit/auto_advance_test.cpp
    unit/vision_facade_test.cpp
    unit/hand_presence_capability_test.cpp
    unit/idle_gate_test.cpp
    unit/lib_stub_functions_test.cpp
    unit/solver/cube_test.cpp
    unit/solver/last_layer_test.cpp
    unit/solver/scan_conversion_test.cpp
    unit/solver/scan_assembly_test.cpp
    unit/solver/notation_test.cpp
    unit/solver/cross_test.cpp
    unit/solver/corners_test.cpp
    unit/solver/middle_edges_test.cpp
    unit/solver/oll_test.cpp
    unit/solver/pll_test.cpp
    unit/solver/solve_test.cpp
    unit/solver/stepped_solution_test.cpp)

target_link_libraries(cube_unit_tests
    PRIVATE
        cube::lib
        cube::gpu_render
        cube::warnings
        cube::sanitizers
        cube::coverage
        Catch2::Catch2WithMain)

# White-box include path for last_layer_test.cpp only: it includes
# cube::lib's private last_layer.hpp directly, because the F2L-preservation
# invariant it verifies is a property of the alphabet *constants*
# themselves (deliberately not public API) - testing separately-written
# copies of the algorithms would not catch a typo in the real ones.
target_include_directories(cube_unit_tests
    PRIVATE "${CMAKE_SOURCE_DIR}/src/lib/src/solver")

cube_deploy_sanitizer_runtime(cube_unit_tests)

# DISCOVERY_MODE PRE_TEST: default POST_BUILD discovery runs the freshly
# linked (ASan-instrumented) binary immediately after build to list its
# tests, which collides with CodeQL's manual build-mode analysis (its own
# LD_PRELOAD trace library must load first). PRE_TEST defers discovery to
# `ctest` invocation instead.
#
# ADD_TAGS_AS_LABELS: promotes each TEST_CASE's Catch2 tags to CTest LABELS,
# so the solver tests tagged "[slow]" (each drives many random scrambles
# through the searched first-two-layer stages - see docs/testing.md) can be
# excluded from a quick local loop with `ctest -LE slow`, or run on their
# own with `ctest -L slow`.
catch_discover_tests(cube_unit_tests DISCOVERY_MODE PRE_TEST ADD_TAGS_AS_LABELS)

# --- Integration tests: exercise several components together --------------
add_executable(cube_integration_tests
    integration/platform_integration_test.cpp
    integration/hand_presence_integration_test.cpp)

target_link_libraries(cube_integration_tests
    PRIVATE
        cube::lib
        cube::platform
        cube::warnings
        cube::sanitizers
        cube::coverage
        Catch2::Catch2WithMain)

cube_deploy_sanitizer_runtime(cube_integration_tests)
cube_deploy_vision_assets(cube_integration_tests)

# WORKING_DIRECTORY: cube_deploy_vision_assets() copies model assets next to
# the built executable (CMAKE_RUNTIME_OUTPUT_DIRECTORY), so the ONNX
# integration tests' relative model path only resolves if ctest actually
# runs the binary from there rather than its own invocation directory.
catch_discover_tests(cube_integration_tests DISCOVERY_MODE PRE_TEST
    WORKING_DIRECTORY "$<TARGET_FILE_DIR:cube_integration_tests>")

# --- Qt controller tests: needs a QCoreApplication for Qt's meta-object
# machinery (signals/slots, NOTIFY properties), so it can't use
# Catch2::Catch2WithMain's auto-generated main() like the two binaries
# above - see qt_test_main.cpp. Gated behind CUBEULATOR_BUILD_APP since
# cube_app_controllers only exists then; doesn't affect the coverage gate
# (linux-gcc-coverage builds with CUBEULATOR_BUILD_APP=OFF).
if(CUBEULATOR_BUILD_APP)
    find_package(Qt6 6.5 REQUIRED COMPONENTS Core)

    add_executable(cube_qt_controller_tests
        qt_test_main.cpp
        unit/camera_controller_test.cpp
        unit/scan_session_test.cpp
        unit/settings_model_test.cpp
        unit/solve_controller_test.cpp)

    target_link_libraries(cube_qt_controller_tests
        PRIVATE
            cube_app_controllers
            cube::warnings
            cube::sanitizers
            cube::coverage
            Qt6::Core
            Catch2::Catch2)

    cube_deploy_sanitizer_runtime(cube_qt_controller_tests)
    cube_deploy_vision_assets(cube_qt_controller_tests)

    catch_discover_tests(cube_qt_controller_tests DISCOVERY_MODE PRE_TEST
        WORKING_DIRECTORY "$<TARGET_FILE_DIR:cube_qt_controller_tests>")
endif()
