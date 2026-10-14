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
# the screenshots have to be rendered. That is why this is not part of a plain
# `ctest` run: it carries the label `system`, which the committed test presets
# exclude (and CI's build-and-test legs say -LE system as well). Run it with
# the matching `<preset>-system` test preset.
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
    known_today_test.cpp
    history_test.cpp
    progress_listener.cpp)

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
#
# The directory is also the one the third_party_notices_complete test scans for
# what the app deploys. Qt6Test is not the app's, so the manifest lists it
# under qt.testOnlyModules (third_party/notices/manifest.json); a further Qt
# module copied here for a test needs a line there too.
cube_platform_deploy_qt_library(cube_system_tests Qt6::Test)

cube_deploy_sanitizer_runtime(cube_system_tests)

# Deliberately not catch_discover_tests: each entry below is a run of the app
# of its own (one engine, one window, a case beginning from App::fresh()), and
# the reach gate judges what all of them saw together. The screenshot tour is
# its own entry as well: it is the slowest part by far, and a picture that
# cannot be taken should not hide what the walk found.
set(CUBE_SYSTEM_OUT "${CMAKE_BINARY_DIR}/system-test-output")

# The walk, cut into shards. On a software renderer the whole suite is around
# half an hour - frames are what cost - and one ctest entry for it either hits
# its timeout or hides which case was the slow one. Catch2 cuts the declaration
# order list into this many contiguous runs (--shard-count/--shard-index); every
# case is in exactly one, and a case added tomorrow is in one without being
# told. Each shard writes its own reach report; system.reach merges them.
#
# Declaration order: the gate is deterministic. Catch2's default is a random
# order, which is how a case that leaves state behind is found - run the
# executable with --order rand (and --rng-seed) for that.
set(CUBE_SYSTEM_WALK_SHARDS 5 CACHE STRING
    "How many ctest entries (system.walk.<n>of<count>) the system walk is cut into")
set(cube_system_walk_tests "")
foreach(shard RANGE 1 ${CUBE_SYSTEM_WALK_SHARDS})
    math(EXPR shard_index "${shard} - 1")
    set(walk_test "system.walk.${shard}of${CUBE_SYSTEM_WALK_SHARDS}")
    add_test(NAME ${walk_test}
        COMMAND cube_system_tests "~[.restart]~[tour]" --order decl
                --shard-count ${CUBE_SYSTEM_WALK_SHARDS} --shard-index ${shard_index}
        WORKING_DIRECTORY "$<TARGET_FILE_DIR:cube_system_tests>")
    # The timeout is a safety net sized for a slow runner, not a budget: the
    # progress lines the run prints say where the time went.
    set_tests_properties(${walk_test} PROPERTIES
        LABELS "system"
        TIMEOUT 1200
        ENVIRONMENT "CUBE_SYSTEM_TEST_OUT=${CUBE_SYSTEM_OUT}")
    list(APPEND cube_system_walk_tests ${walk_test})
endforeach()

# The screenshot tour: a picture of every screen and state, narrow and wide,
# light and dark, in one entry per layout so the two can run side by side.
# Writes <build>/system-test-output/tour/<layout>/screens/*.png. They are the
# longest entries by far, so they say so (COST) and a parallel run starts them
# first rather than leaving one for last.
foreach(layout narrow wide)
    set(tour_test "system.tour.${layout}")
    add_test(NAME ${tour_test}
        COMMAND cube_system_tests "[tour][${layout}]"
        WORKING_DIRECTORY "$<TARGET_FILE_DIR:cube_system_tests>")
    set_tests_properties(${tour_test} PROPERTIES
        LABELS "system"
        TIMEOUT 1200
        COST 1000
        ENVIRONMENT "CUBE_SYSTEM_TEST_OUT=${CUBE_SYSTEM_OUT}/tour/${layout}")
endforeach()

# The reach gate: whether the walk visited every screen, state and control in
# the inventory. Reads the reports the shards wrote and judges their union
# (and fails if a shard's is missing); needs no display. Catch2 runs a binary's
# cases in no order a gate could rely on, so this is its own entry, ordered
# after the walk.
add_test(NAME system.reach
    COMMAND cube_system_tests --check-reach "${CUBE_SYSTEM_OUT}"
    WORKING_DIRECTORY "$<TARGET_FILE_DIR:cube_system_tests>")
set_tests_properties(system.reach PROPERTIES
    LABELS "system"
    DEPENDS "${cube_system_walk_tests}"
    TIMEOUT 60)

# A second launch of the app, with the theme already in the settings store.
add_test(NAME system.relaunch
    COMMAND cube_system_tests "[.restart]"
    WORKING_DIRECTORY "$<TARGET_FILE_DIR:cube_system_tests>")
set_tests_properties(system.relaunch PROPERTIES
    LABELS "system"
    TIMEOUT 300
    ENVIRONMENT "CUBE_SYSTEM_TEST_OUT=${CUBE_SYSTEM_OUT}/relaunch;CUBE_SYSTEM_TEST_THEME=dark")
