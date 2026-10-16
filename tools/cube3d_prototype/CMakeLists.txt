# cube3d_prototype: the layer-lift renderer for a cube of any size, measured against
# the way the 3x3's view would scale if it were built larger. README.md says what it
# is for and how to run it.
#
# A project of its own, not part of the application's build (like lightprobe): it
# is a measurement, run when a decision depends on it, and it must not become a
# dependency of anything that ships. It compiles the N x N library straight from
# its sources, which need nothing but the standard library and the header-only
# part of {fmt}:
#
#   cmake -S tools/cube3d_prototype -B build/cube3d -DCMAKE_PREFIX_PATH=<Qt prefix> \
#         -DFMT_INCLUDE_DIR=<a directory holding fmt/format.h>
cmake_minimum_required(VERSION 3.21)
project(cubeulator_cube3d_prototype LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Quick Quick3D Qml)
qt_standard_project_setup(REQUIRES 6.5)

set(FMT_INCLUDE_DIR "" CACHE PATH "a directory holding fmt/format.h")
if(NOT FMT_INCLUDE_DIR)
    message(FATAL_ERROR "pass -DFMT_INCLUDE_DIR=<a directory holding fmt/format.h>")
endif()

get_filename_component(REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
file(GLOB NXN_SOURCES CONFIGURE_DEPENDS
    "${REPO_ROOT}/src/lib/src/nxn/*.cpp"
    "${REPO_ROOT}/src/lib/src/solver/*.cpp")
add_library(cube_nxn_sources STATIC ${NXN_SOURCES})
target_include_directories(cube_nxn_sources
    PUBLIC
        "${REPO_ROOT}/src/lib/include"
        "${REPO_ROOT}/src/lib/variants/profiling-tracy_disabled"
    PRIVATE
        "${REPO_ROOT}/src/lib/src/solver")
target_include_directories(cube_nxn_sources SYSTEM PUBLIC "${FMT_INCLUDE_DIR}")
target_compile_definitions(cube_nxn_sources PUBLIC FMT_HEADER_ONLY)
if(MSVC)
    target_compile_options(cube_nxn_sources PRIVATE /utf-8)  # {fmt} asks for it
endif()
# Whatever the Qt kit asks of everything linked into a Qt program: the
# WebAssembly kit links with threads, so these sources have to be compiled for
# them. Taking the flags from Qt's own platform target means no test of the
# operating system here.
target_link_libraries(cube_nxn_sources PRIVATE Qt6::Platform)
# The same room src/lib/CMakeLists.txt gives the two files that build tables in
# constant expressions.
if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang" AND CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
    set(CONSTEXPR_BUDGET "/clang:-fconstexpr-steps=10000000")
elseif(MSVC)
    set(CONSTEXPR_BUDGET "/constexpr:steps10000000")
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    set(CONSTEXPR_BUDGET "-fconstexpr-ops-limit=200000000")
else()
    set(CONSTEXPR_BUDGET "-fconstexpr-steps=10000000")
endif()
set_source_files_properties(
    "${REPO_ROOT}/src/lib/src/solver/two_phase_tables.cpp"
    "${REPO_ROOT}/src/lib/src/solver/cfop_last_layer.cpp"
    PROPERTIES COMPILE_OPTIONS "${CONSTEXPR_BUDGET}")

qt_add_executable(cube3d_prototype main.cpp)
qt_add_qml_module(cube3d_prototype
    URI CubePrototype
    VERSION 1.0
    QML_FILES Main.qml LiftCube.qml NaiveCube.qml
    SOURCES
        cube_mesh.hpp cube_mesh.cpp
        cube_geometry.hpp cube_geometry.cpp
        frame_stats.hpp frame_stats.cpp
        player.hpp player.cpp)
target_link_libraries(cube3d_prototype PRIVATE
    cube_nxn_sources Qt6::Core Qt6::Gui Qt6::Quick Qt6::Quick3D Qt6::Qml)

# The page the WebAssembly build is opened from (run.html?mode=lift&size=7):
# it passes the address's query to the program as its command line. Beside the
# build, whatever the kit; only a WebAssembly build has anything for it to load.
configure_file(run.html "${CMAKE_CURRENT_BINARY_DIR}/run.html" COPYONLY)
