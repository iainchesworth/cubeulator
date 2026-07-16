# ---------------------------------------------------------------------------
# Coverage.cmake
#
# Defines an INTERFACE target `cube::coverage` that, when
# CUBEULATOR_ENABLE_COVERAGE is on, turns on gcov-style source-based coverage
# instrumentation (GCC/Clang) via --coverage. Off by default: only the
# dedicated linux-gcc-coverage preset turns it on, so normal dev/CI builds
# pay no instrumentation cost.
#
# Link it PRIVATE into every first-party target whose coverage should be
# measured (library, app core, test executables).
# ---------------------------------------------------------------------------

option(CUBEULATOR_ENABLE_COVERAGE "Enable gcov/llvm-cov source coverage instrumentation" OFF)

add_library(cube_coverage INTERFACE)
add_library(cube::coverage ALIAS cube_coverage)

if(CUBEULATOR_ENABLE_COVERAGE)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(cube_coverage INTERFACE --coverage -fno-inline)
        target_link_options(cube_coverage INTERFACE --coverage)
    else()
        message(WARNING
            "CUBEULATOR_ENABLE_COVERAGE is on but ${CMAKE_CXX_COMPILER_ID} is not "
            "GCC/Clang; coverage instrumentation is not supported on this "
            "compiler and will be skipped.")
    endif()
endif()
