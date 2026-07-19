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

# camera_windows.cpp enumerates via native Media Foundation
# (MFEnumDeviceSources) rather than OpenCvCaptureLoop's generic probe - see
# its own comment for why. mf/mfplat provide MFStartup/MFEnumDeviceSources/
# MFShutdown; mfuuid provides the MF_DEVSOURCE_ATTRIBUTE_* GUID constants'
# definitions (declared extern in the SDK headers, defined only in this lib).
if(WIN32)
    target_link_libraries(cube_platform PRIVATE mf mfplat mfuuid)
endif()

# ---------------------------------------------------------------------------
# cube_app - the Qt6 Quick (QML) GUI shell, shared across every platform (see
# docs/architecture.md). The ios-clang preset still sets
# CUBEULATOR_BUILD_APP=OFF (Qt-for-iOS packaging isn't wired up yet); every
# other preset, android-clang included, builds this block.
# ---------------------------------------------------------------------------
if(CUBEULATOR_BUILD_APP)
    find_package(Qt6 6.5 REQUIRED COMPONENTS Core Concurrent Quick Qml Gui QuickControls2)
    qt_standard_project_setup(REQUIRES 6.5)

    # ---------------------------------------------------------------------
    # cube_app_controllers - QObject-derived bridge types exposed to QML
    # (CameraController etc.). A separate target from cube_app_qml so it's
    # linkable into a Catch2 test binary without pulling in QQmlEngine;
    # registered into the "Cubeulator" QML module imperatively via
    # qmlRegisterSingletonInstance in main.cpp rather than QML_ELEMENT,
    # since QML_ELEMENT's compile-time registration only works for sources
    # compiled directly into a qt_add_qml_module target.
    # ---------------------------------------------------------------------
    add_library(cube_app_controllers STATIC
        qt/controllers/camera_controller.hpp
        qt/controllers/camera_controller.cpp
        qt/controllers/scan_session.hpp
        qt/controllers/scan_session.cpp
        qt/controllers/settings_model.hpp
        qt/controllers/settings_model.cpp
        qt/controllers/bundled_models.hpp
        qt/controllers/bundled_models.cpp
        qt/controllers/frame_image_provider.hpp
        qt/controllers/frame_image_provider.cpp)

    target_include_directories(cube_app_controllers PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/qt")

    # Embeds the hand-detection ONNX model directly into cube_app_controllers'
    # object code as a Qt resource (:/models/...) rather than relying on it
    # being deployed as a loose file next to the executable - see
    # bundled_models.cpp. cube_deploy_vision_assets() (VisionAssets.cmake)
    # still separately copies the same file next to cube_app.exe/the test
    # binaries, which cube::vision's own filesystem-path-based tests
    # (tests/integration/hand_presence_integration_test.cpp) still rely on -
    # that's a Qt-free cube_lib API and can't read a Qt resource.
    qt_add_resources(cube_app_controllers "cube_app_models"
        PREFIX "/models"
        BASE "${PROJECT_SOURCE_DIR}/assets/models"
        FILES "${PROJECT_SOURCE_DIR}/assets/models/palm_detection_mediapipe_2023feb_int8.onnx")

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
    set_source_files_properties(qt/Theme.qml qt/AppState.qml PROPERTIES
        QT_QML_SINGLETON_TYPE TRUE)

    qt_add_qml_module(cube_app_qml
        URI Cubeulator
        VERSION 1.0
        STATIC
        QML_FILES
            qt/Main.qml
            qt/Theme.qml
            qt/AppState.qml
            qt/AppButton.qml
            qt/Icon.qml
            qt/CubeView.qml
            qt/LaunchScreen.qml
            qt/PermissionScreen.qml
            qt/ScanScreen.qml
            qt/SolveScreen.qml
            qt/CompleteScreen.qml
            qt/SettingsScreen.qml)

    qt_add_executable(cube_app
        qt/main.cpp)

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
            Qt6::QuickControls2)

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
