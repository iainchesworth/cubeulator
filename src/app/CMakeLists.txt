# ---------------------------------------------------------------------------
# cube_platform - the four platform-abstraction interfaces (camera,
# gpu_surface, inference_env, paths), each with exactly one compiled
# implementation per OS. Built unconditionally (not gated behind
# CUBEULATOR_BUILD_APP) so the per-OS selection logic below is exercised on
# every preset, including headless CUBEULATOR_BUILD_APP=OFF builds. No Qt
# dependency: the GUI shell further down links this, but this target itself
# has none.
#
# ANDROID/IOS must be checked before WIN32/APPLE/UNIX: CMake sets ANDROID
# alongside UNIX, and IOS alongside APPLE, when cross-compiling.
# ---------------------------------------------------------------------------
if(ANDROID)
    set(CUBEULATOR_PLATFORM_DIR platform/android)
    set(CUBEULATOR_PLATFORM_SUFFIX android)
elseif(IOS)
    set(CUBEULATOR_PLATFORM_DIR platform/ios)
    set(CUBEULATOR_PLATFORM_SUFFIX ios)
elseif(WIN32)
    set(CUBEULATOR_PLATFORM_DIR platform/windows)
    set(CUBEULATOR_PLATFORM_SUFFIX windows)
elseif(APPLE)
    set(CUBEULATOR_PLATFORM_DIR platform/macos)
    set(CUBEULATOR_PLATFORM_SUFFIX macos)
elseif(UNIX)
    set(CUBEULATOR_PLATFORM_DIR platform/linux)
    set(CUBEULATOR_PLATFORM_SUFFIX linux)
else()
    message(FATAL_ERROR "Unsupported target platform")
endif()

add_library(cube_platform
    interfaces/camera.hpp
    interfaces/gpu_surface.hpp
    interfaces/inference_env.hpp
    interfaces/paths.hpp
    platform/detail/opencv_capture_loop.hpp
    platform/detail/opencv_capture_loop.cpp
    # Built on every platform, including the ones whose backend cannot
    # control photometry: PhotometryLockReport::summary() and friends are
    # pure bookkeeping, and the "this platform cannot" wording they produce
    # is exactly what those platforms need.
    platform/detail/photometry_lock.hpp
    platform/detail/photometry_lock.cpp
    "${CUBEULATOR_PLATFORM_DIR}/camera_${CUBEULATOR_PLATFORM_SUFFIX}.cpp"
    "${CUBEULATOR_PLATFORM_DIR}/gpu_surface_${CUBEULATOR_PLATFORM_SUFFIX}.cpp"
    "${CUBEULATOR_PLATFORM_DIR}/inference_env_${CUBEULATOR_PLATFORM_SUFFIX}.cpp"
    "${CUBEULATOR_PLATFORM_DIR}/paths_${CUBEULATOR_PLATFORM_SUFFIX}.cpp")
add_library(cube::platform ALIAS cube_platform)

