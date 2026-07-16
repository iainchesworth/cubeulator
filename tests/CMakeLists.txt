# ---------------------------------------------------------------------------
# Test suites. Catch2 provides both the framework and CTest integration.
# ---------------------------------------------------------------------------
find_package(Catch2 3 REQUIRED)
include(Catch)

# --- Unit tests: fast, isolated, one behaviour each -----------------------
add_executable(cube_unit_tests
    unit/error_test.cpp
    unit/version_test.cpp
    unit/scan_session_config_test.cpp)

target_link_libraries(cube_unit_tests
    PRIVATE
        cube::lib
        cube::warnings
        cube::sanitizers
        cube::coverage
        Catch2::Catch2WithMain)

cube_deploy_sanitizer_runtime(cube_unit_tests)

# DISCOVERY_MODE PRE_TEST: default POST_BUILD discovery runs the freshly
# linked (ASan-instrumented) binary immediately after build to list its
# tests, which collides with CodeQL's manual build-mode analysis (its own
# LD_PRELOAD trace library must load first). PRE_TEST defers discovery to
# `ctest` invocation instead.
catch_discover_tests(cube_unit_tests DISCOVERY_MODE PRE_TEST)

# --- Integration tests: exercise several components together --------------
add_executable(cube_integration_tests
    integration/platform_integration_test.cpp)

target_link_libraries(cube_integration_tests
    PRIVATE
        cube::lib
        cube::platform
        cube::warnings
        cube::sanitizers
        cube::coverage
        Catch2::Catch2WithMain)

cube_deploy_sanitizer_runtime(cube_integration_tests)

catch_discover_tests(cube_integration_tests DISCOVERY_MODE PRE_TEST)
