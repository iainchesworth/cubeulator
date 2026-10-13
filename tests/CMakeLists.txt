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
    unit/direct_scan_test.cpp
    unit/held_cube_test.cpp
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
    unit/solver/hold_test.cpp
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
    integration/camera_lock_verification_test.cpp
    # Detector + tracking loop together on a committed capture frame: it
    # needs the deployed ONNX model beside the binary, which is exactly
    # what this suite (unlike the fast unit one) arranges below.
    integration/refine_cadence_test.cpp)

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

# Capture frames (tests/data, local only - see .gitignore) are inputs, not
# deployables - the refine-cadence test reads one from the source tree when
# it is there rather than have the build copy fixtures next to every binary.
target_compile_definitions(cube_integration_tests PRIVATE
    "CUBE_TEST_DATA_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/data\"")

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
        unit/direct_scan_controller_test.cpp
        unit/frame_image_provider_test.cpp
        unit/held_cube_controller_test.cpp
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

    # --- QML tests: the app's own QML, loaded for real. cube_view_test holds
    # the 3D cube on the solve screen to the solver's cube by reading its
    # stickers back out of the scene, so it needs the whole "Cubeulator" QML
    # module (linked whole, as cube_app links it - see src/app/CMakeLists.txt
    # for why) and a QGuiApplication (qml_test_main.cpp). Nothing is drawn:
    # it runs on the offscreen platform.
    find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Qml Quick Quick3D)

    add_executable(cube_qml_tests
        qml_test_main.cpp
        unit/cube_view_test.cpp
        unit/direct_scan_screen_test.cpp
        unit/solve_camera_inset_test.cpp
        unit/solve_screen_gallery_test.cpp
        unit/solve_screen_test.cpp)

    target_link_libraries(cube_qml_tests
        PRIVATE
            $<LINK_LIBRARY:WHOLE_ARCHIVE,cube_app_qml>
            cube_app_controllers
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
        set_tests_properties(i18n_catalogues_match_strings i18n_no_unwrapped_literals
            i18n_lint_selftest i18n_ids_in_code_exist
            PROPERTIES LABELS "i18n" ENVIRONMENT "PYTHONUTF8=1")
    else()
        message(WARNING "Python 3 not found: the translation gates that are scripts are not registered")
    endif()
endif()

# --- System tests: the whole application driven from its UI (tests/system) ---
# Needs a platform that can draw; labelled `system` so CI's plain ctest legs
# can exclude it. See docs/testing.md.
if(CUBEULATOR_BUILD_APP)
    add_subdirectory(system)
endif()