target_include_directories(cube_platform PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")

# std::getenv is flagged "unsafe" by the MSVC CRT; the usage in the
# Windows/macOS/Linux platform implementations is correct and portable, so
# silence that specific deprecation for this target only. This also fires
# under clang-cl (CXX_COMPILER_ID "Clang"), which uses the same MSVC CRT
# headers/annotations on Windows.
target_compile_definitions(cube_platform PRIVATE
    "$<$<OR:$<CXX_COMPILER_ID:MSVC>,$<AND:$<PLATFORM_ID:Windows>,$<CXX_COMPILER_ID:Clang>>>:_CRT_SECURE_NO_WARNINGS>")

target_link_libraries(cube_platform
    PUBLIC
        cube::lib
    PRIVATE
        cube::warnings
        cube::sanitizers
        cube::coverage)

# camera_windows.cpp enumerates via native DirectShow device enumeration
# (ICreateDevEnum/IEnumMoniker) rather than OpenCvCaptureLoop's generic
# probe - see its own comment for why, and for why DirectShow rather than
# Media Foundation. strmiids provides the CLSID_SystemDeviceEnum/
# CLSID_VideoInputDeviceCategory/IID_ICreateDevEnum GUID definitions; ole32
# provides CoCreateInstance/CoInitializeEx; oleaut32 provides the
# VARIANT/BSTR helpers (VariantInit/VariantClear) used to read each
# device's friendly name.
if(WIN32)
    target_link_libraries(cube_platform PRIVATE strmiids ole32 oleaut32)
endif()

# ---------------------------------------------------------------------------
# cube_app - the Qt6 Quick (QML) GUI shell, shared across every platform (see
# docs/architecture.md). The ios-clang preset still sets
# CUBEULATOR_BUILD_APP=OFF (Qt-for-iOS packaging isn't wired up yet); every
# other preset, android-clang included, builds this block.
# ---------------------------------------------------------------------------
if(CUBEULATOR_BUILD_APP)
    find_package(Qt6 6.5 REQUIRED COMPONENTS Core Concurrent Quick Qml Gui QuickControls2 Quick3D ShaderTools LabsSettings)
    qt_standard_project_setup(REQUIRES 6.5)

    # camera_android.cpp uses QJniObject/QNativeInterface for the
    # ACTION_APPLICATION_DETAILS_SETTINGS permission-settings deep link
    # (see interfaces/camera.hpp: open_permission_settings()). cube_platform
    # is otherwise deliberately Qt-free (see the comment on it above) - this
    # holds only while Android also always builds the Qt app (which is the
    # case for every current preset); a genuinely headless
    # CUBEULATOR_BUILD_APP=OFF Android configure would need this revisited.
    if(ANDROID)
        target_link_libraries(cube_platform PRIVATE Qt6::Core)
    endif()

    # ---------------------------------------------------------------------
    # cube_app_controllers - QObject-derived bridge types exposed to QML
    # (CameraController etc.). A separate target from cube_app_qml so it's
    # linkable into a Catch2 test binary without pulling in QQmlEngine;
    # registered into the "Cubeulator" QML module imperatively via
    # qmlRegisterSingletonInstance in main.cpp rather than QML_ELEMENT,
    # since QML_ELEMENT's compile-time registration only works for sources
    # compiled directly into a qt_add_qml_module target. SolveController
    # lives here (not in cube_app_qml, unlike before) for the same reason:
    # it needs a real C++ reference to the live ScanSession instance
    # (cube::model::ScanProgress can't cross into QML), which only works if
    # both are constructed and wired together directly in main.cpp.
    # ---------------------------------------------------------------------
    add_library(cube_app_controllers STATIC
        qt/controllers/app_controllers.hpp
        qt/controllers/app_controllers.cpp
        qt/controllers/camera_controller.hpp
        qt/controllers/camera_controller.cpp
        qt/controllers/direct_scan.hpp
        qt/controllers/direct_scan.cpp
        qt/controllers/held_cube.hpp
        qt/controllers/held_cube.cpp
        qt/controllers/scan_session.hpp
        qt/controllers/scan_session.cpp
        qt/controllers/settings_model.hpp
        qt/controllers/settings_model.cpp
        qt/controllers/bundled_models.hpp
        qt/controllers/bundled_models.cpp
        qt/controllers/frame_image_provider.hpp
        qt/controllers/frame_image_provider.cpp
        qt/solve_controller.hpp
        qt/solve_controller.cpp)

    target_include_directories(cube_app_controllers PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/qt")

    # Embeds the hand-detection ONNX model directly into cube_app_controllers'
    # object code as a Qt resource (:/models/...) rather than relying on it
    # being deployed as a loose file next to the executable - see
    # bundled_models.cpp. cube_deploy_vision_assets() (VisionAssets.cmake)
    # still separately copies the same file next to cube_app.exe/the test
    # binaries, which cube::vision's own filesystem-path-based tests
    # (tests/integration/hand_presence_integration_test.cpp) still rely on -
    # that's a Qt-free cube_lib API and can't read a Qt resource.
    #
    # The corner-detector weights are not distributed with the repository
    # (assets/models/cube_corner_pose_PROVENANCE.md). A local copy is embedded
    # when there is one; without it the previous scan pipeline's track mode
    # finds no model and falls back, and the direct scan never asked for it.
    set(_cube_app_models
        "${PROJECT_SOURCE_DIR}/assets/models/palm_detection_mediapipe_2023feb_int8.onnx")
    if(EXISTS "${PROJECT_SOURCE_DIR}/assets/models/cube_corner_pose.onnx")
        list(APPEND _cube_app_models "${PROJECT_SOURCE_DIR}/assets/models/cube_corner_pose.onnx")
    endif()
    qt_add_resources(cube_app_controllers "cube_app_models"
        PREFIX "/models"
        BASE "${PROJECT_SOURCE_DIR}/assets/models"
        FILES ${_cube_app_models})
    unset(_cube_app_models)

    # The live M3T tracker (Phase 2) is desktop-only, like the vendored
    # library itself; the scan session's env-gated live-lock hook compiles
    # only where cube::track_m3t exists.
    if(NOT ANDROID AND NOT IOS)
        target_link_libraries(cube_app_controllers PRIVATE cube::track_m3t)
        target_compile_definitions(cube_app_controllers PRIVATE CUBE_HAVE_M3T)
    endif()

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
            qt/ScanScreen.qml
            qt/DirectScanScreen.qml
            qt/SolveScreen.qml
            qt/SolveMethodPicker.qml
            qt/SettingsScreen.qml
            qt/CompleteScreen.qml
        SOURCES
            qt/CameraPermission.hpp
            qt/CameraPermission.cpp
            qt/SystemTheme.hpp
            qt/SystemTheme.cpp
        RESOURCES
            qt/shaders/toon.frag
            qt/shaders/cel.frag
            qt/shaders/celshade_post.frag)

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

    if(ANDROID)
        # Custom AndroidManifest.xml (see src/app/android/) declaring the
        # CAMERA permission androiddeployqt's own auto-detection doesn't
        # know to add for a C++-side QCameraPermission request (confirmed:
        # without this, the OS permission dialog still appears, but
        # `pm revoke`/real-device permission tracking doesn't recognise the
        # app as having requested it at all).
        set_property(TARGET cube_app PROPERTY
            QT_ANDROID_PACKAGE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/android")
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

    if(WIN32)
        # DbgHelp backs main.cpp's unhandled-exception filter, which writes a
        # minidump for the hard faults (access violation, stack overflow) that
        # no C++ catch can see under MSVC's default /EHsc.
        target_link_libraries(cube_app PRIVATE dbghelp)
    endif()

    add_dependencies(cube_app cube_version)

    cube_deploy_sanitizer_runtime(cube_app)
    cube_deploy_vision_assets(cube_app)

    set_target_properties(cube_app PROPERTIES
        WIN32_EXECUTABLE ON
        MACOSX_BUNDLE ON)

    # Windows desktop only: Qt's DLLs aren't next to cube_app.exe or on PATH
    # by default, so it fails to start (STATUS_DLL_NOT_FOUND) unless launched
    # from a shell that already has Qt's bin dir on PATH - which Visual
    # Studio's F5/"Select Startup Item" does not. windeployqt copies
    # everything cube_app.exe actually needs (Qt DLLs, platform/style
    # plugins, translations) next to it after every build, so it runs
    # standalone regardless of how it's launched. macOS would need the
    # equivalent macdeployqt call, not added here - no Mac available to
    # verify it against.
    if(WIN32)
        find_program(CUBEULATOR_WINDEPLOYQT_EXECUTABLE
            NAMES windeployqt6 windeployqt
            HINTS "${Qt6_DIR}/../../../bin")
        if(CUBEULATOR_WINDEPLOYQT_EXECUTABLE)
            add_custom_command(TARGET cube_app POST_BUILD
                COMMAND "${CUBEULATOR_WINDEPLOYQT_EXECUTABLE}"
                    --qmldir "${CMAKE_CURRENT_SOURCE_DIR}/qt"
                    "$<TARGET_FILE:cube_app>"
                COMMENT "windeployqt: copying Qt runtime next to cube_app.exe")
        else()
            message(WARNING
                "windeployqt not found near Qt6_DIR (${Qt6_DIR}); cube_app.exe "
                "will need Qt's bin dir on PATH to run standalone.")
        endif()
    endif()
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
