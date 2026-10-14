# ---------------------------------------------------------------------------
# Test suites. Catch2 provides both the framework and CTest integration.
# ---------------------------------------------------------------------------
find_package(Catch2 3 REQUIRED)
include(Catch)

# --- What every test binary takes from the platform -------------------------
# Decided by the platform's fragment (cmake/Platform.cmake), so no test file
# branches on the OS; every test target below links it. It carries:
#   - the defines first-party code needs from the platform's C runtime
#     (std::getenv is flagged "unsafe" by the MSVC CRT, and this build
#     treats warnings as errors);
#   - cube_test_platform_facts.hpp, generated from platform_facts.hpp.in: the
#     Qt platform plugin the Qt test binaries fall back to, how a platform's
#     desktop session reaches a program (see test_platform.hpp), and whether
#     the system tests' window is made invisible;
#   - tests/ and that header's directory on the include path.
# A fragment may write its variables as a CMake list; the header wants them
# space-separated.
string(REPLACE ";" " " CUBEULATOR_PLATFORM_TEST_DISPLAY_VARIABLES
    "${CUBEULATOR_PLATFORM_TEST_DISPLAY_VARIABLES}")
# ON/OFF in the fragment, a C++ literal in the header.
if(CUBEULATOR_PLATFORM_TEST_HIDE_WINDOW)
    set(CUBEULATOR_PLATFORM_TEST_HIDE_WINDOW_CXX "true")
else()
    set(CUBEULATOR_PLATFORM_TEST_HIDE_WINDOW_CXX "false")
endif()

