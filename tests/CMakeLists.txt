# ---------------------------------------------------------------------------
# Test suites. Catch2 provides both the framework and CTest integration.
# ---------------------------------------------------------------------------
find_package(Catch2 3 REQUIRED)
include(Catch)

# --- Unit tests: fast, isolated, one behaviour each -----------------------
add_executable(cube_unit_tests
    unit/error_test.cpp
    unit/version_test.cpp
    unit/model_test.cpp
    unit/grid_detection_test.cpp
    unit/pose_refiner_test.cpp
    unit/face_projection_test.cpp
    unit/session_palette_test.cpp
    unit/face_observation_test.cpp
    unit/temporal_fusion_test.cpp
    unit/track_fusion_test.cpp
    unit/color_resolution_test.cpp
    unit/corner_observation_test.cpp
    unit/edge_observation_test.cpp
    unit/orientation_lattice_test.cpp
    unit/orientation_tracker_test.cpp
    unit/tracker_sweep_test.cpp
    unit/pattern_id_test.cpp
    unit/patch_purity_test.cpp
    unit/centre_cluster_assembly_test.cpp
    unit/centre_cluster_verify_test.cpp
    unit/centre_palette_test.cpp
    unit/capture_routing_test.cpp
    unit/store_to_assembly_test.cpp
    unit/crash_hunt_test.cpp
    unit/pose_estimation_test.cpp
    unit/frame_debug_test.cpp
    unit/cross_session_test.cpp
    unit/routing_replay_test.cpp
    unit/assembly_probe_test.cpp
    unit/raw_probe_test.cpp
    unit/union_assembly_test.cpp
    unit/frame_recording_test.cpp
    unit/pipeline_scaffold_test.cpp
    unit/recording_v2_test.cpp
    unit/groundtruth_test.cpp
    unit/corner_detector_test.cpp
    unit/pose_from_corners_test.cpp
    unit/camera_intrinsics_test.cpp
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
    integration/hand_presence_integration_test.cpp
    # Hardware probe: hidden behind the "[.camera-probe]" tag AND gated on
    # CUBE_CAMERA_PROBE, so linking it here can never make CI (or a
    # camera-less machine) try to open a device. See the file header.
    integration/camera_property_probe_test.cpp
    # Runs everywhere, CI included: the lock's reporting/bookkeeping, with
    # no device involved. Its one "no capture is running" case deliberately
    # calls the real platform API to prove it fails without opening one.
    integration/camera_photometry_test.cpp
    # Hardware verification of the lock's effect on real pixels: hidden
    # behind "[.camera-lock]" AND gated on CUBE_CAMERA_LOCK_TEST, same
    # double fence as the probe above.
    integration/camera_lock_verification_test.cpp)

target_link_libraries(cube_integration_tests
    PRIVATE
        cube::lib
        cube::platform
        cube::warnings
        cube::sanitizers
        cube::coverage
        Catch2::Catch2WithMain)

# The two hardware tests read their opt-in env var with std::getenv, which
# the MSVC CRT flags "unsafe" - and this build treats warnings as errors.
# Same targeted silencing cube_platform already applies for the same call
# (see src/app/CMakeLists.txt); the usage is correct and portable, and the
# suggested _dupenv_s alternative is Windows-only. Also covers clang-cl,
# which uses the same MSVC CRT headers.
target_compile_definitions(cube_integration_tests PRIVATE
    "$<$<OR:$<CXX_COMPILER_ID:MSVC>,$<AND:$<PLATFORM_ID:Windows>,$<CXX_COMPILER_ID:Clang>>>:_CRT_SECURE_NO_WARNINGS>")

cube_deploy_sanitizer_runtime(cube_integration_tests)
cube_deploy_vision_assets(cube_integration_tests)

# WORKING_DIRECTORY: cube_deploy_vision_assets() copies model assets next to
# the built executable (CMAKE_RUNTIME_OUTPUT_DIRECTORY), so the ONNX
# integration tests' relative model path only resolves if ctest actually
# runs the binary from there rather than its own invocation directory.
catch_discover_tests(cube_integration_tests DISCOVERY_MODE PRE_TEST
    WORKING_DIRECTORY "$<TARGET_FILE_DIR:cube_integration_tests>")

# --- Vendored M3T tracker smoke tests (desktop only) ----------------------
# A separate tiny executable rather than more cases in cube_unit_tests:
# linking cube::m3t would hang GLEW/GLFW/OpenMP (and their DLL loads on
# Windows) onto every fast-suite test process, purely for two hidden smoke
# cases. Here that runtime baggage is confined to one binary. The heavy
# cases are doubly fenced like the camera probes: hidden "[.m3t-smoke]" tag
# AND a CUBE_M3T_SMOKE opt-in (they create a GL context and generate a
# region model); one trivial visible case keeps the link line exercised in
# every ordinary ctest run. Run the smoke checks with:
#   set CUBE_M3T_SMOKE=1 && cube_m3t_tests "[.m3t-smoke]"
if(TARGET cube::m3t)
    add_executable(cube_m3t_tests unit/m3t_vendored_test.cpp)

    target_link_libraries(cube_m3t_tests
        PRIVATE
            cube::m3t
            cube::warnings
            cube::sanitizers
            cube::coverage
            Catch2::Catch2WithMain)

    # std::getenv gate, same targeted silencing as cube_integration_tests.
    target_compile_definitions(cube_m3t_tests PRIVATE
        "$<$<OR:$<CXX_COMPILER_ID:MSVC>,$<AND:$<PLATFORM_ID:Windows>,$<CXX_COMPILER_ID:Clang>>>:_CRT_SECURE_NO_WARNINGS>")

    cube_deploy_sanitizer_runtime(cube_m3t_tests)

    catch_discover_tests(cube_m3t_tests DISCOVERY_MODE PRE_TEST ADD_TAGS_AS_LABELS)
endif()

# --- Qt controller tests: needs a QCoreApplication for Qt's meta-object
# machinery (signals/slots, NOTIFY properties), so it can't use
# Catch2::Catch2WithMain's auto-generated main() like the two binaries
# above - see qt_test_main.cpp. Gated behind CUBEULATOR_BUILD_APP since
# cube_app_controllers only exists then; doesn't affect the coverage gate
# (linux-gcc-coverage builds with CUBEULATOR_BUILD_APP=OFF).
if(CUBEULATOR_BUILD_APP)
    # Qml as well as Core: scan_session_binding_test drives the controllers
    # through real QML bindings, which is the only way to cover the
    # property-notification path the scan screen actually depends on.
    find_package(Qt6 6.5 REQUIRED COMPONENTS Core Qml)

    add_executable(cube_qt_controller_tests
        qt_test_main.cpp
        unit/camera_controller_test.cpp
        unit/scan_session_test.cpp
        unit/scan_session_binding_test.cpp
        unit/settings_model_test.cpp
        unit/solve_controller_test.cpp)

    target_link_libraries(cube_qt_controller_tests
        PRIVATE
            cube_app_controllers
            cube::warnings
            cube::sanitizers
            cube::coverage
            Qt6::Core
            Qt6::Qml
            Catch2::Catch2)

    cube_deploy_sanitizer_runtime(cube_qt_controller_tests)
    cube_deploy_vision_assets(cube_qt_controller_tests)

    catch_discover_tests(cube_qt_controller_tests DISCOVERY_MODE PRE_TEST
        WORKING_DIRECTORY "$<TARGET_FILE_DIR:cube_qt_controller_tests>")
endif()
