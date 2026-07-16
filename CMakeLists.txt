# CMake 4 is required; the version range keeps policy behaviour current up to 4.3.
cmake_minimum_required(VERSION 3.28...4.3)

# ---------------------------------------------------------------------------
# Vendored vcpkg (git submodule at deps/vcpkg).
#
# The dependency provider is pinned by the submodule so builds are fully
# reproducible without a machine-wide vcpkg or a VCPKG_ROOT env var. The
# CMakePresets point CMAKE_TOOLCHAIN_FILE at deps/vcpkg/scripts/buildsystems.
# If the submodule was not checked out, initialise it here so a fresh clone
# configures without extra steps.
# ---------------------------------------------------------------------------
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/deps/vcpkg/scripts/buildsystems/vcpkg.cmake")
    find_package(Git QUIET)
    if(GIT_FOUND AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/.git")
        message(STATUS "Initialising vendored vcpkg submodule (deps/vcpkg)")
        execute_process(
            COMMAND "${GIT_EXECUTABLE}" submodule update --init --depth 1 -- deps/vcpkg
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
            RESULT_VARIABLE CUBEULATOR_SUBMODULE_RESULT)
        if(NOT CUBEULATOR_SUBMODULE_RESULT EQUAL 0)
            message(FATAL_ERROR
                "Failed to initialise the vcpkg submodule. Run:\n"
                "  git submodule update --init --depth 1 deps/vcpkg")
        endif()
    endif()
endif()

if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/deps/vcpkg/scripts/buildsystems/vcpkg.cmake")
    message(FATAL_ERROR
        "Vendored vcpkg not found at deps/vcpkg.\n"
        "Clone with submodules (git clone --recurse-submodules ...) or run:\n"
        "  git submodule update --init --depth 1 deps/vcpkg")
endif()

# ---------------------------------------------------------------------------
# Select vcpkg manifest features BEFORE project() (the vcpkg toolchain reads
# VCPKG_MANIFEST_FEATURES when it is loaded at project() time). Qt is a
# prebuilt dependency found via CMAKE_PREFIX_PATH (see ci/), not vcpkg.
# ---------------------------------------------------------------------------
if(NOT DEFINED CUBEULATOR_BUILD_TESTS OR CUBEULATOR_BUILD_TESTS)
    list(APPEND VCPKG_MANIFEST_FEATURES "tests")
endif()

project(Cubeulator
    VERSION 0.1.0
    DESCRIPTION "A camera-driven, on-device Rubik's Cube solver"
    HOMEPAGE_URL "https://github.com/iainchesworth/cubeulator"
    LANGUAGES CXX)

# ---------------------------------------------------------------------------
# Global language settings: C++23 only, no compiler extensions.
# ---------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Export compile_commands.json for clang-tidy / editors.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Put all runtime artefacts in one place so the Qt app can find its plugins.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(CompilerWarnings)
include(Sanitizers)
include(Coverage)
include(MobileDependencies)

cube_configure_mobile_dependencies()

# ---------------------------------------------------------------------------
# Options
# ---------------------------------------------------------------------------
option(CUBEULATOR_BUILD_APP "Build the Qt6 GUI application" ON)
option(CUBEULATOR_BUILD_TESTS "Build the unit and integration test suites" ON)

# ---------------------------------------------------------------------------
# Targets
# ---------------------------------------------------------------------------
# The library holds ALL of the solver/vision/render/i18n logic and is
# completely GUI-free.
add_subdirectory(src/lib)

# Shared bgfx setup / technical-drawing shading pipeline. Kept as its own
# target (rather than folded into src/app) so it is buildable and testable
# independently of the Qt shell.
add_subdirectory(src/render)

# Always added: src/app/CMakeLists.txt builds the platform-abstraction
# library (cube_platform) unconditionally so the per-OS interface selection
# is exercised on every preset, including CUBEULATOR_BUILD_APP=OFF headless
# builds; the Qt GUI executable inside it is separately gated on
# CUBEULATOR_BUILD_APP.
add_subdirectory(src/app)

if(CUBEULATOR_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

include(Packaging)

message(STATUS "Cubeulator ${PROJECT_VERSION}")
message(STATUS "  Build app   : ${CUBEULATOR_BUILD_APP}")
message(STATUS "  Build tests : ${CUBEULATOR_BUILD_TESTS}")
