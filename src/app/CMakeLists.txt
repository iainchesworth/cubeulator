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

# ---------------------------------------------------------------------------
# cube_app - the Qt6 Quick (QML) GUI shell, shared across every platform (see
# docs/architecture.md). The ios-clang preset still sets
# CUBEULATOR_BUILD_APP=OFF (Qt-for-iOS packaging isn't wired up yet); every
# other preset, android-clang included, builds this block.
# ---------------------------------------------------------------------------
if(CUBEULATOR_BUILD_APP)
    find_package(Qt6 6.5 REQUIRED COMPONENTS Core Quick Gui QuickControls2 Quick3D ShaderTools LabsSettings)
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
            qt/SolveScreen.qml
            qt/SettingsScreen.qml
            qt/CompleteScreen.qml
        SOURCES
            qt/CameraDeviceModel.hpp
            qt/CameraDeviceModel.cpp
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
    target_link_libraries(cube_app_qml PRIVATE cube::platform Qt6::Gui)

    # The generated cube_app_qml_qmltyperegistrations.cpp includes QML_ELEMENT
    # headers as "#include <CameraDeviceModel.hpp>" (bare filename, no qt/
    # prefix) - qt/ itself needs to be on the include path for that to
    # resolve, not just this directory (confirmed: fails with "undeclared
    # identifier 'CameraDeviceModel'" otherwise).
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
            cube::platform
            cube::warnings
            cube::sanitizers
            cube::coverage
            Qt6::Quick
            Qt6::Gui
            Qt6::QuickControls2
            Qt6::Quick3D
            Qt6::LabsSettings)

    add_dependencies(cube_app cube_version)

    cube_deploy_sanitizer_runtime(cube_app)

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
