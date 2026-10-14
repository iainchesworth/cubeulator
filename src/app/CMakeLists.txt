# ---------------------------------------------------------------------------
# cube_platform - the platform-abstraction interfaces (camera,
# paths, platform_info, crash_handler), each with exactly one
# compiled implementation per OS. Built unconditionally (not gated behind
# CUBEULATOR_BUILD_APP) so the per-OS selection is exercised on every preset,
# including headless CUBEULATOR_BUILD_APP=OFF builds. No Qt dependency: the
# GUI shell further down links this, but this target itself has none (bar
# the one Qt library Android's camera needs, added below).
#
# Which implementations those are, and what they link against, is decided
# by the platform's own fragment - src/app/platform/<os>/platform.cmake,
# selected once in cmake/Platform.cmake. Nothing in this file tests the OS.
# ---------------------------------------------------------------------------
add_library(cube_platform
    interfaces/camera.hpp
    interfaces/crash_handler.hpp
    interfaces/paths.hpp
    interfaces/platform_info.hpp
    platform/detail/capture_thread_scope.hpp
    platform/detail/opencv_capture_loop.hpp
    platform/detail/opencv_capture_loop.cpp
    # Built on every platform, including the ones whose backend cannot
    # control photometry: PhotometryLockReport::summary() and friends are
    # pure bookkeeping, and the "this platform cannot" wording they produce
    # is exactly what those platforms need.
    platform/detail/photometry_lock.hpp
    platform/detail/photometry_lock.cpp
    ${CUBEULATOR_PLATFORM_SOURCES})
add_library(cube::platform ALIAS cube_platform)

