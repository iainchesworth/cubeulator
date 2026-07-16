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
# cube_app - the Qt6 Quick (QML) GUI shell. Desktop-only for now (see
# docs/architecture.md: Option B is the default GUI-shell strategy, so
# ios/android get native shells in a later spec, not this QML one) - the
# android-clang/ios-clang presets set CUBEULATOR_BUILD_APP=OFF, so this whole
# block never runs there.
# ---------------------------------------------------------------------------
if(CUBEULATOR_BUILD_APP)
    find_package(Qt6 6.5 REQUIRED COMPONENTS Core Quick Gui QuickControls2)
    qt_standard_project_setup(REQUIRES 6.5)

    qt_add_qml_module(cube_app_qml
        URI Cubeulator
        VERSION 1.0
        QML_FILES
            qt/Main.qml)

    qt_add_executable(cube_app
        qt/main.cpp)

    target_link_libraries(cube_app
        PRIVATE
            $<LINK_LIBRARY:WHOLE_ARCHIVE,cube_app_qml>
            cube::platform
            cube::warnings
            cube::sanitizers
            cube::coverage
            Qt6::Quick
            Qt6::Gui
            Qt6::QuickControls2)

    add_dependencies(cube_app cube_version)

    cube_deploy_sanitizer_runtime(cube_app)

    set_target_properties(cube_app PROPERTIES
        WIN32_EXECUTABLE ON
        MACOSX_BUNDLE ON)
endif()