configure_file("${CMAKE_CURRENT_SOURCE_DIR}/platform_facts.hpp.in"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/cube_test_platform_facts.hpp" @ONLY)

add_library(cube_test_platform INTERFACE)
target_compile_definitions(cube_test_platform INTERFACE ${CUBEULATOR_PLATFORM_COMPILE_DEFINITIONS})
target_include_directories(cube_test_platform INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_BINARY_DIR}/generated")
if(CUBEULATOR_PLATFORM_TEST_LINK_OPTIONS)
    target_link_options(cube_test_platform INTERFACE ${CUBEULATOR_PLATFORM_TEST_LINK_OPTIONS})
endif()

# --- Every platform fragment satisfies the platform contract -----------------
# Includes each src/app/platform/*/platform.cmake in turn and holds it to
# cmake/PlatformContract.cmake, on whichever host this runs: the platforms a
# leg does not build are checked here too (see the script's own header).
add_test(NAME platform_fragments_satisfy_contract
    COMMAND "${CMAKE_COMMAND}" "-DREPO_ROOT=${PROJECT_SOURCE_DIR}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/platform/check_fragments.cmake")

# --- Unit tests: fast, isolated, one behaviour each -----------------------
add_executable(cube_unit_tests
    unit/error_test.cpp
    unit/version_test.cpp
    unit/profiling_test.cpp
    unit/profile_recorder_test.cpp
    unit/color_resolution_test.cpp
    unit/frame_recording_test.cpp
    unit/recording_v2_test.cpp
    unit/diagnostics_test.cpp
    unit/direct_scan_test.cpp
    unit/sticker_colour_test.cpp
    unit/read_set_test.cpp
    unit/corner_view_test.cpp
    unit/held_cube_test.cpp
    unit/lib_stub_functions_test.cpp
    unit/solver/cube_test.cpp
    unit/solver/last_layer_test.cpp
    unit/solver/scan_assembly_test.cpp
    unit/solver/notation_test.cpp
    unit/solver/hold_test.cpp
    unit/solver/cross_test.cpp
    unit/solver/corners_test.cpp
    unit/solver/middle_edges_test.cpp
    unit/solver/oll_test.cpp
    unit/solver/pll_test.cpp
    unit/solver/solve_test.cpp
    unit/solver/cubie_test.cpp
    unit/solver/two_phase_test.cpp
    unit/solver/cfop_test.cpp
    unit/solver/stepped_solution_test.cpp
    unit/solver/piece_search_test.cpp)

target_link_libraries(cube_unit_tests
    PRIVATE
        cube::lib
        cube_test_platform
        cube::warnings
        cube::sanitizers
        cube::coverage
        Catch2::Catch2WithMain)

# profiling_test.cpp asks which cube/profiling.hpp variant CMake selected
# (src/lib/CMakeLists.txt) and checks it against the option that drove the
# choice, so a wrong directory on the include path cannot go unnoticed.
set_source_files_properties(unit/profiling_test.cpp PROPERTIES
    COMPILE_DEFINITIONS
        "CUBE_EXPECT_TRACY=$<BOOL:${CUBEULATOR_ENABLE_TRACY}>;CUBE_EXPECT_RECORDER=$<BOOL:${CUBEULATOR_ENABLE_PROFILE_RECORDER}>")

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

# --- Performance tests: work budgets and a few relative timings -------------
# cube_perf_tests holds the tier that fails a change that makes the app
# meaningfully slower, the way a unit test fails a change that makes it wrong:
# a budget on what the solver and the reader do (nodes visited, pixel buffers
# allocated), exact on every machine, and wall-time checks against a
# calibration workload that only run on request. Its own binary so that it can
# be built and run alone (the dispatch-only `perf` CI leg builds nothing else)
# and so that nothing else's static state or allocator churn is in the way of a
# stopwatch. cube::lib only: no Qt, which is also why it runs on a leg with
# CUBEULATOR_BUILD_APP=OFF. docs/testing.md, "Performance tests".
add_executable(cube_perf_tests
    perf/measure.cpp
    perf/metrics.cpp
    perf/solver_work_test.cpp
    perf/scan_work_test.cpp
    perf/timing_test.cpp
    perf/report_test.cpp
    perf/profiler_listener.cpp)

target_link_libraries(cube_perf_tests
    PRIVATE
        cube::lib
        cube_test_platform
        cube::warnings
        cube::sanitizers
        cube::coverage
        Catch2::Catch2WithMain)

# drawn_cube.hpp, shared with the scan tests; and the solver's private
# headers, for the hard caps the budgets are held under (kMaxVisitedNodes,
# kMaxLastLayerStates) - the same white-box path last_layer_test.cpp uses.
target_include_directories(cube_perf_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/perf"
    "${CMAKE_CURRENT_SOURCE_DIR}/unit"
    "${CMAKE_SOURCE_DIR}/src/lib/src/solver")

# What a build is, as far as a stopwatch goes (perf/perf_build.hpp). Values
# CMake passes in, not preprocessor conditionals: the configuration is a
# generator expression so that a multi-config generator is right per
# configuration, and the instrumented flag is the options that put coverage
# or a sanitizer in. The sanitizers only ever apply to Debug, which is not an
# optimised configuration, so the coverage option is the whole of it.
target_compile_definitions(cube_perf_tests PRIVATE
    "CUBE_PERF_OPTIMISED=$<IF:$<CONFIG:Release,RelWithDebInfo,MinSizeRel>,1,0>"
    "CUBE_PERF_INSTRUMENTED=$<IF:$<BOOL:${CUBEULATOR_ENABLE_COVERAGE}>,1,0>"
    "CUBE_PERF_BUILD_TYPE=\"$<CONFIG>\"")

cube_deploy_sanitizer_runtime(cube_perf_tests)

# Two registrations of one binary, because a stopwatch is the one test that
# cannot share the machine: [timing] cases are RUN_SERIAL, so `ctest -j` runs
# them alone, and they skip themselves unless CUBE_PERF_TIMING is set (the
# *-perf test presets set it; docs/testing.md says why they are opt-in). The
# budget cases are plain counts and run in parallel with everything else. The
# hidden [perf-report] case is in neither: it is a tool, not a test.
catch_discover_tests(cube_perf_tests DISCOVERY_MODE PRE_TEST ADD_TAGS_AS_LABELS
    TEST_SPEC "[perf]~[timing]")
catch_discover_tests(cube_perf_tests DISCOVERY_MODE PRE_TEST ADD_TAGS_AS_LABELS
    TEST_SPEC "[timing]"
    PROPERTIES RUN_SERIAL TRUE)

# The optional Tracy comparison (tools/perf/tracy_check.py) is not run by any
# test - it needs a profiling build and a capture - but its parsing and its
# comparison are, on a canned export, so that it cannot rot unseen.
find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
    add_test(NAME perf_tracy_check_selftest
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tools/perf/test_tracy_check.py")
    set_tests_properties(perf_tracy_check_selftest PROPERTIES
        LABELS "perf" ENVIRONMENT "PYTHONUTF8=1")
endif()

# --- Integration tests: exercise several components together --------------
add_executable(cube_integration_tests
    integration/platform_integration_test.cpp)

# The camera cases go through the OpenCV capture loop's backend
# (detail::capture_backend()) and call the interface from the test's own thread,
# which only the platforms with a native camera device allow; the rest have a
# camera of their own to be tested through their own means (the browser's is
# tests/web's, on a real getUserMedia).
if(CUBEULATOR_PLATFORM_TEST_NATIVE_CAMERA)
    target_sources(cube_integration_tests PRIVATE
        # The interface with no device behind it.
        integration/platform_camera_test.cpp
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
endif()

target_link_libraries(cube_integration_tests
    PRIVATE
        cube::lib
        cube::platform
        cube_test_platform
        cube::warnings
        cube::sanitizers
        cube::coverage
        Catch2::Catch2WithMain)

# Test cases only one platform can run (its own paths, its own environment
# conventions) live in tests/platform/<name>/, keyed by the platform's name
# like src/app/platform is. A platform with none simply has no directory.
file(GLOB CUBE_PLATFORM_TEST_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/platform/${CUBEULATOR_PLATFORM}/*.cpp")
target_sources(cube_integration_tests PRIVATE ${CUBE_PLATFORM_TEST_SOURCES})

cube_deploy_sanitizer_runtime(cube_integration_tests)

catch_discover_tests(cube_integration_tests DISCOVERY_MODE PRE_TEST
    WORKING_DIRECTORY "$<TARGET_FILE_DIR:cube_integration_tests>")

# --- Qt controller tests: needs a QCoreApplication for Qt's meta-object
# machinery (signals/slots, NOTIFY properties), so it can't use
# Catch2::Catch2WithMain's auto-generated main() like the two binaries
# above - see qt_test_main.cpp. Gated behind CUBEULATOR_BUILD_APP since
# cube_app_controllers only exists then; doesn't affect the coverage gate
# (linux-gcc-coverage builds with CUBEULATOR_BUILD_APP=OFF).
if(CUBEULATOR_BUILD_APP)
    find_package(Qt6 6.5 REQUIRED COMPONENTS Core Qml)

    add_executable(cube_qt_controller_tests
        qt_test_main.cpp
        unit/camera_controller_test.cpp
        unit/diagnostics_controller_test.cpp
        unit/direct_scan_controller_test.cpp
        unit/frame_image_provider_test.cpp
        unit/held_cube_controller_test.cpp
        unit/platform_facts_test.cpp
        unit/solve_controller_test.cpp
        unit/solve_history_test.cpp)

    target_link_libraries(cube_qt_controller_tests
        PRIVATE
            cube_app_controllers
            cube_test_platform
            cube::warnings
            cube::sanitizers
            cube::coverage
            Qt6::Core
            Qt6::Qml
            Catch2::Catch2)

    cube_deploy_sanitizer_runtime(cube_qt_controller_tests)

    catch_discover_tests(cube_qt_controller_tests DISCOVERY_MODE PRE_TEST
        WORKING_DIRECTORY "$<TARGET_FILE_DIR:cube_qt_controller_tests>")

    # --- QML tests: the app's own QML, loaded for real. cube_view_test holds
    # the 3D cube on the solve screen to the solver's cube by reading its
    # stickers back out of the scene, so it needs the whole "Cubeulator" QML
    # module (linked whole, as cube_app links it - see src/app/CMakeLists.txt
    # for why) and a QGuiApplication (qml_test_main.cpp). Nothing is drawn:
    # it runs on the offscreen platform.
    find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Qml Quick Quick3D)

    add_executable(cube_qml_tests
        qml_test_main.cpp
        unit/complete_screen_test.cpp
        unit/cube_view_test.cpp
        unit/direct_scan_screen_test.cpp
        unit/solve_camera_inset_test.cpp
        unit/solve_screen_gallery_test.cpp
        unit/solve_screen_test.cpp
        unit/solution_screen_test.cpp)

    target_link_libraries(cube_qml_tests
        PRIVATE
            $<LINK_LIBRARY:WHOLE_ARCHIVE,cube_app_qml>
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
            Catch2::Catch2)

    # qml_test_app.hpp, shared by every test file here, sits beside the main.
    target_include_directories(cube_qml_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")

    # On Windows the Qt runtime and the QML modules the app imports are
    # copied next to cube_app.exe when it is built; this binary sits in the
    # same directory and runs off the same copy.
    add_dependencies(cube_qml_tests cube_app)

    cube_deploy_sanitizer_runtime(cube_qml_tests)

    catch_discover_tests(cube_qml_tests DISCOVERY_MODE PRE_TEST
        WORKING_DIRECTORY "$<TARGET_FILE_DIR:cube_qml_tests>")
endif()

# ---------------------------------------------------------------------------
# Translation gates (E2b, docs/i18n.md). Own block, appended.
#
# The family's gates, adapted to ids: the catalogues are regenerated from
# strings.json and must not change; nothing user-visible is a string literal;
# the catalogues hold what a translation must (nothing empty, placeholders and
# brand terms kept, the same ids everywhere, plural forms); a missing
# translation shows the English and never an id; a language switch changes
# direction, locale and strings together.
# ---------------------------------------------------------------------------
if(CUBEULATOR_BUILD_APP)
    find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Qml)

    add_executable(cube_i18n_tests
        i18n/i18n_test_main.cpp
        i18n/catalogue.hpp
        i18n/catalogue.cpp
        i18n/catalogue_test.cpp
        i18n/translation_runtime_test.cpp
        i18n/language_manager_test.cpp)

    target_link_libraries(cube_i18n_tests
        PRIVATE
            cube_app_i18n
            cube_test_platform
            cube::warnings
            cube::sanitizers
            cube::coverage
            Qt6::Core
            Qt6::Gui
            Qt6::Qml
            Catch2::Catch2)

    target_include_directories(cube_i18n_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/i18n")

    # The tests read the catalogues and strings.json as the files they are, and
    # ask Qt's own lrelease which plural form each language's rule picks.
    target_compile_definitions(cube_i18n_tests PRIVATE
        "CUBE_I18N_TS_DIR=\"${CMAKE_SOURCE_DIR}/src/app/i18n/translations\""
        "CUBE_LANGUAGES_JSON=\"${CMAKE_SOURCE_DIR}/src/app/i18n/languages.json\""
        "CUBE_STRINGS_JSON=\"${CMAKE_SOURCE_DIR}/docs/design/strings.json\""
        "CUBE_LRELEASE_EXECUTABLE=\"$<TARGET_FILE:Qt6::lrelease>\"")

    cube_deploy_sanitizer_runtime(cube_i18n_tests)

    catch_discover_tests(cube_i18n_tests DISCOVERY_MODE PRE_TEST ADD_TAGS_AS_LABELS
        WORKING_DIRECTORY "$<TARGET_FILE_DIR:cube_i18n_tests>")

    # The converted screens, switched live between languages: loaded as items
    # with the app's own controllers (the way cube_qml_tests loads the solve
    # screen) and read back. The whole app through its windows is the system
    # tests' (tests/system).
    find_package(Qt6 6.5 REQUIRED COMPONENTS Quick Quick3D)

    add_executable(cube_i18n_screen_tests
        i18n/screen_test_main.cpp
        i18n/screen_app.hpp
        i18n/screen_test.cpp)

    target_link_libraries(cube_i18n_screen_tests
        PRIVATE
            $<LINK_LIBRARY:WHOLE_ARCHIVE,cube_app_qml>
            cube_app_controllers
            cube_app_i18n
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
            Catch2::Catch2)

    # cube_view_reading.hpp, the solve lane's reading of the 3D cube, is read
    # here and not copied.
    target_include_directories(cube_i18n_screen_tests PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/unit"
        "${CMAKE_CURRENT_SOURCE_DIR}/i18n")

    # As cube_qml_tests: the Qt runtime and the QML modules are copied next to
    # cube_app.exe when it is built, and this binary runs off that copy.
    add_dependencies(cube_i18n_screen_tests cube_app)

    cube_deploy_sanitizer_runtime(cube_i18n_screen_tests)

    catch_discover_tests(cube_i18n_screen_tests DISCOVERY_MODE PRE_TEST ADD_TAGS_AS_LABELS
        WORKING_DIRECTORY "$<TARGET_FILE_DIR:cube_i18n_screen_tests>")

    # The system tests (tests/system, E0a) gain the language cases when they
    # are in the tree: the language switched live in the real window, English
    # to the pseudo-locale to Hebrew and back, the layout mirrored and the cube
    # not, nothing cut off. Deferred, so that it does not matter whether this
    # block comes before or after tests/system in this file. The cases need
    # cube::i18n::install() in AppRuntime's constructor (docs/i18n.md).
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/system/CMakeLists.txt")
        function(cube_i18n_extend_system_tests)
            if(TARGET cube_system_tests)
                target_sources(cube_system_tests PRIVATE
                    "${CMAKE_CURRENT_SOURCE_DIR}/i18n/system/language_test.cpp")
                target_link_libraries(cube_system_tests PRIVATE cube_app_i18n)
            endif()
        endfunction()
        cmake_language(DEFER CALL cube_i18n_extend_system_tests)
    endif()

    # The two gates that are scripts: the catalogues against strings.json (the
    # family's "rerun the update and fail on a diff") and the literal lint.
    find_package(Python3 COMPONENTS Interpreter)
    if(Python3_Interpreter_FOUND)
        add_test(NAME i18n_catalogues_match_strings
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tools/i18n/strings_to_ts.py" --check)
        add_test(NAME i18n_no_unwrapped_literals
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tools/i18n/lint_strings.py")
        add_test(NAME i18n_lint_selftest
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tools/i18n/test_lint_strings.py")
        # Every id the code asks for is in strings.json, found by Qt's own
        # lupdate so that what is checked is what the tooling sees.
        add_test(NAME i18n_ids_in_code_exist
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tools/i18n/check_ids.py"
                    --lupdate "$<TARGET_FILE:Qt6::lupdate>")
        # The browser build bundles its own Japanese and Chinese faces, cut down
        # to the characters the catalogues use: a translation that brings in a
        # character they lack is found here, not as an empty box on a page.
        add_test(NAME i18n_web_cjk_subsets_cover_catalogues
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tools/fonts/subset_cjk.py" --check)
        set_tests_properties(i18n_catalogues_match_strings i18n_no_unwrapped_literals
            i18n_lint_selftest i18n_ids_in_code_exist i18n_web_cjk_subsets_cover_catalogues
            PROPERTIES LABELS "i18n" ENVIRONMENT "PYTHONUTF8=1")
    else()
        message(WARNING "Python 3 not found: the translation gates that are scripts are not registered")
    endif()
endif()

# --- Release tooling (tools/release, docs/releasing.md) --------------------
# The changelog's shape, the cut, the release notes' layout and the version
# edit are held by the tools' own tests; the same file also checks that this
# repository's CHANGELOG.md and CMakeLists.txt are in a state the tools can
# work with, so that the first thing to find out is not a tag.
find_package(Python3 COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
    add_test(NAME release_tools_selftest
        COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_SOURCE_DIR}/tools/release/test_release_tools.py")
    set_tests_properties(release_tools_selftest
        PROPERTIES LABELS "release" ENVIRONMENT "PYTHONUTF8=1")

    # The notices generator on a static Qt's link line (the browser build):
    # QML plugin registration objects and resource bundles are linked as
    # objects, and are attributed by where they sit under the Qt prefix.
    # Runs the real generator over a made-up file API reply; needs no Qt.
    add_test(NAME notices_static_qt_selftest
        COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_SOURCE_DIR}/tools/licences/test_static_qt_objects.py")
    set_tests_properties(notices_static_qt_selftest
        PROPERTIES LABELS "licensing"
                   ENVIRONMENT "PYTHONUTF8=1;CMAKE_COMMAND=${CMAKE_COMMAND}")
endif()

# --- Third-party notices (E0b, docs/licensing.md) --------------------------
# Fails when a component that ships has no notice: a linked library, a Qt
# module or QML import, a deployed DLL, a file under assets/, a directory
# under third_party/. Append-only block of the licensing workstream.
if(TARGET cube_app)
    include(ThirdPartyNotices)
    cube_add_third_party_notices_test(cube_app)
endif()

# --- System tests: the whole application driven from its UI (tests/system) ---
# Needs a platform that can draw; labelled `system`, which the committed test
# presets exclude (run it with `<preset>-system`). See docs/testing.md.
if(CUBEULATOR_BUILD_APP)
    add_subdirectory(system)
endif()

# --- E4a: desktop manners (window geometry, KeepAwake) ---------------------
# Its own binary rather than more cases in cube_qt_controller_tests: it links
# cube_app_desktop (and with it Qt Gui) and nothing from the controllers.
# Plain QCoreApplication from qt_test_main.cpp is enough: no window is shown.
if(CUBEULATOR_BUILD_APP AND TARGET cube_app_desktop)
    add_executable(cube_desktop_tests
        qt_test_main.cpp
        unit/keep_awake_test.cpp
        unit/window_geometry_test.cpp)

    target_link_libraries(cube_desktop_tests
        PRIVATE
            cube_app_desktop
            cube::warnings
            cube::sanitizers
            cube::coverage
            Qt6::Core
            Catch2::Catch2)

    # What only this platform's desktop shell can show (tests/platform/<name>/
    # desktop/), keyed by the platform's name like src/app/platform is.
    file(GLOB CUBE_DESKTOP_PLATFORM_TEST_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/platform/${CUBEULATOR_PLATFORM}/desktop/*.cpp")
    target_sources(cube_desktop_tests PRIVATE ${CUBE_DESKTOP_PLATFORM_TEST_SOURCES})

    cube_deploy_sanitizer_runtime(cube_desktop_tests)

    catch_discover_tests(cube_desktop_tests DISCOVERY_MODE PRE_TEST
        WORKING_DIRECTORY "$<TARGET_FILE_DIR:cube_desktop_tests>")
endif()

# The placeholder About, driven through the real window by the system tests
# (tests/system, E0a). Added to that binary from here, deferred, as the
# translation block above does, so that it does not matter whether tests/system
# comes before or after this block in this file.
if(CUBEULATOR_BUILD_APP AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/system/CMakeLists.txt")
    function(cube_desktop_extend_system_tests)
        if(TARGET cube_system_tests AND TARGET cube_app_desktop)
            target_sources(cube_system_tests PRIVATE
                "${CMAKE_CURRENT_SOURCE_DIR}/system/desktop_shell_test.cpp")
        endif()
    endfunction()
    cmake_language(DEFER CALL cube_desktop_extend_system_tests)
endif()
