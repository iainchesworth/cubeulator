# ---------------------------------------------------------------------------
# System tests: the whole application, driven from its UI.
#
# cube_system_tests boots the real Main.qml with the real controllers
# (cube::app::AppRuntime - the class main() uses), shows the window, and walks
# the journey with mouse, touch and key events. Only the camera underneath is
# a test double (fake_camera.cpp, frames drawn by tests/unit/drawn_cube.hpp).
# See docs/testing.md, "System tests".
#
# Needs a platform that can draw - a desktop session, or Xvfb with software GL
# on a headless Linux box - because Qt Quick 3D has no software fallback and
# the screenshots have to be rendered. That is why this is not part of the
# plain `ctest` run CI's build-and-test legs do: it carries the label
# `system`, and those legs say -LE system.
# ---------------------------------------------------------------------------
find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Qml Quick Quick3D QuickControls2 LabsSettings Test)

add_executable(cube_system_tests
    system_test_main.cpp
    app.cpp
    diagnostics.cpp
    fake_camera.cpp
    reach.cpp
    journey_test.cpp
    settings_test.cpp
    layout_test.cpp
    input_test.cpp
    sweep_test.cpp
    render_test.cpp
    tour_test.cpp
    known_today_test.cpp)

target_include_directories(cube_system_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    # drawn_cube.hpp, shared with the controller tests.
    "${PROJECT_SOURCE_DIR}/tests/unit")

target_link_libraries(cube_system_tests
    PRIVATE
        # Linked whole, as cube_app links it (see src/app/CMakeLists.txt): the
        # "Cubeulator" QML module registers itself from a static initialiser
        # that nothing references.
        $<LINK_LIBRARY:WHOLE_ARCHIVE,cube_app_qml>
        cube_app_runtime
        cube_app_controllers
        cube_test_platform
        cube::lib
        cube::warnings
        cube::sanitizers
        cube::coverage
        Qt6::Core
        Qt6::Gui
        Qt6::Qml
        Qt6::Quick
        Qt6::Quick3D
        Qt6::QuickControls2
        Qt6::LabsSettings
        Qt6::Test
        Catch2::Catch2)

# The Qt runtime and the QML modules the app imports are copied next to
# cube_app when it is built (windeployqt on Windows); this binary sits in the
# same directory and runs off the same copy.
add_dependencies(cube_system_tests cube_app)

# What the platform's Qt deployment does not copy for cube_app because the app
# never needs it: QtTest, which sends the input events. Where that matters
# (Windows: without it the binary dies at start-up with
# STATUS_DLL_NOT_FOUND) the platform's fragment copies it.
cube_platform_deploy_qt_library(cube_system_tests Qt6::Test)

cube_deploy_sanitizer_runtime(cube_system_tests)
cube_deploy_vision_assets(cube_system_tests)

# One ctest entry for the whole suite, deliberately not catch_discover_tests:
# the suite is one session of the app (one engine, one window, state carried
# from case to case on purpose) and the reach gate at the end needs every
# earlier case to have run in the same process.
set(CUBE_SYSTEM_OUT "${CMAKE_BINARY_DIR}/system-test-output")

# Declaration order: the gate is deterministic. Catch2's default is a random
# order, which is how a case that leaves state behind is found - run the
# executable with --order rand (and --rng-seed) for that.
add_test(NAME system.walk
    COMMAND cube_system_tests "~[.restart]" --order decl
    WORKING_DIRECTORY "$<TARGET_FILE_DIR:cube_system_tests>")
set_tests_properties(system.walk PROPERTIES
    LABELS "system"
    TIMEOUT 900
    ENVIRONMENT "CUBE_SYSTEM_TEST_OUT=${CUBE_SYSTEM_OUT}")

# The reach gate: whether the walk visited every screen, state and control in
# the inventory. Reads the report the walk wrote; needs no display. Catch2 runs
# a binary's cases in no order a gate could rely on, so this is its own entry,
# ordered after the walk.
add_test(NAME system.reach
    COMMAND cube_system_tests --check-reach "${CUBE_SYSTEM_OUT}/reach.json"
    WORKING_DIRECTORY "$<TARGET_FILE_DIR:cube_system_tests>")
set_tests_properties(system.reach PROPERTIES
    LABELS "system"
    DEPENDS system.walk
    TIMEOUT 60)

# A second launch of the app, with the theme already in the settings store.
add_test(NAME system.relaunch
    COMMAND cube_system_tests "[.restart]"
    WORKING_DIRECTORY "$<TARGET_FILE_DIR:cube_system_tests>")
set_tests_properties(system.relaunch PROPERTIES
    LABELS "system"
    TIMEOUT 300
    ENVIRONMENT "CUBE_SYSTEM_TEST_OUT=${CUBE_SYSTEM_OUT}/relaunch;CUBE_SYSTEM_TEST_THEME=dark")