target_include_directories(cube_platform PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")

# What the platform's C runtime needs of its callers (the MSVC CRT flags
# std::getenv "unsafe", for one) - see the fragment for the reasoning.
target_compile_definitions(cube_platform PRIVATE ${CUBEULATOR_PLATFORM_COMPILE_DEFINITIONS})

target_link_libraries(cube_platform
    PUBLIC
        cube::lib
    PRIVATE
        cube::warnings
        cube::sanitizers
        cube::coverage
        ${CUBEULATOR_PLATFORM_LINK_LIBRARIES})

# ---------------------------------------------------------------------------
# cube_app - the Qt6 Quick (QML) GUI shell, shared across every platform (see
# docs/architecture.md). The ios-clang preset still sets
# CUBEULATOR_BUILD_APP=OFF (Qt-for-iOS packaging isn't wired up yet); every
# other preset, android-clang included, builds this block.
# ---------------------------------------------------------------------------
if(CUBEULATOR_BUILD_APP)
    find_package(Qt6 6.5 REQUIRED COMPONENTS Core Concurrent Quick Qml Gui QuickControls2 Quick3D ShaderTools LabsSettings)
    qt_standard_project_setup(REQUIRES 6.5)

    # The Qt libraries the platform's own implementations need (Android's
    # camera uses QJniObject for the permission-settings deep link; see its
    # fragment). cube_platform is otherwise deliberately Qt-free.
    target_link_libraries(cube_platform PRIVATE ${CUBEULATOR_PLATFORM_QT_LINK_LIBRARIES})

    # ---------------------------------------------------------------------
    # cube_app_controllers - QObject-derived bridge types exposed to QML
    # (CameraController etc.). A separate target from cube_app_qml so it's
    # linkable into a Catch2 test binary without pulling in QQmlEngine;
    # registered into the "Cubeulator" QML module imperatively via
    # qmlRegisterSingletonInstance in main.cpp rather than QML_ELEMENT,
    # since QML_ELEMENT's compile-time registration only works for sources
    # compiled directly into a qt_add_qml_module target. SolveController
    # lives here (not in cube_app_qml) for the same reason: the scan
    # controller hands it a cube::solver::Cube, which cannot cross into QML,
    # so the two are constructed and wired together in C++.
    # ---------------------------------------------------------------------
    add_library(cube_app_controllers STATIC
        qt/controllers/app_controllers.hpp
        qt/controllers/app_controllers.cpp
        qt/controllers/camera_controller.hpp
        qt/controllers/camera_controller.cpp
        qt/controllers/diagnostic_records.hpp
        qt/controllers/diagnostic_records.cpp
        qt/controllers/diagnostics.hpp
        qt/controllers/diagnostics.cpp
        qt/controllers/direct_scan.hpp
        qt/controllers/direct_scan.cpp
        qt/controllers/held_cube.hpp
        qt/controllers/held_cube.cpp
        qt/controllers/platform_facts.hpp
        qt/controllers/platform_facts.cpp
        qt/controllers/frame_image_provider.hpp
        qt/controllers/frame_image_provider.cpp
        qt/solve_controller.hpp
        qt/solve_controller.cpp)

    target_include_directories(cube_app_controllers PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/qt")

    target_link_libraries(cube_app_controllers
        PUBLIC
            cube::platform
            Qt6::Core
            Qt6::Concurrent
            Qt6::Quick
        PRIVATE
            cube::warnings
            cube::sanitizers
            cube::coverage)

    # STATIC: cube_app_qml is embedded directly into the cube_app executable
    # (via WHOLE_ARCHIVE below), not loaded as a runtime plugin from a
    # qmldir. Qt's own build here is shared, so qt_add_qml_module would
    # otherwise default cube_app_qml to SHARED too - WHOLE_ARCHIVE has no
    # effect on a shared library, so nothing forces its Cubeulator module
    # registration to run: on Linux the .so still gets loaded unconditionally
    # as an explicit link dependency (registration runs as a side effect),
    # but on Windows the linker drops an unreferenced DLL import entirely,
    # so QQmlApplicationEngine fails with "No module named Cubeulator found"
    # (confirmed in a local windows-msvc-debug run).
    # QT_QML_SINGLETON_TYPE is set explicitly below rather than relying on
    # "pragma Singleton" auto-detection: with it alone, Theme/AppState were
    # registered as plain (non-singleton) types in the generated qmldir,
    # so every "Theme.xxx"/"AppState.xxx" reference resolved to undefined
    # everywhere (confirmed in a local linux-gcc-debug run).
    set_source_files_properties(qt/Theme.qml qt/AppState.qml qt/SettingsState.qml PROPERTIES
        QT_QML_SINGLETON_TYPE TRUE)

    qt_add_qml_module(cube_app_qml
        URI Cubeulator
        VERSION 1.0
        STATIC
        QML_FILES
            qt/Main.qml
            qt/Theme.qml
            qt/AppState.qml
            qt/SettingsState.qml
            qt/AppButton.qml
            qt/Icon.qml
            qt/CubeView.qml
            qt/Cube3DView.qml
            qt/StatsOverlay.qml
            qt/LaunchScreen.qml
            qt/PermissionScreen.qml
            qt/DirectScanScreen.qml
            qt/SolveScreen.qml
            qt/SolveMethodPicker.qml
            qt/SettingsScreen.qml
            qt/CompleteScreen.qml
            qt/SolutionScreen.qml
            qt/TurnArrow.qml
        SOURCES
            qt/CameraPermission.hpp
            qt/CameraPermission.cpp
            qt/SystemTheme.hpp
            qt/SystemTheme.cpp
        RESOURCES
            qt/shaders/toon.frag
            qt/shaders/cel.frag
            qt/shaders/celshade_post.frag
            qt/lightprobe/sky.ktx)

    # Qt6::Gui: SystemTheme.cpp uses QGuiApplication/QStyleHints, which live
    # in Gui, not Core (unlike QCameraPermission, added to Core specifically
    # so apps didn't need to link Gui/Multimedia just to ask permission).
    # cube::platform: CameraPermission.cpp calls
    # cube::platform::camera::open_permission_settings(). Qt6::Qml: needed
    # for CameraPermission's own QML_ELEMENT registration (SolveController
    # moved to cube_app_controllers - see the comment there - and no longer
    # needs anything from this target).
    target_link_libraries(cube_app_qml PRIVATE cube::platform Qt6::Gui Qt6::Qml)

    # The generated cube_app_qml_qmltyperegistrations.cpp includes
    # CameraPermission's QML_ELEMENT header via "#include <CameraPermission.hpp>"
    # (bare filename), which resolves via the include *path*, not relative
    # to the source file's own directory - so qt/ itself needs to be on the
    # include path, not just this directory (confirmed: fails with
    # "undeclared identifier" otherwise).
    target_include_directories(cube_app_qml PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/qt")

    qt_add_executable(cube_app
        qt/main.cpp)

    # Target properties the platform's packaging needs (Android's custom
    # manifest, for one): <property> <value> pairs from its fragment.
    if(CUBEULATOR_PLATFORM_APP_PROPERTIES)
        set_target_properties(cube_app PROPERTIES ${CUBEULATOR_PLATFORM_APP_PROPERTIES})
    endif()

    target_link_libraries(cube_app
        PRIVATE
            $<LINK_LIBRARY:WHOLE_ARCHIVE,cube_app_qml>
            cube_app_controllers
            cube::platform
            cube::warnings
            cube::sanitizers
            cube::coverage
            Qt6::Quick
            Qt6::Qml
            Qt6::Gui
            Qt6::QuickControls2
            Qt6::Quick3D
            Qt6::LabsSettings)

    add_dependencies(cube_app cube_version)

    cube_deploy_sanitizer_runtime(cube_app)

    set_target_properties(cube_app PROPERTIES
        WIN32_EXECUTABLE ON
        MACOSX_BUNDLE ON)

    # Puts Qt's runtime next to the built app where the platform needs that
    # (Windows runs windeployqt; macOS has no equivalent wired yet - see the
    # fragments).
    cube_platform_deploy_qt_runtime(cube_app "${CMAKE_CURRENT_SOURCE_DIR}/qt")
endif()

# ---------------------------------------------------------------------------
# cube_app_i18n - the translations plumbing (E2b, docs/i18n.md). Own block,
# appended: nothing above is touched.
#
#   docs/design/strings.json -> tools/i18n/strings_to_ts.py -> i18n/translations/*.ts
#   -> lrelease -idbased -nounfinished (here) -> :/i18n/cubeulator_<code>.qm
#
# The catalogues are generated and committed (a test and a CI step fail if
# they drift from strings.json), not made at build time: a translator edits
# them, and E5 reviews the diff. -idbased because every string in the app is
# a qsTrId(); -nounfinished so that an entry still marked unfinished is left
# out of the .qm and the English underneath answers (cube_app_i18n installs
# the English catalogue under every language).
# ---------------------------------------------------------------------------
if(CUBEULATOR_BUILD_APP)
    find_package(Qt6 6.5 REQUIRED COMPONENTS LinguistTools)

    set(CUBE_I18N_DIR "${CMAKE_CURRENT_SOURCE_DIR}/i18n")
    set(CUBE_I18N_TS_DIR "${CUBE_I18N_DIR}/translations")
    file(GLOB CUBE_I18N_TS_FILES CONFIGURE_DEPENDS "${CUBE_I18N_TS_DIR}/cubeulator_*.ts")

    add_library(cube_app_i18n STATIC
        qt/i18n/language_manager.hpp
        qt/i18n/language_manager.cpp)

    target_include_directories(cube_app_i18n PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/qt")

    target_link_libraries(cube_app_i18n
        PUBLIC
            Qt6::Core
            Qt6::Gui
            Qt6::Qml
        PRIVATE
            cube::warnings
            cube::sanitizers
            cube::coverage)

    qt_add_lrelease(
        TS_FILES ${CUBE_I18N_TS_FILES}
        LRELEASE_TARGET cube_app_i18n_lrelease
        QM_FILES_OUTPUT_VARIABLE CUBE_I18N_QM_FILES
        OPTIONS -idbased -nounfinished -silent)

    qt_add_resources(cube_app_i18n "cube_app_i18n_catalogues"
        PREFIX "/i18n"
        BASE "${CMAKE_CURRENT_BINARY_DIR}"
        FILES ${CUBE_I18N_QM_FILES})
    qt_add_resources(cube_app_i18n "cube_app_i18n_languages"
        PREFIX "/i18n"
        BASE "${CUBE_I18N_DIR}"
        FILES "${CUBE_I18N_DIR}/languages.json")

    # The shell links the manager; main.cpp (and, once E0a lands, AppRuntime)
    # calls cube::i18n::install(). A separate statement so that the block
    # above stays untouched.
    target_link_libraries(cube_app PRIVATE cube_app_i18n)
endif()

# ---------------------------------------------------------------------------
# Third-party notices (docs/licensing.md). Generated from the build so they
# cannot drift from what ships: the cube_third_party_notices target writes
# third-party-notices.json and THIRD-PARTY-NOTICES.txt before cube_app links,
# embeds them as :/licences/* for the About and Licences screens, and installs
# the text file with LICENSE into every desktop package. Append-only block of
# the licensing workstream (E0b).
# ---------------------------------------------------------------------------
if(TARGET cube_app)
    include(ThirdPartyNotices)
    cube_add_third_party_notices(cube_app)
endif()

# ---------------------------------------------------------------------------
# Bundled fonts: whatever a build compiles in under :/fonts is registered at
# start-up (qt/kit/bundled_fonts.cpp, called from main.cpp). The platform
# fragment adds the files - the browser build's Japanese and Chinese faces -
# so nothing here tests the OS. Part of cube_app_qml, which cube_app links
# whole, so that main.cpp finds the function.
# ---------------------------------------------------------------------------
if(CUBEULATOR_BUILD_APP)
    target_sources(cube_app_qml PRIVATE
        qt/kit/bundled_fonts.hpp
        qt/kit/bundled_fonts.cpp)
endif()

# ---------------------------------------------------------------------------
# cube_app_runtime - the application's start-up, shared by cube_app (main.cpp)
# and the system tests (tests/system): the controllers (AppControllers),
# their registration as QML singletons, the language (cube_app_i18n) and the
# engine. A separate target from cube_app_controllers because that one stays
# free of QQmlEngine.
# ---------------------------------------------------------------------------
if(CUBEULATOR_BUILD_APP)
    add_library(cube_app_runtime STATIC
        qt/app_runtime.hpp
        qt/app_runtime.cpp)

    target_include_directories(cube_app_runtime PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/qt")

    target_link_libraries(cube_app_runtime
        PUBLIC
            cube_app_controllers
            cube_app_i18n
            Qt6::Qml
            Qt6::Quick
        PRIVATE
            cube::warnings
            cube::sanitizers
            cube::coverage)

    target_link_libraries(cube_app PRIVATE cube_app_runtime)
endif()

# E4a - desktop packaging and manners (experience lane; this block only).
#
# cube_app_desktop holds what the shells add to start-up: the remembered window
# geometry, the KeepAwake helper and the process identity. It is a separate
# static library so that the tests link it without the QML module. What
# differs between platforms - keep awake, identity, window traits - is the
# platform's own (interfaces/desktop_shell.hpp), added to the library by the
# fragment's cube_platform_configure_desktop_shell(); and
# cube_platform_configure_desktop_app() gives cube_app its icon, version
# resource or bundle metadata, and install rules. Nothing here tests the OS.
# See docs/packaging.md and docs/platform-isolation.md.
# ===========================================================================
if(CUBEULATOR_BUILD_APP)
    find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Qml Quick)

    add_library(cube_app_desktop STATIC
        desktop/desktop_integration.hpp
        desktop/desktop_integration.cpp
        desktop/keep_awake.hpp
        desktop/keep_awake.cpp
        desktop/window_geometry.hpp
        desktop/window_geometry.cpp
        interfaces/desktop_shell.hpp)
    target_include_directories(cube_app_desktop PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(cube_app_desktop
        PUBLIC
            cube::lib
            Qt6::Core
            Qt6::Gui
            Qt6::Qml
            Qt6::Quick
        PRIVATE
            cube::warnings
            cube::sanitizers
            cube::coverage)
    cube_platform_configure_desktop_shell(cube_app_desktop)

    # AppRuntime owns what the screens read from the shell (KeepAwake,
    # AppInfo), so that the system tests boot the same Main.qml with the same
    # singletons the app has.
    target_link_libraries(cube_app_runtime PUBLIC cube_app_desktop)
    target_link_libraries(cube_app PRIVATE cube_app_desktop)

    # DesktopShell.qml: shortcuts, the macOS menu bar and the placeholder
    # About. Added here rather than to the module's own file list above.
    qt_target_qml_sources(cube_app_qml QML_FILES qt/DesktopShell.qml)

    # A phone is packaged by its own tools (androiddeployqt, Xcode), which is
    # why only a desktop gets an installable program here.
    if(CUBEULATOR_PLATFORM_DESKTOP)
        include(DesktopPackaging)
        cube_configure_desktop_app(cube_app)
    endif()
endif()
